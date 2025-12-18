/* Shader Multipass Support - Implementation
 * Implements Shadertoy-style multipass rendering with BufferA-D and Image passes
 * 
 * This is a self-contained shader compilation and rendering system.
 * No legacy dependencies required.
 */

#include "shader_multipass.h"
#include "adaptive_scale.h"
#include "render_optimizer.h"
#include "multipass_optimizer.h"
#include "shader_log.h"
#include "platform_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

/* ============================================
 * Error Logging for Shader Compilation
 * ============================================ */

#define MAX_ERROR_LOG_SIZE 16384
static char g_last_error_log[MAX_ERROR_LOG_SIZE];
static size_t g_error_log_pos = 0;

static void clear_error_log(void) {
    g_last_error_log[0] = '\0';
    g_error_log_pos = 0;
}

static void append_to_error_log(const char *format, ...) {
    if (g_error_log_pos >= MAX_ERROR_LOG_SIZE - 1) return;
    
    va_list args;
    va_start(args, format);
    int written = vsnprintf(g_last_error_log + g_error_log_pos,
                           MAX_ERROR_LOG_SIZE - g_error_log_pos,
                           format, args);
    va_end(args);
    
    if (written > 0) {
        g_error_log_pos += written;
        if (g_error_log_pos >= MAX_ERROR_LOG_SIZE) {
            g_error_log_pos = MAX_ERROR_LOG_SIZE - 1;
        }
    }
}

const char *multipass_get_error_log(void) {
    return g_last_error_log;
}

/* ============================================
 * Shader Compilation Utilities
 * ============================================ */

static void print_shader_with_line_numbers(const char *source, const char *type) {
    if (!source) return;
    
    log_debug("========== %s SHADER SOURCE (with line numbers) ==========", type);
    
    const char *line_start = source;
    const char *line_end;
    int line_num = 1;
    
    while (*line_start) {
        line_end = strchr(line_start, '\n');
        if (line_end) {
            log_debug("%4d: %.*s", line_num, (int)(line_end - line_start), line_start);
            line_start = line_end + 1;
        } else {
            log_debug("%4d: %s", line_num, line_start);
            break;
        }
        line_num++;
    }
    
    log_debug("========== END %s SHADER SOURCE ==========", type);
}

static GLuint compile_shader(GLenum type, const char *source) {
    const char *type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    
    print_shader_with_line_numbers(source, type_str);
    
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        log_error("Failed to create %s shader", type_str);
        append_to_error_log("ERROR: Failed to create %s shader\n", type_str);
        return 0;
    }
    
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        append_to_error_log("\n=== %s SHADER COMPILATION FAILED ===\n\n",
                           (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT");
        
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetShaderInfoLog(shader, info_len, NULL, info_log);
                log_error("%s shader compilation failed: %s", type_str, info_log);
                append_to_error_log("%s\n", info_log);
                free(info_log);
            }
        }
        
        glDeleteShader(shader);
        return 0;
    }
    
    log_debug("%s shader compiled successfully", type_str);
    return shader;
}

static bool shader_create_program_from_sources(const char *vertex_src,
                                                const char *fragment_src,
                                                GLuint *program) {
    if (!program) {
        log_error("Invalid program pointer");
        return false;
    }
    
    clear_error_log();
    
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (vertex_shader == 0) {
        return false;
    }
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }
    
    GLuint prog = glCreateProgram();
    if (prog == 0) {
        log_error("Failed to create shader program");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    
    glAttachShader(prog, vertex_shader);
    glAttachShader(prog, fragment_shader);
    glLinkProgram(prog);
    
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        append_to_error_log("\n=== PROGRAM LINKING FAILED ===\n\n");
        
        GLint info_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetProgramInfoLog(prog, info_len, NULL, info_log);
                log_error("Program linking failed: %s", info_log);
                append_to_error_log("%s\n", info_log);
                free(info_log);
            }
        }
        
        glDeleteProgram(prog);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    *program = prog;
    log_debug("Shader program created successfully (ID: %u)", prog);
    return true;
}



/* ============================================
 * Internal Helper Functions
 * ============================================ */

/* Skip whitespace (currently unused but kept for future use) */
/*
static const char *skip_whitespace(const char *p) {
    while (*p && isspace(*p)) p++;
    return p;
}
*/

/* Check if character is valid identifier char (currently unused but kept for future use) */
/*
static bool is_ident_char(char c) {
    return isalnum(c) || c == '_';
}
*/

/* Find next occurrence of pattern, respecting comments */
static const char *find_pattern(const char *source, const char *pattern) {
    const char *p = source;
    size_t pat_len = strlen(pattern);

    while (*p) {
        /* Skip single-line comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }

        /* Skip multi-line comments */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }

        /* Check for pattern */
        if (strncmp(p, pattern, pat_len) == 0) {
            return p;
        }

        p++;
    }

    return NULL;
}

/* Find the end of a function body (matching closing brace) */
static const char *find_function_end(const char *start) {
    const char *p = start;
    int brace_depth = 0;
    bool in_function = false;

    while (*p) {
        /* Skip comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }

        /* Skip strings */
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (*p) p++;
            continue;
        }

        if (*p == '{') {
            brace_depth++;
            in_function = true;
        } else if (*p == '}') {
            brace_depth--;
            if (in_function && brace_depth == 0) {
                return p + 1;
            }
        }

        p++;
    }

    return p;
}

/* Extract a substring */
static char *extract_substring(const char *start, const char *end) {
    if (!start || !end || end <= start) return NULL;

    size_t len = end - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

/* Duplicate a string */
static char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *result = malloc(len + 1);
    if (result) {
        memcpy(result, s, len + 1);
    }
    return result;
}

/* ============================================
 * Pass Type Utilities
 * ============================================ */

const char *multipass_type_name(multipass_type_t type) {
    switch (type) {
        case PASS_TYPE_BUFFER_A: return "Buffer A";
        case PASS_TYPE_BUFFER_B: return "Buffer B";
        case PASS_TYPE_BUFFER_C: return "Buffer C";
        case PASS_TYPE_BUFFER_D: return "Buffer D";
        case PASS_TYPE_IMAGE:    return "Image";
        case PASS_TYPE_COMMON:   return "Common";
        case PASS_TYPE_SOUND:    return "Sound";
        default:                 return "None";
    }
}

multipass_type_t multipass_type_from_name(const char *name) {
    if (!name) return PASS_TYPE_NONE;

    /* Case-insensitive comparison */
    if (strcasecmp(name, "Buffer A") == 0 || strcasecmp(name, "BufferA") == 0)
        return PASS_TYPE_BUFFER_A;
    if (strcasecmp(name, "Buffer B") == 0 || strcasecmp(name, "BufferB") == 0)
        return PASS_TYPE_BUFFER_B;
    if (strcasecmp(name, "Buffer C") == 0 || strcasecmp(name, "BufferC") == 0)
        return PASS_TYPE_BUFFER_C;
    if (strcasecmp(name, "Buffer D") == 0 || strcasecmp(name, "BufferD") == 0)
        return PASS_TYPE_BUFFER_D;
    if (strcasecmp(name, "Image") == 0)
        return PASS_TYPE_IMAGE;
    if (strcasecmp(name, "Common") == 0)
        return PASS_TYPE_COMMON;
    if (strcasecmp(name, "Sound") == 0)
        return PASS_TYPE_SOUND;

    return PASS_TYPE_NONE;
}

const char *multipass_channel_source_name(channel_source_t source) {
    switch (source) {
        case CHANNEL_SOURCE_BUFFER_A: return "Buffer A";
        case CHANNEL_SOURCE_BUFFER_B: return "Buffer B";
        case CHANNEL_SOURCE_BUFFER_C: return "Buffer C";
        case CHANNEL_SOURCE_BUFFER_D: return "Buffer D";
        case CHANNEL_SOURCE_TEXTURE:  return "Texture";
        case CHANNEL_SOURCE_KEYBOARD: return "Keyboard";
        case CHANNEL_SOURCE_NOISE:    return "Noise";
        case CHANNEL_SOURCE_SELF:     return "Self";
        default:                      return "None";
    }
}

multipass_channel_t multipass_default_channel(channel_source_t source) {
    multipass_channel_t channel = {
        .source = source,
        .texture_id = 0,
        .vflip = false,
        .filter = GL_LINEAR,
        .wrap = GL_CLAMP_TO_EDGE
    };
    return channel;
}

/* ============================================
 * Shader Parsing Functions
 * ============================================ */

int multipass_count_main_functions(const char *source) {
    if (!source) return 0;

    int count = 0;
    const char *p = source;

    while ((p = find_pattern(p, "mainImage")) != NULL) {
        /* Check if it's actually a function definition */
        const char *before = p;
        if (before > source) {
            before--;
            while (before > source && isspace(*before)) before--;
        }

        /* Skip past "mainImage" */
        p += 9;

        /* Skip whitespace */
        while (*p && isspace(*p)) p++;

        /* Must be followed by '(' */
        if (*p == '(') {
            count++;
        }
    }

    return count;
}

bool multipass_detect(const char *source) {
    if (!source) return false;

    /*
     * All shaders go through the multipass system now.
     * Single-pass shaders are treated as Image-only multipass.
     * This simplifies the codebase by removing the legacy single-pass path.
     */
    int main_count = multipass_count_main_functions(source);
    if (main_count >= 1) {
        return true;
    }

    /* Check for mainImage function */
    if (find_pattern(source, "void mainImage") ||
        find_pattern(source, "void main(")) {
        return true;
    }

    return false;
}

