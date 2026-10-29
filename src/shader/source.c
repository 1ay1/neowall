/* nw_shader_source: a .glsl file into passes, bindings and params. GL-free.
 *
 * The pipeline:
 *   1. read the file, expanding `#include "x.glsl"` (relative to the file,
 *      cycle-checked), into one text with a line map back to file:line.
 *   2. split into sections at each `void mainImage`: the comment just above
 *      names the pass ("// Buffer A", "// Image", "// Pass: Blur"); text
 *      before the first pass marked "// Common" is shared by every pass; the
 *      helpers between passes belong to the pass that follows them.
 *   3. read `// @param` tweakables.
 *   4. apply the sidecar manifest (old `bufferA { ch0 self }` and new
 *      `pass "Buffer A" { ... }` forms), else bind channels by default.
 *   5. order passes so each runs after the passes it reads this frame.
 */
#define _DEFAULT_SOURCE

#include "neowall/shader/shader_source.h"
#include "neowall/log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "neowall/config/vibe.h"
#include "version.h"

/* ── text with a line map ────────────────────────────────────────────── */

typedef struct {
    nw_str text;
    NW_LIST(nw_src_loc) map; /* one per line of text */
} mapped;

static bool mapped_add_line(mapped *m, const char *line, size_t len, const char *file, int ln) {
    return nw_str_append_n(&m->text, line, len) && nw_str_append(&m->text, "\n") &&
           nw_list_push(&m->map, ((nw_src_loc){file, ln}));
}

typedef struct {
    nw_arena *arena;
    mapped out;
    nw_strs stack; /* files being expanded, for cycle detection */
    nw_strs *all;  /* every file read */
} expander;

static char *slurp(const char *path, size_t max) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    nw_str s = {0};
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        if (s.len + n > max || !nw_str_append_n(&s, buf, n)) break;
    bool ok = !ferror(f) && feof(f) && !s.oom;
    fclose(f);
    if (!ok) {
        nw_str_free(&s);
        return NULL;
    }
    if (!s.data) nw_str_append(&s, "");
    return nw_str_take(&s);
}

static char *dir_of(nw_arena *a, const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? nw_arena_strndup(a, path, (size_t)(slash - path)) : nw_arena_strdup(a, ".");
}

/* `#include "file"` at the start of a line (whitespace allowed). */
static bool parse_include(const char *line, size_t len, char *out, size_t cap) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (len - i < 9 || strncmp(line + i, "#include", 8)) return false;
    i += 8;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '"') return false;
    size_t s = ++i;
    while (i < len && line[i] != '"') i++;
    if (i >= len || i - s >= cap) return false;
    memcpy(out, line + s, i - s);
    out[i - s] = '\0';
    return true;
}

static nw_result expand(expander *e, const char *text, const char *file, int depth) {
    if (depth > 16) {
        log_error("%s: #include nested more than 16 deep", file);
        return nw_err(NW_ERR_PARSE, "include depth");
    }
    int ln = 1;
    for (const char *p = text; *p; ln++) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len && p[len - 1] == '\r') len--;
        char inc[512];
        if (parse_include(p, len, inc, sizeof(inc))) {
            if (!file) {
                log_error("<memory>:%d: #include isn't allowed here", ln);
                return nw_err(NW_ERR_PARSE, "include");
            }
            const char *full = inc[0] == '/' ? nw_arena_strdup(e->arena, inc)
                                             : nw_arena_printf(e->arena, "%s/%s", dir_of(e->arena, file), inc);
            for (size_t i = 0; i < e->stack.len; i++) {
                if (!strcmp(e->stack.data[i], full)) {
                    log_error("%s:%d: #include \"%s\" includes itself", file, ln, inc);
                    return nw_err(NW_ERR_PARSE, "include cycle");
                }
            }
            char *body = slurp(full, 1u << 20);
            if (!body) {
                log_error("%s:%d: can't read #include \"%s\" (%s)", file, ln, inc, strerror(errno));
                return nw_err(NW_ERR_NOT_FOUND, "include");
            }
            if (!nw_list_push(&e->stack, full) || (e->all && !nw_list_push(e->all, full))) {
                free(body);
                return nw_err(NW_ERR_OOM, "include");
            }
            nw_result r = expand(e, body, full, depth + 1);
            e->stack.len--;
            free(body);
            NW_TRY(r);
        } else if (!mapped_add_line(&e->out, p, len, file, ln)) {
            return nw_err(NW_ERR_OOM, "shader text");
        }
        if (!nl) break;
        p = nl + 1;
    }
    return nw_ok();
}

/* ── scanning, comment-aware ─────────────────────────────────────────── */

/* Next "void mainImage" or "void mainCubemap" (a Shadertoy Cube A pass)
 * outside comments, from `p`. */
static const char *find_main(const char *p) {
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (!strncmp(p, "void", 4) && !isalnum((unsigned char)p[4]) && p[4] != '_') {
            const char *q = p + 4;
            while (*q == ' ' || *q == '\t' || *q == '\n') q++;
            if (!strncmp(q, "mainImage", 9) && !isalnum((unsigned char)q[9]) && q[9] != '_') return p;
            if (!strncmp(q, "mainCubemap", 11) && !isalnum((unsigned char)q[11]) && q[11] != '_')
                return p;
        }
        p++;
    }
    return NULL;
}

/* End of the function whose header starts at `p` (just past its '}'). */
static const char *func_end(const char *p) {
    int depth = 0;
    bool seen = false;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (*p == '{') depth++, seen = true;
        else if (*p == '}' && --depth == 0 && seen) return p + 1;
        p++;
    }
    return p;
}

static int line_index(const char *text, const char *at) {
    int n = 0;
    for (const char *p = text; p < at; p++)
        if (*p == '\n') n++;
    return n; /* 0-based */
}

