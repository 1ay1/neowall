/* See compat.h. */
#include "compat.h"

#include <ctype.h>
#include <string.h>

#include "neowall/shader/shadertoy_compat.h"

static const char *const builtins[] = {
    "iTime", "iResolution", "iMouse", "iFrame", "iTimeDelta", "iFrameRate", "iDate",
    "iSampleRate", "iChannel0", "iChannel1", "iChannel2", "iChannel3", "iChannelResolution",
    "iChannelTime", "iGlobalTime",
};

static bool is_ident(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* `uniform <type> <builtin>...;` on its own line. */
static bool redeclares_builtin(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (len - i < 8 || strncmp(line + i, "uniform", 7) || !isspace((unsigned char)line[i + 7])) return false;
    i += 7;
    while (i < len && isspace((unsigned char)line[i])) i++;
    while (i < len && is_ident(line[i])) i++; /* type */
    while (i < len && isspace((unsigned char)line[i])) i++;
    size_t s = i;
    while (i < len && is_ident(line[i])) i++;
    for (size_t k = 0; k < NW_COUNTOF(builtins); k++)
        if (strlen(builtins[k]) == i - s && !strncmp(line + s, builtins[k], i - s)) return true;
    return false;
}

static bool is_precision(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    return len - i >= 10 && !strncmp(line + i, "precision", 9) && isspace((unsigned char)line[i + 9]);
}

/* Constant-expression check good enough for real shaders: an initializer is
 * constant if every identifier in it is a type constructor, a literal, or a
 * macro-looking ALL_CAPS name. Anything that looks like a function call
 * (cos, sqrt, transpose, user funcs) or a lowercase variable means the
 * desktop compiler may reject it as const. */
static bool initializer_needs_runtime(const char *init, size_t len) {
    static const char *const ctors[] = {"float", "int", "uint", "bool", "vec2", "vec3", "vec4",
                                        "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
                                        "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4",
                                        "mat2x2", "mat3x3", "mat4x4", "true", "false"};
    for (size_t i = 0; i < len;) {
        if (isdigit((unsigned char)init[i]) || (init[i] == '.' && i + 1 < len && isdigit((unsigned char)init[i + 1]))) {
            while (i < len && (is_ident(init[i]) || init[i] == '.')) i++; /* 1.0, 1e-3, 2u */
            continue;
        }
        if (!is_ident(init[i])) {
            i++;
            continue;
        }
        size_t s = i;
        while (i < len && is_ident(init[i])) i++;
        size_t n = i - s;
        bool ok = false;
        for (size_t k = 0; k < NW_COUNTOF(ctors) && !ok; k++)
            ok = strlen(ctors[k]) == n && !strncmp(init + s, ctors[k], n);
        if (!ok) {
            bool caps = true;
            for (size_t k = s; k < i && caps; k++)
                caps = isupper((unsigned char)init[k]) || isdigit((unsigned char)init[k]) || init[k] == '_';
            ok = caps; /* PI, TAU, MAX_STEPS: macros, expanded to literals */
        }
        if (!ok) return true;
    }
    return false;
}

/* A file-scope `const` declaration whose initializer isn't constant, on this
 * line: returns the offset of "const" to remove, else -1. */
static long const_to_demote(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (len - i < 6 || strncmp(line + i, "const", 5) || !isspace((unsigned char)line[i + 5])) return -1;
    size_t at = i;
    const char *eq = memchr(line + i, '=', len - i);
    const char *semi = memchr(line + i, ';', len - i);
    if (!eq || !semi || semi < eq) return -1;
    if (memchr(line + i, '(', (size_t)(eq - (line + i)))) return -1; /* const in a parameter list */
    return initializer_needs_runtime(eq + 1, (size_t)(semi - eq - 1)) ? (long)at : -1;
}

char *nw_compat_fix(const char *src) {
    char *base = shadertoy_compat_fix(src); /* #version, iChannelResolution */
    if (!base) return NULL;
    nw_str out = {0};
    int depth = 0;           /* brace depth: only file scope is rewritten */
    bool in_block = false;   /* inside a block comment */
    for (const char *p = base; *p;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);

        bool file_scope = depth == 0 && !in_block;
        long demote = file_scope ? const_to_demote(p, len) : -1;
        if (file_scope && (redeclares_builtin(p, len) || is_precision(p, len))) {
            nw_str_append(&out, "// (neowall: provided by the engine) ");
            nw_str_append_n(&out, p, len);
        } else if (demote >= 0) {
            nw_str_append_n(&out, p, (size_t)demote);
            nw_str_append_n(&out, p + demote + 5, len - (size_t)demote - 5);
        } else {
            nw_str_append_n(&out, p, len);
        }
        if (nl) nw_str_append(&out, "\n");

        /* brace depth, ignoring comments and strings */
        for (size_t i = 0; i < len; i++) {
            if (in_block) {
                if (p[i] == '*' && i + 1 < len && p[i + 1] == '/') in_block = false, i++;
                continue;
            }
            if (p[i] == '/' && i + 1 < len && p[i + 1] == '/') break;
            if (p[i] == '/' && i + 1 < len && p[i + 1] == '*') {
                in_block = true;
                i++;
                continue;
            }
            if (p[i] == '{') depth++;
            else if (p[i] == '}' && depth > 0) depth--;
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(base);
    if (out.oom) {
        nw_str_free(&out);
        return NULL;
    }
    if (!out.data) nw_str_append(&out, "");
    return nw_str_take(&out);
}