char *multipass_extract_common(const char *source) {
    if (!source) return NULL;

    /* Find start of first mainImage function */
    const char *first_main = find_pattern(source, "void mainImage");
    if (!first_main) {
        first_main = find_pattern(source, "void main(");
    }

    if (!first_main) {
        return NULL;
    }

    /* Go back to find the start of function (might have return type, etc.) */
    const char *func_start = first_main;
    while (func_start > source && *(func_start - 1) != '\n') {
        func_start--;
    }

    /* Everything before the first function is common code */
    if (func_start > source) {
        return extract_substring(source, func_start);
    }

    return NULL;
}

multipass_parse_result_t *multipass_parse_shader(const char *source) {
    multipass_parse_result_t *result = calloc(1, sizeof(multipass_parse_result_t));
    if (!result) return NULL;

    if (!source) {
        result->error_message = str_dup("Source is NULL");
        return result;
    }

    int main_count = multipass_count_main_functions(source);

    if (main_count <= 1) {
        /* Single pass shader */
        result->is_multipass = false;
        result->pass_count = 1;
        result->pass_sources[0] = str_dup(source);
        result->pass_types[0] = PASS_TYPE_IMAGE;
        return result;
    }

    result->is_multipass = true;
    log_info("Detected multipass shader with %d mainImage functions", main_count);

    /* Extract common code (everything before first mainImage) */
    result->common_source = multipass_extract_common(source);

    /*
     * MULTIPASS EXTRACTION STRATEGY:
     *
     * For shaders with multiple mainImage functions, we need to:
     * 1. Extract each mainImage function separately
     * 2. Include helper functions that appear BETWEEN mainImage functions
     *    with the passes that need them (but NOT other mainImage functions)
     *
     * Example: If shader has mainImage1, helperFunc, mainImage2, helperFunc2, mainImage3
     * - Pass 0: mainImage1 only
     * - Pass 1: helperFunc + mainImage2
     * - Pass 2: helperFunc + helperFunc2 + mainImage3
     */

    /* First, find all mainImage positions and their function boundaries */
    const char *main_starts[MULTIPASS_MAX_PASSES];  /* Start of "void mainImage" */
    const char *main_ends[MULTIPASS_MAX_PASSES];    /* End of mainImage function body */
    const char *line_starts[MULTIPASS_MAX_PASSES];  /* Start of line containing mainImage */
    int found_count = 0;
    
    (void)main_starts; /* Currently unused but kept for future use */

    const char *p = source;
    while (found_count < MULTIPASS_MAX_PASSES) {
        const char *main_start = find_pattern(p, "void mainImage");
        if (!main_start) break;

        /* Find start of the line */
        const char *line_start = main_start;
        while (line_start > source && *(line_start - 1) != '\n') {
            line_start--;
        }

        main_starts[found_count] = main_start;
        line_starts[found_count] = line_start;
        main_ends[found_count] = find_function_end(main_start);
        found_count++;
        p = main_ends[found_count - 1];
    }

    /* Now extract each pass with proper helper function inclusion */
    for (int pass_index = 0; pass_index < found_count; pass_index++) {
        const char *line_start = line_starts[pass_index];
        const char *func_end = main_ends[pass_index];

        /* Check for pass marker in preceding lines */
        multipass_type_t detected_type = PASS_TYPE_NONE;
        const char *check = line_start;
        int lines_back = 0;
        while (check > source && lines_back < 5) {
            /* Go to previous line */
            check--;
            while (check > source && *(check - 1) != '\n') check--;

            /* Check this line for markers - be more specific to avoid false positives */
            /* Only check comment lines */
            const char *line_content = check;
            while (*line_content && isspace(*line_content)) line_content++;

            if (line_content[0] == '/' && (line_content[1] == '/' || line_content[1] == '*')) {
                if (strstr(check, "Buffer A") || strstr(check, "BufferA")) {
                    detected_type = PASS_TYPE_BUFFER_A;
                    break;
                } else if (strstr(check, "Buffer B") || strstr(check, "BufferB")) {
                    detected_type = PASS_TYPE_BUFFER_B;
                    break;
                } else if (strstr(check, "Buffer C") || strstr(check, "BufferC")) {
                    detected_type = PASS_TYPE_BUFFER_C;
                    break;
                } else if (strstr(check, "Buffer D") || strstr(check, "BufferD")) {
                    detected_type = PASS_TYPE_BUFFER_D;
                    break;
                } else if (strstr(check, "// Image") || strstr(check, "/* Image")) {
                    detected_type = PASS_TYPE_IMAGE;
                    break;
                }
            }

            lines_back++;
        }

        /*
         * Default assignment based on order if no marker found:
         * - For 2 passes: Buffer A, Image
         * - For 3 passes: Buffer A, Buffer B, Image
         * - For 4 passes: Buffer A, Buffer B, Buffer C, Image
         * - etc.
         * The LAST pass is always Image, all others are Buffers A, B, C, D
         */
        if (detected_type == PASS_TYPE_NONE) {
            if (pass_index == found_count - 1) {
                detected_type = PASS_TYPE_IMAGE;  /* Last pass is always Image */
            } else {
                /* Assign buffers A, B, C, D in order */
                detected_type = PASS_TYPE_BUFFER_A + pass_index;
                if (detected_type > PASS_TYPE_BUFFER_D) {
                    detected_type = PASS_TYPE_BUFFER_D;  /* Cap at Buffer D */
                }
            }
        }

        log_info("Pass %d assigned type: %s", pass_index, multipass_type_name(detected_type));

        /*
         * For passes after the first one, include ALL helper functions defined
         * between the FIRST mainImage end and THIS mainImage start.
         * This ensures functions like makeBloom() (defined between pass 0 and 1)
         * are available to pass 2 as well.
         */
        if (pass_index > 0) {
            /* Get ALL helper code from end of FIRST mainImage to start of THIS mainImage */
            const char *helpers_start = main_ends[0];  /* After first mainImage */
            const char *helpers_end = line_start;

            /* We need to EXCLUDE other mainImage functions from the helpers */
            /* Build a string with only the helper functions */
            size_t max_helpers_len = (helpers_end > helpers_start) ? (helpers_end - helpers_start) : 0;
            char *helpers_only = NULL;
            size_t helpers_only_len = 0;

            if (max_helpers_len > 0) {
                helpers_only = malloc(max_helpers_len + 1);
                if (helpers_only) {
                    helpers_only[0] = '\0';
                    helpers_only_len = 0;

                    /* Copy code between each mainImage, skipping the mainImage functions themselves */
                    for (int prev = 0; prev < pass_index; prev++) {
                        const char *seg_start = main_ends[prev];
                        const char *seg_end = line_starts[prev + 1];

                        if (seg_end > seg_start) {
                            size_t seg_len = seg_end - seg_start;
                            memcpy(helpers_only + helpers_only_len, seg_start, seg_len);
                            helpers_only_len += seg_len;
                        }
                    }
                    helpers_only[helpers_only_len] = '\0';
                }
            }

            /* Calculate sizes for final combined source */
            size_t main_len = func_end - line_start;
            size_t total_len = helpers_only_len + main_len + 16;

            char *combined = malloc(total_len);
            if (combined) {
                combined[0] = '\0';

                /* Add accumulated helper functions */
                if (helpers_only && helpers_only_len > 0) {
                    strcat(combined, helpers_only);
                }

                /* Add this mainImage function */
                strncat(combined, line_start, main_len);

                result->pass_sources[pass_index] = combined;
            } else {
                result->pass_sources[pass_index] = extract_substring(line_start, func_end);
            }

            free(helpers_only);
        } else {
            /* First pass - just extract the mainImage function */
            result->pass_sources[pass_index] = extract_substring(line_start, func_end);
        }

        result->pass_types[pass_index] = detected_type;

        log_info("Extracted pass %d: %s", pass_index, multipass_type_name(detected_type));
    }

    result->pass_count = found_count;

    return result;
}

void multipass_free_parse_result(multipass_parse_result_t *result) {
    if (!result) return;

    for (int i = 0; i < MULTIPASS_MAX_PASSES; i++) {
        free(result->pass_sources[i]);
    }
    free(result->common_source);
    free(result->error_message);
    free(result);
}

/* ============================================
 * Shader wrapper for each pass
 * ============================================ */

/* Shadertoy wrapper prefix - Desktop OpenGL 3.3 Core */
static const char *multipass_wrapper_prefix =
    "#version 330 core\n"
    "\n"
    "// Shadertoy compatibility uniforms\n"
    "uniform float iTime;\n"
    "uniform vec3 iResolution;\n"
    "uniform vec4 iMouse;\n"
    "uniform int iFrame;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform vec4 iDate;\n"
    "uniform float iSampleRate;\n"
    "\n"
    "// Texture samplers\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n"
    "\n"
    "// Channel resolutions\n"
    "uniform vec3 iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "\n"
    "// Output\n"
    "out vec4 fragColor;\n"
    "\n";

static const char *multipass_wrapper_suffix =
    "\n"
    "void main() {\n"
    "    mainImage(fragColor, gl_FragCoord.xy);\n"
    "}\n";

/**
 * Fix common Shadertoy compatibility issues in shader source.
 * 
 * Handles:
 * - iChannelResolution[n] used as vec2 (add .xy swizzle)
 * - texture(sampler, vec3) -> texture(sampler, (vec3).xy) for 2D textures
 * - Other implicit vec3->vec2 casts
 * 
 * Returns a newly allocated string that must be freed.
 */
