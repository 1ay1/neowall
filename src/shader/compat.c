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

/* Words that are free identifiers in GLSL ES (and so in Shadertoy code) but
 * reserved in desktop GLSL 330. `float packed` or `noise1(x)` is then a
 * syntax error; such a word gets a trailing underscore everywhere. */
static const char *const desktop_reserved[] = {
    "packed", "precise", "noise1", "noise2", "noise3", "noise4", "double", "dvec2", "dvec3", "dvec4",
    "dmat2", "dmat3", "dmat4", "input", "output", "filter", "common", "partition", "active",
    "superp", "sizeof", "cast", "namespace", "using", "hvec2", "hvec3", "hvec4", "fvec2", "fvec3",
    "fvec4", "sampler3DRect", "half", "fixed", "unsigned", "goto", "inline", "noinline", "public",
    "static", "extern", "external", "interface", "long", "short", "template", "this", "typedef",
    "union", "volatile", "class", "asm", "enum",
};

static bool is_reserved(const char *s, size_t n) {
    for (size_t k = 0; k < NW_COUNTOF(desktop_reserved); k++)
        if (strlen(desktop_reserved[k]) == n && !strncmp(desktop_reserved[k], s, n)) return true;
    return false;
}

/* `float x;` / `vec3 a, b;` inside a function: WebGL zero-fills a local
 * declared without a value, desktop drivers leave garbage (Mesa: 0, NVIDIA:
 * whatever was there). Real shaders count on the zero (`vec3 acc; acc +=`),
 * so such a declaration gets an explicit `= T(0)`. */
static const char *const value_types[] = {
    "float", "int", "uint", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
    "uvec2", "uvec3", "uvec4", "mat2", "mat3", "mat4", "bool", "bvec2", "bvec3", "bvec4",
};

static const char *value_type(const char *s, size_t n) {
    for (size_t k = 0; k < NW_COUNTOF(value_types); k++)
        if (strlen(value_types[k]) == n && !strncmp(value_types[k], s, n)) return value_types[k];
    return NULL;
}

static const char *zero_of(const char *type) {
    if (!strcmp(type, "bool")) return "false";
    if (type[0] == 'b') return NULL; /* bvecN(false) is fine too, but rare: leave it */
    return "0";
}

/* Token-level ES -> desktop fixes, comments and strings copied verbatim:
 * reserved words renamed, uninitialised locals zeroed. */
static char *es_fix(const char *src) {
    nw_str out = {0};
    int depth = 0, paren = 0;
    int struct_depth = -1;  /* depth of a struct body we're in, else -1 */
    bool after_struct = false;
    bool stmt_start = true; /* the next token starts a statement */
    const char *p = src;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            nw_str_append_n(&out, p, n);
            p += n;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            size_t n = e ? (size_t)(e + 2 - p) : strlen(p);
            nw_str_append_n(&out, p, n);
            p += n;
            continue;
        }
        if (*p == '#') {
            /* a preprocessor line: copy (with continuations) as is */
            const char *e = p;
            while (*e && !(*e == '\n' && e[-1] != '\\')) e++;
            nw_str_append_n(&out, p, (size_t)(e - p));
            p = e;
            stmt_start = true;
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            while (is_ident(*p)) p++;
            size_t n = (size_t)(p - s);
            if (is_reserved(s, n)) {
                nw_str_append_n(&out, s, n);
                nw_str_append(&out, "_");
                stmt_start = false;
                continue;
            }
            const char *type = depth > 0 && paren == 0 && stmt_start && struct_depth < 0 ? value_type(s, n) : NULL;
            if (n == 6 && !strncmp(s, "struct", 6)) after_struct = true;
            nw_str_append_n(&out, s, n);
            stmt_start = false;
            if (!type || !zero_of(type)) continue;
            /* `T name[, name]* ;` with no '=', '[' or '(' : zero each name */
            const char *q = p;
            bool simple = true;
            int names = 0;
            for (;;) {
                while (*q == ' ' || *q == '\t') q++;
                if (!isalpha((unsigned char)*q) && *q != '_') { simple = false; break; }
                while (is_ident(*q)) q++;
                names++;
                while (*q == ' ' || *q == '\t') q++;
                if (*q == ',') { q++; continue; }
                if (*q != ';') simple = false;
                break;
            }
            if (!simple || !names) continue;
            /* rewrite the declarators, adding = T(0) */
            while (p < q) {
                while (*p == ' ' || *p == '\t') nw_str_append_n(&out, p++, 1);
                const char *ns = p;
                while (is_ident(*p)) p++;
                const char *nm = ns;
                size_t nn = (size_t)(p - ns);
                bool reserved = is_reserved(nm, nn);
                nw_str_append_n(&out, nm, nn);
                if (reserved) nw_str_append(&out, "_");
                if (!strcmp(type, "bool")) nw_str_append(&out, " = false");
                else nw_str_printf(&out, " = %s(0)", type);
                while (*p == ' ' || *p == '\t') nw_str_append_n(&out, p++, 1);
                if (*p == ',') nw_str_append_n(&out, p++, 1);
            }
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            /* numbers (1e5, 0x1F, 2.0f) can contain letters: skip whole */
            const char *s = p;
            while (is_ident(*p) || *p == '.') p++;
            nw_str_append_n(&out, s, (size_t)(p - s));
            stmt_start = false;
            continue;
        }
        if (*p == '{') {
            depth++, stmt_start = true;
            if (after_struct && struct_depth < 0) struct_depth = depth;
            after_struct = false;
        } else if (*p == '}') {
            if (depth == struct_depth) struct_depth = -1;
            depth = depth > 0 ? depth - 1 : 0, stmt_start = true;
        } else if (*p == ';') stmt_start = paren == 0, after_struct = false;
        else if (*p == '(') paren++, stmt_start = false;
        else if (*p == ')') paren = paren > 0 ? paren - 1 : 0, stmt_start = false;
        else if (!isspace((unsigned char)*p)) stmt_start = false;
        nw_str_append_n(&out, p, 1);
        p++;
    }
    if (out.oom) {
        nw_str_free(&out);
        return NULL;
    }
    if (!out.data) nw_str_append(&out, "");
    return nw_str_take(&out);
}

char *nw_compat_fix(const char *src) {
    char *fixed = shadertoy_fix(src);
    if (!fixed) return NULL;
    char *base = es_fix(fixed);
    free(fixed);
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
