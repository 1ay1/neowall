/* The GL half of the shader system. See neowall/shader/engine.h.
 *
 * Per pass: a program with cached uniform locations, and (for buffer passes)
 * a double-buffered render target. A pass that runs earlier in the frame
 * than its reader is read as written this frame; itself or a later pass is
 * read as last frame (feedback). Built-in inputs (noise, blue noise, font,
 * audio, keyboard) are created once per engine and shared by every pass. */
#define _DEFAULT_SOURCE

#include "neowall/shader/engine.h"
#include "neowall/log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include "neowall/gfx/gfx.h"
#include "neowall/image/image.h"
#include "neowall/shader/program_cache.h"
#include "neowall/shader/shader_hint.h"
#include "neowall/textures.h"

#define AUDIO_W 512

/* A render target on the heap, so a pass can own one by pointer. */
typedef struct {
    nw_gfx_target t;
} nw_target;

static nw_target *nw_target_new(int w, int h, nw_format format, nw_filter filter, nw_wrap wrap) {
    nw_target *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    nw_gfx_format f = format == NW_FMT_RGBA8 ? NW_GFX_FMT_RGBA8 : format == NW_FMT_RGBA32F ? NW_GFX_FMT_RGBA32F : NW_GFX_FMT_RGBA16F;
    nw_gfx_filter fl = filter == NW_FILTER_NEAREST ? NW_GFX_FILTER_NEAREST
                       : filter == NW_FILTER_MIPMAP ? NW_GFX_FILTER_MIPMAP : NW_GFX_FILTER_LINEAR;
    if (nw_is_err(nw_gfx_target_create(&t->t, w, h, f, fl, true))) {
        free(t);
        return NULL;
    }
    GLint gl_wrap = wrap == NW_WRAP_REPEAT ? GL_REPEAT : wrap == NW_WRAP_MIRROR ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
    for (int k = 0; k < 2; k++) {
        if (!t->t.textures[k].id) continue;
        glBindTexture(GL_TEXTURE_2D, t->t.textures[k].id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);
    }
    return t;
}

static void nw_target_free(nw_target *t) {
    if (!t) return;
    nw_gfx_target_destroy(&t->t);
    free(t);
}

static GLuint nw_target_read_tex(const nw_target *t) {
    const nw_gfx_texture *x = nw_gfx_target_read(&t->t);
    return x ? x->id : 0;
}

static GLuint nw_target_write_tex(const nw_target *t) {
    const nw_gfx_texture *x = nw_gfx_target_write(&t->t);
    return x ? x->id : 0;
}

/* An image file as an RGBA8 mipmapped repeat texture; 0 on failure. */
static GLuint image_texture(const char *path, int *w, int *h) {
    struct image_data *img = image_load(path, 0, 0, 0);
    if (!img || !img->pixels || !img->width || !img->height) {
        if (img) image_free(img);
        return 0;
    }
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)img->width, (GLsizei)img->height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    *w = (int)img->width;
    *h = (int)img->height;
    image_free(img);
    return t;
}

static const char vertex_src[] =
    "#version 330 core\n"
    "const vec2 v[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));\n"
    "void main() { gl_Position = vec4(v[gl_VertexID], 0.0, 1.0); }\n";

typedef struct {
    char name[64];
    GLint loc;
} cached_uniform;

typedef struct {
    GLuint tex;
    float w, h;
} input_tex;

typedef struct {
    GLuint prog;
    nw_target *target; /* buffer passes; NULL for the image pass */
    int w, h;
    GLint u_time, u_res, u_mouse, u_frame, u_dt, u_fps, u_date, u_rate, u_span;
    GLint u_ch[NW_CHANNELS], u_chres, u_chtime;
    NW_LIST(cached_uniform) extra;
    /* location of the k-th uniform the caller hands in, by position: the
     * lists come in the same order every frame, so after the first frame a
     * lookup is one pointer compare, not a string search */
    NW_LIST(const char *) slot_name;
    NW_LIST(GLint) slot_loc;
    NW_LIST(GLint) params; /* parallel to src->params */
    input_tex image_ch[NW_CHANNELS]; /* NW_IN_IMAGE textures, per channel */
} pass_gl;

