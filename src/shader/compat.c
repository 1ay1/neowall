/* See compat.h. */
#define _DEFAULT_SOURCE /* strndup */
#include "compat.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

/* Names of consts already demoted above (`const float PI = acos(-1.)`): one
 * of them in a later initializer makes that one runtime too. */
typedef NW_LIST(char *) name_list;

static bool in_list(const name_list *l, const char *s, size_t n) {
    for (size_t i = 0; i < l->len; i++)
        if (strlen(l->data[i]) == n && !strncmp(l->data[i], s, n)) return true;
    return false;
}

/* Constant-expression check good enough for real shaders: an initializer is
 * constant if every identifier in it is a type constructor, a literal, or a
 * macro-looking ALL_CAPS name that wasn't itself demoted. Anything that
 * looks like a function call (cos, sqrt, transpose, user funcs) or a
 * lowercase variable means the desktop compiler may reject it as const. */
static bool initializer_needs_runtime(const char *init, size_t len, const name_list *demoted) {
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
        if (in_list(demoted, init + s, n)) return true;
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
 * line: returns the offset of "const" to remove, else -1. A demoted name is
 * added to `demoted`. */
static long const_to_demote(const char *line, size_t len, name_list *demoted) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (len - i < 6 || strncmp(line + i, "const", 5) || !isspace((unsigned char)line[i + 5])) return -1;
    size_t at = i;
    const char *eq = memchr(line + i, '=', len - i);
    const char *semi = memchr(line + i, ';', len - i);
    if (!eq || !semi || semi < eq) return -1;
    if (memchr(line + i, '(', (size_t)(eq - (line + i)))) return -1; /* const in a parameter list */
    if (!initializer_needs_runtime(eq + 1, (size_t)(semi - eq - 1), demoted)) return -1;
    /* the declared name: the last identifier before '=' */
    const char *e = eq;
    while (e > line + i && !is_ident(e[-1])) e--;
    const char *s = e;
    while (s > line + i && is_ident(s[-1])) s--;
    char *name = e > s ? strndup(s, (size_t)(e - s)) : NULL;
    if (name && !nw_list_push(demoted, name)) free(name);
    return (long)at;
}

/* The token-level fixes: drop a stray `#version` (we emit our own, and two
 * are an error), and `iChannelResolution[n]` -> `iChannelResolution[n].xy`
 * where no component follows (it's a vec3 here). Comments and strings are
 * copied verbatim. */
static char *shadertoy_fix(const char *source) {
    if (!source) return NULL;

    size_t src_len = strlen(source);
    /* Worst case adds ".xy" (3 bytes) per token; 2x is comfortably safe. */
    size_t alloc_size = src_len * 2 + 16;
    char *result = malloc(alloc_size);
    if (!result) return NULL;

    const char *src = source;
    char *dst = result;
    bool at_line_start = true;  /* true when only whitespace seen on this line */

    while (*src) {
        /* Pass through line comments verbatim. */
        if (src[0] == '/' && src[1] == '/') {
            while (*src && *src != '\n') *dst++ = *src++;
            continue;
        }
        /* Pass through block comments verbatim. */
        if (src[0] == '/' && src[1] == '*') {
            *dst++ = *src++;
            *dst++ = *src++;
            while (*src && !(src[0] == '*' && src[1] == '/')) *dst++ = *src++;
            if (*src) { *dst++ = *src++; *dst++ = *src++; }
            at_line_start = false;
            continue;
        }
        /* Pass through string literals verbatim. */
        if (*src == '"') {
            *dst++ = *src++;
            while (*src && *src != '"') {
                if (*src == '\\' && src[1]) *dst++ = *src++;
                *dst++ = *src++;
            }
            if (*src) *dst++ = *src++;
            at_line_start = false;
            continue;
        }

        /* A user `#version ...` directive must not survive: the wrapper emits
         * its own, and two #version lines is a hard compile error. Drop it. */
        if (at_line_start && strncmp(src, "#version", 8) == 0) {
            while (*src && *src != '\n') src++;
            continue;  /* at_line_start stays true; newline copied next iter */
        }

        /* iChannelResolution[n] -> iChannelResolution[n].xy when no component
         * access already follows (vec3 uniform used as vec2). */
        if (strncmp(src, "iChannelResolution[", 19) == 0) {
            memcpy(dst, src, 19);
            dst += 19; src += 19;
            while (*src && *src != ']') *dst++ = *src++;   /* index */
            if (*src == ']') *dst++ = *src++;              /* closing bracket */
            if (*src != '.' && *src != '[') {
                memcpy(dst, ".xy", 3);
                dst += 3;
            }
            at_line_start = false;
            continue;
        }

        /* Track whether we are still at the start of a line (whitespace only). */
        if (*src == '\n') {
            at_line_start = true;
        } else if (*src != ' ' && *src != '\t' && *src != '\r') {
            at_line_start = false;
        }
        *dst++ = *src++;
    }

    *dst = '\0';
    return result;
}

char *nw_compat_fix(const char *src) {
    char *base = shadertoy_fix(src);
    if (!base) return NULL;
    nw_str out = {0};
    name_list demoted = {0};
    int depth = 0;           /* brace depth: only file scope is rewritten */
    bool in_block = false;   /* inside a block comment */
    for (const char *p = base; *p;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);

        bool file_scope = depth == 0 && !in_block;
        long demote = file_scope ? const_to_demote(p, len, &demoted) : -1;
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
    for (size_t i = 0; i < demoted.len; i++) free(demoted.data[i]);
    nw_list_free(&demoted);
    if (out.oom) {
        nw_str_free(&out);
        return NULL;
    }
    if (!out.data) nw_str_append(&out, "");
    return nw_str_take(&out);
}
