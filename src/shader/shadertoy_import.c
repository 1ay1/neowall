/* See neowall/shader/shadertoy_import.h.
 *
 * The JSON reader here is strict and small: objects, arrays, strings (with
 * \uXXXX, surrogate pairs), numbers, true/false/null, nesting capped. It
 * reads untrusted files, so every read is bounds-checked and it's fuzzed. */
#include "neowall/shader/shadertoy_import.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/base.h"

/* ── JSON ─────────────────────────────────────────────────────────────── */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval jval;
struct jval {
    jtype t;
    bool b;
    double n;
    char *s;           /* J_STR, NUL-terminated (embedded NULs dropped) */
    jval *items;       /* J_ARR values / J_OBJ values */
    char **keys;       /* J_OBJ keys */
    size_t len;
};

typedef struct {
    const char *p, *end;
    int depth;
    char *err;
    size_t err_cap;
} jparser;

#define JSON_MAX_DEPTH 64

static void jfree(jval *v) {
    if (!v) return;
    free(v->s);
    for (size_t i = 0; i < v->len; i++) {
        jfree(&v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
}

static bool jfail(jparser *j, const char *what) {
    if (j->err && j->err_cap && !j->err[0]) snprintf(j->err, j->err_cap, "bad JSON: %s", what);
    return false;
}

static void jws(jparser *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int hexval(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool read_u4(jparser *j, unsigned *out) {
    if (j->end - j->p < 4) return false;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexval(j->p[i]);
        if (h < 0) return false;
        v = v * 16 + (unsigned)h;
    }
    j->p += 4;
    *out = v;
    return true;
}

static void put_utf8(nw_str *s, unsigned cp) {
    char b[4];
    size_t n;
    if (cp < 0x80) b[0] = (char)cp, n = 1;
    else if (cp < 0x800) b[0] = (char)(0xC0 | cp >> 6), b[1] = (char)(0x80 | (cp & 0x3F)), n = 2;
    else if (cp < 0x10000)
        b[0] = (char)(0xE0 | cp >> 12), b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F)), n = 3;
    else
        b[0] = (char)(0xF0 | cp >> 18), b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)),
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[3] = (char)(0x80 | (cp & 0x3F)), n = 4;
    nw_str_append_n(s, b, n);
}