struct nw_engine {
    const nw_shader_source *src; /* borrowed: the caller keeps it alive */
    NW_LIST(pass_gl) passes;
    NW_LIST(float) param_values;  /* 4 per param */
    GLuint vao;
    GLuint tex_audio, tex_keyboard;
    bool uses_audio, uses_keyboard;  /* some pass samples it: worth uploading */
};

static nw_str g_errors;

const char *nw_engine_last_errors(void) { return nw_str_cstr(&g_errors); }
bool nw_engine_animated(const nw_engine *e) { return e->src->animated; }

/* ── errors back to file:line ────────────────────────────────────────── */

static const char *source_line_of(const char *path, int line, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    int n = 1;
    const char *got = NULL;
    while (fgets(buf, (int)cap, f)) {
        if (n++ == line) {
            buf[strcspn(buf, "\r\n")] = '\0';
            got = buf;
            break;
        }
    }
    fclose(f);
    return got;
}

static long read_num(const char **p) {
    long v = 0;
    while (isdigit((unsigned char)**p)) v = v * 10 + (*(*p)++ - '0');
    return v;
}

/* Driver logs say "S(L)" (NVIDIA), "S:L(C)" (Mesa) or "ERROR: S:L:" (ANGLE,
 * glslang), where S is our #line source number: the file's index in
 * src->includes. Each becomes "file:line: [pass] message" + the source line
 * and a caret. */
static void report_log(const nw_shader_source *src, const char *pass, const char *log) {
    for (const char *ln = log; ln && *ln;) {
        const char *eol = strchr(ln, '\n');
        size_t len = eol ? (size_t)(eol - ln) : strlen(ln);
        const char *p = ln;
        if (!strncmp(p, "ERROR: ", 7)) p += 7;
        else if (!strncmp(p, "WARNING: ", 9)) p += 9;
        long s = -1, line = -1, col = 0;
        const char *msg = NULL;
        if (isdigit((unsigned char)*p)) {
            s = read_num(&p);
            if (*p == '(') {
                p++;
                line = read_num(&p);
                if (*p == ')') p++;
            } else if (*p == ':') {
                p++;
                line = read_num(&p);
                if (*p == '(') {
                    p++;
                    col = read_num(&p);
                    if (*p == ')') p++;
                }
            }
            while (*p == ' ' || *p == ':') p++;
            if (!strncmp(p, "error", 5)) p += 5;
            while (*p == ' ' || *p == ':') p++;
            if (*p == 'C' && isdigit((unsigned char)p[1])) { /* NVIDIA "C1503:" */
                while (*p && *p != ':') p++;
                while (*p == ':' || *p == ' ') p++;
            }
            msg = p;
        }
        if (msg && line > 0 && s >= 0 && (size_t)s < src->includes.len) {
            const char *file = src->includes.data[s];
            int mlen = (int)(len - (size_t)(msg - ln));
            /* drivers repeat themselves (NVIDIA prints some errors twice) */
            char head[768];
            snprintf(head, sizeof(head), "%s:%ld: [%s] %.*s\n", file, line, pass, mlen, msg);
            if (strstr(nw_str_cstr(&g_errors), head)) {
                ln = eol ? eol + 1 : NULL;
                continue;
            }
            nw_str_append(&g_errors, head);
            char buf[512];
            const char *text = source_line_of(file, (int)line, buf, sizeof(buf));
            if (text) {
                nw_str_printf(&g_errors, "  %5ld | %s\n", line, text);
                /* caret at the column, or under the name the message quotes */
                long at = col;
                if (at <= 0) {
                    for (const char *q = msg; q < msg + mlen && at <= 0; q++) {
                        if (*q != '\'' && *q != '"' && *q != '`') continue;
                        const char *e = q + 1;
                        while (isalnum((unsigned char)*e) || *e == '_') e++;
                        if (e > q + 1) {
                            char name[64];
                            snprintf(name, sizeof(name), "%.*s", (int)(e - q - 1), q + 1);
                            const char *hit = strstr(text, name);
                            if (hit) at = (long)(hit - text) + 1;
                        }
                    }
                }
                if (at > 0) nw_str_printf(&g_errors, "        | %*s^\n", (int)at - 1, "");
            }
            char m[512], hint[160];
            snprintf(m, sizeof(m), "%.*s", mlen, msg);
            if (shader_hint(m, file, (int)line, hint, sizeof(hint)))
                nw_str_printf(&g_errors, "  help: %s\n", hint);
        } else if (len) {
            nw_str_printf(&g_errors, "[%s] %.*s\n", pass, (int)len, ln);
        }
        ln = eol ? eol + 1 : NULL;
    }
}