static char *fix_shadertoy_compatibility(const char *source) {
    if (!source) return NULL;
    
    size_t src_len = strlen(source);
    /* Allocate extra space for potential .xy additions */
    size_t alloc_size = src_len * 3 + 1;
    char *result = malloc(alloc_size);
    if (!result) return NULL;
    
    const char *src = source;
    char *dst = result;
    
    while (*src) {
        /* Check for iChannelResolution[n] pattern */
        if (strncmp(src, "iChannelResolution[", 19) == 0) {
            /* Copy "iChannelResolution[" */
            memcpy(dst, src, 19);
            dst += 19;
            src += 19;
            
            /* Copy the index (digit) */
            while (*src && *src != ']') {
                *dst++ = *src++;
            }
            
            /* Copy the closing bracket */
            if (*src == ']') {
                *dst++ = *src++;
            }
            
            /* Check if already followed by a swizzle or component access */
            if (*src != '.' && *src != '[') {
                /* Add .xy swizzle for vec3->vec2 compatibility */
                memcpy(dst, ".xy", 3);
                dst += 3;
            }
            continue;
        }
        
        /* Check for texture(iChannel, expr) where expr might be vec3 */
        /* Pattern: "texture(iChannel" followed by digit, comma, then expression */
        if (strncmp(src, "texture(iChannel", 16) == 0) {
            /* Copy "texture(iChannel" */
            memcpy(dst, src, 16);
            dst += 16;
            src += 16;
            
            /* Copy the channel number */
            while (*src && *src >= '0' && *src <= '9') {
                *dst++ = *src++;
            }
            
            /* Skip whitespace and comma */
            while (*src && (*src == ' ' || *src == '\t')) {
                *dst++ = *src++;
            }
            if (*src == ',') {
                *dst++ = *src++;
            }
            while (*src && (*src == ' ' || *src == '\t')) {
                *dst++ = *src++;
            }
            
            /* Now we're at the coordinate expression */
            /* Check if it starts with something that's likely a vec3 */
            /* Common patterns: variable name, function call, or expression */
            
            /* We need to find the end of this expression (the closing paren or next comma) */
            /* and wrap it with parentheses + .xy if it doesn't already have .xy */
            
            /* Count parentheses to find the expression end */
            int paren_depth = 1; /* We're inside texture( */
            const char *expr_start = src;
            const char *expr_end = src;
            bool has_swizzle = false;
            
            while (*expr_end && paren_depth > 0) {
                if (*expr_end == '(') paren_depth++;
                else if (*expr_end == ')') paren_depth--;
                else if (*expr_end == ',' && paren_depth == 1) break; /* Next argument */
                
                /* Check for .xy, .xz, .yz etc swizzle at end of expression */
                if (*expr_end == '.' && paren_depth == 1) {
                    const char *after_dot = expr_end + 1;
                    if ((*after_dot == 'x' || *after_dot == 'y' || *after_dot == 'z' || 
                         *after_dot == 'r' || *after_dot == 'g' || *after_dot == 'b' ||
                         *after_dot == 's' || *after_dot == 't' || *after_dot == 'p')) {
                        has_swizzle = true;
                    }
                }
                expr_end++;
            }
            
            /* Back up to the actual end of the expression */
            if (paren_depth == 0) expr_end--;
            while (expr_end > expr_start && (*(expr_end-1) == ' ' || *(expr_end-1) == '\t')) {
                expr_end--;
            }
            
            /* Copy the expression */
            size_t expr_len = expr_end - expr_start;
            if (!has_swizzle && expr_len > 0) {
                /* Wrap with parentheses and add .xy */
                *dst++ = '(';
                memcpy(dst, expr_start, expr_len);
                dst += expr_len;
                memcpy(dst, ").xy", 4);
                dst += 4;
            } else {
                /* Already has swizzle, copy as-is */
                memcpy(dst, expr_start, expr_len);
                dst += expr_len;
            }
            
            src = expr_end;
            continue;
        }
        
        /* Copy character as-is */
        *dst++ = *src++;
    }
    
    *dst = '\0';
    return result;
}

/* Wrap a pass source with Shadertoy compatibility layer */
static char *wrap_pass_source(const char *common, const char *pass_source) {
    size_t prefix_len = strlen(multipass_wrapper_prefix);
    size_t common_len = common ? strlen(common) : 0;
    size_t pass_len = pass_source ? strlen(pass_source) : 0;
    size_t suffix_len = strlen(multipass_wrapper_suffix);

    /* Extra space for .xy additions (worst case: every iChannelResolution gets .xy) */
    size_t total = prefix_len + (common_len * 2) + (pass_len * 2) + suffix_len + 64;
    char *wrapped = malloc(total);
    if (!wrapped) return NULL;

    wrapped[0] = '\0';
    strcat(wrapped, multipass_wrapper_prefix);
    
    /* Apply compatibility fixes to common code */
    if (common) {
        char *fixed_common = fix_shadertoy_compatibility(common);
        if (fixed_common) {
            strcat(wrapped, fixed_common);
            free(fixed_common);
        } else {
            strcat(wrapped, common);
        }
    }
    strcat(wrapped, "\n");
    
    /* Apply compatibility fixes to pass source */
    if (pass_source) {
        char *fixed_pass = fix_shadertoy_compatibility(pass_source);
        if (fixed_pass) {
            strcat(wrapped, fixed_pass);
            free(fixed_pass);
        } else {
            strcat(wrapped, pass_source);
        }
    }
    strcat(wrapped, multipass_wrapper_suffix);

    return wrapped;
}

/* Vertex shader for fullscreen quad - Desktop OpenGL 3.3 Core */
static const char *fullscreen_vertex_shader =
    "#version 330 core\n"
    "in vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

/* ============================================
 * Multipass Shader Creation
 * ============================================ */

multipass_shader_t *multipass_create(const char *source) {
    multipass_parse_result_t *parsed = multipass_parse_shader(source);
    if (!parsed) return NULL;

    multipass_shader_t *shader = multipass_create_from_parsed(parsed);
    multipass_free_parse_result(parsed);

    return shader;
}

