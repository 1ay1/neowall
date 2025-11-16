/* Shader Library - Utility Functions Implementation
 * Helper functions for common shader operations in the editor
 */

#include "shader_utils.h"
#include "shader_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>

/* ============================================
 * Internal Helper Functions
 * ============================================ */

static char *strdup_safe(const char *str) {
    if (!str) return NULL;
    return strdup(str);
}

static size_t count_lines(const char *str) {
    if (!str) return 0;
    size_t count = 1;
    for (const char *p = str; *p; p++) {
        if (*p == '\n') count++;
    }
    return count;
}

static char *extract_line(const char *source, int line_num) {
    if (!source || line_num < 1) return NULL;

    const char *line_start = source;
    int current_line = 1;

    // Find the start of the target line
    while (current_line < line_num && *line_start) {
        if (*line_start == '\n') {
            current_line++;
        }
        line_start++;
    }

    if (current_line != line_num) return NULL;

    // Find the end of the line
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') {
        line_end++;
    }

    size_t len = line_end - line_start;
    char *line = malloc(len + 1);
    if (!line) return NULL;

    memcpy(line, line_start, len);
    line[len] = '\0';
    return line;
}

/* ============================================
 * Shader Analysis Implementation
 * ============================================ */

shader_error_info_t *shader_parse_error_log(const char *shader_log,
                                             const char *shader_source) {
    if (!shader_log) return NULL;

    shader_error_info_t *info = calloc(1, sizeof(shader_error_info_t));
    if (!info) return NULL;

    info->line_number = -1;
    info->message = strdup_safe(shader_log);

    // Try to parse line number from error message
    // Common formats: "0:42:", "ERROR: 0:42:", "0(42):"
    const char *p = shader_log;
    while (*p) {
        if (isdigit(*p)) {
            // Look for : or ) after the number
            while (isdigit(*p)) p++;
            if (*p == ':' || *p == ')') {
                // Skip the : or )
                if (*p == ':') p++;
                // Check if next part is also a number (line number)
                while (*p == ' ' || *p == '\t') p++;
                if (isdigit(*p)) {
                    info->line_number = atoi(p);
                    break;
                }
            }
        }
        p++;
    }

    // Extract code snippet if we have a line number and source
    if (info->line_number > 0 && shader_source) {
        char *line = extract_line(shader_source, info->line_number);
        if (line) {
            size_t snippet_size = strlen(line) + 64;
            info->code_snippet = malloc(snippet_size);
            if (info->code_snippet) {
                snprintf(info->code_snippet, snippet_size,
                         "Line %d: %s", info->line_number, line);
            }
            free(line);
        }
    }

    return info;
}

void shader_free_error_info(shader_error_info_t *error_info) {
    if (!error_info) return;
    free(error_info->message);
    free(error_info->code_snippet);
    free(error_info);
}

