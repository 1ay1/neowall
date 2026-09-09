/* Multipass parser — extracts Shadertoy multi-pass shader sources into per-pass
 * fragments. Split out of shader_multipass.c so the parser can be fuzzed and
 * unit-tested in isolation from the GL compile/render path.
 *
 * Public entry points (declared in shader_multipass.h):
 *   multipass_count_main_functions, multipass_detect, multipass_extract_common,
 *   multipass_parse_shader, multipass_free_parse_result.
 *
 * The four string-scan helpers (mp_find_pattern, mp_find_function_end,
 * mp_extract_substring, mp_str_dup) are shared with shader_multipass.c via
 * multipass_internal.h. */

#include "neowall/shader/shader_multipass.h"
#include "neowall/shader/shader_log.h"
#include "multipass_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "version.h"

/* Longest comment line examined when looking for a "Buffer A"/"Image" pass
 * marker. Markers are short; anything past this is prose and is truncated
 * rather than scanned. */
#define MP_MARKER_LINE_MAX 256

/* ============================================
 * Shared string-scan helpers (see multipass_internal.h)
 * ============================================ */

const char *mp_find_pattern(const char *source, const char *pattern) {
    const char *p = source;
    size_t pat_len = strlen(pattern);

    while (*p) {
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
        if (strncmp(p, pattern, pat_len) == 0) return p;
        p++;
    }
    return NULL;
}

const char *mp_find_function_end(const char *start) {
    const char *p = start;
    int brace_depth = 0;
    bool in_function = false;

    while (*p) {
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
            if (in_function && brace_depth == 0) return p + 1;
        }
        p++;
    }
    return p;
}

char *mp_extract_substring(const char *start, const char *end) {
    if (!start || !end || end <= start) return NULL;
    size_t len = (size_t)(end - start);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

char *mp_str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *result = malloc(len + 1);
    if (result) memcpy(result, s, len + 1);
    return result;
}

/* ============================================
 * Public parser API
 * ============================================ */

int multipass_count_main_functions(const char *source) {
    if (!source) return 0;

    int count = 0;
    const char *p = source;

    while ((p = mp_find_pattern(p, "mainImage")) != NULL) {
        p += 9; /* past "mainImage" */
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '(') count++;
    }

    return count;
}

bool multipass_detect(const char *source) {
    if (!source) return false;

    /* All shaders go through the multipass system now. Single-pass shaders are
     * treated as Image-only multipass. */
    if (multipass_count_main_functions(source) >= 1) return true;
    if (mp_find_pattern(source, "void mainImage") ||
        mp_find_pattern(source, "void main(")) return true;
    return false;
}

char *multipass_extract_common(const char *source) {
    if (!source) return NULL;

    const char *first_main = mp_find_pattern(source, "void mainImage");
    if (!first_main) first_main = mp_find_pattern(source, "void main(");
    if (!first_main) return NULL;

    const char *func_start = first_main;
    while (func_start > source && *(func_start - 1) != '\n') func_start--;

    if (func_start > source) return mp_extract_substring(source, func_start);
    return NULL;
}