multipass_shader_t *multipass_create_from_parsed(const multipass_parse_result_t *parse_result) {
    if (!parse_result) return NULL;

    multipass_shader_t *shader = calloc(1, sizeof(multipass_shader_t));
    if (!shader) return NULL;

    shader->common_source = parse_result->common_source ?
                            str_dup(parse_result->common_source) : NULL;
    shader->pass_count = parse_result->pass_count;
    shader->image_pass_index = -1;
    shader->has_buffers = false;
    shader->resolution_scale = 1.0f;   /* Start at full resolution */
    shader->min_resolution_scale = 0.25f;
    shader->max_resolution_scale = 1.0f;
    shader->scaled_width = 0;
    shader->scaled_height = 0;
    
    /* Initialize industry-grade adaptive resolution system */
    adaptive_init(&shader->adaptive, NULL);  /* Use default config */
    
    /* Initialize render optimizer (will be fully initialized in multipass_init_gl) */
    memset(&shader->optimizer, 0, sizeof(shader->optimizer));
    shader->use_smart_buffer_sizing = true;
    
    /* Initialize multipass optimizer for smart per-buffer resolution and half-rate updates */
    multipass_optimizer_init(&shader->multipass_opt);

    for (int i = 0; i < parse_result->pass_count; i++) {
        multipass_pass_t *pass = &shader->passes[i];

        pass->type = parse_result->pass_types[i];
        pass->name = str_dup(multipass_type_name(pass->type));
        pass->source = str_dup(parse_result->pass_sources[i]);
        pass->is_compiled = false;

        /*
         * VERY SMART CHANNEL BINDING with confidence scoring
         *
         * Analyzes shader source with multiple heuristics to determine optimal bindings.
         * Uses a scoring system to handle ambiguous cases correctly.
         * 
         * Heuristics:
         * 1. Noise texture: /1024, /512, /256, *0.001, .x only (single channel read)
         * 2. Self-feedback: uv, fragCoord/iResolution, temporal mixing patterns
         * 3. Buffer read: texture(iChannelN, uv) where N matches buffer index pattern
         * 4. Shadertoy conventions: iChannel0 often = self for buffers
         */
        
        if (pass->type == PASS_TYPE_IMAGE) {
            shader->image_pass_index = i;
            /* Image pass reads from buffers in order */
            pass->channels[0].source = CHANNEL_SOURCE_BUFFER_A;
            pass->channels[1].source = CHANNEL_SOURCE_BUFFER_B;
            pass->channels[2].source = CHANNEL_SOURCE_BUFFER_C;
            pass->channels[3].source = CHANNEL_SOURCE_BUFFER_D;
        } else {
            shader->has_buffers = true;
            
            const char *src = pass->source;
            
            for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
                /* Confidence scores: positive = noise, negative = buffer/self */
                int noise_score = 0;
                int buffer_score = 0;
                int self_score = 0;
                bool channel_used = false;
                
                if (src) {
                    char channel_name[16];
                    snprintf(channel_name, sizeof(channel_name), "iChannel%d", c);
                    
                    const char *usage = src;
                    while ((usage = strstr(usage, channel_name)) != NULL) {
                        channel_used = true;
                        
                        /* Scan context around this usage (before and after) */
                        const char *line_start = usage;
                        while (line_start > src && *(line_start-1) != '\n') line_start--;
                        
                        const char *line_end = usage;
                        while (*line_end && *line_end != '\n' && *line_end != ';') line_end++;
                        
                        /* Check for noise texture patterns */
                        /* Pattern: division by large power of 2 (texture atlas/noise) */
                        if (strstr(usage, "/1024") || strstr(usage, "/ 1024") ||
                            strstr(usage, "/512") || strstr(usage, "/ 512") ||
                            strstr(usage, "/256") || strstr(usage, "/ 256")) {
                            noise_score += 100;  /* Very strong noise indicator */
                        }
                        
                        /* Pattern: multiplication by very small number */
                        const char *p = usage;
                        while (p < usage + 60 && *p) {
                            if ((strncmp(p, "*0.00", 5) == 0 || strncmp(p, "* 0.00", 6) == 0) &&
                                !strstr(line_start, "mix") && !strstr(line_start, "smoothstep")) {
                                noise_score += 80;
                                break;
                            }
                            p++;
                        }
                        
                        /* Pattern: .x, .y, .z, .r only access (noise often single channel) */
                        p = usage + strlen(channel_name);
                        while (*p == ' ' || *p == ',') p++;
                        if (*p == ')') {
                            p++;
                            while (*p == ' ') p++;
                            if (*p == '.' && (p[1] == 'x' || p[1] == 'r') && 
                                (p[2] == ';' || p[2] == ')' || p[2] == ',' || p[2] == ' ' ||
                                 p[2] == '*' || p[2] == '+' || p[2] == '-' || p[2] == '/')) {
                                noise_score += 30;  /* Moderate noise indicator */
                            }
                        }
                        
                        /* Check for buffer/screen-space read patterns */
                        /* Pattern: fragCoord or iResolution nearby */
                        p = line_start;
                        while (p < line_end) {
                            if (strncmp(p, "fragCoord", 9) == 0 ||
                                strncmp(p, "iResolution", 11) == 0) {
                                buffer_score += 50;
                                break;
                            }
                            p++;
                        }
                        
                        /* Pattern: simple uv variable (very common for feedback) */
                        p = usage;
                        while (p < usage + 40 && *p) {
                            if (p[0] == 'u' && p[1] == 'v' && 
                                (p[2] == ')' || p[2] == '.' || p[2] == ',' || p[2] == ' ' || p[2] == '*' || p[2] == '+')) {
                                buffer_score += 40;
                                break;
                            }
                            /* Also check for common coordinate variable names */
                            if (strncmp(p, "coord", 5) == 0 || strncmp(p, "pos", 3) == 0 ||
                                strncmp(p, "st)", 3) == 0 || strncmp(p, "st,", 3) == 0) {
                                buffer_score += 30;
                                break;
                            }
                            p++;
                        }
                        
                        /* Pattern: temporal mixing (strong self-feedback indicator) */
                        if (strstr(line_start, "mix") && strstr(line_start, channel_name)) {
                            self_score += 60;
                        }
                        if (strstr(line_start, "+=") || strstr(line_start, "*=")) {
                            self_score += 20;  /* Accumulation pattern */
                        }
                        
                        usage++;
                    }
                }
                
                /* Determine channel source based on scores and conventions */
                if (!channel_used) {
                    /* Channel not used at all - default to noise (harmless) */
                    pass->channels[c].source = CHANNEL_SOURCE_NOISE;
                } else if (noise_score > buffer_score && noise_score > self_score && noise_score >= 50) {
                    /* Clear noise texture usage */
                    pass->channels[c].source = CHANNEL_SOURCE_NOISE;
                    log_info("  %s iChannel%d: noise (score: noise=%d, buffer=%d, self=%d)", 
                             pass->name, c, noise_score, buffer_score, self_score);
                } else if (buffer_score > 0 || self_score > 0) {
                    /* Screen-space read detected - determine if self or other buffer */
                    
                    /*
                     * Shadertoy convention: For buffer passes, iChannel0 is usually self-feedback
                     * UNLESS there's strong evidence of noise texture usage.
                     * This is the most common pattern in Shadertoy.
                     */
                    if (c == 0 && noise_score < 50) {
                        /* iChannel0 in buffer pass = almost always self-feedback */
                        pass->channels[c].source = CHANNEL_SOURCE_SELF;
                        log_info("  %s iChannel%d: self (convention + scores: noise=%d, buffer=%d, self=%d)", 
                                 pass->name, c, noise_score, buffer_score, self_score);
                    } else if (self_score > buffer_score) {
                        /* Temporal/accumulation pattern detected */
                        pass->channels[c].source = CHANNEL_SOURCE_SELF;
                        log_info("  %s iChannel%d: self (score: noise=%d, buffer=%d, self=%d)", 
                                 pass->name, c, noise_score, buffer_score, self_score);
                    } else {
                        /* Reading from another buffer */
                        /* Map channel index to buffer: ch1->BufA, ch2->BufB, etc. for non-zero channels */
                        if (c == 0) {
                            pass->channels[c].source = CHANNEL_SOURCE_SELF;
                        } else if (c == 1) {
                            pass->channels[c].source = CHANNEL_SOURCE_BUFFER_A;
                        } else if (c == 2) {
                            pass->channels[c].source = CHANNEL_SOURCE_BUFFER_B;
                        } else {
                            pass->channels[c].source = CHANNEL_SOURCE_BUFFER_C;
                        }
                        log_info("  %s iChannel%d: buffer (score: noise=%d, buffer=%d, self=%d)", 
                                 pass->name, c, noise_score, buffer_score, self_score);
                    }
                } else {
                    /* Channel used but pattern unclear - use Shadertoy conventions */
                    if (c == 0) {
                        /* iChannel0 = self for buffer passes (most common convention) */
                        pass->channels[c].source = CHANNEL_SOURCE_SELF;
                    } else if (c == 1) {
                        pass->channels[c].source = CHANNEL_SOURCE_BUFFER_A;
                    } else if (c == 2) {
                        pass->channels[c].source = CHANNEL_SOURCE_BUFFER_B;
                    } else {
                        pass->channels[c].source = CHANNEL_SOURCE_BUFFER_C;
                    }
                    log_info("  %s iChannel%d: convention default (channel used, pattern unclear)", 
                             pass->name, c);
                }
            }
        }

        const char* src_names[] = {"None", "BufA", "BufB", "BufC", "BufD", "Tex", "Kbd", "Noise", "Self"};
        log_info("  Pass %d (%s): ch0=%s, ch1=%s, ch2=%s, ch3=%s",
                 i, pass->name,
                 src_names[pass->channels[0].source],
                 src_names[pass->channels[1].source],
                 src_names[pass->channels[2].source],
                 src_names[pass->channels[3].source]);
    }

    log_info("Created multipass shader with %d passes (has_buffers=%d, image_index=%d)",
             shader->pass_count, shader->has_buffers, shader->image_pass_index);

    /* Analyze passes for smart optimization */
    const char *pass_sources[MULTIPASS_MAX_PASSES];
    int pass_types[MULTIPASS_MAX_PASSES];
    for (int i = 0; i < shader->pass_count; i++) {
        pass_sources[i] = shader->passes[i].source;
        pass_types[i] = (int)shader->passes[i].type;
    }
    multipass_optimizer_analyze_shader(&shader->multipass_opt,
                                       pass_sources,
                                       pass_types,
                                       shader->pass_count,
                                       shader->image_pass_index);

    return shader;
}