/* The pass a comment line names, or NULL. Comment-only lines. */
static const char *marker_name(nw_arena *a, const char *line, size_t len) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (len - i < 2 || line[i] != '/' || (line[i + 1] != '/' && line[i + 1] != '*')) return NULL;
    char buf[256];
    size_t n = len - i < sizeof(buf) - 1 ? len - i : sizeof(buf) - 1;
    memcpy(buf, line + i, n);
    buf[n] = '\0';
    static const char *const fixed[][2] = {
        {"Buffer A", "Buffer A"}, {"BufferA", "Buffer A"}, {"Buffer B", "Buffer B"},
        {"BufferB", "Buffer B"},  {"Buffer C", "Buffer C"}, {"BufferC", "Buffer C"},
        {"Buffer D", "Buffer D"}, {"BufferD", "Buffer D"},
    };
    for (size_t k = 0; k < NW_COUNTOF(fixed); k++)
        if (strstr(buf, fixed[k][0])) return fixed[k][1];
    const char *body = buf + 2;
    while (*body == ' ' || *body == '\t' || *body == '=' || *body == '-') body++;
    if (!strncasecmp(body, "Image", 5) && !isalnum((unsigned char)body[5])) return "Image";
    if (!strncasecmp(body, "Pass:", 5)) {
        body += 5;
        while (*body == ' ') body++;
        size_t m = strcspn(body, "*/\n");
        while (m && isspace((unsigned char)body[m - 1])) m--;
        if (m) return nw_arena_strndup(a, body, m);
    }
    return NULL;
}

/* ── params ──────────────────────────────────────────────────────────── */

static int type_width(const char *t) {
    if (!strcmp(t, "float") || !strcmp(t, "int") || !strcmp(t, "bool")) return 1;
    if (!strcmp(t, "vec2")) return 2;
    if (!strcmp(t, "vec3")) return 3;
    if (!strcmp(t, "vec4")) return 4;
    return 0;
}

/* `// @param [min .. max] [step s] ["help"]` on the line above
 * `uniform <type> <name> [= <default>];`. The default may be a scalar or
 * vecN(...). The uniform line is removed from the source: the engine
 * declares it (GLSL 330 has no uniform initialisers). */
static bool parse_param(nw_arena *a, const char *ann, const char *decl, nw_param *out) {
    memset(out, 0, sizeof(*out));
    const char *p = strstr(ann, "@param");
    if (!p) return false;
    p += 6;
    char *end;
    double lo = strtod(p, &end);
    if (end != p) {
        p = end;
        while (*p == ' ') p++;
        if (p[0] == '.' && p[1] == '.') {
            p += 2;
            double hi = strtod(p, &end);
            if (end != p) {
                out->min = (float)lo;
                out->max = (float)hi;
                p = end;
            }
        }
    }
    const char *st = strstr(p, "step");
    if (st) out->step = strtof(st + 4, NULL);
    const char *q1 = strchr(p, '"');
    const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
    if (q2) out->help = nw_arena_strndup(a, q1 + 1, (size_t)(q2 - q1 - 1));

    while (isspace((unsigned char)*decl)) decl++;
    if (strncmp(decl, "uniform", 7) || !isspace((unsigned char)decl[7])) return false;
    decl += 7;
    char type[16], name[64];
    if (sscanf(decl, " %15s %63[A-Za-z0-9_]", type, name) != 2) return false;
    int w = type_width(type);
    if (!w) return false;
    out->type = nw_arena_strdup(a, type);
    out->name = nw_arena_strdup(a, name);
    const char *eq = strchr(decl, '=');
    if (eq) {
        const char *v = eq + 1;
        const char *paren = strchr(v, '(');
        const char *semi = strchr(v, ';');
        if (paren && (!semi || paren < semi)) v = paren + 1;
        for (int i = 0; i < w; i++) {
            out->def[i] = strtof(v, &end);
            if (end == v) {
                if (i == 1) for (int k = 1; k < w; k++) out->def[k] = out->def[0]; /* vec3(0.5) */
                break;
            }
            v = end;
            while (*v == ' ' || *v == ',') v++;
        }
        if (!strcmp(type, "bool")) out->def[0] = strstr(eq, "true") ? 1.0f : 0.0f;
    }
    return true;
}

/* ── source model ────────────────────────────────────────────────────── */

int nw_shader_source_find_pass(const nw_shader_source *src, const char *name) {
    for (size_t i = 0; i < src->passes.len; i++)
        if (!strcasecmp(src->passes.data[i].name, name)) return (int)i;
    /* cube, cubeA, cubemap, "Cubemap A" (Shadertoy's own input name) */
    if (!strncasecmp(name, "cube", 4)) {
        for (size_t i = 0; i < src->passes.len; i++)
            if (src->passes.data[i].is_cube) return (int)i;
        return -1;
    }
    /* manifest shorthands: a, bufferA, buffer_a */
    if (strlen(name) == 1 || !strncasecmp(name, "buffer", 6)) {
        char letter = (char)toupper((unsigned char)name[strlen(name) - 1]);
        char full[16];
        snprintf(full, sizeof(full), "Buffer %c", letter);
        for (size_t i = 0; i < src->passes.len; i++)
            if (!strcmp(src->passes.data[i].name, full)) return (int)i;
    }
    return -1;
}

void nw_shader_source_free(nw_shader_source *src) {
    if (!src) return;
    nw_list_free(&src->passes);
    nw_list_free(&src->params);
    nw_list_free(&src->binds);
    nw_list_free(&src->includes);
    nw_arena_free(&src->arena_);
    free(src);
}

/* Pieces of the mapped text, as [first_line, last_line) ranges. */
typedef struct {
    int from, to;
} range;

/* Copy lines [from,to) of the mapped text into the arena as one string,
 * with `#line N S` directives wherever the file or line jumps (S = index
 * of the file in src->includes, the GLSL "source string number"), so the
 * driver reports N and S and we can turn them back into file:line.
 * Lines in `skip` are blanked. */