static bool jstring(jparser *j, char **out) {
    if (j->p >= j->end || *j->p != '"') return jfail(j, "expected a string");
    j->p++;
    nw_str s = {0};
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if ((unsigned char)c < 0x20) {
            nw_str_free(&s);
            return jfail(j, "control character in a string");
        }
        if (c != '\\') {
            nw_str_append_n(&s, &c, 1);
            continue;
        }
        if (j->p >= j->end) break;
        c = *j->p++;
        unsigned cp;
        switch (c) {
        case '"': case '\\': case '/': nw_str_append_n(&s, &c, 1); break;
        case 'b': nw_str_append(&s, "\b"); break;
        case 'f': nw_str_append(&s, "\f"); break;
        case 'n': nw_str_append(&s, "\n"); break;
        case 'r': nw_str_append(&s, "\r"); break;
        case 't': nw_str_append(&s, "\t"); break;
        case 'u':
            if (!read_u4(j, &cp)) {
                nw_str_free(&s);
                return jfail(j, "bad \\u escape");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                unsigned lo;
                if (j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    j->p += 2;
                    if (read_u4(j, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else cp = 0xFFFD;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            if (cp) put_utf8(&s, cp); /* an embedded NUL would cut the shader short: drop it */
            break;
        default: nw_str_free(&s); return jfail(j, "bad escape");
        }
    }
    if (j->p >= j->end) {
        nw_str_free(&s);
        return jfail(j, "unterminated string");
    }
    j->p++;
    if (s.oom) {
        nw_str_free(&s);
        return jfail(j, "out of memory");
    }
    if (!s.data) nw_str_append(&s, "");
    *out = nw_str_take(&s);
    return *out != NULL;
}

static bool jvalue(jparser *j, jval *v);

static bool jpush(jval *v, size_t *cap, char *key, jval item) {
    if (v->len == *cap) {
        size_t nc = *cap ? *cap * 2 : 4;
        jval *ni = realloc(v->items, nc * sizeof(*ni));
        if (!ni) return false;
        v->items = ni;
        if (v->t == J_OBJ) {
            char **nk = realloc(v->keys, nc * sizeof(*nk));
            if (!nk) return false;
            v->keys = nk;
        }
        *cap = nc;
    }
    v->items[v->len] = item;
    if (v->t == J_OBJ) v->keys[v->len] = key;
    v->len++;
    return true;
}

static bool jcontainer(jparser *j, jval *v, bool obj) {
    if (++j->depth > JSON_MAX_DEPTH) return jfail(j, "nested too deep");
    *v = (jval){.t = obj ? J_OBJ : J_ARR};
    size_t cap = 0;
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == (obj ? '}' : ']')) {
        j->p++;
        j->depth--;
        return true;
    }
    for (;;) {
        char *key = NULL;
        jval item = {0};
        if (obj) {
            jws(j);
            if (!jstring(j, &key)) goto bad;
            jws(j);
            if (j->p >= j->end || *j->p != ':') {
                free(key);
                jfail(j, "expected ':'");
                goto bad;
            }
            j->p++;
        }
        if (!jvalue(j, &item) || !jpush(v, &cap, key, item)) {
            free(key);
            jfree(&item);
            jfail(j, "bad value");
            goto bad;
        }
        jws(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == (obj ? '}' : ']')) {
            j->p++;
            j->depth--;
            return true;
        }
        jfail(j, obj ? "expected ',' or '}'" : "expected ',' or ']'");
        goto bad;
    }
bad:
    jfree(v);
    *v = (jval){0};
    return false;
}

static bool jvalue(jparser *j, jval *v) {
    jws(j);
    if (j->p >= j->end) return jfail(j, "unexpected end");
    char c = *j->p;
    if (c == '{') return jcontainer(j, v, true);
    if (c == '[') return jcontainer(j, v, false);
    if (c == '"') {
        *v = (jval){.t = J_STR};
        return jstring(j, &v->s);
    }
    static const struct { const char *w; jtype t; bool b; } lits[] = {
        {"true", J_BOOL, true}, {"false", J_BOOL, false}, {"null", J_NULL, false}};
    for (size_t i = 0; i < NW_COUNTOF(lits); i++) {
        size_t n = strlen(lits[i].w);
        if ((size_t)(j->end - j->p) >= n && !strncmp(j->p, lits[i].w, n)) {
            j->p += n;
            *v = (jval){.t = lits[i].t, .b = lits[i].b};
            return true;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char buf[64];
        size_t n = 0;
        while (j->p < j->end && n < sizeof(buf) - 1 &&
               (strchr("+-.eE", *j->p) || (*j->p >= '0' && *j->p <= '9')))
            buf[n++] = *j->p++;
        buf[n] = '\0';
        char *e;
        double d = strtod(buf, &e);
        if (e == buf || *e) return jfail(j, "bad number");
        *v = (jval){.t = J_NUM, .n = d};
        return true;
    }
    return jfail(j, "unexpected character");
}

static const jval *jget(const jval *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (size_t i = 0; i < o->len; i++)
        if (!strcmp(o->keys[i], key)) return &o->items[i];
    return NULL;
}

static const char *jstr(const jval *o, const char *key) {
    const jval *v = jget(o, key);
    return v && v->t == J_STR ? v->s : NULL;
}

/* ── import ───────────────────────────────────────────────────────────── */

/* Shadertoy's own outputs: inputs name them by a preview asset,
 * /media/previz/buffer00.png .. buffer03.png and cubemap00.png, in `src`
 * (API export) or `filepath` (site export). */
static const char *buffer_letter(const jval *in) {
    const char *src = jstr(in, "src");
    if (!src) src = jstr(in, "filepath");
    if (!src) return NULL;
    static const struct { const char *file, *name; } map[] = {
        {"buffer00.png", "bufferA"}, {"buffer01.png", "bufferB"}, {"buffer02.png", "bufferC"},
        {"buffer03.png", "bufferD"}, {"cubemap00.png", "cube"}};
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    for (size_t i = 0; i < NW_COUNTOF(map); i++)
        if (!strcmp(base, map[i].file)) return map[i].name;
    return NULL;
}

/* A neowall source for one iChannel input, or NULL to leave it unbound.
 * `note` gets a line when it's a stand-in. */
static const char *input_source(const jval *in, const char *pass, int ch, nw_str *notes) {
    const char *ctype = jstr(in, "ctype");
    if (!ctype) ctype = jstr(in, "type");
    if (!ctype) return NULL;
    const char *src = jstr(in, "src");
    if (!src) src = jstr(in, "filepath");
    if (!strcmp(ctype, "buffer") || !strcmp(ctype, "cubemap")) {
        const char *b = buffer_letter(in);
        if (b) return b;
    }
    if (!strcmp(ctype, "keyboard")) return "keyboard";
    if (!strcmp(ctype, "mic") || !strcmp(ctype, "music") || !strcmp(ctype, "musicstream")) {
        nw_str_printf(notes, "%s iChannel%d: %s input -> the live audio spectrum\n", pass, ch, ctype);
        return "audio";
    }
    if (!strcmp(ctype, "cubemap")) {
        nw_str_printf(notes, "%s iChannel%d: Shadertoy cube map %s -> the built-in sky\n", pass, ch, src ? src : "");
        return "sky";
    }
    if (!strcmp(ctype, "texture")) {
        /* Shadertoy's textures are theirs, so we can't fetch them */
        nw_str_printf(notes, "%s iChannel%d: Shadertoy texture %s -> noise (drop an image next to the shader and "
                             "bind it with ch%d yourfile.png)\n",
                      pass, ch, src ? src : "", ch);
        return "noise";
    }
    if (!strcmp(ctype, "volume")) {
        nw_str_printf(notes, "%s iChannel%d: 3D noise volume isn't supported -> unbound\n", pass, ch);
        return "none";
    }
    nw_str_printf(notes, "%s iChannel%d: %s isn't supported -> unbound\n", pass, ch, ctype);
    return "none";
}

static const char *manifest_pass_name(const char *name) {
    static const struct { const char *st, *nw; } map[] = {
        {"Image", "image"},      {"Buffer A", "bufferA"}, {"Buffer B", "bufferB"},
        {"Buffer C", "bufferC"}, {"Buffer D", "bufferD"}, {"Buf A", "bufferA"},
        {"Buf B", "bufferB"},    {"Buf C", "bufferC"},    {"Buf D", "bufferD"},
        {"Cube A", "cube"},
    };
    for (size_t i = 0; i < NW_COUNTOF(map); i++)
        if (!strcmp(name, map[i].st)) return map[i].nw;
    return NULL;
}

/* Comment marker the source parser uses to name a pass. */
static const char *marker(const char *mname) {
    if (!strcmp(mname, "image")) return "Image";
    if (!strcmp(mname, "cube")) return "Cube A";
    static char buf[16];
    snprintf(buf, sizeof(buf), "Buffer %c", mname[6]);
    return buf;
}

void nw_import_free(nw_import *im) {
    if (!im) return;
    free(im->glsl);
    free(im->manifest);
    free(im->notes);
    free(im->name);
    *im = (nw_import){0};
}

nw_result nw_shadertoy_import(const char *json, size_t len, nw_import *out, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    jparser j = {.p = json, .end = json + len, .err = err, .err_cap = err_cap};
    jval root = {0};
    if (!jvalue(&j, &root)) return nw_err(NW_ERR_PARSE, "shadertoy json");
    jws(&j);
    if (j.p != j.end) {
        jfree(&root);
        jfail(&j, "trailing data");
        return nw_err(NW_ERR_PARSE, "shadertoy json");
    }
    /* {"Shader": {...}}, a bare shader object, or the site's [ {...} ] */
    const jval *sh = &root;
    if (sh->t == J_ARR && sh->len) sh = &sh->items[0];
    if (jget(sh, "Shader")) sh = jget(sh, "Shader");
    const jval *passes = jget(sh, "renderpass");
    if (!passes || passes->t != J_ARR || !passes->len) {
        jfree(&root);
        if (err && err_cap) snprintf(err, err_cap, "not a Shadertoy export: no 'renderpass' list");
        return nw_err(NW_ERR_PARSE, "shadertoy json");
    }

    nw_str glsl = {0}, man = {0}, notes = {0};
    char *title = NULL;
    const jval *info = jget(sh, "info");
    const char *name = jstr(info, "name"), *user = jstr(info, "username"), *id = jstr(info, "id");
    if (name) title = strdup(name);
    nw_str_printf(&glsl, "// %s%s%s\n", name ? name : "Imported shader", user ? " by " : "", user ? user : "");
    if (id) nw_str_printf(&glsl, "// https://www.shadertoy.com/view/%s\n", id);
    nw_str_append(&glsl, "// Imported with `neowall import`. Check the original license before sharing.\n");

    /* Common first, then the buffers and the cube in Shadertoy's order, the
     * image last: the parser names passes by the marker above each one */
    static const char *const order[] = {"Common", "Buffer A", "Buf A", "Buffer B", "Buf B", "Buffer C", "Buf C",
                                        "Buffer D", "Buf D", "Cube A", "Image"};
    bool any_binding = false;
    for (size_t o = 0; o < NW_COUNTOF(order); o++) {
        for (size_t i = 0; i < passes->len; i++) {
            const jval *p = &passes->items[i];
            const char *pname = jstr(p, "name");
            const char *ptype = jstr(p, "type");
            /* old exports name passes by type only */
            if ((!pname || !*pname) && ptype) {
                pname = !strcmp(ptype, "image")    ? "Image"
                        : !strcmp(ptype, "common") ? "Common"
                        : !strcmp(ptype, "cubemap") ? "Cube A"
                                                    : NULL;
            }
            if (!pname || strcmp(pname, order[o])) continue;
            const char *code = jstr(p, "code");
            if (!code) continue;
            if (!strcmp(pname, "Common")) {
                nw_str_printf(&glsl, "\n// Common\n%s\n", code);
                continue;
            }
            const char *mname = manifest_pass_name(pname);
            if (!mname) continue;
            nw_str_printf(&glsl, "\n// %s\n%s\n", marker(mname), code);

            const jval *inputs = jget(p, "inputs");
            nw_str block = {0};
            for (size_t k = 0; inputs && inputs->t == J_ARR && k < inputs->len; k++) {
                const jval *in = &inputs->items[k];
                const jval *chv = jget(in, "channel");
                int ch = chv && chv->t == J_NUM ? (int)chv->n : (int)k;
                if (ch < 0 || ch > 3) continue;
                const char *srcn = input_source(in, pname, ch, &notes);
                if (!srcn) continue;
                /* a buffer reading itself is `self` */
                if (!strcmp(srcn, mname)) srcn = "self";
                nw_str_printf(&block, "  ch%d %s\n", ch, srcn);
            }
            if (block.data) {
                nw_str_printf(&man, "%s {\n%s}\n", mname, block.data);
                any_binding = true;
            }
            nw_str_free(&block);
        }
    }
    for (size_t i = 0; i < passes->len; i++) {
        const char *pname = jstr(&passes->items[i], "name");
        const char *ptype = jstr(&passes->items[i], "type");
        if ((pname && !strcmp(pname, "Sound")) || (ptype && !strcmp(ptype, "sound")))
            nw_str_append(&notes, "Sound pass: wallpapers don't play audio, left out\n");
    }
    jfree(&root);

    if (glsl.oom || man.oom || notes.oom) {
        nw_str_free(&glsl), nw_str_free(&man), nw_str_free(&notes), free(title);
        return nw_err(NW_ERR_OOM, "import");
    }
    if (!strstr(nw_str_cstr(&glsl), "mainImage")) {
        nw_str_free(&glsl), nw_str_free(&man), nw_str_free(&notes), free(title);
        if (err && err_cap) snprintf(err, err_cap, "the export has no Image pass");
        return nw_err(NW_ERR_PARSE, "shadertoy json");
    }
    *out = (nw_import){.glsl = nw_str_take(&glsl),
                       .manifest = any_binding ? nw_str_take(&man) : NULL,
                       .notes = notes.data ? nw_str_take(&notes) : NULL,
                       .name = title};
    nw_str_free(&man);
    return nw_ok();
}