bool multipass_init_gl(multipass_shader_t *shader, int width, int height) {
    if (!shader) return false;

    if (shader->is_initialized) {
        log_debug("Multipass GL already initialized");
        return true;
    }

    log_info("Initializing multipass GL resources (%dx%d)", width, height);
    
    /* Initialize the render optimizer for GPU state caching
     * 
     * IMPORTANT: The optimizer caches GL state to avoid redundant calls.
     * For multipass rendering, most state changes every pass, so we only
     * benefit from caching:
     * - Render state (depth test, blend, etc.) - set once per frame
     * - Clear color - rarely changes
     * - Viewport - changes per pass but optimizer tracks it
     * 
     * We DON'T cache aggressively:
     * - Programs - change every pass
     * - Textures - change every pass  
     * - FBOs - change every pass
     * - Uniforms - many change every frame (iTime, iFrame, etc.)
     */
    render_optimizer_init(&shader->optimizer);
    shader->optimizer.enabled = true;
    shader->optimizer.aggressive_mode = false;  /* Conservative mode */
    
    shader->use_smart_buffer_sizing = true;  /* Enable by default */

    /* Get the default framebuffer ID (GTK may use non-zero FBO) */
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &shader->default_framebuffer);
    log_info("Default framebuffer ID: %d", shader->default_framebuffer);

    /* Create VAO and VBO for fullscreen quad */
    static const float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    /* VAO is required for desktop OpenGL 3.3 Core profile */
    glGenVertexArrays(1, &shader->vao);
    glBindVertexArray(shader->vao);

    glGenBuffers(1, &shader->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, shader->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    /* Generate high-quality noise texture (1024x1024 for Shadertoy compatibility)
     * Many shaders expect texture(iChannel0, p/1024.0) to sample noise */
    glGenTextures(1, &shader->noise_texture);
    glBindTexture(GL_TEXTURE_2D, shader->noise_texture);

    #define NOISE_SIZE 1024
    unsigned char *noise_data = malloc(NOISE_SIZE * NOISE_SIZE * 4);
    if (noise_data) {
        /* Use a simple but decent PRNG for reproducible noise */
        unsigned int seed = 12345;
        for (int y = 0; y < NOISE_SIZE; y++) {
            for (int x = 0; x < NOISE_SIZE; x++) {
                int idx = (y * NOISE_SIZE + x) * 4;
                /* LCG-based pseudo-random with mixing for each channel */
                seed = seed * 1664525u + 1013904223u;
                noise_data[idx + 0] = (seed >> 24) & 0xFF;
                seed = seed * 1664525u + 1013904223u;
                noise_data[idx + 1] = (seed >> 24) & 0xFF;
                seed = seed * 1664525u + 1013904223u;
                noise_data[idx + 2] = (seed >> 24) & 0xFF;
                seed = seed * 1664525u + 1013904223u;
                noise_data[idx + 3] = (seed >> 24) & 0xFF;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, NOISE_SIZE, NOISE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise_data);
        free(noise_data);
    }
    #undef NOISE_SIZE
    /* Use NEAREST for crisp noise values, LINEAR can cause blurring */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    /* Calculate base scaled resolution for buffer passes */
    int base_scaled_w = (int)(width * shader->resolution_scale);
    int base_scaled_h = (int)(height * shader->resolution_scale);
    if (base_scaled_w < 1) base_scaled_w = 1;
    if (base_scaled_h < 1) base_scaled_h = 1;
    shader->scaled_width = base_scaled_w;
    shader->scaled_height = base_scaled_h;
    
    log_info("Base resolution scale: %.2f (base buffers: %dx%d, output: %dx%d)",
             shader->resolution_scale, base_scaled_w, base_scaled_h, width, height);

    /* Initialize each pass with smart per-buffer resolution */
    for (int i = 0; i < shader->pass_count; i++) {
        multipass_pass_t *pass = &shader->passes[i];
        
        /* Buffer passes use smart per-buffer resolution, Image pass uses full resolution */
        if (pass->type >= PASS_TYPE_BUFFER_A && pass->type <= PASS_TYPE_BUFFER_D) {
            /* Use multipass optimizer for per-buffer resolution */
            if (shader->multipass_opt.enabled && shader->multipass_opt.smart_resolution_enabled) {
                int opt_w, opt_h;
                multipass_optimizer_get_pass_resolution(&shader->multipass_opt, i,
                                                        base_scaled_w, base_scaled_h,
                                                        &opt_w, &opt_h);
                pass->width = opt_w;
                pass->height = opt_h;
                log_info("  Pass %d (%s): %dx%d (%.0f%% of base)",
                         i, pass->name, opt_w, opt_h, 
                         (float)(opt_w * opt_h) / (float)(base_scaled_w * base_scaled_h) * 100.0f);
            } else {
                pass->width = base_scaled_w;
                pass->height = base_scaled_h;
            }
        } else {
            pass->width = width;
            pass->height = height;
        }
        pass->ping_pong_index = 0;
        pass->needs_clear = true;

        /* Create FBO and textures for buffer passes */
        if (pass->type >= PASS_TYPE_BUFFER_A && pass->type <= PASS_TYPE_BUFFER_D) {
            glGenFramebuffers(1, &pass->fbo);
            glGenTextures(2, pass->textures);

            for (int t = 0; t < 2; t++) {
                glBindTexture(GL_TEXTURE_2D, pass->textures[t]);
                /* Use GL_RGBA16F for good precision with half the bandwidth
                 * 16-bit floats are faster for memory-bound shaders
                 * Use scaled resolution for buffer passes */
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, pass->width, pass->height, 0,
                            GL_RGBA, GL_HALF_FLOAT, NULL);
                /* 
                 * Start with GL_LINEAR - we'll upgrade to GL_LINEAR_MIPMAP_LINEAR
                 * in multipass_compile_all() if any shader uses textureLod on this buffer.
                 * This avoids mipmap generation overhead for buffers that don't need it.
                 */
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }

            log_info("Created FBO and textures for %s", pass->name);
        }
    }

    shader->is_initialized = true;
    shader->frame_count = 0;

    /* Initialize GPU timer queries for accurate frame time measurement */
    adaptive_init_gpu_timing(&shader->adaptive);

    return true;
}

/* Cache uniform locations after compilation to avoid glGetUniformLocation per frame */
static void cache_uniform_locations(multipass_pass_t *pass) {
    if (!pass || !pass->program) return;
    
    GLuint prog = pass->program;
    uniform_locations_t *u = &pass->uniforms;
    
    u->iTime = glGetUniformLocation(prog, "iTime");
    u->iTimeDelta = glGetUniformLocation(prog, "iTimeDelta");
    u->iFrameRate = glGetUniformLocation(prog, "iFrameRate");
    u->iFrame = glGetUniformLocation(prog, "iFrame");
    u->iResolution = glGetUniformLocation(prog, "iResolution");
    u->iMouse = glGetUniformLocation(prog, "iMouse");
    u->iDate = glGetUniformLocation(prog, "iDate");
    u->iSampleRate = glGetUniformLocation(prog, "iSampleRate");
    u->iChannelResolution = glGetUniformLocation(prog, "iChannelResolution");
    
    u->iChannel[0] = glGetUniformLocation(prog, "iChannel0");
    u->iChannel[1] = glGetUniformLocation(prog, "iChannel1");
    u->iChannel[2] = glGetUniformLocation(prog, "iChannel2");
    u->iChannel[3] = glGetUniformLocation(prog, "iChannel3");
    
    u->cached = true;
    
    log_debug("Cached uniform locations for %s: iTime=%d, iResolution=%d, iFrame=%d",
              pass->name, u->iTime, u->iResolution, u->iFrame);
}

/* Cache buffer pass indices for each channel to avoid linear search every frame */
static void cache_channel_buffer_indices(multipass_shader_t *shader) {
    if (!shader) return;
    
    for (int p = 0; p < shader->pass_count; p++) {
        multipass_pass_t *pass = &shader->passes[p];
        
        for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
            pass->channel_buffer_index[c] = -1;  /* Default: not a buffer */
            
            channel_source_t src = pass->channels[c].source;
            if (src >= CHANNEL_SOURCE_BUFFER_A && src <= CHANNEL_SOURCE_BUFFER_D) {
                int target_type = PASS_TYPE_BUFFER_A + (src - CHANNEL_SOURCE_BUFFER_A);
                
                /* Find the pass index for this buffer type */
                for (int i = 0; i < shader->pass_count; i++) {
                    if ((int)shader->passes[i].type == target_type) {
                        pass->channel_buffer_index[c] = i;
                        break;
                    }
                }
            }
        }
    }
    
    log_debug("Cached channel buffer indices for %d passes", shader->pass_count);
}

/* Check if shader source uses textureLod (needs mipmaps) */
static bool shader_uses_textureLod(const char *source) {
    return source && strstr(source, "textureLod") != NULL;
}

bool multipass_compile_pass(multipass_shader_t *shader, int pass_index) {
    if (!shader || pass_index < 0 || pass_index >= shader->pass_count) {
        return false;
    }

    multipass_pass_t *pass = &shader->passes[pass_index];

    log_info("Compiling pass %d: %s", pass_index, pass->name);

    /* Clean up previous compilation */
    if (pass->program) {
        glDeleteProgram(pass->program);
        pass->program = 0;
    }
    if (pass->compile_error) {
        free(pass->compile_error);
        pass->compile_error = NULL;
    }

    /* Wrap pass source with compatibility layer */
    char *wrapped = wrap_pass_source(shader->common_source, pass->source);
    if (!wrapped) {
        pass->compile_error = str_dup("Failed to allocate memory for shader wrapping");
        pass->is_compiled = false;
        return false;
    }

    /* Compile shaders */
    GLuint program;
    bool success = shader_create_program_from_sources(fullscreen_vertex_shader, wrapped, &program);

    free(wrapped);

    if (!success) {
        const char *error_log = multipass_get_error_log();
        pass->compile_error = str_dup(error_log ? error_log : "Unknown compilation error");
        pass->is_compiled = false;
        log_error("Failed to compile pass %s: %s", pass->name, pass->compile_error);
        return false;
    }

    pass->program = program;
    pass->is_compiled = true;
    
    /* Cache uniform locations for performance */
    cache_uniform_locations(pass);
    
    /* Check if this shader uses textureLod (needs mipmaps) */
    pass->needs_mipmaps = shader_uses_textureLod(pass->source);
    if (pass->needs_mipmaps) {
        log_debug("Pass %s uses textureLod, will generate mipmaps", pass->name);
    }

    log_info("Successfully compiled pass %s (program=%u)", pass->name, program);

    return true;
}

bool multipass_compile_all(multipass_shader_t *shader) {
    if (!shader) return false;

    bool all_success = true;

    for (int i = 0; i < shader->pass_count; i++) {
        if (!multipass_compile_pass(shader, i)) {
            all_success = false;
        }
    }
    
    /* Cache buffer pass indices for fast texture binding */
    cache_channel_buffer_indices(shader);
    
    /* 
     * Determine which buffer passes need mipmaps based on whether
     * any pass that READS from them uses textureLod.
     * This is more accurate than checking the buffer's own source.
     */
    for (int buf = 0; buf < shader->pass_count; buf++) {
        multipass_pass_t *buf_pass = &shader->passes[buf];
        if (buf_pass->type < PASS_TYPE_BUFFER_A || buf_pass->type > PASS_TYPE_BUFFER_D) {
            continue;  /* Only check buffer passes */
        }
        
        buf_pass->needs_mipmaps = false;
        
        /* Check all passes that might read from this buffer */
        for (int reader = 0; reader < shader->pass_count; reader++) {
            multipass_pass_t *reader_pass = &shader->passes[reader];
            if (!reader_pass->source) continue;
            
            /* Check if this reader uses textureLod */
            if (!shader_uses_textureLod(reader_pass->source)) continue;
            
            /* Check if this reader reads from our buffer */
            for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
                channel_source_t src = reader_pass->channels[c].source;
                if (src == CHANNEL_SOURCE_BUFFER_A + (buf_pass->type - PASS_TYPE_BUFFER_A)) {
                    buf_pass->needs_mipmaps = true;
                    log_debug("Buffer %s needs mipmaps: read by %s via iChannel%d",
                              buf_pass->name, reader_pass->name, c);
                    
                    /* Upgrade texture filter to support mipmaps */
                    for (int t = 0; t < 2; t++) {
                        glBindTexture(GL_TEXTURE_2D, buf_pass->textures[t]);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                        glGenerateMipmap(GL_TEXTURE_2D);
                    }
                    break;
                }
            }
            if (buf_pass->needs_mipmaps) break;
        }
    }

    return all_success;
}