/* ── build ───────────────────────────────────────────────────────────── */

static GLuint make_tex(GLenum ifmt, int w, int h, GLenum fmt, GLenum type, const void *data, bool linear) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, fmt, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void cache_locations(pass_gl *g, const nw_shader_source *src) {
    GLuint p = g->prog;
    g->u_time = glGetUniformLocation(p, "iTime");
    g->u_res = glGetUniformLocation(p, "iResolution");
    g->u_mouse = glGetUniformLocation(p, "iMouse");
    g->u_frame = glGetUniformLocation(p, "iFrame");
    g->u_dt = glGetUniformLocation(p, "iTimeDelta");
    g->u_fps = glGetUniformLocation(p, "iFrameRate");
    g->u_date = glGetUniformLocation(p, "iDate");
    g->u_rate = glGetUniformLocation(p, "iSampleRate");
    g->u_span = glGetUniformLocation(p, "iSpanOffset");
    g->u_chres = glGetUniformLocation(p, "iChannelResolution");
    g->u_chtime = glGetUniformLocation(p, "iChannelTime");
    for (int c = 0; c < NW_CHANNELS; c++) {
        char n[16];
        snprintf(n, sizeof(n), "iChannel%d", c);
        g->u_ch[c] = glGetUniformLocation(p, n);
    }
    for (size_t i = 0; i < src->params.len; i++)
        (void)nw_list_push(&g->params, glGetUniformLocation(p, src->params.data[i].name));
}

static GLuint g_vs;           /* the one vertex shader, shared */
static int g_parallel = -1;   /* KHR_parallel_shader_compile: -1 unknown */

/* ── build ───────────────────────────────────────────────────────────────── */

/* Every pass's compile is issued before any is checked, so with
 * KHR_parallel_shader_compile the driver spreads them over its threads.
 * nw_engine_new then waits for all of them. */
typedef enum { NW_BUILD_PENDING, NW_BUILD_DONE, NW_BUILD_FAILED } nw_build_state;

typedef struct {
    GLuint fs, prog;
    bool linked;   /* glLinkProgram issued (or loaded from the cache) */
    uint64_t key;  /* program cache key */
    bool cached;   /* came from the cache: nothing to store */
} pending_pass;

typedef struct {
    const nw_shader_source *src;
    NW_LIST(pending_pass) p;
    nw_engine *done;
    nw_build_state state;
    nw_str errors;
} nw_engine_build;

static bool parallel_compile(void) {
    if (g_parallel < 0) {
        if (!glGetString(GL_VERSION)) return false; /* no context yet: ask again later */
        g_parallel = 0;
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; i++) {
            const char *x = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (x && (!strcmp(x, "GL_KHR_parallel_shader_compile") || !strcmp(x, "GL_ARB_parallel_shader_compile")))
                g_parallel = 1;
        }
        if (g_parallel) glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu); /* as many as the driver likes */
    }
    return g_parallel == 1;
}

