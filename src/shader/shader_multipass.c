/* Shader Multipass Support - Implementation
 * Implements Shadertoy-style multipass rendering with BufferA-D and Image passes
 * 
 * This is a self-contained shader compilation and rendering system.
 * No legacy dependencies required.
 */

#include "neowall/shader/shader_multipass.h"
#include "neowall/shader/adaptive_scale.h"
#include "neowall/shader/render_optimizer.h"
#include "neowall/shader/multipass_optimizer.h"
#include "neowall/shader/shadertoy_compat.h"
#include "neowall/shader/shader_log.h"
#include "neowall/shader/shader_error_log.h"
#include "multipass_internal.h"
#include "neowall/shader/platform_compat.h"
#include "neowall/shader/shader_stdlib.h"
#include "neowall/shader/reactive.h"
#include "neowall/shader/program_cache.h"
#include "neowall/textures.h"
#ifdef NEOWALL_HAVE_TERMINAL
#include "term_render.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

/* ============================================
 * Error Logging — delegates to shader_error_log
 * ============================================ */

static inline void clear_error_log(void) { shader_error_log_clear(); }

#define append_to_error_log shader_error_log_append

const char *multipass_get_error_log(void) { return shader_error_log_get(); }

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
    /* Allow snapshotting the linked binary (program_cache). Per spec the hint
     * must be set BEFORE glLinkProgram to guarantee a non-zero binary length. */
    glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
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
 *
 * Shared parser helpers (find_pattern, find_function_end, extract_substring,
 * str_dup) live in multipass_parse.c and are reached through
 * multipass_internal.h. We keep terse local aliases so the compile/render code
 * below reads unchanged.
 * ============================================ */

#define find_pattern        mp_find_pattern
#define find_function_end   mp_find_function_end
#define extract_substring   mp_extract_substring
#define str_dup             mp_str_dup

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
        case CHANNEL_SOURCE_AUDIO:    return "Audio";
        case CHANNEL_SOURCE_FONT:     return "Font";
        case CHANNEL_SOURCE_TERM:     return "Terminal";
        default:                      return "None";
    }
}

/* ---- manifest binding API (Tier 2/3) ---- */

channel_source_t multipass_channel_source_from_name(const char *name) {
    if (!name) return CHANNEL_SOURCE_NONE;
    if (!strcasecmp(name, "audio"))    return CHANNEL_SOURCE_AUDIO;
    if (!strcasecmp(name, "font"))     return CHANNEL_SOURCE_FONT;
    if (!strcasecmp(name, "term") || !strcasecmp(name, "terminal") ||
        !strcasecmp(name, "text")) return CHANNEL_SOURCE_TERM;
    if (!strcasecmp(name, "noise"))    return CHANNEL_SOURCE_NOISE;
    if (!strcasecmp(name, "self"))     return CHANNEL_SOURCE_SELF;
    if (!strcasecmp(name, "keyboard")) return CHANNEL_SOURCE_KEYBOARD;
    if (!strcasecmp(name, "texture"))  return CHANNEL_SOURCE_TEXTURE;
    if (!strcasecmp(name, "bufferA") || !strcasecmp(name, "buffer_a") ||
        !strcasecmp(name, "a")) return CHANNEL_SOURCE_BUFFER_A;
    if (!strcasecmp(name, "bufferB") || !strcasecmp(name, "buffer_b") ||
        !strcasecmp(name, "b")) return CHANNEL_SOURCE_BUFFER_B;
    if (!strcasecmp(name, "bufferC") || !strcasecmp(name, "buffer_c") ||
        !strcasecmp(name, "c")) return CHANNEL_SOURCE_BUFFER_C;
    if (!strcasecmp(name, "bufferD") || !strcasecmp(name, "buffer_d") ||
        !strcasecmp(name, "d")) return CHANNEL_SOURCE_BUFFER_D;
    return CHANNEL_SOURCE_NONE;
}

uniform_bind_t multipass_bind_from_name(const char *name) {
    if (!name) return UNIFORM_BIND_CONST;
    if (!strcasecmp(name, "cpu"))          return UNIFORM_BIND_CPU;
    if (!strcasecmp(name, "ram"))          return UNIFORM_BIND_RAM;
    if (!strcasecmp(name, "net_down") || !strcasecmp(name, "netdown")) return UNIFORM_BIND_NET_DOWN;
    if (!strcasecmp(name, "net_up")   || !strcasecmp(name, "netup"))   return UNIFORM_BIND_NET_UP;
    if (!strcasecmp(name, "battery"))      return UNIFORM_BIND_BATTERY;
    if (!strcasecmp(name, "time_of_day") || !strcasecmp(name, "timeofday")) return UNIFORM_BIND_TIME_OF_DAY;
    if (!strcasecmp(name, "sun"))          return UNIFORM_BIND_SUN;
    if (!strcasecmp(name, "audio") || !strcasecmp(name, "audio_level")) return UNIFORM_BIND_AUDIO_LEVEL;
    if (!strcasecmp(name, "audio_bass") || !strcasecmp(name, "bass")) return UNIFORM_BIND_AUDIO_BASS;
    if (!strcasecmp(name, "audio_mid")  || !strcasecmp(name, "mid"))  return UNIFORM_BIND_AUDIO_MID;
    if (!strcasecmp(name, "audio_treble") || !strcasecmp(name, "treble")) return UNIFORM_BIND_AUDIO_TREBLE;
    if (!strcasecmp(name, "audio_beat") || !strcasecmp(name, "beat")) return UNIFORM_BIND_AUDIO_BEAT;
    if (!strcasecmp(name, "key_energy") || !strcasecmp(name, "keys")) return UNIFORM_BIND_KEY_ENERGY;
    if (!strcasecmp(name, "mouse_energy") || !strcasecmp(name, "mouse")) return UNIFORM_BIND_MOUSE_ENERGY;
    if (!strcasecmp(name, "swap"))        return UNIFORM_BIND_SWAP;
    if (!strcasecmp(name, "disk_read")  || !strcasecmp(name, "diskread"))  return UNIFORM_BIND_DISK_READ;
    if (!strcasecmp(name, "disk_write") || !strcasecmp(name, "diskwrite")) return UNIFORM_BIND_DISK_WRITE;
    if (!strcasecmp(name, "load"))        return UNIFORM_BIND_LOAD;
    if (!strcasecmp(name, "cpu_temp") || !strcasecmp(name, "cputemp")) return UNIFORM_BIND_CPU_TEMP;
    if (!strcasecmp(name, "gpu"))         return UNIFORM_BIND_GPU;
    if (!strcasecmp(name, "gpu_temp") || !strcasecmp(name, "gputemp")) return UNIFORM_BIND_GPU_TEMP;
    if (!strcasecmp(name, "uptime"))      return UNIFORM_BIND_UPTIME;
    if (!strcasecmp(name, "procs") || !strcasecmp(name, "processes")) return UNIFORM_BIND_PROCS;
    return UNIFORM_BIND_CONST;
}

void multipass_set_channel(multipass_shader_t *shader,
                           multipass_type_t pass_type,
                           int channel, channel_source_t source) {
    if (!shader || channel < 0 || channel >= MULTIPASS_MAX_CHANNELS) return;
    shader->explicit_bindings = true;
    for (int i = 0; i < shader->pass_count; i++) {
        if (shader->passes[i].type == pass_type) {
            shader->passes[i].channels[channel].source = source;
            /* recompute the cached buffer index for this channel */
            shader->passes[i].channel_buffer_index[channel] = -1;
            if (source >= CHANNEL_SOURCE_BUFFER_A && source <= CHANNEL_SOURCE_BUFFER_D) {
                int target = PASS_TYPE_BUFFER_A + (source - CHANNEL_SOURCE_BUFFER_A);
                for (int j = 0; j < shader->pass_count; j++) {
                    if ((int)shader->passes[j].type == target) {
                        shader->passes[i].channel_buffer_index[channel] = j;
                        break;
                    }
                }
            }
            log_info("Manifest: %s iChannel%d -> %s",
                     shader->passes[i].name, channel,
                     multipass_channel_source_name(source));
            return;
        }
    }
}

