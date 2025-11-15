#include "platform_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "shader.h"

/**
 * Shader Version Adaptation Layer
 *
 * Automatically adapts shaders between OpenGL ES 2.0 and ES 3.0 syntax.
 * This allows Shadertoy shaders (which often use ES 3.0 features) to work
 * seamlessly with neowall, while maintaining backward compatibility with ES 2.0.
 *
 * Key conversions:
 * - #version directives (100 <-> 300 es)
 * - texture2D() <-> texture()
 * - attribute/varying <-> in/out
 * - gl_FragColor <-> out vec4 fragColor
 */

/* Get appropriate GLSL version string based on detected OpenGL ES version */
const char *get_glsl_version_string(bool use_es3) {
    if (use_es3) {
        return "#version 300 es\n";
    }

    return "#version 100\n";
}

/* Check if shader already has a version directive */
static bool has_version_directive(const char *shader_code) {
    if (!shader_code) {
        return false;
    }

    const char *p = shader_code;

    /* Skip initial whitespace and comments */
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '/')) {
        if (*p == '/' && *(p + 1) == '/') {
            /* Skip single-line comment */
            while (*p && *p != '\n') p++;
        } else if (*p == '/' && *(p + 1) == '*') {
            /* Skip multi-line comment */
            p += 2;
            while (*p && !(*p == '*' && *(p + 1) == '/')) p++;
            if (*p) p += 2;
        } else {
            p++;
        }
    }

    /* Check if first non-comment line starts with #version */
    return strncmp(p, "#version", 8) == 0;
}

/* Extract version number from #version directive */
static int extract_version_number(const char *shader_code) {
    const char *version_line = strstr(shader_code, "#version");
    if (!version_line) {
        return 0;
    }

    int version = 0;
    if (sscanf(version_line, "#version %d", &version) == 1) {
        return version;
    }

    return 0;
}

/* Convert ES 3.0 shader to ES 2.0 compatible syntax */
static char *convert_es3_to_es2(const char *shader_code) {
    if (!shader_code) {
        return NULL;
    }

    size_t len = strlen(shader_code);
    size_t capacity = len * 2;  // Extra space for potential expansions
    char *result = malloc(capacity);
    if (!result) {
        return NULL;
    }

    size_t out_pos = 0;
    const char *p = shader_code;

    /* Skip existing #version directive if present */
    if (strncmp(p, "#version", 8) == 0) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* Add ES 2.0 version directive */
    const char *version = "#version 100\n";
    size_t version_len = strlen(version);
    memcpy(result + out_pos, version, version_len);
    out_pos += version_len;

    /* Convert texture() calls to texture2D() */
    /* Convert 'in' to 'varying' (fragment shader) or 'attribute' (vertex shader) */
    /* Convert 'out' to 'varying' */

    while (*p) {
        /* Check for texture() function call */
        if (strncmp(p, "texture(", 8) == 0) {
            const char *replacement = "texture2D(";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 8;
            continue;
        }

        /* Check for 'in ' keyword (with space to avoid 'int', 'sin', etc.) */
        if (strncmp(p, "in ", 3) == 0) {
            const char *replacement = "varying ";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 3;
            continue;
        }

        /* Check for 'out ' keyword */
        if (strncmp(p, "out ", 4) == 0) {
            const char *replacement = "varying ";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 4;
            continue;
        }

        /* Copy character as-is */
        if (out_pos >= capacity - 1) {
            capacity *= 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                return NULL;
            }
            result = new_result;
        }
        result[out_pos++] = *p++;
    }

    result[out_pos] = '\0';
    return result;
}