static bool done_compiling(GLuint obj, bool is_prog) {
    if (!parallel_compile()) return true; /* the next query just blocks */
    GLint v = 0;
    if (is_prog) glGetProgramiv(obj, GL_COMPLETION_STATUS_KHR, &v);
    else glGetShaderiv(obj, GL_COMPLETION_STATUS_KHR, &v);
    return v != 0;
}

static void build_free(nw_engine_build *b);

static nw_result build_start(const nw_shader_source *src, nw_engine_build **out) {
    nw_engine_build *b = calloc(1, sizeof(*b));
    if (!b) return nw_err(NW_ERR_OOM, "engine");
    b->src = src;
    b->state = NW_BUILD_PENDING;
    (void)parallel_compile();
    if (!g_vs) {
        g_vs = glCreateShader(GL_VERTEX_SHADER);
        const char *v = vertex_src;
        glShaderSource(g_vs, 1, &v, NULL);
        glCompileShader(g_vs);
    }
    /* Issue every compile before asking about any: that's what lets the
     * driver spread them over its threads. */
    for (size_t i = 0; i < src->passes.len; i++) {
        char *text;
        nw_src_loc *map;
        size_t n;
        if (nw_is_err(nw_shader_assemble(src, (int)i, &text, &map, &n))) {
            build_free(b);
            return nw_err(NW_ERR_OOM, "assemble");
        }
        pending_pass pp = {.key = program_cache_key(vertex_src, text)};
        /* a program we linked before: no compile at all */
        if ((pp.prog = program_cache_load(pp.key))) {
            pp.linked = pp.cached = true;
        } else {
            pp.fs = glCreateShader(GL_FRAGMENT_SHADER);
            const char *t = text;
            glShaderSource(pp.fs, 1, &t, NULL);
            glCompileShader(pp.fs);
        }
        free(text);
        free(map);
        if (!nw_list_push(&b->p, pp)) {
            if (pp.fs) glDeleteShader(pp.fs);
            if (pp.prog) glDeleteProgram(pp.prog);
            build_free(b);
            return nw_err(NW_ERR_OOM, "engine");
        }
    }
    *out = b;
    return nw_ok();
}

static void collect_log(nw_engine_build *b, size_t i, GLuint obj, bool is_prog) {
    char log[8192] = "";
    if (is_prog) glGetProgramInfoLog(obj, sizeof(log), NULL, log);
    else glGetShaderInfoLog(obj, sizeof(log), NULL, log);
    nw_str_clear(&g_errors);
    report_log(b->src, b->src->passes.data[i].name, log[0] ? log : "failed with no driver log");
    nw_str_append(&b->errors, nw_str_cstr(&g_errors));
}

/* Everything compiled and linked: the rest of what nw_engine_new did. */
static nw_engine *finish(nw_engine_build *b) {
    const nw_shader_source *src = b->src;
    nw_engine *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->src = src;
    for (size_t i = 0; i < b->p.len; i++) {
        pass_gl g = {.prog = b->p.data[i].prog};
        b->p.data[i].prog = 0; /* the engine owns it now */
        cache_locations(&g, src);
        /* the compiler drops unused uniforms, so this is exact */
        if (glGetUniformLocation(g.prog, "iAudio") >= 0) e->uses_audio = true;
        for (int c = 0; c < NW_CHANNELS; c++) {
            if (src->passes.data[i].ch[c].kind == NW_IN_AUDIO) e->uses_audio = true;
            if (src->passes.data[i].ch[c].kind == NW_IN_KEYBOARD) e->uses_keyboard = true;
        }
        if (!nw_list_push(&e->passes, g)) {
            glDeleteProgram(g.prog);
            nw_engine_free(e);
            return NULL;
        }
    }
    glGenVertexArrays(1, &e->vao);
    static const float zero[AUDIO_W * 2];
    e->tex_audio = make_tex(GL_R16F, AUDIO_W, 2, GL_RED, GL_FLOAT, zero, true);
    static const uint8_t keys[256 * 3];
    e->tex_keyboard = make_tex(GL_R8, 256, 3, GL_RED, GL_UNSIGNED_BYTE, keys, false);
    for (size_t i = 0; i < src->passes.len; i++) {
        for (int c = 0; c < NW_CHANNELS; c++) {
            const nw_input *in = &src->passes.data[i].ch[c];
            if (in->kind != NW_IN_IMAGE) continue;
            int w = 0, h = 0;
            GLuint t = image_texture(in->path, &w, &h);
            if (!t) log_error("shader: can't load texture %s", in->path);
            e->passes.data[i].image_ch[c] = (input_tex){t, (float)w, (float)h};
        }
    }
    for (size_t i = 0; i < src->params.len; i++)
        for (int k = 0; k < 4; k++) (void)nw_list_push(&e->param_values, src->params.data[i].def[k]);
    if (nw_gfx_check_errors("engine build")) {
        nw_engine_free(e);
        return NULL;
    }
    return e;
}