void multipass_add_user_uniform(multipass_shader_t *shader,
                                const char *name, uniform_bind_t bind, float value) {
    if (!shader || !name || shader->user_uniform_count >= MULTIPASS_MAX_USER_UNIFORMS) return;
    multipass_user_uniform_t *uu = &shader->user_uniforms[shader->user_uniform_count++];
    snprintf(uu->name, sizeof(uu->name), "%s", name);
    uu->bind = bind;
    uu->value = value;
    uu->location = -1;
    log_info("Manifest: user uniform '%s' (bind=%d, value=%.3f)", uu->name, bind, value);
}

nw_result multipass_attach_terminal(multipass_shader_t *shader,
                                    const char *cmd, int cols, int rows,
                                    int cell_w, int cell_h, const char *font_path) {
    if (!shader || !cmd) return nw_err(NW_ERR_INVALID_ARG, "attach_terminal: null");
#ifdef NEOWALL_HAVE_TERMINAL
    if (shader->term) {
        term_render_destroy(shader->term);
        shader->term = NULL;
    }
    term_render_opts o = {
        .cmd = cmd,
        .cols = cols > 0 ? cols : 100,
        .rows = rows > 0 ? rows : 30,
        .cell_w = cell_w > 0 ? cell_w : 9,
        .cell_h = cell_h > 0 ? cell_h : 18,
        .font_data = NULL, .font_len = 0,
        .font_path = font_path,
        .default_fg = -1, .default_bg = -1,   /* no override → built-in */
    };
    /* Optional overrides carried on the shader (set by the config layer). */
    if (shader->term_cwd && shader->term_cwd[0]) o.cwd = shader->term_cwd;
    if (shader->term_env && shader->term_env[0]) o.term_env = shader->term_env;
    if (shader->term_font_bold)   o.font_bold_path   = shader->term_font_bold;
    if (shader->term_font_italic) o.font_italic_path = shader->term_font_italic;
    if (shader->term_has_fg) o.default_fg = shader->term_fg;
    if (shader->term_has_bg) o.default_bg = shader->term_bg;
    nw_result err = nw_ok();
    shader->term = term_render_create(&o, &err);
    if (!shader->term) {
        log_error("attach_terminal: %s (%s)", nw_status_str(err.status),
                  err.context ? err.context : "");
        return err;
    }
    /* An image-only shader that samples the terminal is animated (it changes
     * whenever the child draws), so keep the redraw loop alive. */
    shader->is_animated = true;
    log_info("Attached terminal '%s' (%dx%d cells, %dx%d px)",
             cmd, o.cols, o.rows, o.cell_w, o.cell_h);
    return nw_ok();
#else
    (void)cols; (void)rows; (void)cell_w; (void)cell_h; (void)font_path;
    return nw_err(NW_ERR_UNSUPPORTED, "terminal source disabled at build time");
#endif
}

bool multipass_terminal_mouse(multipass_shader_t *shader, int px, int py,
                              int button, bool pressed, bool motion) {
#ifdef NEOWALL_HAVE_TERMINAL
    if (!shader || !shader->term) return false;
    return term_render_mouse(shader->term, px, py, button, pressed, motion);
#else
    (void)shader; (void)px; (void)py; (void)button; (void)pressed; (void)motion;
    return false;
#endif
}

bool multipass_terminal_wants_mouse(const multipass_shader_t *shader) {
#ifdef NEOWALL_HAVE_TERMINAL
    return shader && shader->term && term_render_wants_mouse(shader->term);
#else
    (void)shader;
    return false;
#endif
}

bool multipass_terminal_animating(const multipass_shader_t *shader,
                                  unsigned fx_settle_ms) {
#ifdef NEOWALL_HAVE_TERMINAL
    if (!shader || !shader->term) return false;
    return term_render_animating(shader->term, fx_settle_ms);
#else
    (void)shader; (void)fx_settle_ms;
    return false;
#endif
}

void multipass_terminal_shutdown(multipass_shader_t *shader) {
#ifdef NEOWALL_HAVE_TERMINAL
    /* Kill the terminal child + its process group NOW, without touching any GL
     * state. Safe to call from the main thread during daemon shutdown, before
     * (or instead of) the full multipass_destroy — which otherwise never runs
     * on the exit path, orphaning the child (a TUI then spins on its dead PTY).
     * Idempotent: term_render_destroy nulls shader->term. */
    if (shader && shader->term) {
        term_render_destroy(shader->term);
        shader->term = NULL;
    }
#else
    (void)shader;
#endif
}