static int file_index(const nw_shader_source *src, const char *file) {
    for (size_t i = 0; i < src->includes.len; i++)
        if (src->includes.data[i] == file || (file && !strcmp(src->includes.data[i], file))) return (int)i;
    return 0;
}

static const char *emit_range(const nw_shader_source *src, nw_arena *a, const mapped *m,
                              const char *text, range r, const bool *skip, const char **first_file,
                              int *first_line) {
    nw_str s = {0};
    const char *cur_file = NULL;
    int cur_line = -1;
    const char *p = text;
    for (int i = 0; i < r.from && *p; i++) {
        const char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : p + strlen(p);
    }
    for (int i = r.from; i < r.to && *p; i++) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        const nw_src_loc *loc = &m->map.data[i];
        if (i == r.from) {
            *first_file = loc->file;
            *first_line = loc->line;
        }
        if (loc->file != cur_file || loc->line != cur_line) {
            nw_str_printf(&s, "#line %d %d\n", loc->line, file_index(src, loc->file));
            cur_file = loc->file;
            cur_line = loc->line;
        }
        if (skip && skip[i]) nw_str_append(&s, "");
        else nw_str_append_n(&s, p, len);
        nw_str_append(&s, "\n");
        cur_line++;
        p = nl ? nl + 1 : p + len;
    }
    const char *out = s.oom ? NULL : nw_arena_strdup(a, nw_str_cstr(&s));
    nw_str_free(&s);
    return out;
}

/* ── default channel bindings ────────────────────────────────────────── */

/* Without a manifest, guess what each iChannelN is, from how the pass uses
 * it. This is the Shadertoy convention, made predictable:
 *   - a buffer pass reading the channel at its own fragCoord/uv: itself
 *     (feedback);
 *   - fetches at tiny noise-like scales (/256, *0.01, texelFetch with & 255):
 *     the noise texture;
 *   - otherwise, in a multipass shader, channels map to buffers in order
 *     (Image ch0 = Buffer A, ch1 = Buffer B, ...; a buffer's ch1+ = the
 *     other buffers), and in a single-pass shader to noise.
 * A manifest always wins. */
static bool uses_channel(const char *body, int c) {
    char name[16];
    snprintf(name, sizeof(name), "iChannel%d", c);
    for (const char *p = strstr(body, name); p; p = strstr(p + 1, name))
        if (!isalnum((unsigned char)p[9]) && p[9] != '_') return true;
    return false;
}

/* Sampled with a direction rather than a uv: `texture(iChannelN, rd)` where
 * the coordinate is a vec3 the pass built. Text-level, so it looks for a
 * vec3-ish argument: a `vec3(` constructor, or a name declared vec3 in the
 * body. */
static bool cube_like(const char *body, int c) {
    char name[16];
    snprintf(name, sizeof(name), "iChannel%d", c);
    for (const char *p = strstr(body, name); p; p = strstr(p + 1, name)) {
        const char *q = p + strlen(name);
        while (*q == ' ') q++;
        if (*q != ',') continue;
        q++;
        while (*q == ' ') q++;
        if (!strncmp(q, "vec3(", 5)) return true;
        /* calls that give back a direction */
        static const char *const dir_fns[] = {"reflect(", "refract(", "normalize(", "cross("};
        bool fn = false;
        for (size_t k = 0; k < NW_COUNTOF(dir_fns); k++)
            if (!strncmp(q, dir_fns[k], strlen(dir_fns[k]))) fn = true;
        if (fn) {
            /* normalize(uv) on a vec2 is a 2D lookup: only count it when the
             * argument is itself a vec3 (or another call we can't see into) */
            const char *a = strchr(q, '(') + 1;
            while (*a == ' ') a++;
            char arg[64];
            size_t m = 0;
            while ((isalnum((unsigned char)a[m]) || a[m] == '_') && m < sizeof(arg) - 1) arg[m] = a[m], m++;
            arg[m] = '\0';
            if (strncmp(q, "normalize(", 10) || !m || a[m] != ')') return true;
            char d2[80];
            snprintf(d2, sizeof(d2), "vec3 %s", arg);
            if (strstr(body, d2)) return true;
            continue;
        }
        /* a plain identifier: is it declared vec3 somewhere in the pass? */
        char id[64];
        size_t n = 0;
        while ((isalnum((unsigned char)q[n]) || q[n] == '_') && n < sizeof(id) - 1) id[n] = q[n], n++;
        id[n] = '\0';
        if (!n || (q[n] != ')' && q[n] != ' ' && q[n] != ',')) continue;
        char decl[80];
        snprintf(decl, sizeof(decl), "vec3 %s", id);
        for (const char *d = strstr(body, decl); d; d = strstr(d + 1, decl))
            if (!isalnum((unsigned char)d[strlen(decl)]) && d[strlen(decl)] != '_') return true;
        /* `in vec3 rd` in a parameter list */
        snprintf(decl, sizeof(decl), "vec3  %s", id);
        if (strstr(body, decl)) return true;
    }
    return false;
}

static bool noise_like(const char *body, int c) {
    char name[16];
    snprintf(name, sizeof(name), "iChannel%d", c);
    for (const char *p = strstr(body, name); p; p = strstr(p + 1, name)) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char line[256];
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        if (strstr(line, "/256") || strstr(line, "/ 256") || strstr(line, "/1024") ||
            strstr(line, "/ 1024") || strstr(line, "*0.0") || strstr(line, "& 255") ||
            strstr(line, "&255") || strstr(line, "/64.") || strstr(line, "/ 64."))
            return true;
    }
    return false;
}

static bool self_like(const char *body, int c) {
    char name[16];
    snprintf(name, sizeof(name), "iChannel%d", c);
    for (const char *p = strstr(body, name); p; p = strstr(p + 1, name)) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char line[256];
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        if (strstr(line, "fragCoord") || strstr(line, "uv)") || strstr(line, "uv,") ||
            strstr(line, "iResolution"))
            return true;
    }
    return false;
}