multipass_parse_result_t *multipass_parse_shader(const char *source) {
    multipass_parse_result_t *result = calloc(1, sizeof(multipass_parse_result_t));
    if (!result) return NULL;

    if (!source) {
        result->error_message = mp_str_dup("Source is NULL");
        return result;
    }

    int main_count = multipass_count_main_functions(source);

    if (main_count <= 1) {
        result->is_multipass = false;
        result->pass_count = 1;
        result->pass_sources[0] = mp_str_dup(source);
        result->pass_types[0] = PASS_TYPE_IMAGE;
        return result;
    }

    result->is_multipass = true;
    log_info("Detected multipass shader with %d mainImage functions", main_count);

    result->common_source = multipass_extract_common(source);

    /* Find all mainImage positions and their function boundaries. */
    const char *main_starts[MULTIPASS_MAX_PASSES];
    const char *main_ends[MULTIPASS_MAX_PASSES];
    const char *line_starts[MULTIPASS_MAX_PASSES];
    int found_count = 0;
    (void)main_starts;

    const char *p = source;
    while (found_count < MULTIPASS_MAX_PASSES) {
        const char *main_start = mp_find_pattern(p, "void mainImage");
        if (!main_start) break;

        const char *line_start = main_start;
        while (line_start > source && *(line_start - 1) != '\n') line_start--;

        main_starts[found_count] = main_start;
        line_starts[found_count] = line_start;
        main_ends[found_count] = mp_find_function_end(main_start);
        found_count++;
        p = main_ends[found_count - 1];
    }

    /* Extract each pass with proper helper-function inclusion. */
    for (int pass_index = 0; pass_index < found_count; pass_index++) {
        const char *line_start = line_starts[pass_index];
        const char *func_end = main_ends[pass_index];

        /* Look back up to 5 lines for a pass marker in a comment.
         *
         * The marker search is bounded to the comment line itself. strstr()
         * would otherwise scan from the comment all the way to EOF, so any
         * later mention of "Buffer A" anywhere in the file — in prose, in a
         * header block, in an unrelated pass — would misclassify this pass.
         * That made a stray word in a comment silently rewire the pipeline. */
        multipass_type_t detected_type = PASS_TYPE_NONE;
        const char *check = line_start;
        int lines_back = 0;
        while (check > source && lines_back < 5) {
            check--;
            while (check > source && *(check - 1) != '\n') check--;

            const char *line_content = check;
            while (*line_content && isspace((unsigned char)*line_content)) line_content++;

            if (line_content[0] == '/' && (line_content[1] == '/' || line_content[1] == '*')) {
                /* Copy just this line so every strstr below is line-local. */
                size_t line_len = strcspn(check, "\n");
                if (line_len > MP_MARKER_LINE_MAX - 1) line_len = MP_MARKER_LINE_MAX - 1;
                char line_buf[MP_MARKER_LINE_MAX];
                memcpy(line_buf, check, line_len);
                line_buf[line_len] = '\0';

                if (strstr(line_buf, "Buffer A") || strstr(line_buf, "BufferA")) { detected_type = PASS_TYPE_BUFFER_A; break; }
                if (strstr(line_buf, "Buffer B") || strstr(line_buf, "BufferB")) { detected_type = PASS_TYPE_BUFFER_B; break; }
                if (strstr(line_buf, "Buffer C") || strstr(line_buf, "BufferC")) { detected_type = PASS_TYPE_BUFFER_C; break; }
                if (strstr(line_buf, "Buffer D") || strstr(line_buf, "BufferD")) { detected_type = PASS_TYPE_BUFFER_D; break; }
                if (strstr(line_buf, "// Image") || strstr(line_buf, "/* Image")) { detected_type = PASS_TYPE_IMAGE; break; }
            }
            lines_back++;
        }

        /* Default by position: last pass is Image, others are Buffers A..D. */
        if (detected_type == PASS_TYPE_NONE) {
            if (pass_index == found_count - 1) {
                detected_type = PASS_TYPE_IMAGE;
            } else {
                detected_type = PASS_TYPE_BUFFER_A + pass_index;
                if (detected_type > PASS_TYPE_BUFFER_D) detected_type = PASS_TYPE_BUFFER_D;
            }
        }

        log_info("Pass %d assigned type: %s", pass_index, multipass_type_name(detected_type));

        if (pass_index > 0) {
            /* For passes after the first, include every helper-function block
             * that lives between previous mainImage end and this mainImage
             * start. mainImages themselves are excluded. */
            const char *helpers_end = line_start;
            size_t max_helpers_len = 0;
            const char *helpers_start_dbg = main_ends[0];
            (void)helpers_start_dbg;
            (void)helpers_end;
            if (line_start > main_ends[0]) max_helpers_len = (size_t)(line_start - main_ends[0]);

            char *helpers_only = NULL;
            size_t helpers_only_len = 0;

            if (max_helpers_len > 0) {
                helpers_only = malloc(max_helpers_len + 1);
                if (helpers_only) {
                    helpers_only[0] = '\0';
                    for (int prev = 0; prev < pass_index; prev++) {
                        const char *seg_start = main_ends[prev];
                        const char *seg_end = line_starts[prev + 1];
                        if (seg_end > seg_start) {
                            size_t seg_len = (size_t)(seg_end - seg_start);
                            memcpy(helpers_only + helpers_only_len, seg_start, seg_len);
                            helpers_only_len += seg_len;
                        }
                    }
                    helpers_only[helpers_only_len] = '\0';
                }
            }

            size_t main_len = (size_t)(func_end - line_start);
            size_t total_len = helpers_only_len + main_len + 16;

            char *combined = malloc(total_len);
            if (combined) {
                combined[0] = '\0';
                if (helpers_only && helpers_only_len > 0) strcat(combined, helpers_only);
                strncat(combined, line_start, main_len);
                result->pass_sources[pass_index] = combined;
            } else {
                result->pass_sources[pass_index] = mp_extract_substring(line_start, func_end);
            }

            free(helpers_only);
        } else {
            result->pass_sources[pass_index] = mp_extract_substring(line_start, func_end);
        }

        result->pass_types[pass_index] = detected_type;
        log_info("Extracted pass %d: %s", pass_index, multipass_type_name(detected_type));
    }

    result->pass_count = found_count;
    return result;
}

void multipass_free_parse_result(multipass_parse_result_t *result) {
    if (!result) return;
    for (int i = 0; i < MULTIPASS_MAX_PASSES; i++) free(result->pass_sources[i]);
    free(result->common_source);
    free(result->error_message);
    free(result);
}

bool shader_check_required_version(const char *source, const char *shader_path) {
    if (!source) return true;

    /* Look for: #pragma neowall requires <major>[.<minor>[.<patch>]]
     *
     * Scanned line by line so the directive is only honoured at the start of a
     * line, the way a real preprocessor directive must appear — a mention
     * inside a comment or a string is not a requirement. */
    const char *line = source;
    while (line && *line) {
        const char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        char buf[256];
        if (line_len < sizeof(buf)) {
            memcpy(buf, line, line_len);
            buf[line_len] = '\0';

            const char *p = buf;
            while (*p == ' ' || *p == '\t') p++;

            if (strncmp(p, "#pragma", 7) == 0) {
                p += 7;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, "neowall", 7) == 0) {
                    p += 7;
                    while (*p == ' ' || *p == '\t') p++;
                    if (strncmp(p, "requires", 8) == 0) {
                        p += 8;
                        while (*p == ' ' || *p == '\t') p++;

                        int major = 0, minor = 0, patch = 0;
                        int fields = sscanf(p, "%d.%d.%d", &major, &minor, &patch);
                        if (fields < 1) {
                            log_warn("Shader %s: malformed '#pragma neowall requires' (ignored)",
                                     shader_path ? shader_path : "<memory>");
                            return true;
                        }

                        int required = major * 10000 + minor * 100 + patch;
                        if (required > NEOWALL_VERSION_NUMBER) {
                            log_error("Shader %s requires neowall %d.%d.%d, but this is %s",
                                      shader_path ? shader_path : "<memory>",
                                      major, minor, patch, NEOWALL_VERSION_STRING);
                            log_error("Newer reactive uniforms would read as 0 instead of failing, "
                                      "so this shader is refused rather than rendered wrong.");
                            return false;
                        }
                        return true;
                    }
                }
            }
        }

        if (!eol) break;
        line = eol + 1;
    }

    return true;
}