shader_stats_t *shader_get_statistics(const char *shader_source) {
    if (!shader_source) return NULL;

    shader_stats_t *stats = calloc(1, sizeof(shader_stats_t));
    if (!stats) return NULL;

    stats->line_count = count_lines(shader_source);

    // Count uniforms
    const char *p = shader_source;
    while ((p = strstr(p, "uniform")) != NULL) {
        // Make sure it's not in a comment
        const char *line_start = p;
        while (line_start > shader_source && *(line_start-1) != '\n') {
            line_start--;
        }
        const char *check = line_start;
        bool in_comment = false;
        while (check < p) {
            if (*check == '/' && *(check+1) == '/') {
                in_comment = true;
                break;
            }
            check++;
        }
        if (!in_comment) {
            stats->uniform_count++;
        }
        p += 7; // strlen("uniform")
    }

    // Count texture samples
    p = shader_source;
    while ((p = strstr(p, "texture")) != NULL) {
        stats->texture_count++;
        p += 7;
    }

    // Check for loops
    stats->uses_loops = (strstr(shader_source, "for") != NULL ||
                         strstr(shader_source, "while") != NULL);

    // Check for conditionals
    stats->uses_conditionals = (strstr(shader_source, "if") != NULL);

    // Check for Shadertoy format
    stats->is_shadertoy_format = (strstr(shader_source, "mainImage") != NULL);

    // Count functions (look for pattern: type name()
    p = shader_source;
    while (*p) {
        // Look for potential function definitions
        if (strstr(p, "void") == p || strstr(p, "float") == p ||
            strstr(p, "vec") == p || strstr(p, "int") == p ||
            strstr(p, "bool") == p) {
            // Skip the type
            while (*p && !isspace(*p)) p++;
            while (*p && isspace(*p)) p++;
            // Check for identifier followed by (
            if (isalpha(*p) || *p == '_') {
                while (*p && (isalnum(*p) || *p == '_')) p++;
                while (*p && isspace(*p)) p++;
                if (*p == '(') {
                    stats->function_count++;
                }
            }
        }
        p++;
    }

    // Estimate complexity (0-100)
    stats->complexity_score = 0;
    stats->complexity_score += stats->line_count / 10;
    stats->complexity_score += stats->uniform_count * 2;
    stats->complexity_score += stats->texture_count * 5;
    stats->complexity_score += stats->function_count * 3;
    if (stats->uses_loops) stats->complexity_score += 20;
    if (stats->uses_conditionals) stats->complexity_score += 10;

    // Cap at 100
    if (stats->complexity_score > 100) stats->complexity_score = 100;

    return stats;
}

void shader_free_stats(shader_stats_t *stats) {
    free(stats);
}

shader_validation_t *shader_validate_syntax(const char *shader_source,
                                             bool is_fragment) {
    if (!shader_source) return NULL;

    shader_validation_t *val = calloc(1, sizeof(shader_validation_t));
    if (!val) return NULL;

    val->is_valid = true;
    val->has_main = (strstr(shader_source, "main") != NULL ||
                     strstr(shader_source, "mainImage") != NULL);
    val->has_version = (strstr(shader_source, "#version") != NULL);

    // Detect version
    const char *version_line = strstr(shader_source, "#version");
    if (version_line) {
        sscanf(version_line, "#version %d", &val->detected_version);
    }

    // Allocate space for warnings/errors
    val->warnings = malloc(sizeof(char*) * 10);
    val->errors = malloc(sizeof(char*) * 10);

    // Basic validation checks
    if (!val->has_main) {
        val->errors[val->error_count++] = strdup("Missing main() or mainImage() function");
        val->is_valid = false;
    }

    if (!val->has_version) {
        val->warnings[val->warning_count++] = strdup("Missing #version directive");
    }

    // Check for common mistakes
    if (strstr(shader_source, "gl_FragColor") && val->detected_version >= 300) {
        val->warnings[val->warning_count++] =
            strdup("gl_FragColor is deprecated in GLSL ES 3.0+, use 'out vec4 fragColor'");
    }

    if (strstr(shader_source, "texture2D") && val->detected_version >= 300) {
        val->warnings[val->warning_count++] =
            strdup("texture2D() is deprecated in GLSL ES 3.0+, use texture()");
    }

    if (is_fragment && !strstr(shader_source, "precision")) {
        val->warnings[val->warning_count++] =
            strdup("Missing precision qualifier (add 'precision mediump float;')");
    }

    return val;
}

void shader_free_validation(shader_validation_t *validation) {
    if (!validation) return;

    for (size_t i = 0; i < validation->warning_count; i++) {
        free(validation->warnings[i]);
    }
    free(validation->warnings);

    for (size_t i = 0; i < validation->error_count; i++) {
        free(validation->errors[i]);
    }
    free(validation->errors);

    free(validation);
}

/* ============================================
 * Shader Formatting Implementation
 * ============================================ */

char *shader_format_source(const char *shader_source) {
    if (!shader_source) return NULL;

    size_t len = strlen(shader_source);
    char *formatted = malloc(len * 2); // Extra space for indentation
    if (!formatted) return NULL;

    const char *p = shader_source;
    char *out = formatted;
    int indent_level = 0;
    bool line_start = true;

    while (*p) {
        if (line_start && *p != '\n') {
            // Add indentation
            for (int i = 0; i < indent_level; i++) {
                *out++ = ' ';
                *out++ = ' ';
                *out++ = ' ';
                *out++ = ' ';
            }
            line_start = false;
        }

        // Track braces for indentation
        if (*p == '{') {
            indent_level++;
        } else if (*p == '}') {
            indent_level--;
            if (indent_level < 0) indent_level = 0;
        }

        *out++ = *p;

        if (*p == '\n') {
            line_start = true;
        }

        p++;
    }

    *out = '\0';
    return formatted;
}