static void default_bindings(nw_shader_source *src) {
    int nbuf = 0;
    int bufs[16];
    int cube = -1;
    for (size_t i = 0; i < src->passes.len && nbuf < 16; i++)
        if (src->passes.data[i].is_cube) cube = (int)i;
        else if (!src->passes.data[i].is_image) bufs[nbuf++] = (int)i;

    for (size_t i = 0; i < src->passes.len; i++) {
        nw_pass_src *p = &src->passes.data[i];
        int next_buf = 0;
        for (int c = 0; c < NW_CHANNELS; c++) {
            nw_input *in = &p->ch[c];
            if (!uses_channel(p->body, c)) {
                *in = (nw_input){.kind = NW_IN_NONE};
                continue;
            }
            if ((int)i != cube && cube_like(p->body, c)) {
                /* sampled with a direction: the Cube A pass, or a built-in
                 * sky standing in for one of Shadertoy's cube maps */
                *in = cube >= 0 ? (nw_input){.kind = NW_IN_CUBE, .pass = cube} : (nw_input){.kind = NW_IN_SKY};
            } else if (noise_like(p->body, c)) {
                *in = (nw_input){.kind = NW_IN_NOISE, .wrap_repeat = true};
            } else if (!p->is_image && c == 0 && nbuf > 0 && bufs[0] == (int)i && self_like(p->body, c)) {
                /* the first buffer reading its own coords: feedback. Later
                 * buffers' ch0 is Buffer A by Shadertoy convention. */
                *in = (nw_input){.kind = NW_IN_PASS, .pass = (int)i};
            } else if (nbuf > 0) {
                /* buffers in order: ch0 = first buffer, ch1 = the next, ...,
                 * skipping this pass itself */
                int pick = -1;
                for (int k = 0; k < nbuf; k++) {
                    int bi = bufs[(next_buf + k) % nbuf];
                    if (bi != (int)i) {
                        pick = bi;
                        next_buf = (next_buf + k + 1) % nbuf;
                        break;
                    }
                }
                *in = pick >= 0 ? (nw_input){.kind = NW_IN_PASS, .pass = pick}
                                : (nw_input){.kind = NW_IN_PASS, .pass = (int)i};
            } else {
                *in = (nw_input){.kind = NW_IN_NOISE, .wrap_repeat = true};
            }
        }
    }
}

/* ── manifest ────────────────────────────────────────────────────────── */