static nw_build_state build_poll(nw_engine_build *b) {
    if (b->state != NW_BUILD_PENDING) return b->state;
    bool all = true, failed = false;
    for (size_t i = 0; i < b->p.len; i++) {
        pending_pass *pp = &b->p.data[i];
        if (!pp->linked) {
            if (!done_compiling(pp->fs, false)) {
                all = false;
                continue;
            }
            GLint ok = 0;
            glGetShaderiv(pp->fs, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                collect_log(b, i, pp->fs, false);
                failed = true;
                pp->linked = true; /* nothing more to do for this one */
                continue;
            }
            pp->prog = glCreateProgram();
            glProgramParameteri(pp->prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
            glAttachShader(pp->prog, g_vs);
            glAttachShader(pp->prog, pp->fs);
            glLinkProgram(pp->prog);
            pp->linked = true;
            all = false; /* check the link next poll */
            continue;
        }
        if (!pp->prog) {
            failed = true; /* its compile failed above */
            continue;
        }
        if (!done_compiling(pp->prog, true)) {
            all = false;
            continue;
        }
    }
    if (!all) return NW_BUILD_PENDING;
    /* every pass is compiled and linked (or failed): settle */
    for (size_t i = 0; i < b->p.len && !failed; i++) {
        GLint ok = 0;
        glGetProgramiv(b->p.data[i].prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            collect_log(b, i, b->p.data[i].prog, true);
            failed = true;
        }
    }
    for (size_t i = 0; i < b->p.len && !failed; i++)
        if (!b->p.data[i].cached) program_cache_store(b->p.data[i].key, b->p.data[i].prog);
    if (!failed) b->done = finish(b);
    b->state = !failed && b->done ? NW_BUILD_DONE : NW_BUILD_FAILED;
    return b->state;
}

static void build_free(nw_engine_build *b) {
    if (!b) return;
    for (size_t i = 0; i < b->p.len; i++) {
        if (b->p.data[i].fs) glDeleteShader(b->p.data[i].fs);
        if (b->p.data[i].prog) glDeleteProgram(b->p.data[i].prog);
    }
    if (b->done) nw_engine_free(b->done);
    nw_list_free(&b->p);
    nw_str_free(&b->errors);
    free(b);
}

nw_result nw_engine_new(const nw_shader_source *src, nw_engine **out) {
    nw_engine_build *b;
    NW_TRY(build_start(src, &b));
    nw_build_state s;
    /* we wait anyway, so skip the completion queries: the status calls
     * block until each step is done */
    int saved = g_parallel;
    g_parallel = 0;
    while ((s = build_poll(b)) == NW_BUILD_PENDING) {}
    g_parallel = saved;
    nw_str_clear(&g_errors);
    nw_str_append(&g_errors, nw_str_cstr(&b->errors));
    nw_engine *e = NULL;
    if (s == NW_BUILD_DONE) {
        e = b->done;
        b->done = NULL;
    }
    build_free(b);
    if (!e) return nw_err(NW_ERR_GL, "shader compile");
    *out = e;
    return nw_ok();
}

void nw_engine_free(nw_engine *e) {
    if (!e) return;
    for (size_t i = 0; i < e->passes.len; i++) {
        pass_gl *g = &e->passes.data[i];
        glDeleteProgram(g->prog);
        nw_target_free(g->target);
        for (int c = 0; c < NW_CHANNELS; c++)
            if (g->image_ch[c].tex) glDeleteTextures(1, &g->image_ch[c].tex);
        nw_list_free(&g->extra);
        nw_list_free(&g->slot_name);
        nw_list_free(&g->slot_loc);
        nw_list_free(&g->params);
    }
    nw_list_free(&e->passes);
    GLuint ts[] = {e->tex_audio, e->tex_keyboard};
    for (size_t i = 0; i < NW_COUNTOF(ts); i++)
        if (ts[i]) glDeleteTextures(1, &ts[i]);
    nw_list_free(&e->param_values);
    if (e->vao) glDeleteVertexArrays(1, &e->vao);
    free(e);
}

bool nw_engine_read_state(nw_engine *e, float out[16]) {
    if (!e->src->state_pass) return false;
    int p = nw_shader_source_find_pass(e->src, e->src->state_pass);
    if (p < 0 || p == e->src->image_pass || !e->passes.data[p].target) return false;
    /* after the frame's swap the fbo still holds what was just drawn */
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, e->passes.data[p].target->t.fbo);
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_FLOAT, out);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    return !nw_gfx_check_errors("read state");
}