void multipass_resize(multipass_shader_t *shader, int width, int height) {
    if (!shader || !shader->is_initialized) return;

    /* Calculate base scaled resolution (from adaptive resolution system) */
    int base_scaled_w = (int)(width * shader->resolution_scale);
    int base_scaled_h = (int)(height * shader->resolution_scale);
    if (base_scaled_w < 1) base_scaled_w = 1;
    if (base_scaled_h < 1) base_scaled_h = 1;

    /* Quick check: if Image pass has correct size and base scale unchanged, skip resize */
    if (shader->image_pass_index >= 0) {
        multipass_pass_t *img = &shader->passes[shader->image_pass_index];
        if (img->width == width && img->height == height &&
            shader->scaled_width == base_scaled_w && shader->scaled_height == base_scaled_h) {
            return;
        }
    }

    shader->scaled_width = base_scaled_w;
    shader->scaled_height = base_scaled_h;

    for (int i = 0; i < shader->pass_count; i++) {
        multipass_pass_t *pass = &shader->passes[i];

        /* Determine target resolution for this pass:
         * - Image pass: always full output resolution
         * - Buffer passes: use smart per-buffer resolution from optimizer */
        int target_w, target_h;
        if (pass->type >= PASS_TYPE_BUFFER_A && pass->type <= PASS_TYPE_BUFFER_D) {
            /* Use multipass optimizer to get smart per-buffer resolution */
            if (shader->multipass_opt.enabled && shader->multipass_opt.smart_resolution_enabled) {
                multipass_optimizer_get_pass_resolution(&shader->multipass_opt, i,
                                                        base_scaled_w, base_scaled_h,
                                                        &target_w, &target_h);
            } else {
                /* Fallback to uniform scaling */
                target_w = base_scaled_w;
                target_h = base_scaled_h;
            }
        } else {
            /* Image pass - always full resolution */
            target_w = width;
            target_h = height;
        }

        if (pass->width == target_w && pass->height == target_h) {
            continue;
        }

        pass->width = target_w;
        pass->height = target_h;

        /* Resize buffer textures */
        if (pass->type >= PASS_TYPE_BUFFER_A && pass->type <= PASS_TYPE_BUFFER_D) {
            for (int t = 0; t < 2; t++) {
                glBindTexture(GL_TEXTURE_2D, pass->textures[t]);
                /* Use GL_RGBA16F for HDR bloom support - matches init */
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, target_w, target_h, 0,
                            GL_RGBA, GL_HALF_FLOAT, NULL);
                /* Only regenerate mipmaps if this buffer needs them */
                if (pass->needs_mipmaps) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
            }
            pass->needs_clear = true;
        }
    }
}

void multipass_destroy(multipass_shader_t *shader) {
    if (!shader) return;

    /* Delete passes */
    for (int i = 0; i < shader->pass_count; i++) {
        multipass_pass_t *pass = &shader->passes[i];

        if (pass->program) glDeleteProgram(pass->program);
        if (pass->fbo) glDeleteFramebuffers(1, &pass->fbo);
        if (pass->textures[0]) glDeleteTextures(2, pass->textures);

        free(pass->name);
        free(pass->source);
        free(pass->compile_error);
    }

    /* Delete shared resources */
    if (shader->vbo) glDeleteBuffers(1, &shader->vbo);
    if (shader->vao) glDeleteVertexArrays(1, &shader->vao);
    if (shader->noise_texture) glDeleteTextures(1, &shader->noise_texture);
    if (shader->keyboard_texture) glDeleteTextures(1, &shader->keyboard_texture);
    
    /* Cleanup adaptive resolution system */
    adaptive_destroy(&shader->adaptive);
    
    /* Cleanup render optimizer */
    render_optimizer_destroy(&shader->optimizer);

    free(shader->common_source);
    free(shader);
}

/* ============================================
 * Rendering Functions
 * ============================================ */

void multipass_set_uniforms(multipass_shader_t *shader,
                            int pass_index,
                            float shader_time,
                            float mouse_x, float mouse_y,
                            bool mouse_click) {
    if (!shader || pass_index < 0 || pass_index >= shader->pass_count) return;

    multipass_pass_t *pass = &shader->passes[pass_index];
    if (!pass->program) return;

    /* Use direct GL call for program - changes every pass, no benefit from caching */
    glUseProgram(pass->program);

    /* Use cached uniform locations for performance */
    const uniform_locations_t *u = &pass->uniforms;

    /* Time uniforms - these change every frame, use direct GL calls */
    if (u->iTime >= 0) glUniform1f(u->iTime, shader_time);
    if (u->iTimeDelta >= 0) glUniform1f(u->iTimeDelta, 1.0f / 60.0f);
    if (u->iFrameRate >= 0) glUniform1f(u->iFrameRate, 60.0f);
    if (u->iFrame >= 0) glUniform1i(u->iFrame, shader->frame_count);

    /* Resolution - changes per pass */
    if (u->iResolution >= 0) {
        float w = (float)pass->width;
        float h = (float)pass->height;
        glUniform3f(u->iResolution, w, h, w / h);
    }

    /* Mouse */
    if (u->iMouse >= 0) {
        float click_x = mouse_click ? mouse_x : 0.0f;
        float click_y = mouse_click ? mouse_y : 0.0f;
        glUniform4f(u->iMouse, mouse_x, mouse_y, click_x, click_y);
    }

    /* Date - only update if uniform is used (avoid syscall overhead) */
    if (u->iDate >= 0) {
        /* Cache date info - only update once per second would be even better,
         * but for now just use the cached location */
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        if (tm_info) {
            float year = (float)(tm_info->tm_year + 1900);
            float month = (float)(tm_info->tm_mon + 1);
            float day = (float)tm_info->tm_mday;
            float seconds = (float)(tm_info->tm_hour * 3600 +
                                    tm_info->tm_min * 60 +
                                    tm_info->tm_sec);
            glUniform4f(u->iDate, year, month, day, seconds);
        }
    }

    if (u->iSampleRate >= 0) glUniform1f(u->iSampleRate, 44100.0f);

    /* Channel resolutions - use static data */
    if (u->iChannelResolution >= 0) {
        static const float resolutions[12] = {
            256.0f, 256.0f, 1.0f,
            256.0f, 256.0f, 1.0f,
            256.0f, 256.0f, 1.0f,
            256.0f, 256.0f, 1.0f
        };
        glUniform3fv(u->iChannelResolution, 4, resolutions);
    }
}

void multipass_bind_textures(multipass_shader_t *shader, int pass_index) {
    if (!shader || pass_index < 0 || pass_index >= shader->pass_count) return;

    multipass_pass_t *pass = &shader->passes[pass_index];
    if (!pass->program) return;

    log_debug_frame(shader->frame_count, "Binding textures for pass %d (%s):", pass_index, pass->name);

    /* Use cached uniform locations - textures change every pass so no caching benefit */
    const uniform_locations_t *u = &pass->uniforms;

    for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
        /* Skip if this channel uniform doesn't exist in the shader */
        if (u->iChannel[c] < 0) continue;

        GLuint tex = shader->noise_texture;  /* Default to noise */
        const char *source_name = "noise";
        (void)source_name; /* Used for debug logging */

        switch (pass->channels[c].source) {
            case CHANNEL_SOURCE_BUFFER_A:
            case CHANNEL_SOURCE_BUFFER_B:
            case CHANNEL_SOURCE_BUFFER_C:
            case CHANNEL_SOURCE_BUFFER_D: {
                /* Use cached buffer index instead of linear search */
                int cached_idx = pass->channel_buffer_index[c];
                multipass_pass_t *buf_pass = (cached_idx >= 0) ? &shader->passes[cached_idx] : NULL;

                if (buf_pass && buf_pass->textures[0]) {
                    /*
                     * IMPORTANT: Read from the CURRENT ping-pong index
                     * This is the texture that was written to in the previous frame
                     * or the most recently completed render of this buffer
                     */
                    tex = buf_pass->textures[buf_pass->ping_pong_index];
                    source_name = buf_pass->name;
                    log_debug_frame(shader->frame_count, "  iChannel%d: Bound to %s tex[%d]=%u",
                              c, buf_pass->name, buf_pass->ping_pong_index, tex);
                } else {
                    int buf_idx = pass->channels[c].source - CHANNEL_SOURCE_BUFFER_A;
                    log_debug_frame(shader->frame_count, "  iChannel%d: Buffer %c not found, using noise", c, 'A' + buf_idx);
                }
                break;
            }

            case CHANNEL_SOURCE_SELF:
                if (pass->textures[0]) {
                    /* For self-reference, read from current ping-pong (previous frame) */
                    tex = pass->textures[pass->ping_pong_index];
                    source_name = "self(feedback)";
                }
                break;

            case CHANNEL_SOURCE_NOISE:
            default:
                tex = shader->noise_texture;
                source_name = "noise";
                break;
        }

        /* Direct GL calls - textures change every pass, no caching benefit */
        glActiveTexture(GL_TEXTURE0 + c);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(u->iChannel[c], c);
    }
}