static bool parse_input(nw_shader_source *src, const char *dir, const char *v, nw_input *out,
                        const char *where) {
    static const struct { const char *name; nw_input_kind kind; } fixed[] = {
        {"noise", NW_IN_NOISE}, {"audio", NW_IN_AUDIO}, {"font", NW_IN_FONT},
        {"keyboard", NW_IN_KEYBOARD}, {"blue_noise", NW_IN_BLUE_NOISE}, {"bluenoise", NW_IN_BLUE_NOISE},
        {"none", NW_IN_NONE}, {"sky", NW_IN_SKY},
    };
    *out = (nw_input){0};
    /* an image, tagged (texture:art.png) or by extension. Before the
     * built-in names, so a file called noise.png is still a file. */
    const char *file = !strncasecmp(v, "texture:", 8) ? v + 8 : NULL;
    const char *dot = strrchr(v, '.');
    if (!file && dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")))
        file = v;
    if (file && *file) {
        out->kind = NW_IN_IMAGE;
        out->wrap_repeat = true;
        out->path = file[0] == '/' || file[0] == '~' || !dir ? nw_arena_strdup(&src->arena_, file)
                                                             : nw_arena_printf(&src->arena_, "%s/%s", dir, file);
        return out->path != NULL;
    }
    for (size_t k = 0; k < NW_COUNTOF(fixed); k++) {
        if (!strcasecmp(v, fixed[k].name)) {
            out->kind = fixed[k].kind;
            out->wrap_repeat = fixed[k].kind == NW_IN_NOISE || fixed[k].kind == NW_IN_BLUE_NOISE;
            return true;
        }
    }
    int p = nw_shader_source_find_pass(src, v);
    if (p >= 0) {
        *out = (nw_input){.kind = src->passes.data[p].is_cube ? NW_IN_CUBE : NW_IN_PASS, .pass = p};
        return true;
    }
    /* the terminal's grid is read through iTermCells, not a channel */
    if (!strcasecmp(v, "term") || !strcasecmp(v, "terminal") || !strcasecmp(v, "text")) {
        out->kind = NW_IN_NONE;
        return true;
    }
    log_error("%s: unknown channel source '%s' (a pass name, cube, self, noise, blue_noise, audio, font, sky, "
             "keyboard, none, or an image file)", where, v);
    return false;
}

static bool want_enum(const char *v, const char *const *names, size_t n, int *out) {
    for (size_t i = 0; i < n; i++)
        if (!strcasecmp(v, names[i])) {
            *out = (int)i;
            return true;
        }
    return false;
}

static nw_result apply_pass_block(nw_shader_source *src, const char *dir, int pi, VibeObject *o,
                                  const char *where) {
    nw_pass_src *p = &src->passes.data[pi];
    for (size_t i = 0; i < o->count; i++) {
        const char *k = o->entries[i].key;
        const VibeValue *v = o->entries[i].value;
        const char *sv = v->type == VIBE_TYPE_STRING ? v->as_string : NULL;
        int c = -1;
        if ((!strncmp(k, "ch", 2) && strlen(k) == 3) || (!strncmp(k, "channel", 7) && strlen(k) == 8))
            c = k[strlen(k) - 1] - '0';
        if (c >= 0 && c < NW_CHANNELS) {
            if (!sv) {
                log_error("%s: '%s' must name a source", where, k);
                return nw_err(NW_ERR_PARSE, "manifest");
            }
            if (!strcasecmp(sv, "self")) {
                p->ch[c] = (nw_input){.kind = NW_IN_PASS, .pass = pi};
            } else if (!parse_input(src, dir, sv, &p->ch[c], where)) {
                return nw_err(NW_ERR_PARSE, "manifest");
            }
            src->explicit_bindings = true;
            continue;
        }
        int e;
        static const char *const fmts[] = {"rgba8", "rgba16f", "rgba32f"};
        static const char *const filters[] = {"linear", "nearest", "mipmap"};
        static const char *const wraps[] = {"clamp", "repeat", "mirror"};
        if (!strcmp(k, "format") && sv && want_enum(sv, fmts, 3, &e)) p->format = (nw_format)e;
        else if (!strcmp(k, "filter") && sv && want_enum(sv, filters, 3, &e)) p->filter = (nw_filter)e;
        else if (!strcmp(k, "wrap") && sv && want_enum(sv, wraps, 3, &e)) p->wrap = (nw_wrap)e;
        else if (!strcmp(k, "scale") && (v->type == VIBE_TYPE_FLOAT || v->type == VIBE_TYPE_INTEGER)) {
            double s = v->type == VIBE_TYPE_FLOAT ? v->as_float : (double)v->as_integer;
            if (!(s > 0 && s <= 4)) {
                log_error("%s: 'scale' must be in (0, 4]", where);
                return nw_err(NW_ERR_PARSE, "manifest");
            }
            p->scale = s;
        } else if (!strcmp(k, "size") && sv && sscanf(sv, "%dx%d", &p->fixed_w, &p->fixed_h) == 2 &&
                   p->fixed_w > 0 && p->fixed_h > 0 && p->fixed_w <= 16384 && p->fixed_h <= 16384) {
        } else {
            log_error("%s: bad or unknown key '%s'", where, k);
            return nw_err(NW_ERR_PARSE, "manifest");
        }
    }
    return nw_ok();
}

static nw_result apply_manifest(nw_shader_source *src, const char *text, const char *mpath) {
    VibeError verr = {0};
    VibeValue *root = vibe_parse(text, strlen(text), &verr);
    if (!root) {
        log_error("%s:%d: %s", mpath, verr.line, verr.message ? verr.message : "syntax error");
        vibe_error_free(&verr);
        return nw_err(NW_ERR_PARSE, "manifest syntax");
    }
    const char *dir = src->path ? dir_of(&src->arena_, src->path) : NULL;
    nw_result r = nw_ok();
    VibeObject *top = root->as_object;
    for (size_t i = 0; !nw_is_err(r) && i < top->count; i++) {
        const char *k = top->entries[i].key;
        VibeValue *v = top->entries[i].value;
        char where[768];
        snprintf(where, sizeof(where), "%s: %s", mpath, k);
        if (!strcmp(k, "shader")) continue; /* resolved by the loader */
        if (!strcmp(k, "pass") && v->type == VIBE_TYPE_OBJECT) {
            /* pass { "Buffer A" { ... } } */
            for (size_t j = 0; !nw_is_err(r) && j < v->as_object->count; j++) {
                const char *pn = v->as_object->entries[j].key;
                VibeValue *b = v->as_object->entries[j].value;
                int pi = nw_shader_source_find_pass(src, pn);
                snprintf(where, sizeof(where), "%s: pass \"%s\"", mpath, pn);
                if (pi < 0 || b->type != VIBE_TYPE_OBJECT) {
                    log_error("%s: no such pass in the shader", where);
                    r = nw_err(NW_ERR_PARSE, "manifest");
                } else {
                    r = apply_pass_block(src, dir, pi, b->as_object, where);
                }
            }
        } else if (v->type == VIBE_TYPE_OBJECT && !strcmp(k, "uniforms")) {
            for (size_t j = 0; j < v->as_object->count; j++) {
                const VibeValue *b = v->as_object->entries[j].value;
                char num[32];
                const char *spec = b->type == VIBE_TYPE_STRING ? b->as_string : NULL;
                if (!spec && (b->type == VIBE_TYPE_FLOAT || b->type == VIBE_TYPE_INTEGER)) {
                    snprintf(num, sizeof(num), "%g", b->type == VIBE_TYPE_FLOAT ? b->as_float : (double)b->as_integer);
                    spec = num;
                }
                nw_uniform_bind ub = {nw_arena_strdup(&src->arena_, v->as_object->entries[j].key),
                                      spec ? nw_arena_strdup(&src->arena_, spec) : NULL};
                if (!ub.source || !nw_list_push(&src->binds, ub)) {
                    log_error("%s: uniform '%s' needs a source", where, ub.name);
                    r = nw_err(NW_ERR_PARSE, "manifest");
                    break;
                }
            }
        } else if (v->type == VIBE_TYPE_OBJECT) {
            /* old form: bufferA { ch0 self }, image { ch1 bufferA } */
            int pi = !strcasecmp(k, "image") ? src->image_pass : nw_shader_source_find_pass(src, k);
            if (pi < 0) {
                log_error("%s: no such pass in the shader", where);
                r = nw_err(NW_ERR_PARSE, "manifest");
            } else {
                r = apply_pass_block(src, dir, pi, v->as_object, where);
            }
        } else if ((!strncmp(k, "channel", 7) && strlen(k) == 8) && v->type == VIBE_TYPE_STRING) {
            /* old form: channel0 audio  (applies to the image pass) */
            int c = k[7] - '0';
            if (c < 0 || c >= NW_CHANNELS ||
                !parse_input(src, dir, v->as_string, &src->passes.data[src->image_pass].ch[c], where)) {
                r = nw_err(NW_ERR_PARSE, "manifest");
            } else {
                src->explicit_bindings = true;
            }
        } else if (!strcmp(k, "state") && v->type == VIBE_TYPE_STRING) {
            if (nw_shader_source_find_pass(src, v->as_string) < 0) {
                log_error("%s: state pass '%s' isn't in the shader", where, v->as_string);
                r = nw_err(NW_ERR_PARSE, "manifest");
            } else {
                src->state_pass = nw_arena_strdup(&src->arena_, v->as_string);
            }
        } else {
            log_warn("%s: unknown manifest key (ignored)", where);
        }
    }
    vibe_value_free(root);
    return r;
}

/* ── ordering ────────────────────────────────────────────────────────── */

/* Run order: a pass runs after every other pass it reads, so it sees their
 * output from THIS frame. Reading itself, or a pass that would have to run
 * later (a cycle), gives last frame's output: that's feedback. Image runs
 * last. Stable: ties keep file order. */
static nw_result order_passes(nw_shader_source *src) {
    size_t n = src->passes.len;
    int *order = calloc(n, sizeof(int));
    int *pos = calloc(n, sizeof(int));
    bool *done = calloc(n, sizeof(bool));
    if (!order || !pos || !done) {
        free(order), free(pos), free(done);
        return nw_err(NW_ERR_OOM, "pass order");
    }
    size_t k = 0;
    while (k < n) {
        size_t before = k;
        for (size_t i = 0; i < n; i++) {
            if (done[i] || (int)i == src->image_pass) continue;
            bool ready = true;
            for (int c = 0; c < NW_CHANNELS && ready; c++) {
                const nw_input *in = &src->passes.data[i].ch[c];
                if ((in->kind == NW_IN_PASS || in->kind == NW_IN_CUBE) && in->pass != (int)i &&
                    in->pass != src->image_pass && !done[in->pass])
                    ready = false;
            }
            if (ready) {
                done[i] = true;
                order[k++] = (int)i;
            }
        }
        if (k == before) {
            /* a cycle among buffers: break it at the first remaining pass,
             * whose reads of not-yet-run passes become last-frame reads */
            for (size_t i = 0; i < n; i++)
                if (!done[i] && (int)i != src->image_pass) {
                    done[i] = true;
                    order[k++] = (int)i;
                    break;
                }
            if (k == before) {
                done[src->image_pass] = true;
                order[k++] = src->image_pass;
            }
        }
    }
    for (size_t i = 0; i < n; i++) pos[order[i]] = (int)i;

    NW_LIST(nw_pass_src) sorted = {0};
    if (!nw_list_reserve(&sorted, n)) {
        free(order), free(pos), free(done);
        return nw_err(NW_ERR_OOM, "pass order");
    }
    for (size_t i = 0; i < n; i++) {
        nw_pass_src p = src->passes.data[order[i]];
        for (int c = 0; c < NW_CHANNELS; c++)
            if (p.ch[c].kind == NW_IN_PASS || p.ch[c].kind == NW_IN_CUBE) p.ch[c].pass = pos[p.ch[c].pass];
        sorted.data[sorted.len++] = p;
    }
    src->image_pass = pos[src->image_pass];
    nw_list_free(&src->passes);
    src->passes.data = sorted.data;
    src->passes.len = sorted.len;
    src->passes.cap = sorted.cap;
    free(order), free(pos), free(done);
    return nw_ok();
}

/* ── load ────────────────────────────────────────────────────────────── */

static bool body_animated(const char *b) {
    static const char *const t[] = {"iTime", "iFrame", "iDate", "iMouse", "iTimeDelta", "iAudio",
                                    "iKeyEnergy", "iMouseEnergy", "iCpu", "iRam", "iGpu", "iNet",
                                    "iDisk", "iLoad", "iWindow", "iFocusedWindow", "iTimeOfDay",
                                    "iSun", "iBattery", "iThermal", "iActivity", "iPulse", "iState"};
    for (size_t i = 0; i < NW_COUNTOF(t); i++)
        if (strstr(b, t[i])) return true;
    return false;
}

bool nw_shader_requires_ok(const char *text, const char *path) {
    if (!text) return true;
    /* only at the start of a line, like a real directive: a mention in a
     * comment isn't a requirement */
    for (const char *line = text; line && *line;) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "#pragma", 7)) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            if (!strncmp(p, "neowall", 7)) {
                p += 7;
                while (*p == ' ' || *p == '\t') p++;
                if (!strncmp(p, "requires", 8)) {
                    p += 8;
                    int major = 0, minor = 0, patch = 0;
                    if (sscanf(p, "%d.%d.%d", &major, &minor, &patch) < 1) {
                        log_warn("%s: malformed '#pragma neowall requires' (ignored)", path ? path : "<memory>");
                        return true;
                    }
                    if (major * 10000 + minor * 100 + patch > NEOWALL_VERSION_NUMBER) {
                        /* newer uniforms would silently read 0: refuse instead */
                        log_error("%s requires neowall %d.%d.%d, but this is %s", path ? path : "<memory>",
                                  major, minor, patch, NEOWALL_VERSION_STRING);
                        return false;
                    }
                    return true;
                }
            }
        }
        const char *eol = strchr(line, '\n');
        line = eol ? eol + 1 : NULL;
    }
    return true;
}