/* ── frame ───────────────────────────────────────────────────────────── */

static void pass_size(const nw_pass_src *p, int w, int h, int *ow, int *oh) {
    if (p->fixed_w > 0) {
        *ow = p->fixed_w;
        *oh = p->fixed_h;
        return;
    }
    *ow = (int)fmax(1.0, round(w * p->scale));
    *oh = (int)fmax(1.0, round(h * p->scale));
}

/* Every buffer pass gets a target of its size. A resize keeps nothing:
 * feedback contents at the old size are meaningless. */
static void ensure_targets(nw_engine *e, int w, int h) {
    for (size_t i = 0; i < e->passes.len; i++) {
        const nw_pass_src *ps = &e->src->passes.data[i];
        pass_gl *g = &e->passes.data[i];
        if ((int)i == e->src->image_pass) continue;
        int tw, th;
        pass_size(ps, w, h, &tw, &th);
        if (g->target && g->w == tw && g->h == th) continue;
        nw_target_free(g->target);
        g->target = nw_target_new(tw, th, ps->format, ps->filter, ps->wrap);
        if (!g->target) {
            log_error("shader: can't create a %dx%d target for pass \"%s\"", tw, th, ps->name);
            continue;
        }
        g->w = tw;
        g->h = th;
    }
}

/* Noise, blue noise and the font atlas are slow to make (blue noise takes
 * seconds) and never change, so they're made on first use and shared by
 * every engine. There's one GL context per process. */
static GLuint builtin(int which) {
    static GLuint tex[3];
    if (!tex[which]) tex[which] = which == 0 ? texture_create_rgba_noise(256, 256) : which == 1 ? texture_create_blue_noise(256, 256) : texture_create_font_atlas(0, 0);
    return tex[which];
}

static GLuint resolve(nw_engine *e, int pass, int c, float *res) {
    const nw_input *in = &e->src->passes.data[pass].ch[c];
    res[0] = res[1] = 0;
    res[2] = 1;
    switch (in->kind) {
    case NW_IN_PASS: {
        pass_gl *g = &e->passes.data[in->pass];
        if (!g->target) return 0;
        res[0] = (float)g->w;
        res[1] = (float)g->h;
        return in->pass < pass ? nw_target_write_tex(g->target) : nw_target_read_tex(g->target);
    }
    case NW_IN_NOISE: res[0] = res[1] = 256; return builtin(0);
    case NW_IN_BLUE_NOISE: res[0] = res[1] = 256; return builtin(1);
    case NW_IN_FONT: res[0] = res[1] = 1024; return builtin(2);
    case NW_IN_AUDIO: res[0] = AUDIO_W; res[1] = 2; return e->tex_audio;
    case NW_IN_KEYBOARD: res[0] = 256; res[1] = 3; return e->tex_keyboard;
    case NW_IN_IMAGE: {
        const input_tex *t = &e->passes.data[pass].image_ch[c];
        res[0] = t->w;
        res[1] = t->h;
        return t->tex;
    }
    case NW_IN_NONE: return 0;
    }
    return 0;
}