void multipass_swap_buffers(multipass_shader_t *shader, int pass_index) {
    /*
     * NOTE: This function is now deprecated as ping-pong swapping
     * is handled directly in multipass_render_pass after rendering.
     * Kept for API compatibility.
     */
    (void)shader;
    (void)pass_index;
}

void multipass_render_pass(multipass_shader_t *shader,
                           int pass_index,
                           float time,
                           float mouse_x, float mouse_y,
                           bool mouse_click) {
    if (!shader || pass_index < 0 || pass_index >= shader->pass_count) return;

    multipass_pass_t *pass = &shader->passes[pass_index];

    if (!pass->is_compiled || !pass->program) {
        return;
    }
    
    /* Track pass rendering for statistics */
    shader->optimizer.stats.passes_rendered++;

    log_debug_frame(shader->frame_count, "Rendering pass %d: %s (program=%u, fbo=%u, size=%dx%d)",
              pass_index, pass->name, pass->program, pass->fbo, pass->width, pass->height);

    /* Bind FBO for buffer passes, or default framebuffer for Image pass
     * FBOs change every pass - use direct GL calls */
    if (pass->fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, pass->fbo);

        /*
         * Ping-pong buffer logic:
         * - ping_pong_index points to the texture containing the PREVIOUS frame's result
         * - We WRITE to the OTHER texture (1 - ping_pong_index)
         * - Other passes READ from ping_pong_index (previous result)
         * - After rendering, we swap so the newly written texture becomes readable
         */
        int write_idx = 1 - pass->ping_pong_index;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, pass->textures[write_idx], 0);

        log_debug_frame(shader->frame_count, "Pass %d: writing to tex[%d]=%u, reading from tex[%d]=%u",
                  pass_index, write_idx, pass->textures[write_idx],
                  pass->ping_pong_index, pass->textures[pass->ping_pong_index]);

        /* Clear on first frame */
        if (pass->needs_clear) {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            pass->needs_clear = false;
        }
    } else {
        /* Image pass renders to screen - use the stored default framebuffer
         * (GTK GL contexts may use non-zero FBO as default) */
        glBindFramebuffer(GL_FRAMEBUFFER, shader->default_framebuffer);
    }

    glViewport(0, 0, pass->width, pass->height);

    /* Use program and set uniforms - program is set in set_uniforms via optimizer */
    multipass_set_uniforms(shader, pass_index, time, mouse_x, mouse_y, mouse_click);
    multipass_bind_textures(shader, pass_index);

    /* Draw fullscreen quad - VAO/VBO already bound in multipass_render */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* For buffer passes, finalize the render */
    if (pass->fbo) {
        int write_idx = 1 - pass->ping_pong_index;

        /* Only generate mipmaps if any shader actually uses textureLod
         * This is expensive so we only do it when needed */
        if (pass->needs_mipmaps) {
            glBindTexture(GL_TEXTURE_2D, pass->textures[write_idx]);
            glGenerateMipmap(GL_TEXTURE_2D);
            log_debug_frame(shader->frame_count, "Generated mipmaps for pass %d texture[%d]=%u",
                      pass_index, write_idx, pass->textures[write_idx]);
        }

        /*
         * SWAP ping-pong index AFTER rendering:
         * Now ping_pong_index points to the texture we just wrote,
         * so other passes will read from our fresh output
         */
        pass->ping_pong_index = write_idx;
        log_debug_frame(shader->frame_count, "Pass %d: ping_pong_index now %d (points to freshly rendered texture)",
                  pass_index, pass->ping_pong_index);
    }
}