nw_result nw_shader_source_parse(const char *text, const char *name, const char *manifest_text,
                                 nw_shader_source **out) {
    if (!nw_shader_requires_ok(text, name)) return nw_err(NW_ERR_UNSUPPORTED, "shader requires a newer neowall");
    nw_shader_source *src = calloc(1, sizeof(*src));
    if (!src) return nw_err(NW_ERR_OOM, "shader source");
    nw_arena_init(&src->arena_);
    nw_arena *a = &src->arena_;
    src->path = name ? nw_arena_strdup(a, name) : NULL;
    const char *file = src->path ? src->path : "<memory>";
    if (src->path) (void)nw_list_push(&src->includes, src->path);

    expander e = {.arena = a, .all = &src->includes};
    if (src->path) (void)nw_list_push(&e.stack, src->path);
    nw_result r = expand(&e, text, src->path, 0);
    nw_list_free(&e.stack);
    if (nw_is_err(r)) goto fail;
    if (e.out.text.oom) {
        r = nw_err(NW_ERR_OOM, "shader text");
        goto fail;
    }
    const char *all = nw_str_cstr(&e.out.text);
    int nlines = (int)e.out.map.len;

    /* lines that are `// @param` + `uniform` pairs: record and blank them */
    bool *skip = calloc((size_t)nlines + 1, sizeof(bool));
    const char **line_ptr = calloc((size_t)nlines + 1, sizeof(char *));
    if (!skip || !line_ptr) {
        free(skip), free(line_ptr);
        r = nw_err(NW_ERR_OOM, "shader text");
        goto fail;
    }
    {
        const char *p = all;
        for (int i = 0; i < nlines; i++) {
            line_ptr[i] = p;
            const char *nl = strchr(p, '\n');
            p = nl ? nl + 1 : p + strlen(p);
        }
    }
    for (int i = 0; i + 1 < nlines; i++) {
        const char *ln = line_ptr[i];
        size_t len = (size_t)(line_ptr[i + 1] - ln);
        char ann[512], decl[512];
        size_t lead = 0;
        while (lead < len && (ln[lead] == ' ' || ln[lead] == '\t')) lead++;
        if (len >= sizeof(ann) || len - lead < 9 || strncmp(ln + lead, "//", 2)) continue;
        size_t at = lead + 2;
        while (at < len && ln[at] == ' ') at++;
        if (strncmp(ln + at, "@param", 6)) continue;
        memcpy(ann, ln, len);
        ann[len] = '\0';
        size_t dlen = (size_t)((i + 2 < nlines ? line_ptr[i + 2] : all + strlen(all)) - line_ptr[i + 1]);
        if (dlen >= sizeof(decl)) continue;
        memcpy(decl, line_ptr[i + 1], dlen);
        decl[dlen] = '\0';
        nw_param prm;
        if (parse_param(a, ann, decl, &prm)) {
            if (!nw_list_push(&src->params, prm)) {
                free(skip), free(line_ptr);
                r = nw_err(NW_ERR_OOM, "params");
                goto fail;
            }
            skip[i + 1] = true;
        } else {
            log_warn("%s:%d: @param must sit right above 'uniform <float|vecN|int|bool> name [= value];'",
                    e.out.map.data[i].file ? e.out.map.data[i].file : file, e.out.map.data[i].line);
        }
    }

    /* passes */
    /* A Shadertoy sound pass (mainSound) makes audio, which a wallpaper
     * doesn't play: leave its function out so it can't break the picture. */
    for (const char *sp = strstr(all, "mainSound"); sp; sp = strstr(sp + 1, "mainSound")) {
        const char *fn = sp;
        while (fn > all && fn[-1] != '\n') fn--;
        const char *t = fn;
        while (*t == ' ' || *t == '\t') t++;
        if (strncmp(t, "vec2", 4) || (isalnum((unsigned char)sp[9]) || sp[9] == '_')) continue;
        int from = line_index(all, fn), to = line_index(all, func_end(sp));
        for (int k = from; k <= to && k < nlines; k++) skip[k] = true;
        log_info("%s: skipping the sound pass (mainSound): wallpapers don't play audio", file);
    }
    NW_LIST(const char *) mains = {0};
    for (const char *m = find_main(all); m; m = find_main(func_end(m))) (void)nw_list_push(&mains, m);
    if (mains.len == 0) {
        log_error("%s: no 'void mainImage(out vec4, in vec2)' found", file);
        free(skip), free(line_ptr);
        nw_list_free(&mains);
        r = nw_err(NW_ERR_PARSE, "no mainImage");
        goto fail;
    }

    /* Multipass: every line outside a mainImage body (top-level helpers,
     * uniforms, defines, wherever they sit in the file) is common to all
     * passes. A pass is just its mainImage. GLSL drops what a pass doesn't
     * use, and helpers written between two passes are meant to be shared.
     * Single pass: the whole file is the Image pass. */
    bool *in_main = calloc((size_t)nlines + 1, sizeof(bool));
    if (!in_main) {
        free(skip), free(line_ptr);
        nw_list_free(&mains);
        r = nw_err(NW_ERR_OOM, "passes");
        goto fail;
    }
    for (size_t pi = 0; pi < mains.len; pi++) {
        int from = line_index(all, mains.data[pi]);
        int to = line_index(all, func_end(mains.data[pi]));
        for (int k = from; k <= to && k < nlines; k++) in_main[k] = true;
    }
    if (mains.len > 1) {
        /* common = all lines not in a main, with markers blanked */
        bool *cskip = calloc((size_t)nlines + 1, sizeof(bool));
        if (!cskip) {
            free(in_main), free(skip), free(line_ptr);
            nw_list_free(&mains);
            r = nw_err(NW_ERR_OOM, "passes");
            goto fail;
        }
        for (int k = 0; k < nlines; k++) cskip[k] = skip[k] || in_main[k];
        range cr = {0, nlines};
        src->common = emit_range(src, a, &e.out, all, cr, cskip, &src->common_file, &src->common_first_line);
        free(cskip);
    }

    for (size_t pi = 0; pi < mains.len; pi++) {
        int fn_line = line_index(all, mains.data[pi]);
        int end_line = line_index(all, func_end(mains.data[pi])) + 1;
        /* the function, not the comment, says it's a cube pass */
        const char *fn = mains.data[pi] + 4;
        while (*fn == ' ' || *fn == '\t' || *fn == '\n') fn++;
        bool cube = !strncmp(fn, "mainCubemap", 11);
        /* marker: up to 5 comment lines above the function */
        const char *pname = NULL;
        for (int k = fn_line - 1; k >= 0 && k >= fn_line - 5 && !pname; k--) {
            if (in_main[k]) break;
            size_t len = (size_t)(line_ptr[k + 1] - line_ptr[k]);
            pname = marker_name(a, line_ptr[k], len - (len && line_ptr[k][len - 1] == '\n'));
        }
        if (cube) {
            pname = "Cube A"; /* Shadertoy has exactly one */
        } else if (!pname || !strcmp(pname, "Cube A")) {
            static const char *const letters[] = {"Buffer A", "Buffer B", "Buffer C", "Buffer D"};
            pname = pi == mains.len - 1 ? "Image"
                    : pi < 4           ? letters[pi]
                                       : nw_arena_printf(a, "Buffer %zu", pi + 1);
        }
        nw_pass_src p = {
            .name = pname,
            .is_image = !cube && !strcmp(pname, "Image"),
            .is_cube = cube,
            .format = NW_FMT_RGBA16F,
            .scale = 1.0,
        };
        range rr = mains.len == 1 ? (range){0, nlines} : (range){fn_line, end_line};
        p.body = emit_range(src, a, &e.out, all, rr, skip, &p.file, &p.first_line);
        if (!p.body || !nw_list_push(&src->passes, p)) {
            free(in_main), free(skip), free(line_ptr);
            nw_list_free(&mains);
            r = nw_err(NW_ERR_OOM, "passes");
            goto fail;
        }
    }
    free(in_main);
    nw_list_free(&mains);
    free(skip);
    free(line_ptr);

    /* exactly one Image: the one named so, else the last pass */
    src->image_pass = -1;
    for (size_t i = 0; i < src->passes.len; i++) {
        if (!src->passes.data[i].is_image) continue;
        if (src->image_pass >= 0) {
            log_error("%s: more than one pass is named Image", file);
            r = nw_err(NW_ERR_PARSE, "two Image passes");
            goto fail;
        }
        src->image_pass = (int)i;
    }
    if (src->image_pass < 0) {
        /* the last pass that isn't the cube */
        for (size_t i = src->passes.len; i-- > 0 && src->image_pass < 0;)
            if (!src->passes.data[i].is_cube) src->image_pass = (int)i;
        if (src->image_pass < 0) {
            log_error("%s: a cube pass needs a 'void mainImage' to show it", file);
            r = nw_err(NW_ERR_PARSE, "no mainImage");
            goto fail;
        }
        src->passes.data[src->image_pass].is_image = true;
        src->passes.data[src->image_pass].name = "Image";
    }
    for (size_t i = 0; i < src->passes.len; i++)
        for (size_t j = i + 1; j < src->passes.len; j++)
            if (!strcmp(src->passes.data[i].name, src->passes.data[j].name)) {
                log_error("%s: two passes are named \"%s\"", file, src->passes.data[i].name);
                r = nw_err(NW_ERR_PARSE, "duplicate pass");
                goto fail;
            }

    default_bindings(src);
    if (manifest_text) {
        char mpath[512];
        snprintf(mpath, sizeof(mpath), "%s", src->path ? src->path : "<manifest>");
        char *dot = strrchr(mpath, '.');
        if (dot && src->path) snprintf(dot, sizeof(mpath) - (size_t)(dot - mpath), ".neowall");
        r = apply_manifest(src, manifest_text, mpath);
        if (nw_is_err(r)) goto fail;
    }
    r = order_passes(src);
    if (nw_is_err(r)) goto fail;

    src->animated = src->passes.len > 1;
    for (size_t i = 0; i < src->passes.len && !src->animated; i++)
        src->animated = body_animated(src->passes.data[i].body);
    if (src->common && body_animated(src->common)) src->animated = true;

    nw_str_free(&e.out.text);
    nw_list_free(&e.out.map);
    *out = src;
    return nw_ok();

fail:
    nw_str_free(&e.out.text);
    nw_list_free(&e.out.map);
    nw_shader_source_free(src);
    return r;
}