static GLint extra_location(pass_gl *g, const char *name) {
    for (size_t i = 0; i < g->extra.len; i++)
        if (!strcmp(g->extra.data[i].name, name)) return g->extra.data[i].loc;
    cached_uniform u = {.loc = glGetUniformLocation(g->prog, name)};
    snprintf(u.name, sizeof(u.name), "%s", name);
    (void)nw_list_push(&g->extra, u);
    return u.loc;
}

static void set_uniforms(nw_engine *e, size_t i, pass_gl *g, const nw_frame_in *in, int w, int h) {
    bool spanned = in->span_w > 0 && in->span_h > 0;
    bool is_image = (int)i == e->src->image_pass;
    /* a spanned image pass draws its slice of the whole span */
    float rw = spanned && is_image ? (float)in->span_w : (float)w;
    float rh = spanned && is_image ? (float)in->span_h : (float)h;
    static const float no_offset[2] = {0, 0};
    if (g->u_time >= 0) glUniform1f(g->u_time, (float)in->time);
    if (g->u_res >= 0) glUniform3f(g->u_res, rw, rh, 1.0f);
    if (g->u_mouse >= 0) glUniform4fv(g->u_mouse, 1, in->mouse);
    if (g->u_frame >= 0) glUniform1i(g->u_frame, in->frame);
    if (g->u_dt >= 0) glUniform1f(g->u_dt, (float)in->dt);
    if (g->u_fps >= 0) glUniform1f(g->u_fps, in->fps > 0 ? in->fps : in->dt > 0 ? (float)(1.0 / in->dt) : 60.0f);
    if (g->u_date >= 0) glUniform4fv(g->u_date, 1, in->date);
    if (g->u_rate >= 0) glUniform1f(g->u_rate, 44100.0f);
    if (g->u_span >= 0) glUniform2fv(g->u_span, 1, spanned && is_image ? in->span_offset : no_offset);

    float chres[NW_CHANNELS * 3], chtime[NW_CHANNELS];
    for (int c = 0; c < NW_CHANNELS; c++) {
        GLuint t = resolve(e, (int)i, c, &chres[c * 3]);
        /* the config's channels fill what the shader didn't bind itself */
        if (in->channel_override[c] && !e->src->explicit_bindings &&
            e->src->passes.data[i].ch[c].kind != NW_IN_PASS) {
            t = in->channel_override[c];
            chres[c * 3] = in->channel_override_size[c][0];
            chres[c * 3 + 1] = in->channel_override_size[c][1];
        }
        glActiveTexture(GL_TEXTURE0 + (GLenum)c);
        glBindTexture(GL_TEXTURE_2D, t);
        if (g->u_ch[c] >= 0) glUniform1i(g->u_ch[c], c);
        chtime[c] = (float)in->time;
    }
    glActiveTexture(GL_TEXTURE0);
    if (g->u_chres >= 0) glUniform3fv(g->u_chres, NW_CHANNELS, chres);
    if (g->u_chtime >= 0) glUniform1fv(g->u_chtime, NW_CHANNELS, chtime);

    for (size_t k = 0; k < in->texture_count; k++) {
        GLint loc = extra_location(g, in->textures[k].name);
        if (loc < 0) continue;
        GLint unit = NW_CHANNELS + (GLint)k;
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        glBindTexture(GL_TEXTURE_2D, in->textures[k].tex);
        glUniform1i(loc, unit);
    }
    glActiveTexture(GL_TEXTURE0);

    for (size_t k = 0; k < e->src->params.len; k++) {
        GLint loc = g->params.data[k];
        if (loc < 0) continue;
        const float *v = &e->param_values.data[k * 4];
        const char *t = e->src->params.data[k].type;
        if (!strcmp(t, "float")) glUniform1f(loc, v[0]);
        else if (!strcmp(t, "vec2")) glUniform2fv(loc, 1, v);
        else if (!strcmp(t, "vec3")) glUniform3fv(loc, 1, v);
        else if (!strcmp(t, "vec4")) glUniform4fv(loc, 1, v);
        else glUniform1i(loc, (int)v[0]);
    }
    for (size_t k = 0; k < in->uniform_count + in->uniform2_count; k++) {
        const nw_uniform_value *u = k < in->uniform_count ? &in->uniforms[k] : &in->uniforms2[k - in->uniform_count];
        GLint loc;
        if (k < g->slot_name.len && g->slot_name.data[k] == u->name) {
            loc = g->slot_loc.data[k];
        } else {
            loc = extra_location(g, u->name);
            if (k < g->slot_name.len) {
                g->slot_name.data[k] = u->name;
                g->slot_loc.data[k] = loc;
            } else if (k == g->slot_name.len && nw_list_push(&g->slot_name, u->name) &&
                       !nw_list_push(&g->slot_loc, loc)) {
                g->slot_name.len--;
            }
        }
        if (loc < 0) continue;
        if (u->is_int) glUniform1i(loc, (int)u->v[0]);
        else if (u->float_array) glUniform1fv(loc, u->count, u->v);
        else if (u->count == 1) glUniform1f(loc, u->v[0]);
        else if (u->count == 2) glUniform2fv(loc, 1, u->v);
        else if (u->count == 3) glUniform3fv(loc, 1, u->v);
        else if (u->count == 4) glUniform4fv(loc, 1, u->v);
        else glUniform4fv(loc, u->count / 4, u->v); /* vec4 arrays */
    }
}