char *shader_add_line_numbers(const char *shader_source, int start_line) {
    if (!shader_source) return NULL;

    size_t line_count = count_lines(shader_source);
    size_t max_line_num_width = snprintf(NULL, 0, "%d", (int)(start_line + line_count));

    // Estimate size: original + line numbers + spaces
    size_t estimated_size = strlen(shader_source) + line_count * (max_line_num_width + 3);
    char *result = malloc(estimated_size + 1);
    if (!result) return NULL;

    char *out = result;
    const char *p = shader_source;
    int current_line = start_line;

    // Add first line number
    out += sprintf(out, "%*d: ", (int)max_line_num_width, current_line++);

    while (*p) {
        *out++ = *p;
        if (*p == '\n' && *(p+1) != '\0') {
            out += sprintf(out, "%*d: ", (int)max_line_num_width, current_line++);
        }
        p++;
    }

    *out = '\0';
    return result;
}

char *shader_strip_comments(const char *shader_source, bool keep_newlines) {
    if (!shader_source) return NULL;

    size_t len = strlen(shader_source);
    char *result = malloc(len + 1);
    if (!result) return NULL;

    const char *p = shader_source;
    char *out = result;

    while (*p) {
        // Single-line comment
        if (*p == '/' && *(p+1) == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
            if (*p == '\n') {
                if (keep_newlines) *out++ = '\n';
                p++;
            }
            continue;
        }

        // Multi-line comment
        if (*p == '/' && *(p+1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p+1) == '/')) {
                if (keep_newlines && *p == '\n') *out++ = '\n';
                p++;
            }
            if (*p == '*' && *(p+1) == '/') p += 2;
            continue;
        }

        *out++ = *p++;
    }

    *out = '\0';
    return result;
}

/* ============================================
 * Shader Templates Implementation
 * ============================================ */

static const char *template_basic =
    "// Basic gradient shader\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "    fragColor = vec4(uv, 0.5, 1.0);\n"
    "}\n";

static const char *template_animated =
    "// Animated color cycle\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0, 2, 4));\n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *template_plasma =
    "// Plasma effect\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
    "    \n"
    "    float d1 = length(uv - vec2(sin(iTime * 0.3), cos(iTime * 0.5)));\n"
    "    float d2 = length(uv - vec2(cos(iTime * 0.4), sin(iTime * 0.6)));\n"
    "    \n"
    "    float plasma = sin(d1 * 10.0 + iTime) + cos(d2 * 8.0 - iTime);\n"
    "    vec3 col = 0.5 + 0.5 * cos(plasma + vec3(0, 2, 4));\n"
    "    \n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *template_noise =
    "// Procedural noise pattern\n"
    "float hash(vec2 p) {\n"
    "    p = fract(p * vec2(123.34, 456.21));\n"
    "    p += dot(p, p + 45.32);\n"
    "    return fract(p.x * p.y);\n"
    "}\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "    vec2 p = uv * 10.0 + iTime * 0.5;\n"
    "    \n"
    "    float n = hash(floor(p));\n"
    "    vec3 col = vec3(n);\n"
    "    \n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *template_raymarch =
    "// Basic raymarching template\n"
    "float sdSphere(vec3 p, float r) {\n"
    "    return length(p) - r;\n"
    "}\n"
    "\n"
    "float map(vec3 p) {\n"
    "    return sdSphere(p, 1.0);\n"
    "}\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
    "    \n"
    "    vec3 ro = vec3(0, 0, -3);\n"
    "    vec3 rd = normalize(vec3(uv, 1));\n"
    "    \n"
    "    float t = 0.0;\n"
    "    for (int i = 0; i < 64; i++) {\n"
    "        vec3 p = ro + rd * t;\n"
    "        float d = map(p);\n"
    "        if (d < 0.001) break;\n"
    "        t += d;\n"
    "        if (t > 20.0) break;\n"
    "    }\n"
    "    \n"
    "    vec3 col = vec3(1.0 - t / 20.0);\n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *template_shadertoy =
    "// Shadertoy template\n"
    "// Available uniforms:\n"
    "//   iTime          - shader playback time (seconds)\n"
    "//   iResolution    - viewport resolution (pixels)\n"
    "//   iChannel0-3    - texture channels\n"
    "//   iMouse         - mouse pixel coords\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    // Normalized pixel coordinates (from 0 to 1)\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "    \n"
    "    // Time varying pixel color\n"
    "    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0, 2, 4));\n"
    "    \n"
    "    // Output to screen\n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