bool multipass_terminal_write(multipass_shader_t *shader, const void *bytes, size_t len) {
#ifdef NEOWALL_HAVE_TERMINAL
    if (!shader || !shader->term) return false;
    return term_render_write(shader->term, bytes, len);
#else
    (void)shader; (void)bytes; (void)len;
    return false;
#endif
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
 *
 * The actual parser lives in multipass_parse.c; declarations are in
 * shader_multipass.h. Kept here as a signpost.
 * ============================================ */

#if 0 /* moved to multipass_parse.c — retained under #if 0 to keep the diff
         compact; will be physically deleted in a follow-up commit */

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

#endif /* moved to multipass_parse.c */

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
    "// Shift from this window's gl_FragCoord to the virtual screen's. Zero\n"
    "// unless the wallpaper is spanned across several monitors.\n"
    "uniform vec2 iSpanOffset;\n"
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
    "// Live-terminal source (CHANNEL_SOURCE_TERM). The cell record grid is an\n"
    "// INTEGER texture; the shader reads it via nwTerm(). Bind the matching\n"
    "// iChannelN to \"terminal\" so the engine points it at the cell texture,\n"
    "// then sample with nwTerm(iChannelN_as_usampler...). To keep call sites\n"
    "// simple we expose a dedicated integer sampler + the metadata uniforms.\n"
    "uniform highp usampler2D iTermCells;\n"
    "uniform highp usampler2D iTermChange; // R32UI: per-cell last-change ms\n"
    "uniform sampler2D iTermAtlas;\n"
    "uniform sampler2D iTermColorAtlas;\n"
    "uniform vec4 iTermInfo;       // cols, rows, cellW, cellH\n"
    "uniform vec2 iTermAtlasSize;  // atlas texel w, h\n"
    "uniform vec3 iTermCursor;     // cursorX, cursorY, visible\n"
    "uniform vec4 iTermCursorPrev; // prevX, prevY, moveTime, (unused)\n"
    "uniform vec4 iTermFX;         // bloom, scanline, crt-curve, chromatic\n"
    "uniform vec2 iTermFade;       // x = change-fade intensity, y = now (ms)\n"
    "#define NW_HAS_ITERMFX 1\n"
    "\n"
    "// Channel resolutions\n"
    "uniform vec3 iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "\n"
    "// --- Shadertoy / GLSL-ES compatibility shims ---\n"
    "// Legacy ES sampling builtins -> desktop core equivalents. Defined as\n"
    "// macros so we never have to rewrite call sites in the user source.\n"
    "#define texture2D texture\n"
    "#define texture2DLod textureLod\n"
    "#define texture2DProj textureProj\n"
    "#define textureCube texture\n"
    "#define textureCubeLod textureLod\n"
    "// Legacy global-time alias used by older Shadertoy shaders.\n"
    "#define iGlobalTime iTime\n"
    "\n"
    "// Output\n"
    "out vec4 fragColor;\n"
    "\n";

static const char *multipass_wrapper_suffix =
    "\n"
    "void main() {\n"
    "    mainImage(fragColor, gl_FragCoord.xy + iSpanOffset);\n"
    "}\n";

/**
 * Normalize Shadertoy source for desktop OpenGL 3.3 core compilation.
 *
 * The implementation lives in the GL-free shadertoy_compat module so it can be
 * unit-tested headless. See shadertoy_compat.h. The GLSL-ES builtin shims
 * (texture2D -> texture, etc.) are handled by #define macros in
 * multipass_wrapper_prefix, not here.
 */
static char *fix_shadertoy_compatibility(const char *source) {
    return shadertoy_compat_fix(source);
}

/* Wrap a pass source with Shadertoy compatibility layer + neowall std-lib.
 * Layout: #version/uniforms (prefix) -> reactive uniforms -> GLSL std-lib ->
 * user common -> user pass -> main() suffix. The reactive block and std-lib are
 * injected unconditionally; unused uniforms/functions are stripped by the GLSL
 * compiler, so plain Shadertoy shaders are unaffected. */
static char *wrap_pass_source(const char *common, const char *pass_source,
                              const char *user_uniform_decls) {
    size_t prefix_len = strlen(multipass_wrapper_prefix);
    size_t react_len  = strlen(neowall_reactive_uniforms);
    size_t lib_len    = strlen(neowall_glsl_stdlib) + strlen(neowall_glsl_stdlib2) + strlen(neowall_glsl_stdlib3) + strlen(neowall_glsl_stdlib4) + strlen(neowall_glsl_stdlib5) + strlen(neowall_glsl_stdlib6) + strlen(neowall_glsl_stdlib7);
    size_t udecl_len  = user_uniform_decls ? strlen(user_uniform_decls) : 0;
    size_t common_len = common ? strlen(common) : 0;
    size_t pass_len = pass_source ? strlen(pass_source) : 0;
    size_t suffix_len = strlen(multipass_wrapper_suffix);

    /* Extra space for .xy additions (worst case: every iChannelResolution gets .xy) */
    size_t total = prefix_len + react_len + lib_len + udecl_len +
                   (common_len * 2) + (pass_len * 2) + suffix_len + 64;
    char *wrapped = malloc(total);
    if (!wrapped) return NULL;

    wrapped[0] = '\0';
    strcat(wrapped, multipass_wrapper_prefix);
    strcat(wrapped, neowall_reactive_uniforms);
    strcat(wrapped, neowall_glsl_stdlib);
    strcat(wrapped, neowall_glsl_stdlib2);
    strcat(wrapped, neowall_glsl_stdlib3);
    strcat(wrapped, neowall_glsl_stdlib4);
    strcat(wrapped, neowall_glsl_stdlib5);
    strcat(wrapped, neowall_glsl_stdlib6);
    strcat(wrapped, neowall_glsl_stdlib7);
    if (user_uniform_decls) {
        strcat(wrapped, user_uniform_decls);
    }
    
    /* Apply compatibility fixes to common code. A #line 1 directive resets the
     * compiler's line counter so any error the driver reports points at the
     * user's own source line, not an offset into the injected preamble. */
    if (common) {
        strcat(wrapped, "#line 1\n");
        char *fixed_common = fix_shadertoy_compatibility(common);
        if (fixed_common) {
            strcat(wrapped, fixed_common);
            free(fixed_common);
        } else {
            strcat(wrapped, common);
        }
    }
    strcat(wrapped, "\n");

    /* Apply compatibility fixes to pass source. Reset line numbering again so a
     * single-file shader's errors read as "line N" of that file. */
    if (pass_source) {
        strcat(wrapped, "#line 1\n");
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

    /* Detect whether this shader is ANIMATED. A shader that references no
     * frame-varying input renders the same image every frame — the engine can
     * paint it once and stop the redraw loop entirely (0% GPU at idle).
     * Conservative: any time/frame/interactive/buffer reference counts as
     * animated; buffers imply feedback which can evolve without iTime. */
    shader->is_animated = (parse_result->pass_count > 1);
    {
        const char *animate_tokens[] = {
            "iTime", "iGlobalTime", "iFrame", "iDate", "iMouse",
            "iChannel", "iAudio", "iKeyEnergy", "iMouseEnergy",
            "iCpu", "iRam", "iNet", "iLoad", "iGpu", "iDisk", "iSwap",
            "iBattery", "iCharging", "iTimeOfDay", "iSun", "iDayFraction",
            "iProcs", "iUptime", "iTemp", "iNv", "iThermal", "iActivity",
            "iPulse", "iCpuMax", "iCpuSpread", "iActivity"
        };
        for (int i = 0; !shader->is_animated && i < parse_result->pass_count; i++) {
            const char *src = parse_result->pass_sources[i];
            if (!src) continue;
            for (size_t t = 0; t < sizeof(animate_tokens)/sizeof(animate_tokens[0]); t++) {
                if (strstr(src, animate_tokens[t])) { shader->is_animated = true; break; }
            }
        }
        if (!shader->is_animated && parse_result->common_source) {
            for (size_t t = 0; t < sizeof(animate_tokens)/sizeof(animate_tokens[0]); t++) {
                if (strstr(parse_result->common_source, animate_tokens[t])) {
                    shader->is_animated = true; break;
                }
            }
        }
        if (!shader->is_animated) {
            log_info("Static shader detected (no time/interactive inputs) - will render once and idle");
        }
    }

    for (int i = 0; i < parse_result->pass_count; i++) {
        multipass_pass_t *pass = &shader->passes[i];

        pass->type = parse_result->pass_types[i];
        pass->name = str_dup(multipass_type_name(pass->type));
        pass->source = str_dup(parse_result->pass_sources[i]);
        pass->is_compiled = false;

        /*
         * CHANNEL BINDING (heuristic, best-effort)
         *
         * Shadertoy shaders declare which texture feeds each iChannelN out of
         * band (in the website UI), so a bare .glsl file carries no binding
         * metadata. We recover the intent by scoring the source text for
         * tell-tale usage patterns (noise lookups, self-feedback, buffer
         * reads). This is correct for the overwhelmingly common Shadertoy
         * idioms but is fundamentally a guess: an exotic shader can still be
         * bound wrong, in which case it renders incorrectly rather than
         * crashing. If you hit that, the per-channel decision is logged below
         * (noise/buffer/self with scores) so it can be diagnosed, and an
         * explicit binding syntax in the config is the proper long-term fix.
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

    /* Bake the fullscreen-quad vertex layout into the VAO ONCE. Every pass
     * uses the same quad; binding the VAO at render time restores the whole
     * attribute state with a single call instead of re-specifying
     * pointer/enable per frame. */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    /* Generate a Shadertoy-compatible 256x256 RGBA noise texture.
     * Shadertoy's noise channels have two properties shaders depend on:
     *   1. Bilinear filtering (iq-style value noise samples BETWEEN texels)
     *   2. green(x,y) == red(x+37, y+17)  (and alpha == blue offset the same
     *      way) so one fetch of .yx yields two correlated z-slices:
     *      mix(rg.x, rg.y, f.z) in the classic noise() function.
     * Size must be 256: shaders index with (uv+0.5)/256.0 and
     * iChannelResolution reports 256. */
    glGenTextures(1, &shader->noise_texture);
    glBindTexture(GL_TEXTURE_2D, shader->noise_texture);

    #define NOISE_SIZE 256
    unsigned char *noise_data = malloc(NOISE_SIZE * NOISE_SIZE * 4);
    if (noise_data) {
        /* Pass 1: fill red + blue with reproducible LCG noise */
        unsigned int seed = 12345;
        for (int i = 0; i < NOISE_SIZE * NOISE_SIZE; i++) {
            seed = seed * 1664525u + 1013904223u;
            noise_data[i * 4 + 0] = (seed >> 24) & 0xFF;  /* red */
            seed = seed * 1664525u + 1013904223u;
            noise_data[i * 4 + 2] = (seed >> 24) & 0xFF;  /* blue */
        }
        /* Pass 2: green = red shifted by (37,17), alpha = blue shifted */
        for (int y = 0; y < NOISE_SIZE; y++) {
            int sy = (y + 17) & (NOISE_SIZE - 1);
            for (int x = 0; x < NOISE_SIZE; x++) {
                int sx = (x + 37) & (NOISE_SIZE - 1);
                int dst = (y * NOISE_SIZE + x) * 4;
                int src = (sy * NOISE_SIZE + sx) * 4;
                noise_data[dst + 1] = noise_data[src + 0];  /* green */
                noise_data[dst + 3] = noise_data[src + 2];  /* alpha */
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, NOISE_SIZE, NOISE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise_data);
        free(noise_data);
    }
    #undef NOISE_SIZE
    /* LINEAR + REPEAT + mipmaps to match Shadertoy noise channel sampling.
     * Shadertoy channels default to 'mipmap' filtering; shaders that need
     * raw mip-0 texels pass an explicit LOD bias (the classic ', -100.').
     * Without mips, minified noise lookups alias and shimmer in motion. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);

    /* Live audio texture: width REACTIVE_AUDIO_BINS, 2 rows.
     * Row 0 = FFT spectrum, row 1 = waveform. Single red channel, float.
     * Re-uploaded each frame in multipass_set_uniforms from the reactive snap. */
    glGenTextures(1, &shader->audio_texture);
    glBindTexture(GL_TEXTURE_2D, shader->audio_texture);
    {
        float zero[REACTIVE_AUDIO_BINS * 2];
        memset(zero, 0, sizeof(zero));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, REACTIVE_AUDIO_BINS, 2, 0,
                     GL_RED, GL_FLOAT, zero);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Bitmap font atlas: a crisp 8x12 monospace face baked into a 128x72
     * texture (16x6 ASCII grid). Bound to any channel set to CHANNEL_SOURCE_FONT
     * so shaders can render legible text via the nwGlyph/nwChar stdlib helpers. */
    shader->font_texture = texture_create_font_atlas(0, 0);
    if (shader->font_texture) {
        log_info("Created font atlas texture (id=%u)", shader->font_texture);
    } else {
        log_error("Failed to create font atlas texture");
    }

#ifdef NEOWALL_HAVE_TERMINAL
    /* Live terminal source: an integer cell-record texture (RGBA32UI, one texel
     * per grid cell) plus an R8 glyph-coverage atlas. Both are (re)uploaded in
     * multipass_render when the terminal drew. Created only when a terminal was
     * attached (multipass_attach_terminal, before init). */
    if (shader->term) {
        int cols = term_render_cols(shader->term);
        int rows = term_render_rows(shader->term);

        glGenTextures(1, &shader->term_cell_texture);
        glBindTexture(GL_TEXTURE_2D, shader->term_cell_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, cols, rows, 0,
                     GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL);
        /* Integer textures MUST use NEAREST (no filtering of uint samplers). */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        /* Per-cell last-change timestamps (R32UI) for the change-driven fade,
         * same cols x rows grid, NEAREST like every integer texture. */
        glGenTextures(1, &shader->term_change_texture);
        glBindTexture(GL_TEXTURE_2D, shader->term_change_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, cols, rows, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        int aw = term_render_atlas_w(shader->term);
        int ah = term_render_atlas_h(shader->term);
        glGenTextures(1, &shader->term_atlas_texture);
        glBindTexture(GL_TEXTURE_2D, shader->term_atlas_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0,
                     GL_RED, GL_UNSIGNED_BYTE, NULL);
        /* LINEAR gives sub-pixel AA on the coverage bitmap (kitty-style). */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        shader->term_atlas_uploaded_w = 0;
        shader->term_atlas_uploaded_h = 0;

        /* Color-emoji atlas (RGBA8). Only created when the terminal actually
         * loaded a color-emoji font; otherwise the shader's color branch never
         * fires (no cell sets TERM_FLAG_COLOR) and we keep the texture unbound. */
        if (term_render_color_atlas(shader->term)) {
            glGenTextures(1, &shader->term_color_atlas_texture);
            glBindTexture(GL_TEXTURE_2D, shader->term_color_atlas_texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, aw, ah, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        log_info("Created terminal textures: cells %dx%d, atlas %dx%d%s", cols, rows, aw, ah,
                 shader->term_color_atlas_texture ? " (+color emoji)" : "");
    }
#endif

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
    u->iSpanOffset = glGetUniformLocation(prog, "iSpanOffset");
    u->iMouse = glGetUniformLocation(prog, "iMouse");
    u->iDate = glGetUniformLocation(prog, "iDate");
    u->iSampleRate = glGetUniformLocation(prog, "iSampleRate");
    u->iChannelResolution = glGetUniformLocation(prog, "iChannelResolution");
    
    u->iChannel[0] = glGetUniformLocation(prog, "iChannel0");
    u->iChannel[1] = glGetUniformLocation(prog, "iChannel1");
    u->iChannel[2] = glGetUniformLocation(prog, "iChannel2");
    u->iChannel[3] = glGetUniformLocation(prog, "iChannel3");

    /* neowall reactive uniforms */
    u->iCpu          = glGetUniformLocation(prog, "iCpu");
    u->iCpuCores     = glGetUniformLocation(prog, "iCpuCores");
    u->iCpuCoreCount = glGetUniformLocation(prog, "iCpuCoreCount");
    u->iCpuMax       = glGetUniformLocation(prog, "iCpuMax");
    u->iCpuSpread    = glGetUniformLocation(prog, "iCpuSpread");
    u->iRam          = glGetUniformLocation(prog, "iRam");
    u->iRamGB        = glGetUniformLocation(prog, "iRamGB");
    u->iRamTotalGB   = glGetUniformLocation(prog, "iRamTotalGB");
    u->iNetDown      = glGetUniformLocation(prog, "iNetDown");
    u->iNetUp        = glGetUniformLocation(prog, "iNetUp");
    u->iNetDownRaw   = glGetUniformLocation(prog, "iNetDownRaw");
    u->iNetUpRaw     = glGetUniformLocation(prog, "iNetUpRaw");
    u->iSwap         = glGetUniformLocation(prog, "iSwap");
    u->iDiskRead     = glGetUniformLocation(prog, "iDiskRead");
    u->iDiskWrite    = glGetUniformLocation(prog, "iDiskWrite");
    u->iLoad         = glGetUniformLocation(prog, "iLoad");
    u->iLoadRaw      = glGetUniformLocation(prog, "iLoadRaw");
    u->iCpuTemp      = glGetUniformLocation(prog, "iCpuTemp");
    u->iCpuTempC     = glGetUniformLocation(prog, "iCpuTempC");
    u->iGpu          = glGetUniformLocation(prog, "iGpu");
    u->iGpuTemp      = glGetUniformLocation(prog, "iGpuTemp");
    u->iGpuTempC     = glGetUniformLocation(prog, "iGpuTempC");
    u->iNvGpu        = glGetUniformLocation(prog, "iNvGpu");
    u->iNvVram       = glGetUniformLocation(prog, "iNvVram");
    u->iNvGpuTempC   = glGetUniformLocation(prog, "iNvGpuTempC");
    u->iNvPower      = glGetUniformLocation(prog, "iNvPower");
    u->iNvActive     = glGetUniformLocation(prog, "iNvActive");
    u->iThermal      = glGetUniformLocation(prog, "iThermal");
    u->iActivity     = glGetUniformLocation(prog, "iActivity");
    u->iPulse        = glGetUniformLocation(prog, "iPulse");
    u->iUptimeHours  = glGetUniformLocation(prog, "iUptimeHours");
    u->iProcs        = glGetUniformLocation(prog, "iProcs");
    u->iProcCount    = glGetUniformLocation(prog, "iProcCount");
    u->iBattery      = glGetUniformLocation(prog, "iBattery");
    u->iCharging     = glGetUniformLocation(prog, "iCharging");
    u->iTimeOfDay    = glGetUniformLocation(prog, "iTimeOfDay");
    u->iSun          = glGetUniformLocation(prog, "iSun");
    u->iDayFraction  = glGetUniformLocation(prog, "iDayFraction");
    u->iKeyEnergy    = glGetUniformLocation(prog, "iKeyEnergy");
    u->iMouseEnergy  = glGetUniformLocation(prog, "iMouseEnergy");
    u->iAudioLevel   = glGetUniformLocation(prog, "iAudioLevel");
    u->iAudioBass    = glGetUniformLocation(prog, "iAudioBass");
    u->iAudioMid     = glGetUniformLocation(prog, "iAudioMid");
    u->iAudioTreble  = glGetUniformLocation(prog, "iAudioTreble");
    u->iAudioBeat    = glGetUniformLocation(prog, "iAudioBeat");
    u->iAudioActive  = glGetUniformLocation(prog, "iAudioActive");
    u->iAudio        = glGetUniformLocation(prog, "iAudio");
    u->iTermAtlas    = glGetUniformLocation(prog, "iTermAtlas");
    u->iTermColorAtlas = glGetUniformLocation(prog, "iTermColorAtlas");
    u->iTermCells    = glGetUniformLocation(prog, "iTermCells");
    u->iTermChange   = glGetUniformLocation(prog, "iTermChange");
    u->iTermInfo     = glGetUniformLocation(prog, "iTermInfo");
    u->iTermAtlasSize = glGetUniformLocation(prog, "iTermAtlasSize");
    u->iTermCursor   = glGetUniformLocation(prog, "iTermCursor");
    u->iTermCursorPrev = glGetUniformLocation(prog, "iTermCursorPrev");
    u->iTermFX       = glGetUniformLocation(prog, "iTermFX");
    u->iTermFade     = glGetUniformLocation(prog, "iTermFade");

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

    /* Wrap pass source with compatibility layer.
     * Build the manifest user-uniform declarations (each is a float uniform). */
    char user_decls[MULTIPASS_MAX_USER_UNIFORMS * (MULTIPASS_UNIFORM_NAME_MAX + 24) + 64];
    user_decls[0] = '\0';
    if (shader->user_uniform_count > 0) {
        strcat(user_decls, "// --- manifest user uniforms ---\n");
        for (int ui = 0; ui < shader->user_uniform_count; ui++) {
            char line[MULTIPASS_UNIFORM_NAME_MAX + 24];
            snprintf(line, sizeof(line), "uniform float %s;\n",
                     shader->user_uniforms[ui].name);
            strcat(user_decls, line);
        }
    }

    char *wrapped = wrap_pass_source(shader->common_source, pass->source,
                                     user_decls[0] ? user_decls : NULL);
    if (!wrapped) {
        pass->compile_error = str_dup("Failed to allocate memory for shader wrapping");
        pass->is_compiled = false;
        return false;
    }

    /* Compile shaders — binary cache first. A cache hit skips the driver's
     * GLSL frontend entirely (50-300ms for big raymarchers -> ~1ms). */
    GLuint program = 0;
    uint64_t cache_key = program_cache_key(fullscreen_vertex_shader, wrapped);
    bool success = program_cache_load(cache_key, &program);
    if (!success) {
        success = shader_create_program_from_sources(fullscreen_vertex_shader, wrapped, &program);
        if (success) {
            program_cache_store(cache_key, program);
        }
    }

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

void multipass_set_span(multipass_shader_t *shader, int virt_width, int virt_height,
                        int off_x, int off_y) {
    if (!shader) return;

    bool spans = virt_width > 0 && virt_height > 0;

    shader->span_width = spans ? virt_width : 0;
    shader->span_height = spans ? virt_height : 0;
    shader->span_off_x = spans ? off_x : 0;
    shader->span_off_y = spans ? off_y : 0;
}

void multipass_resize(multipass_shader_t *shader, int width, int height) {
    if (!shader || !shader->is_initialized) return;

    /* Buffer passes are sized to the virtual screen when spanning, not to this
     * window: they are sampled at fragCoord/iResolution.xy in virtual
     * coordinates, so a window-sized buffer would be read off its own end. The
     * Image pass keeps the window's size — it still only fills the window.
     * Equal to width/height whenever the output is not spanned. */
    int buffer_w = shader->span_width > 0 ? shader->span_width : width;
    int buffer_h = shader->span_height > 0 ? shader->span_height : height;

    /* Calculate base scaled resolution (from adaptive resolution system) */
    int base_scaled_w = (int)(buffer_w * shader->resolution_scale);
    int base_scaled_h = (int)(buffer_h * shader->resolution_scale);
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
    if (shader->audio_texture) glDeleteTextures(1, &shader->audio_texture);
    if (shader->font_texture) glDeleteTextures(1, &shader->font_texture);
#ifdef NEOWALL_HAVE_TERMINAL
    if (shader->term_cell_texture) glDeleteTextures(1, &shader->term_cell_texture);
    if (shader->term_change_texture) glDeleteTextures(1, &shader->term_change_texture);
    if (shader->term_atlas_texture) glDeleteTextures(1, &shader->term_atlas_texture);
    if (shader->term_color_atlas_texture) glDeleteTextures(1, &shader->term_color_atlas_texture);
    if (shader->term) term_render_destroy(shader->term);
    free(shader->term_cwd);
    free(shader->term_env);
    free(shader->term_font_bold);
    free(shader->term_font_italic);
#endif
    
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

    /* Time uniforms - these change every frame, use direct GL calls.
     * iTimeDelta/iFrameRate carry REAL measured frame timing (computed once
     * per frame in multipass_render) so time-step-integrating shaders
     * (physics, flow fields) advance at the correct rate at any FPS. */
    if (u->iTime >= 0) glUniform1f(u->iTime, shader_time);
    shader->frame_shader_time = shader_time;
    if (u->iTimeDelta >= 0) glUniform1f(u->iTimeDelta, shader->frame_dt);
    if (u->iFrameRate >= 0) glUniform1f(u->iFrameRate, shader->frame_fps);
    if (u->iFrame >= 0) glUniform1i(u->iFrame, shader->frame_count);

    /* Resolution - changes per pass.
     *
     * When spanning, the Image pass fills only this monitor's window but must
     * think in virtual-screen coordinates, so it reports the virtual size and
     * shifts gl_FragCoord onto its slice. Buffer passes need neither: they are
     * already sized to the virtual screen (multipass_resize), so pass->width IS
     * the virtual width and their slice offset is zero. */
    bool spanned = shader->span_width > 0 && shader->span_height > 0;
    bool is_image = (pass->type == PASS_TYPE_IMAGE);

    if (u->iResolution >= 0) {
        float w = (spanned && is_image) ? (float)shader->span_width : (float)pass->width;
        float h = (spanned && is_image) ? (float)shader->span_height : (float)pass->height;
        glUniform3f(u->iResolution, w, h, w / h);
    }

    if (u->iSpanOffset >= 0) {
        float ox = (spanned && is_image) ? (float)shader->span_off_x : 0.0f;
        float oy = (spanned && is_image) ? (float)shader->span_off_y : 0.0f;
        glUniform2f(u->iSpanOffset, ox, oy);
    }

    /* Mouse */
    if (u->iMouse >= 0) {
        float click_x = mouse_click ? mouse_x : 0.0f;
        float click_y = mouse_click ? mouse_y : 0.0f;
        glUniform4f(u->iMouse, mouse_x, mouse_y, click_x, click_y);
    }

    /* Date - cached per wall-clock second (localtime() is surprisingly
     * expensive: TZ lookup + conversion). Only rebuilt when the second
     * ticks; multiple passes in the same frame always hit the cache. */
    if (u->iDate >= 0) {
        time_t t = time(NULL);
        if ((long long)t != shader->date_cached_sec) {
            struct tm tm_buf;
            if (localtime_r(&t, &tm_buf)) {
                shader->date_cached[0] = (float)(tm_buf.tm_year + 1900);
                shader->date_cached[1] = (float)(tm_buf.tm_mon + 1);
                shader->date_cached[2] = (float)tm_buf.tm_mday;
                shader->date_cached[3] = (float)(tm_buf.tm_hour * 3600 +
                                                 tm_buf.tm_min * 60 +
                                                 tm_buf.tm_sec);
            }
            shader->date_cached_sec = (long long)t;
        }
        glUniform4f(u->iDate, shader->date_cached[0], shader->date_cached[1],
                    shader->date_cached[2], shader->date_cached[3]);
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

    /* --- neowall reactive uniforms (live system + audio) ---
     * Snapshot taken ONCE per frame in multipass_render (mutex-guarded copy);
     * all passes read the same coherent values. Each glUniform is skipped
     * if the shader didn't reference that uniform (location < 0). */
    const reactive_snapshot_t *rp = &shader->frame_reactive;
    #define r (*rp)
    if (u->iCpu >= 0)         glUniform1f(u->iCpu, r.cpu);
    if (u->iCpuCoreCount >= 0) glUniform1i(u->iCpuCoreCount, r.cpu_cores);
    if (u->iCpuCores >= 0)    glUniform1fv(u->iCpuCores, 8, r.cpu_per);
    if (u->iCpuMax >= 0)      glUniform1f(u->iCpuMax, r.cpu_max);
    if (u->iCpuSpread >= 0)   glUniform1f(u->iCpuSpread, r.cpu_spread);
    if (u->iRam >= 0)         glUniform1f(u->iRam, r.ram);
    if (u->iRamGB >= 0)       glUniform1f(u->iRamGB, r.ram_gb);
    if (u->iRamTotalGB >= 0)  glUniform1f(u->iRamTotalGB, r.ram_total_gb);
    if (u->iNetDown >= 0)     glUniform1f(u->iNetDown, r.net_down);
    if (u->iNetUp >= 0)       glUniform1f(u->iNetUp, r.net_up);
    if (u->iNetDownRaw >= 0)  glUniform1f(u->iNetDownRaw, r.net_down_mbs);
    if (u->iNetUpRaw >= 0)    glUniform1f(u->iNetUpRaw, r.net_up_mbs);
    if (u->iSwap >= 0)        glUniform1f(u->iSwap, r.swap);
    if (u->iDiskRead >= 0)    glUniform1f(u->iDiskRead, r.disk_read);
    if (u->iDiskWrite >= 0)   glUniform1f(u->iDiskWrite, r.disk_write);
    if (u->iLoad >= 0)        glUniform1f(u->iLoad, r.load_avg);
    if (u->iLoadRaw >= 0)     glUniform1f(u->iLoadRaw, r.load_raw);
    if (u->iCpuTemp >= 0)     glUniform1f(u->iCpuTemp, r.cpu_temp);
    if (u->iCpuTempC >= 0)    glUniform1f(u->iCpuTempC, r.cpu_temp_c);
    if (u->iGpu >= 0)         glUniform1f(u->iGpu, r.gpu);
    if (u->iGpuTemp >= 0)     glUniform1f(u->iGpuTemp, r.gpu_temp);
    if (u->iGpuTempC >= 0)    glUniform1f(u->iGpuTempC, r.gpu_temp_c);
    if (u->iNvGpu >= 0)       glUniform1f(u->iNvGpu, r.nv_gpu);
    if (u->iNvVram >= 0)      glUniform1f(u->iNvVram, r.nv_vram);
    if (u->iNvGpuTempC >= 0)  glUniform1f(u->iNvGpuTempC, r.nv_temp_c);
    if (u->iNvPower >= 0)     glUniform1f(u->iNvPower, r.nv_power);
    if (u->iNvActive >= 0)    glUniform1f(u->iNvActive, r.nv_active ? 1.0f : 0.0f);
    if (u->iThermal >= 0)     glUniform1f(u->iThermal, r.thermal);
    if (u->iActivity >= 0)    glUniform1f(u->iActivity, r.activity);
    if (u->iPulse >= 0)       glUniform1f(u->iPulse, r.pulse);
    if (u->iUptimeHours >= 0) glUniform1f(u->iUptimeHours, r.uptime_hours);
    if (u->iProcs >= 0)       glUniform1f(u->iProcs, r.procs);
    if (u->iProcCount >= 0)   glUniform1i(u->iProcCount, r.proc_count);
    if (u->iBattery >= 0)     glUniform1f(u->iBattery, r.battery);
    if (u->iCharging >= 0)    glUniform1f(u->iCharging, r.charging ? 1.0f : 0.0f);
    if (u->iTimeOfDay >= 0)   glUniform1f(u->iTimeOfDay, r.time_of_day);
    if (u->iSun >= 0)         glUniform1f(u->iSun, r.sun);
    if (u->iDayFraction >= 0) glUniform1f(u->iDayFraction, r.day_fraction);
    if (u->iKeyEnergy >= 0)   glUniform1f(u->iKeyEnergy, r.key_energy);
    if (u->iMouseEnergy >= 0) glUniform1f(u->iMouseEnergy, r.mouse_energy);
    if (u->iAudioLevel >= 0)  glUniform1f(u->iAudioLevel, r.audio_level);
    if (u->iAudioBass >= 0)   glUniform1f(u->iAudioBass, r.audio_bass);
    if (u->iAudioMid >= 0)    glUniform1f(u->iAudioMid, r.audio_mid);
    if (u->iAudioTreble >= 0) glUniform1f(u->iAudioTreble, r.audio_treble);
    if (u->iAudioBeat >= 0)   glUniform1f(u->iAudioBeat, r.audio_beat);
    if (u->iAudioActive >= 0) glUniform1f(u->iAudioActive, r.audio_active ? 1.0f : 0.0f);

    /* --- manifest user uniforms (Tier 2/3) ---
     * Resolve location against this pass program (cheap: count is tiny and only
     * non-zero for manifest-driven shaders) and push the live/const value. */
    for (int ui = 0; ui < shader->user_uniform_count; ui++) {
        const multipass_user_uniform_t *uu = &shader->user_uniforms[ui];
        GLint loc = glGetUniformLocation(pass->program, uu->name);
        if (loc < 0) continue;
        float v = uu->value;
        switch (uu->bind) {
            case UNIFORM_BIND_CPU:          v = r.cpu; break;
            case UNIFORM_BIND_RAM:          v = r.ram; break;
            case UNIFORM_BIND_NET_DOWN:     v = r.net_down; break;
            case UNIFORM_BIND_NET_UP:       v = r.net_up; break;
            case UNIFORM_BIND_BATTERY:      v = r.battery; break;
            case UNIFORM_BIND_TIME_OF_DAY:  v = r.time_of_day; break;
            case UNIFORM_BIND_SUN:          v = r.sun; break;
            case UNIFORM_BIND_AUDIO_LEVEL:  v = r.audio_level; break;
            case UNIFORM_BIND_AUDIO_BASS:   v = r.audio_bass; break;
            case UNIFORM_BIND_AUDIO_MID:    v = r.audio_mid; break;
            case UNIFORM_BIND_AUDIO_TREBLE: v = r.audio_treble; break;
            case UNIFORM_BIND_AUDIO_BEAT:   v = r.audio_beat; break;
            case UNIFORM_BIND_KEY_ENERGY:   v = r.key_energy; break;
            case UNIFORM_BIND_MOUSE_ENERGY: v = r.mouse_energy; break;
            case UNIFORM_BIND_SWAP:         v = r.swap; break;
            case UNIFORM_BIND_DISK_READ:    v = r.disk_read; break;
            case UNIFORM_BIND_DISK_WRITE:   v = r.disk_write; break;
            case UNIFORM_BIND_LOAD:         v = r.load_avg; break;
            case UNIFORM_BIND_CPU_TEMP:     v = r.cpu_temp; break;
            case UNIFORM_BIND_GPU:          v = r.gpu; break;
            case UNIFORM_BIND_GPU_TEMP:     v = r.gpu_temp; break;
            case UNIFORM_BIND_UPTIME:       v = r.uptime_hours; break;
            case UNIFORM_BIND_PROCS:        v = r.procs; break;
            case UNIFORM_BIND_CONST: default: break;
        }
        glUniform1f(loc, v);
    }
    #undef r
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

            case CHANNEL_SOURCE_AUDIO:
                tex = shader->audio_texture;
                source_name = "audio";
                break;

            case CHANNEL_SOURCE_FONT:
                tex = shader->font_texture ? shader->font_texture : shader->noise_texture;
                source_name = "font";
                break;

            case CHANNEL_SOURCE_TERM:
                /* The cell-record texture is an INTEGER sampler (usampler2D) in
                 * the shader; bind it here on the channel unit. The atlas +
                 * metadata ride dedicated uniforms bound below. */
                tex = shader->term_cell_texture ? shader->term_cell_texture
                                                : shader->noise_texture;
                source_name = "terminal";
                break;
        }

        /* Direct GL calls - textures change every pass, no caching benefit */
        glActiveTexture(GL_TEXTURE0 + c);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(u->iChannel[c], c);
    }

    /* Bind the live audio texture to the dedicated iAudio sampler on unit 4,
     * separate from the iChannel0..3 units so audioBand()/spectrum() work even
     * when all four channels are bound to buffers. */
    if (u->iAudio >= 0) {
        glActiveTexture(GL_TEXTURE0 + MULTIPASS_MAX_CHANNELS);
        glBindTexture(GL_TEXTURE_2D, shader->audio_texture);
        glUniform1i(u->iAudio, MULTIPASS_MAX_CHANNELS);
    }

#ifdef NEOWALL_HAVE_TERMINAL
    /* Terminal glyph-atlas sampler on unit 5, plus grid/atlas/cursor metadata.
     * The cell texture itself is bound to whichever iChannelN the shader set to
     * the "terminal" source above. */
    if (u->iTermAtlas >= 0 && shader->term_atlas_texture) {
        glActiveTexture(GL_TEXTURE0 + MULTIPASS_MAX_CHANNELS + 1);
        glBindTexture(GL_TEXTURE_2D, shader->term_atlas_texture);
        glUniform1i(u->iTermAtlas, MULTIPASS_MAX_CHANNELS + 1);
    }
    if (u->iTermCells >= 0 && shader->term_cell_texture) {
        glActiveTexture(GL_TEXTURE0 + MULTIPASS_MAX_CHANNELS + 2);
        glBindTexture(GL_TEXTURE_2D, shader->term_cell_texture);
        glUniform1i(u->iTermCells, MULTIPASS_MAX_CHANNELS + 2);
    }
    /* Per-cell change-time texture on unit 8 (R32UI) for the change-driven
     * fade. Fall back to the cell texture when absent so the sampler is valid;
     * the fade is gated by iTermFade.x anyway. */
    if (u->iTermChange >= 0) {
        GLuint chtex = shader->term_change_texture ? shader->term_change_texture
                                                   : shader->term_cell_texture;
        if (chtex) {
            glActiveTexture(GL_TEXTURE0 + MULTIPASS_MAX_CHANNELS + 4);
            glBindTexture(GL_TEXTURE_2D, chtex);
            glUniform1i(u->iTermChange, MULTIPASS_MAX_CHANNELS + 4);
        }
    }
    /* Color-emoji atlas on unit 7. When absent, bind the coverage atlas as a
     * harmless stand-in so the sampler is always valid; the shader never reads
     * it unless a cell carries the color flag (which requires the font). */
    if (u->iTermColorAtlas >= 0) {
        GLuint ctex = shader->term_color_atlas_texture ? shader->term_color_atlas_texture
                                                       : shader->term_atlas_texture;
        if (ctex) {
            glActiveTexture(GL_TEXTURE0 + MULTIPASS_MAX_CHANNELS + 3);
            glBindTexture(GL_TEXTURE_2D, ctex);
            glUniform1i(u->iTermColorAtlas, MULTIPASS_MAX_CHANNELS + 3);
        }
    }
    if (shader->term) {
        if (u->iTermInfo >= 0) {
            glUniform4f(u->iTermInfo,
                        (float)term_render_cols(shader->term),
                        (float)term_render_rows(shader->term),
                        (float)term_render_cell_w(shader->term),
                        (float)term_render_cell_h(shader->term));
        }
        if (u->iTermAtlasSize >= 0) {
            glUniform2f(u->iTermAtlasSize,
                        (float)term_render_atlas_w(shader->term),
                        (float)term_render_atlas_h(shader->term));
        }
        if (u->iTermCursor >= 0) {
            int cx = 0, cy = 0; bool vis = false;
            term_render_cursor(shader->term, &cx, &cy, &vis);

            /* Cursor as a physical object: when the cell changes, remember where
             * it was and stamp the move time, so the shader can slide the drawn
             * cursor from the old cell to the new one over a short window rather
             * than hard-jumping (which is all a real terminal can do). */
            if (!shader->term_cursor_seen) {
                shader->term_cursor_px = cx;
                shader->term_cursor_py = cy;
                shader->term_cursor_move_t = shader->frame_shader_time;
                shader->term_cursor_seen = true;
            } else if (cx != shader->term_cursor_px || cy != shader->term_cursor_py) {
                /* Anchor the slide at wherever the cursor visually IS right now
                 * (it may still be mid-slide), then retarget to the new cell. */
                shader->term_cursor_move_t = shader->frame_shader_time;
                /* prev stays the last settled cell; update AFTER upload below. */
            }
            glUniform3f(u->iTermCursor, (float)cx, (float)cy, vis ? 1.0f : 0.0f);
            if (u->iTermCursorPrev >= 0) {
                glUniform4f(u->iTermCursorPrev,
                            (float)shader->term_cursor_px,
                            (float)shader->term_cursor_py,
                            shader->term_cursor_move_t, 0.0f);
            }
            shader->term_cursor_px = cx;
            shader->term_cursor_py = cy;
        }
        if (u->iTermFX >= 0) {
            glUniform4f(u->iTermFX, shader->term_fx[0], shader->term_fx[1],
                        shader->term_fx[2], shader->term_fx[3]);
        }
        if (u->iTermFade >= 0) {
            glUniform2f(u->iTermFade, shader->term_fade,
                        (float)term_render_now_ms(shader->term));
        }
    }
#endif
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

    /* ---- Per-frame cache (consumed by every pass via set_uniforms) ----
     * Monotonic wall clock: measure real dt for iTimeDelta/iFrameRate and
     * feed the same timestamp to the adaptive-resolution system below. */
    double wall_time;
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    wall_time = (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wall_time = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    if (shader->last_frame_wall > 0.0) {
        float dt = (float)(wall_time - shader->last_frame_wall);
        /* Clamp: a paused/occluded wallpaper can sit idle for minutes; feeding
         * a 60s dt into a physics shader explodes it. Cap at 1/4s, floor at
         * 0.1ms to avoid div-by-zero in shaders that do 1.0/iTimeDelta. */
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0001f) dt = 0.0001f;
        shader->frame_dt = dt;
        /* EMA-smoothed FPS so iFrameRate doesn't jitter frame to frame */
        float inst = 1.0f / dt;
        shader->frame_fps = shader->frame_fps > 0.0f
            ? shader->frame_fps * 0.9f + inst * 0.1f
            : inst;
    } else {
        shader->frame_dt = 1.0f / 60.0f;
        shader->frame_fps = 60.0f;
    }
    shader->last_frame_wall = wall_time;

    /* One reactive snapshot per FRAME (mutex-guarded ~2.2KB copy), shared by
     * all passes and the audio texture upload below. */
    reactive_get(&shader->frame_reactive);

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

    /* Update adaptive resolution using the wall-clock time sampled at the
     * top of this frame (not shader time, which can be paused/scaled) */
    adaptive_update(&shader->adaptive, wall_time);
    
    /* Sync resolution scale from adaptive system */
    shader->resolution_scale = adaptive_get_scale(&shader->adaptive);

    /* Query the CURRENT framebuffer binding every frame
     * GTK's GtkGLArea can change its FBO on resize, so we must always query */
    GLint current_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &current_fbo);
    shader->default_framebuffer = current_fbo;

    log_debug_frame(shader->frame_count, "=== Frame %d ===", shader->frame_count);

    /* Refresh the live audio texture once per frame from this frame's
     * reactive snapshot. Skipped cheaply if no audio is live (the texture
     * just stays zero). */
    if (shader->audio_texture) {
        const reactive_snapshot_t *ra = &shader->frame_reactive;
        if (ra->audio_active) {
            float rows[REACTIVE_AUDIO_BINS * 2];
            memcpy(&rows[0], ra->audio_spectrum, sizeof(ra->audio_spectrum));
            memcpy(&rows[REACTIVE_AUDIO_BINS], ra->audio_waveform, sizeof(ra->audio_waveform));
            glBindTexture(GL_TEXTURE_2D, shader->audio_texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, REACTIVE_AUDIO_BINS, 2,
                            GL_RED, GL_FLOAT, rows);
        }
    }

#ifdef NEOWALL_HAVE_TERMINAL
    /* Refresh the terminal textures once per frame. term_render_update pulls a
     * frame-coherent snapshot and returns true only when the grid changed, so
     * the (potentially large) cell upload is skipped on idle frames. The atlas
     * is re-uploaded only when new glyphs were rasterized. */
    if (shader->term && shader->term_cell_texture) {
        bool cells_changed = term_render_update(shader->term);

        if (term_render_atlas_dirty(shader->term)) {
            int aw = term_render_atlas_w(shader->term);
            int ah = term_render_atlas_h(shader->term);
            const uint8_t *bits = term_render_atlas(shader->term);
            int y0 = 0, y1 = ah;
            term_render_atlas_dirty_rows(shader->term, &y0, &y1);
            if (y0 < 0) y0 = 0;
            if (y1 > ah) y1 = ah;
            if (y1 > y0) {
                glBindTexture(GL_TEXTURE_2D, shader->term_atlas_texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                /* Push only the rows that changed since the last upload. The
                 * atlas is a fixed-size R8 texture, so a sub-rect covering
                 * [y0,y1) avoids re-sending the whole 2048x2048 (4 MB) on every
                 * new glyph. */
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, aw, y1 - y0,
                                GL_RED, GL_UNSIGNED_BYTE,
                                bits + (size_t)y0 * aw);
            }
            term_render_clear_atlas_dirty(shader->term);
        }

        /* Color-emoji atlas: same dirty-row sub-rect upload, RGBA8. */
        if (shader->term_color_atlas_texture && term_render_color_atlas_dirty(shader->term)) {
            int aw = term_render_atlas_w(shader->term);
            int ah = term_render_atlas_h(shader->term);
            const uint8_t *bits = term_render_color_atlas(shader->term);
            int y0 = 0, y1 = ah;
            term_render_color_atlas_dirty_rows(shader->term, &y0, &y1);
            if (y0 < 0) y0 = 0;
            if (y1 > ah) y1 = ah;
            if (bits && y1 > y0) {
                glBindTexture(GL_TEXTURE_2D, shader->term_color_atlas_texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, aw, y1 - y0,
                                GL_RGBA, GL_UNSIGNED_BYTE,
                                bits + (size_t)y0 * aw * 4);
            }
            term_render_clear_color_atlas_dirty(shader->term);
        }

        if (cells_changed) {
            int cols = term_render_cols(shader->term);
            int rows = term_render_rows(shader->term);
            const uint32_t *cells = term_render_cells(shader->term);
            /* Push only the band of rows that changed since the last frame
             * (computed by term_render_update's row diff). For a terminal where
             * a few lines move per frame this is a fraction of the full
             * cols*rows*16-byte grid. UNPACK_ROW_LENGTH keeps the source
             * stride at the full row width while we upload a sub-rect. */
            int y0 = 0, y1 = rows;
            term_render_cells_dirty_rows(shader->term, &y0, &y1);
            if (y0 < 0) y0 = 0;
            if (y1 > rows) y1 = rows;
            if (y1 > y0) {
                glBindTexture(GL_TEXTURE_2D, shader->term_cell_texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, cols);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, cols, y1 - y0,
                                GL_RGBA_INTEGER, GL_UNSIGNED_INT,
                                cells + (size_t)y0 * cols * 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

                /* Push the matching band of per-cell change timestamps (R32UI,
                 * one uint per cell) so the shader's change-driven fade sees the
                 * fresh stamps. Same [y0,y1) band as the cell upload. */
                const uint32_t *chg = term_render_change_ms(shader->term);
                if (chg && shader->term_change_texture) {
                    glBindTexture(GL_TEXTURE_2D, shader->term_change_texture);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, cols);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, cols, y1 - y0,
                                    GL_RED_INTEGER, GL_UNSIGNED_INT,
                                    chg + (size_t)y0 * cols);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
            }
        }
    }
#endif

    /* Set optimal render state ONCE at start of frame
     * These are the ONLY things worth caching - they're set once and never change */
    opt_disable(&shader->optimizer, GL_DEPTH_TEST);
    opt_disable(&shader->optimizer, GL_BLEND);
    opt_disable(&shader->optimizer, GL_CULL_FACE);
    opt_disable(&shader->optimizer, GL_SCISSOR_TEST);
    opt_depth_mask(&shader->optimizer, GL_FALSE);
    opt_color_mask(&shader->optimizer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /*
     * Setup vertex state ONCE for all passes: the quad layout was baked into
     * the VAO at init, so a single bind restores everything.
     */
    glBindVertexArray(shader->vao);

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

    /* Cleanup vertex state (attribute state lives in the VAO; just unbind) */
    glBindVertexArray(0);

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

/* Report the changed screen region from the last render, in framebuffer pixels
 * with a BOTTOM-LEFT origin (EGL/GL damage convention). Returns true and fills
 * the x/y/w/h out-params with a partial rect when the last frame changed only a
 * sub-region we can prove is local; returns false when the caller should damage
 * the whole surface.
 *
 * Only the crisp built-in terminal pass-through (a single Image pass sampling
 * the cell grid 1:1, no user post-shader) qualifies: there a dirty band of cell
 * rows maps to a contiguous pixel band, so we damage just those scanlines (plus
 * a one-cell margin for glyph overhang / linear-atlas bleed). Any buffered or
 * user shader can spread a changed texel arbitrarily, so those full-damage.
 * `crisp` tells us the host sees no custom term shader bound. */
bool multipass_last_damage(const multipass_shader_t *shader, bool crisp,
                           int surf_w, int surf_h,
                           int *x, int *y, int *w, int *h) {
    if (!shader || !shader->term || !crisp) return false;
    if (shader->has_buffers || shader->pass_count != 1) return false;
    if (surf_w <= 0 || surf_h <= 0) return false;

    int rows = term_render_rows(shader->term);
    if (rows <= 0) return false;

    int y0 = 0, y1 = rows;
    term_render_cells_dirty_rows(shader->term, &y0, &y1);
    if (y0 < 0) y0 = 0;
    if (y1 > rows) y1 = rows;
    if (y1 <= y0) return false;                 /* nothing changed */
    if (y0 == 0 && y1 == rows) return false;    /* whole grid — just full-damage */

    /* Cell row -> surface pixel band. The grid covers the full surface height,
     * so each cell row is surf_h/rows px tall; add a one-row margin each side. */
    double px_per_row = (double)surf_h / (double)rows;
    int top_px    = (int)((double)(y0 - 1) * px_per_row);           /* top-down */
    int bot_px    = (int)((double)(y1 + 1) * px_per_row + 0.5);
    if (top_px < 0) top_px = 0;
    if (bot_px > surf_h) bot_px = surf_h;
    int band_h = bot_px - top_px;
    if (band_h <= 0) return false;

    /* Flip to bottom-left origin for EGL damage: a top-down band [top,bot) has
     * its bottom edge at surf_h-bot from the framebuffer bottom. */
    if (x) *x = 0;
    if (y) *y = surf_h - bot_px;
    if (w) *w = surf_w;
    if (h) *h = band_h;
    return true;
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