/* Convert ES 2.0 shader to ES 3.0 syntax */
static char *convert_es2_to_es3(const char *shader_code, bool is_fragment_shader) {
    if (!shader_code) {
        return NULL;
    }

    size_t len = strlen(shader_code);
    size_t capacity = len * 2;
    char *result = malloc(capacity);
    if (!result) {
        return NULL;
    }

    size_t out_pos = 0;
    const char *p = shader_code;

    /* Skip existing #version directive if present */
    if (strncmp(p, "#version", 8) == 0) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* Add ES 3.0 version directive */
    const char *version = "#version 300 es\n";
    size_t version_len = strlen(version);
    memcpy(result + out_pos, version, version_len);
    out_pos += version_len;

    /* Add output declaration for fragment shaders */
    if (is_fragment_shader) {
        const char *out_decl = "out vec4 fragColor;\n";
        size_t out_len = strlen(out_decl);
        memcpy(result + out_pos, out_decl, out_len);
        out_pos += out_len;
    }

    /* Convert texture2D() to texture() */
    /* Convert varying to in (fragment) or out (vertex) */
    /* Convert attribute to in (vertex only) */
    /* Convert gl_FragColor to fragColor (fragment only) */

    while (*p) {
        /* Check for texture2D() */
        if (strncmp(p, "texture2D(", 10) == 0) {
            const char *replacement = "texture(";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 10;
            continue;
        }

        /* Check for varying */
        if (strncmp(p, "varying ", 8) == 0) {
            const char *replacement = is_fragment_shader ? "in " : "out ";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 8;
            continue;
        }

        /* Check for attribute (vertex shader only) */
        if (!is_fragment_shader && strncmp(p, "attribute ", 10) == 0) {
            const char *replacement = "in ";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 10;
            continue;
        }

        /* Check for gl_FragColor (fragment shader only) */
        if (is_fragment_shader && strncmp(p, "gl_FragColor", 12) == 0) {
            const char *replacement = "fragColor";
            size_t rep_len = strlen(replacement);
            if (out_pos + rep_len >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + out_pos, replacement, rep_len);
            out_pos += rep_len;
            p += 12;
            continue;
        }

        /* Copy character as-is */
        if (out_pos >= capacity - 1) {
            capacity *= 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                return NULL;
            }
            result = new_result;
        }
        result[out_pos++] = *p++;
    }

    result[out_pos] = '\0';
    return result;
}

/**
 * Adapt shader code to match the target OpenGL ES version
 *
 * @param use_es3 Whether to target ES 3.0 (true) or ES 2.0 (false)
 * @param shader_code Original shader source code
 * @param is_fragment_shader True for fragment shaders, false for vertex shaders
 * @return Adapted shader code (caller must free), or NULL on error
 */
char *adapt_shader_for_version(bool use_es3,
                                const char *shader_code,
                                bool is_fragment_shader) {
    if (!shader_code) {
        return NULL;
    }

    /* Detect shader version */
    int shader_version = extract_version_number(shader_code);
    bool shader_is_es3 = (shader_version >= 300);
    bool shader_has_version = has_version_directive(shader_code);

    /* If shader doesn't specify version, assume it matches the target */
    if (!shader_has_version) {
        shader_is_es3 = use_es3;
    }

    /* Log adaptation decision */
    log_debug("Shader adaptation: shader_version=%d, target_es3=%d, is_fragment=%d",
              shader_version, use_es3, is_fragment_shader);

    /* Check if adaptation is needed */
    bool need_es3_to_es2 = shader_is_es3 && !use_es3;
    bool need_es2_to_es3 = !shader_is_es3 && use_es3;

    if (need_es3_to_es2) {
        log_info("Converting ES 3.0 shader to ES 2.0 for compatibility");
        return convert_es3_to_es2(shader_code);
    }

    if (need_es2_to_es3) {
        log_debug("Converting ES 2.0 shader to ES 3.0 (optional optimization)");
        return convert_es2_to_es3(shader_code, is_fragment_shader);
    }

    /* No adaptation needed - add version directive if missing */
    if (!shader_has_version) {
        const char *version = get_glsl_version_string(use_es3);
        size_t version_len = strlen(version);
        size_t code_len = strlen(shader_code);
        char *result = malloc(version_len + code_len + 1);
        if (!result) {
            return NULL;
        }
        memcpy(result, version, version_len);
        memcpy(result + version_len, shader_code, code_len);
        result[version_len + code_len] = '\0';
        return result;
    }

    /* Shader is already compatible - return copy */
    return strdup(shader_code);
}

/**
 * Adapt vertex shader for current GL version
 */
char *adapt_vertex_shader(bool use_es3, const char *shader_code) {
    return adapt_shader_for_version(use_es3, shader_code, false);
}

/**
 * Adapt fragment shader for current GL version
 */
char *adapt_fragment_shader(bool use_es3, const char *shader_code) {
    return adapt_shader_for_version(use_es3, shader_code, true);
}