const char *shader_get_template(const char *template_name) {
    if (!template_name) return template_basic;

    if (strcmp(template_name, "basic") == 0) return template_basic;
    if (strcmp(template_name, "animated") == 0) return template_animated;
    if (strcmp(template_name, "plasma") == 0) return template_plasma;
    if (strcmp(template_name, "noise") == 0) return template_noise;
    if (strcmp(template_name, "raymarch") == 0) return template_raymarch;
    if (strcmp(template_name, "shadertoy") == 0) return template_shadertoy;

    return template_basic;
}

const char **shader_list_templates(size_t *count) {
    static const char *templates[] = {
        "basic",
        "animated",
        "plasma",
        "noise",
        "raymarch",
        "shadertoy"
    };

    if (count) *count = 6;
    return templates;
}

/* ============================================
 * Shader Information Extraction
 * ============================================ */

size_t shader_extract_uniforms(const char *shader_source,
                                char ***uniform_names,
                                char ***uniform_types) {
    if (!shader_source || !uniform_names || !uniform_types) return 0;

    // Count uniforms first
    size_t count = 0;
    const char *p = shader_source;
    while ((p = strstr(p, "uniform")) != NULL) {
        count++;
        p += 7;
    }

    if (count == 0) return 0;

    *uniform_names = malloc(sizeof(char*) * count);
    *uniform_types = malloc(sizeof(char*) * count);

    size_t idx = 0;
    p = shader_source;

    while ((p = strstr(p, "uniform")) != NULL && idx < count) {
        p += 7;
        while (*p && isspace(*p)) p++;

        // Extract type
        const char *type_start = p;
        while (*p && (isalnum(*p) || *p == '_')) p++;
        size_t type_len = p - type_start;
        (*uniform_types)[idx] = malloc(type_len + 1);
        memcpy((*uniform_types)[idx], type_start, type_len);
        (*uniform_types)[idx][type_len] = '\0';

        while (*p && isspace(*p)) p++;

        // Extract name
        const char *name_start = p;
        while (*p && (isalnum(*p) || *p == '_')) p++;
        size_t name_len = p - name_start;
        (*uniform_names)[idx] = malloc(name_len + 1);
        memcpy((*uniform_names)[idx], name_start, name_len);
        (*uniform_names)[idx][name_len] = '\0';

        idx++;
    }

    return idx;
}

void shader_free_uniforms(char **uniform_names,
                          char **uniform_types,
                          size_t count) {
    if (uniform_names) {
        for (size_t i = 0; i < count; i++) {
            free(uniform_names[i]);
        }
        free(uniform_names);
    }

    if (uniform_types) {
        for (size_t i = 0; i < count; i++) {
            free(uniform_types[i]);
        }
        free(uniform_types);
    }
}

/* ============================================
 * Miscellaneous Utilities
 * ============================================ */

bool shader_is_likely_valid(const char *shader_source) {
    if (!shader_source) return false;
    if (strlen(shader_source) < 10) return false;

    // Check for at least some GLSL keywords
    bool has_keywords = (strstr(shader_source, "void") != NULL ||
                         strstr(shader_source, "float") != NULL ||
                         strstr(shader_source, "vec") != NULL);

    // Check for main function
    bool has_main = (strstr(shader_source, "main") != NULL);

    return has_keywords && has_main;
}