void nw_engine_frame(nw_engine *e, const nw_frame_in *in) {
    GLint screen = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &screen);
    /* buffers cover the whole span: they're read at fragCoord/iResolution in
     * span coordinates, so an output-sized buffer would be read off its end */
    bool spanned = in->span_w > 0 && in->span_h > 0;
    ensure_targets(e, spanned ? in->span_w : in->width, spanned ? in->span_h : in->height);

    if (in->audio_spectrum && in->audio_waveform && e->uses_audio) {
        glBindTexture(GL_TEXTURE_2D, e->tex_audio);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, AUDIO_W, 1, GL_RED, GL_FLOAT, in->audio_spectrum);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, AUDIO_W, 1, GL_RED, GL_FLOAT, in->audio_waveform);
    }
    if (in->keyboard && e->uses_keyboard) {
        glBindTexture(GL_TEXTURE_2D, e->tex_keyboard);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 3, GL_RED, GL_UNSIGNED_BYTE, in->keyboard);
    }

    glBindVertexArray(e->vao);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    for (size_t i = 0; i < e->passes.len; i++) {
        pass_gl *g = &e->passes.data[i];
        bool is_image = (int)i == e->src->image_pass;
        int w = in->width, h = in->height;
        if (is_image) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)screen);
        } else {
            if (!g->target) continue;
            nw_gfx_target_bind(&g->target->t);
            w = g->w;
            h = g->h;
        }
        glViewport(0, 0, w, h);
        glUseProgram(g->prog);
        set_uniforms(e, i, g, in, w, h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (!is_image && e->src->passes.data[i].filter == NW_FILTER_MIPMAP) {
            glBindTexture(GL_TEXTURE_2D, nw_target_write_tex(g->target));
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }
    for (size_t i = 0; i < e->passes.len; i++)
        if (e->passes.data[i].target) nw_gfx_target_swap(&e->passes.data[i].target->t);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)screen);
    glBindVertexArray(0);
}