void multipass_render(multipass_shader_t *shader,
                      float time,
                      float mouse_x, float mouse_y,
                      bool mouse_click) {
    if (!shader || !shader->is_initialized) return;

    /* Start GPU timing for this frame (if enabled) */
    adaptive_begin_frame(&shader->adaptive);
    
    /* Begin optimizer frame for state caching and temporal analysis */
    render_optimizer_begin_frame(&shader->optimizer, time, mouse_x, mouse_y, mouse_click);
    
    /* Begin multipass optimizer frame for static scene detection */
    multipass_optimizer_begin_frame(&shader->multipass_opt, time, mouse_x, mouse_y, mouse_click);
    
    /* Reset per-frame workload tracking for accurate feedback to adaptive_scale */
    multipass_optimizer_reset_frame_workload(&shader->multipass_opt);
    
    /* ========================================================================
     * SYNCHRONIZED OPTIMIZATION MODE
     * 
     * Coordinate between adaptive_scale (global resolution) and multipass_optimizer
     * (per-buffer resolution + pass skipping) for maximum performance.
     * 
     * Three optimization levels:
     * 1. NORMAL: Per-buffer smart resolution only
     * 2. AGGRESSIVE: Enable half-rate buffer updates  
     * 3. EMERGENCY: Maximum savings - all optimizations active
     * ======================================================================== */
    
    float current_fps = adaptive_get_current_fps(&shader->adaptive);
    float target_fps = shader->adaptive.config.target_fps;
    bool adaptive_emergency = shader->adaptive.in_emergency;
    bool adaptive_thermal = shader->adaptive.thermal_throttling;
    float stability = shader->adaptive.stability_score;
    
    if (target_fps > 0.0f) {
        float fps_ratio = current_fps / target_fps;
        
        /* EMERGENCY MODE: Sync with adaptive_scale's emergency state
         * When adaptive detects severe performance drop, go maximum aggressive */
        if (adaptive_emergency || adaptive_thermal) {
            if (!shader->multipass_opt.half_rate_enabled) {
                shader->multipass_opt.half_rate_enabled = true;
                shader->multipass_opt.global_quality = 0.5f;  /* Reduce quality bias */
                log_info("Optimizer: EMERGENCY MODE - enabling all optimizations "
                         "(adaptive emergency=%d, thermal=%d)",
                         adaptive_emergency, adaptive_thermal);
            }
        }
        /* AGGRESSIVE MODE: Enable half-rate if FPS is struggling */
        else if (fps_ratio < 0.90f && !shader->multipass_opt.half_rate_enabled) {
            shader->multipass_opt.half_rate_enabled = true;
            shader->multipass_opt.global_quality = 0.6f;
            log_info("Optimizer: AGGRESSIVE MODE - enabling half-rate updates "
                     "(FPS: %.1f / %.1f = %.0f%%)",
                     current_fps, target_fps, fps_ratio * 100.0f);
        }
        /* NORMAL MODE: Disable aggressive optimizations when performance is good */
        else if (fps_ratio > 0.98f && stability > 0.7f && 
                 shader->multipass_opt.half_rate_enabled) {
            shader->multipass_opt.half_rate_enabled = false;
            shader->multipass_opt.global_quality = 0.8f;  /* Restore quality */
            log_info("Optimizer: NORMAL MODE - performance recovered "
                     "(FPS: %.1f, stability: %.0f%%)",
                     current_fps, stability * 100.0f);
        }
    }

    /* Update adaptive resolution using wall-clock time (not shader time)
     * This ensures proper FPS measurement even when shader time is paused/scaled */
    double wall_time;
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    wall_time = (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    wall_time = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
    adaptive_update(&shader->adaptive, wall_time);
    
    /* Sync resolution scale from adaptive system */
    shader->resolution_scale = adaptive_get_scale(&shader->adaptive);

    /* Query the CURRENT framebuffer binding every frame
     * GTK's GtkGLArea can change its FBO on resize, so we must always query */
    GLint current_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &current_fbo);
    shader->default_framebuffer = current_fbo;

    log_debug_frame(shader->frame_count, "=== Frame %d ===", shader->frame_count);

    /* Set optimal render state ONCE at start of frame
     * These are the ONLY things worth caching - they're set once and never change */
    opt_disable(&shader->optimizer, GL_DEPTH_TEST);
    opt_disable(&shader->optimizer, GL_BLEND);
    opt_disable(&shader->optimizer, GL_CULL_FACE);
    opt_disable(&shader->optimizer, GL_SCISSOR_TEST);
    opt_depth_mask(&shader->optimizer, GL_FALSE);
    opt_color_mask(&shader->optimizer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /*
     * Setup vertex state ONCE for all passes (major performance optimization)
     * All passes use the same fullscreen quad
     */
    /* Bind VAO - required for desktop OpenGL 3.3 Core */
    glBindVertexArray(shader->vao);
    glBindBuffer(GL_ARRAY_BUFFER, shader->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    /*
     * Shadertoy rendering order:
     * 1. Render all buffer passes in order (A, B, C, D)
     * 2. After each buffer pass, generate mipmaps for textureLod support
     * 3. Render Image pass last to the screen
     */

    /* Render buffer passes first (in order A, B, C, D) 
     * Use multipass optimizer to skip passes when scene is static */
    for (int type = PASS_TYPE_BUFFER_A; type <= PASS_TYPE_BUFFER_D; type++) {
        for (int i = 0; i < shader->pass_count; i++) {
            if ((int)shader->passes[i].type == type) {
                /* Check if optimizer says we can skip this pass */
                bool should_render = multipass_optimizer_should_render_pass(&shader->multipass_opt, i);
                
                /* Record pass for workload feedback (pass full base resolution for comparison) */
                multipass_optimizer_record_pass(&shader->multipass_opt, i,
                                                shader->passes[i].width,
                                                shader->passes[i].height,
                                                shader->scaled_width,
                                                shader->scaled_height,
                                                should_render);
                
                if (should_render) {
                    log_debug_frame(shader->frame_count, "Executing buffer pass: %s", shader->passes[i].name);
                    multipass_render_pass(shader, i, time, mouse_x, mouse_y, mouse_click);
                    multipass_optimizer_pass_rendered(&shader->multipass_opt, i, 
                                                      shader->passes[i].width, 
                                                      shader->passes[i].height);
                } else {
                    log_debug_frame(shader->frame_count, "Skipping buffer pass: %s (static scene)", shader->passes[i].name);
                    multipass_optimizer_pass_skipped(&shader->multipass_opt, i);
                }
            }
        }
    }

    /* Render Image pass last (directly to screen) */
    if (shader->image_pass_index >= 0) {
        log_debug_frame(shader->frame_count, "Executing Image pass (index=%d)", shader->image_pass_index);

        /* Ensure we're rendering to the default framebuffer (screen) */
        glBindFramebuffer(GL_FRAMEBUFFER, shader->default_framebuffer);

        /* Get viewport size from Image pass */
        multipass_pass_t *image_pass = &shader->passes[shader->image_pass_index];
        glViewport(0, 0, image_pass->width, image_pass->height);

        /* Clear the screen before rendering Image pass */
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        multipass_render_pass(shader, shader->image_pass_index, time,
                              mouse_x, mouse_y, mouse_click);
    } else {
        log_error("No Image pass found! (image_pass_index=%d, pass_count=%d)",
                  shader->image_pass_index, shader->pass_count);
    }

    /* Cleanup vertex state */
    glDisableVertexAttribArray(0);

    /* End GPU timing for this frame */
    adaptive_end_frame(&shader->adaptive);
    
    /* End optimizer frame - updates statistics and temporal state */
    render_optimizer_end_frame(&shader->optimizer);
    
    /* End multipass optimizer frame */
    multipass_optimizer_end_frame(&shader->multipass_opt);
    
    /* Log multipass optimizer stats every 600 frames */
    if (shader->frame_count > 0 && shader->frame_count % 600 == 0) {
        multipass_optimizer_log_stats(&shader->multipass_opt);
        
        /* Log current optimization mode and sync status */
        const char *mode_name = "NORMAL";
        if (shader->adaptive.in_emergency || shader->adaptive.thermal_throttling) {
            mode_name = "EMERGENCY";
        } else if (shader->multipass_opt.half_rate_enabled) {
            mode_name = "AGGRESSIVE";
        }
        
        log_info("  Optimization mode: %s (adaptive scale: %.0f%%, quality: %.0f%%)",
                 mode_name,
                 shader->adaptive.current_scale * 100.0f,
                 shader->multipass_opt.global_quality * 100.0f);
        
        /* Log combined effective savings */
        float base_pixels = (float)(shader->scaled_width * shader->scaled_height);
        float actual_pixels = 0.0f;
        for (int i = 0; i < shader->pass_count; i++) {
            if (shader->passes[i].type != PASS_TYPE_IMAGE) {
                actual_pixels += (float)(shader->passes[i].width * shader->passes[i].height);
            }
        }
        if (base_pixels > 0.0f) {
            float savings = (1.0f - actual_pixels / (base_pixels * (shader->pass_count - 1))) * 100.0f;
            log_info("  Buffer pixel savings: %.1f%% (per-buffer smart resolution)", savings);
        }
        
        /* Log workload feedback metrics */
        float effective_workload = multipass_optimizer_get_effective_workload(&shader->multipass_opt);
        float pixel_reduction = multipass_optimizer_get_pixel_reduction(&shader->multipass_opt);
        log_info("  Effective workload: %.1f%% (pixel reduction: %.1f%%)",
                 effective_workload * 100.0f, pixel_reduction * 100.0f);
    }

    shader->frame_count++;
}

/* ============================================
 * Adaptive Resolution API (delegates to adaptive_scale module)
 * ============================================ */

void multipass_set_resolution_scale(multipass_shader_t *shader, float scale) {
    if (!shader) return;
    adaptive_force_scale(&shader->adaptive, scale);
    shader->resolution_scale = adaptive_get_scale(&shader->adaptive);
    shader->scaled_width = 0;
    shader->scaled_height = 0;
}

float multipass_get_resolution_scale(const multipass_shader_t *shader) {
    return shader ? adaptive_get_scale(&shader->adaptive) : 1.0f;
}

void multipass_set_adaptive_resolution(multipass_shader_t *shader, 
                                        bool enabled,
                                        float target_fps,
                                        float min_scale,
                                        float max_scale) {
    if (!shader) return;
    
    adaptive_set_enabled(&shader->adaptive, enabled);
    adaptive_set_target_fps(&shader->adaptive, target_fps);
    adaptive_set_scale_range(&shader->adaptive, min_scale, max_scale);
    
    /* Sync to shader fields */
    shader->min_resolution_scale = shader->adaptive.config.min_scale;
    shader->max_resolution_scale = shader->adaptive.config.max_scale;
}

void multipass_configure_adaptive(multipass_shader_t *shader,
                                  const adaptive_config_t *config) {
    if (!shader || !config) return;
    shader->adaptive.config = *config;
    shader->min_resolution_scale = config->min_scale;
    shader->max_resolution_scale = config->max_scale;
}

void multipass_set_adaptive_mode(multipass_shader_t *shader, adaptive_mode_t mode) {
    if (!shader) return;
    adaptive_set_mode(&shader->adaptive, mode);
}

bool multipass_is_adaptive_resolution(const multipass_shader_t *shader) {
    return shader ? shader->adaptive.enabled : false;
}

float multipass_get_current_fps(const multipass_shader_t *shader) {
    return shader ? adaptive_get_current_fps(&shader->adaptive) : 0.0f;
}

adaptive_stats_t multipass_get_adaptive_stats(const multipass_shader_t *shader) {
    if (!shader) {
        adaptive_stats_t empty = {0};
        return empty;
    }
    return adaptive_get_stats(&shader->adaptive);
}

void multipass_reset(multipass_shader_t *shader) {
    if (!shader) return;

    shader->frame_count = 0;

    for (int i = 0; i < shader->pass_count; i++) {
        shader->passes[i].ping_pong_index = 0;
        shader->passes[i].needs_clear = true;
    }
}

/* ============================================
 * Query Functions
 * ============================================ */

const char *multipass_get_error(const multipass_shader_t *shader, int pass_index) {
    if (!shader || pass_index < 0 || pass_index >= shader->pass_count) {
        return NULL;
    }
    return shader->passes[pass_index].compile_error;
}

char *multipass_get_all_errors(const multipass_shader_t *shader) {
    if (!shader) return NULL;

    size_t total_len = 0;
    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].compile_error) {
            total_len += strlen(shader->passes[i].name) + 3;
            total_len += strlen(shader->passes[i].compile_error) + 2;
        }
    }

    if (total_len == 0) return NULL;

    char *result = malloc(total_len + 1);
    if (!result) return NULL;

    result[0] = '\0';
    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].compile_error) {
            strcat(result, shader->passes[i].name);
            strcat(result, ": ");
            strcat(result, shader->passes[i].compile_error);
            strcat(result, "\n");
        }
    }

    return result;
}

bool multipass_has_errors(const multipass_shader_t *shader) {
    if (!shader) return true;

    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].compile_error) {
            return true;
        }
    }

    return false;
}

bool multipass_is_ready(const multipass_shader_t *shader) {
    if (!shader || !shader->is_initialized) return false;

    for (int i = 0; i < shader->pass_count; i++) {
        if (!shader->passes[i].is_compiled) {
            return false;
        }
    }

    return true;
}

multipass_pass_t *multipass_get_pass_by_type(multipass_shader_t *shader,
                                              multipass_type_t type) {
    if (!shader) return NULL;

    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].type == type) {
            return &shader->passes[i];
        }
    }

    return NULL;
}

int multipass_get_pass_index(const multipass_shader_t *shader, multipass_type_t type) {
    if (!shader) return -1;

    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].type == type) {
            return i;
        }
    }

    return -1;
}

GLuint multipass_get_buffer_texture(const multipass_shader_t *shader,
                                     multipass_type_t type) {
    if (!shader) return 0;

    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].type == type) {
            return shader->passes[i].textures[shader->passes[i].ping_pong_index];
        }
    }

    return 0;
}

/* ============================================
 * Debug Functions
 * ============================================ */

void multipass_debug_dump(const multipass_shader_t *shader) {
    if (!shader) {
        log_debug("Multipass shader: NULL");
        return;
    }

    log_debug("=== Multipass Shader Debug ===");
    log_debug("Pass count: %d", shader->pass_count);
    log_debug("Image pass index: %d", shader->image_pass_index);
    log_debug("Has buffers: %d", shader->has_buffers);
    log_debug("Is initialized: %d", shader->is_initialized);
    log_debug("Frame count: %d", shader->frame_count);

    for (int i = 0; i < shader->pass_count; i++) {
        const multipass_pass_t *pass = &shader->passes[i];
        log_debug("--- Pass %d: %s ---", i, pass->name);
        log_debug("  Type: %d (%s)", pass->type, multipass_type_name(pass->type));
        log_debug("  Program: %u", pass->program);
        log_debug("  FBO: %u", pass->fbo);
        log_debug("  Textures: [%u, %u]", pass->textures[0], pass->textures[1]);
        log_debug("  Size: %dx%d", pass->width, pass->height);
        log_debug("  Compiled: %d", pass->is_compiled);
        log_debug("  Ping-pong: %d", pass->ping_pong_index);

        for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
            log_debug("  Channel %d: %s", c,
                     multipass_channel_source_name(pass->channels[c].source));
        }

        if (pass->compile_error) {
            log_debug("  Error: %s", pass->compile_error);
        }
    }

    log_debug("=== End Multipass Debug ===");
}