const char *shader_get_version_string(int es_version) {
    switch (es_version) {
        case 100: return "#version 100";
        case 300: return "#version 300 es";
        case 310: return "#version 310 es";
        case 320: return "#version 320 es";
        default: return "#version 100";
    }
}

int shader_detect_version(const char *shader_source) {
    if (!shader_source) return 0;

    const char *version_line = strstr(shader_source, "#version");
    if (!version_line) return 0;

    int version = 0;
    sscanf(version_line, "#version %d", &version);
    return version;
}

char *shader_generate_description(const char *shader_source) {
    if (!shader_source) return strdup("Empty shader");

    shader_stats_t *stats = shader_get_statistics(shader_source);
    if (!stats) return strdup("Invalid shader");

    char *desc = malloc(256);
    if (!desc) {
        shader_free_stats(stats);
        return NULL;
    }

    if (stats->is_shadertoy_format) {
        snprintf(desc, 256, "Shadertoy shader (%zu lines, complexity: %d%%)",
                 stats->line_count, stats->complexity_score);
    } else {
        snprintf(desc, 256, "GLSL shader (%zu lines, %zu uniforms, complexity: %d%%)",
                 stats->line_count, stats->uniform_count, stats->complexity_score);
    }

    shader_free_stats(stats);
    return desc;
}

int shader_estimate_performance(const char *shader_source) {
    if (!shader_source) return 100; // Worst case

    shader_stats_t *stats = shader_get_statistics(shader_source);
    if (!stats) return 100;

    int score = stats->complexity_score;

    shader_free_stats(stats);
    return score;
}

const char *shader_generate_fullscreen_vertex(bool use_es3) {
    if (use_es3) {
        return "#version 300 es\n"
               "in vec2 position;\n"
               "void main() {\n"
               "    gl_Position = vec4(position, 0.0, 1.0);\n"
               "}\n";
    } else {
        return "#version 100\n"
               "attribute vec2 position;\n"
               "void main() {\n"
               "    gl_Position = vec4(position, 0.0, 1.0);\n"
               "}\n";
    }
}

char *shader_generate_fragment_boilerplate(bool use_es3,
                                            bool include_time,
                                            bool include_resolution) {
    size_t size = 512;
    char *code = malloc(size);
    if (!code) return NULL;

    char *p = code;

    if (use_es3) {
        p += sprintf(p, "#version 300 es\n");
        p += sprintf(p, "precision mediump float;\n\n");
        p += sprintf(p, "out vec4 fragColor;\n\n");
    } else {
        p += sprintf(p, "#version 100\n");
        p += sprintf(p, "precision mediump float;\n\n");
    }

    if (include_time) {
        p += sprintf(p, "uniform float iTime;\n");
    }

    if (include_resolution) {
        p += sprintf(p, "uniform vec2 iResolution;\n");
    }

    p += sprintf(p, "\nvoid main() {\n");

    if (use_es3) {
        p += sprintf(p, "    fragColor = vec4(1.0, 0.0, 0.0, 1.0);\n");
    } else {
        p += sprintf(p, "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n");
    }

    p += sprintf(p, "}\n");

    return code;
}

char *shader_minify(const char *shader_source) {
    if (!shader_source) return NULL;

    // First strip comments
    char *no_comments = shader_strip_comments(shader_source, true);
    if (!no_comments) return NULL;

    size_t len = strlen(no_comments);
    char *minified = malloc(len + 1);
    if (!minified) {
        free(no_comments);
        return NULL;
    }

    const char *p = no_comments;
    char *out = minified;
    bool prev_space = false;

    while (*p) {
        if (isspace(*p)) {
            if (!prev_space && *p != '\n') {
                *out++ = ' ';
                prev_space = true;
            }
        } else {
            *out++ = *p;
            prev_space = false;
        }
        p++;
    }

    *out = '\0';
    free(no_comments);
    return minified;
}

size_t shader_estimate_size(const char *shader_source) {
    if (!shader_source) return 0;

    char *minified = shader_minify(shader_source);
    if (!minified) return strlen(shader_source);

    size_t size = strlen(minified);
    free(minified);
    return size;
}