nw_result nw_shader_source_load(const char *path, nw_shader_source **out) {
    char glsl[1024], manifest[1024];
    snprintf(glsl, sizeof(glsl), "%s", path);
    const char *dot = strrchr(path, '.');
    char *mtext = NULL;

    if (dot && !strcmp(dot, ".neowall")) {
        /* the manifest names the shader; default <name>.glsl */
        snprintf(manifest, sizeof(manifest), "%s", path);
        mtext = slurp(manifest, 1u << 20);
        if (!mtext) {
            log_error("%s: %s", manifest, strerror(errno));
            return nw_err(NW_ERR_NOT_FOUND, "manifest");
        }
        snprintf(glsl, sizeof(glsl), "%.*s.glsl", (int)(dot - path), path);
        VibeError ve = {0};
        VibeValue *root = vibe_parse(mtext, strlen(mtext), &ve);
        if (root) {
            VibeValue *sh = vibe_object_get(root->as_object, "shader");
            if (sh && sh->type == VIBE_TYPE_STRING) {
                const char *slash = strrchr(path, '/');
                if (sh->as_string[0] == '/' || !slash) snprintf(glsl, sizeof(glsl), "%s", sh->as_string);
                else snprintf(glsl, sizeof(glsl), "%.*s/%s", (int)(slash - path), path, sh->as_string);
            }
            vibe_value_free(root);
        }
        vibe_error_free(&ve);
    } else {
        int base = dot ? (int)(dot - path) : (int)strlen(path);
        snprintf(manifest, sizeof(manifest), "%.*s.neowall", base, path);
        mtext = slurp(manifest, 1u << 20); /* optional */
    }

    char *text = slurp(glsl, 4u << 20);
    if (!text) {
        log_error("%s: %s", glsl, strerror(errno));
        free(mtext);
        return nw_err(NW_ERR_NOT_FOUND, "shader");
    }
    nw_result r = nw_shader_source_parse(text, glsl, mtext, out);
    if (!nw_is_err(r) && mtext) (*out)->manifest = nw_arena_strdup(&(*out)->arena_, manifest);
    free(text);
    free(mtext);
    return r;
}
