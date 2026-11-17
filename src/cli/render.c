/* `neowall render` — draw a shader offscreen: no compositor, no daemon.
 *
 *   neowall render tokyo_night.glsl                      → tokyo_night.png
 *   neowall render x.glsl -o shot.png --size 1920x1080 --time 12
 *   neowall render x.glsl --at 19:30 --set iAudioBeat=1  (dusk, on the beat)
 *   neowall render x.glsl --sheet 6                      (contact sheet over time)
 *   neowall render x.glsl --bench                        (GPU ms per pass)
 *   neowall render x.glsl --check                        (NaN/Inf per pass)
 *   neowall render x.glsl --passes                       (every buffer as a PNG)
 *
 * Everything runs on a headless EGL context (EGL device → surfaceless →
 * default display), so it works over SSH, in CI, and next to a running daemon
 * without touching its outputs or its state file. Reactive inputs are pinned
 * to calm, deterministic values (or the real ones with --live), so the same
 * command gives the same picture. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <errno.h>
#include <math.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "neowall/cli/cli.h"
#include "neowall/core/log.h"
#include "neowall/core/wall_clock.h"
#include "neowall/output/state_file.h"
#include "neowall/shader/engine.h"
#include "neowall/shader/shader.h"
#include "neowall/shader/wallshader.h"
#include "neowall/source/reactive.h"

#ifndef EGL_PLATFORM_DEVICE_EXT
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

/* ── options ─────────────────────────────────────────────────────────── */

typedef struct {
    const char *shader;
    char out[4096];
    int w, h;
    double time;    /* iTime of the (last) frame */
    double fps;     /* simulation step */
    int frames;     /* frames simulated up to `time`; 0 = derive */
    int sheet;      /* N > 0: N frames spread over [0, time] in one PNG */
    bool bench, check, passes, live, quiet;
    bool have_at;
    int at_h, at_m;
    float mouse[2]; /* normalised; <0 = none */
    int n_set;
    char set_name[WALLSHADER_MAX_OVERRIDES][48];
    float set_value[WALLSHADER_MAX_OVERRIDES];
} render_opts;

static void render_usage(FILE *f) {
    fprintf(f,
            "Usage: neowall render <shader.glsl|.neowall> [options]\n"
            "\n"
            "Draw a shader offscreen and save a PNG. No compositor or daemon needed;\n"
            "safe to run while the daemon is up (it never touches its outputs).\n"
            "\n"
            "Output\n"
            "  -o, --output FILE     PNG to write (default: <shader>.png in the cwd)\n"
            "  -s, --size WxH        resolution (default 1920x1080)\n"
            "      --sheet N         contact sheet: N moments spread over 0..--time\n"
            "      --passes          also save every buffer pass (FILE.<pass>.png)\n"
            "\n"
            "Time & inputs (deterministic unless --live)\n"
            "  -t, --time SEC        iTime of the frame (default 3)\n"
            "      --fps N           simulation rate for feedback buffers (default 30)\n"
            "      --frames N        frames simulated to reach --time (default time*fps)\n"
            "      --at HH:MM        local time of day (iTimeOfDay, iSun, iDate)\n"
            "      --mouse X,Y       pointer, 0..1 (iMouse)\n"
            "      --set NAME=VALUE  pin any float uniform (iAudioBeat=1, iCpu=0.8, uMy=2)\n"
            "      --live            use the real CPU/audio/battery readings instead\n"
            "\n"
            "Diagnostics\n"
            "      --bench           GPU time per pass, and what it costs at 30/60 fps\n"
            "      --check           find NaN/Inf pixels in every pass (exit 2 if any)\n"
            "  -q, --quiet           only errors\n"
            "\n"
            "Exit: 0 ok, 1 load/compile error, 2 --check found NaN/Inf, 77 no GPU.\n");
}

static bool parse_size(const char *s, int *w, int *h) {
    return sscanf(s, "%dx%d", w, h) == 2 && *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
}

static int parse_args(int argc, char **argv, render_opts *o) {
    memset(o, 0, sizeof(*o));
    o->w = 1920, o->h = 1080, o->time = 3.0, o->fps = 30.0;
    o->mouse[0] = o->mouse[1] = -1.0f;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
#define NEXT()                                                       \
    do {                                                             \
        if (i + 1 >= argc) {                                         \
            fprintf(stderr, "neowall render: %s needs a value\n", a); \
            return -1;                                               \
        }                                                            \
        v = argv[++i];                                               \
    } while (0)
        const char *v = NULL;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            render_usage(stdout);
            return 1;
        } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
            NEXT();
            snprintf(o->out, sizeof(o->out), "%s", v);
        } else if (!strcmp(a, "-s") || !strcmp(a, "--size")) {
            NEXT();
            if (!parse_size(v, &o->w, &o->h)) {
                fprintf(stderr, "neowall render: bad --size '%s' (want WxH)\n", v);
                return -1;
            }
        } else if (!strcmp(a, "-t") || !strcmp(a, "--time")) {
            NEXT();
            o->time = atof(v);
            if (o->time < 0) o->time = 0;
        } else if (!strcmp(a, "--fps")) {
            NEXT();
            o->fps = atof(v);
            if (!(o->fps > 0 && o->fps <= 1000)) o->fps = 30;
        } else if (!strcmp(a, "--frames")) {
            NEXT();
            o->frames = atoi(v);
        } else if (!strcmp(a, "--sheet")) {
            NEXT();
            o->sheet = atoi(v);
            if (o->sheet < 1 || o->sheet > 64) {
                fprintf(stderr, "neowall render: --sheet wants 1..64\n");
                return -1;
            }
        } else if (!strcmp(a, "--at")) {
            NEXT();
            if (sscanf(v, "%d:%d", &o->at_h, &o->at_m) != 2 || o->at_h < 0 || o->at_h > 23 ||
                o->at_m < 0 || o->at_m > 59) {
                fprintf(stderr, "neowall render: bad --at '%s' (want HH:MM)\n", v);
                return -1;
            }
            o->have_at = true;
        } else if (!strcmp(a, "--mouse")) {
            NEXT();
            if (sscanf(v, "%f,%f", &o->mouse[0], &o->mouse[1]) != 2) {
                fprintf(stderr, "neowall render: bad --mouse '%s' (want X,Y in 0..1)\n", v);
                return -1;
            }
        } else if (!strcmp(a, "--set")) {
            NEXT();
            const char *eq = strchr(v, '=');
            size_t n       = eq ? (size_t)(eq - v) : 0;
            if (!eq || n == 0 || n >= sizeof(o->set_name[0]) || o->n_set >= WALLSHADER_MAX_OVERRIDES) {
                fprintf(stderr, "neowall render: bad --set '%s' (want NAME=VALUE)\n", v);
                return -1;
            }
            memcpy(o->set_name[o->n_set], v, n);
            o->set_name[o->n_set][n] = '\0';
            o->set_value[o->n_set++] = (float)atof(eq + 1);
        } else if (!strcmp(a, "--bench")) {
            o->bench = true;
        } else if (!strcmp(a, "--check")) {
            o->check = true;
        } else if (!strcmp(a, "--passes")) {
            o->passes = true;
        } else if (!strcmp(a, "--live")) {
            o->live = true;
        } else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
            o->quiet = true;
        } else if (a[0] == '-') {
            fprintf(stderr, "neowall render: unknown option '%s' (see --help)\n", a);
            return -1;
        } else if (!o->shader) {
            o->shader = a;
        } else {
            fprintf(stderr, "neowall render: one shader at a time ('%s'?)\n", a);
            return -1;
        }
#undef NEXT
    }
    if (!o->shader) {
        render_usage(stderr);
        return -1;
    }
    if (!o->out[0]) {
        const char *base = strrchr(o->shader, '/');
        base             = base ? base + 1 : o->shader;
        const char *dot  = strrchr(base, '.');
        size_t n         = dot ? (size_t)(dot - base) : strlen(base);
        snprintf(o->out, sizeof(o->out), "%.*s.png", (int)n, base);
    }
    if (o->frames <= 0) {
        int f     = (int)lround(o->time * o->fps);
        o->frames = f < 1 ? 1 : (f > 2000 ? 2000 : f);
    }
    return 0;
}

/* ── headless EGL ────────────────────────────────────────────────────── */

typedef struct {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;
    const char *how;
} headless;

static bool has_ext(const char *list, const char *name) {
    if (!list) return false;
    size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

static bool try_context(headless *h) {
    if (!eglInitialize(h->dpy, NULL, NULL)) return false;
    if (!eglBindAPI(EGL_OPENGL_API)) goto fail;
    const char *ext = eglQueryString(h->dpy, EGL_EXTENSIONS);
    bool surfaceless = has_ext(ext, "EGL_KHR_surfaceless_context");
    const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                               EGL_OPENGL_BIT,   EGL_RED_SIZE,    8,
                               EGL_GREEN_SIZE,   8,               EGL_BLUE_SIZE,
                               8,                EGL_NONE};
    EGLConfig cfg = NULL;
    EGLint n      = 0;
    if (!eglChooseConfig(h->dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        cfg = NULL;
        if (!has_ext(ext, "EGL_KHR_no_config_context")) goto fail;
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION,
                               3,
                               EGL_CONTEXT_MINOR_VERSION,
                               3,
                               EGL_CONTEXT_OPENGL_PROFILE_MASK,
                               EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                               EGL_NONE};
    h->ctx = eglCreateContext(h->dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (h->ctx == EGL_NO_CONTEXT) goto fail;
    h->surf = EGL_NO_SURFACE;
    if (!surfaceless) {
        if (!cfg) goto fail;
        const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        h->surf           = eglCreatePbufferSurface(h->dpy, cfg, pb);
        if (h->surf == EGL_NO_SURFACE) goto fail;
    }
    if (!eglMakeCurrent(h->dpy, h->surf, h->surf, h->ctx)) goto fail;
    return true;
fail:
    if (h->ctx != EGL_NO_CONTEXT) eglDestroyContext(h->dpy, h->ctx);
    h->ctx = EGL_NO_CONTEXT;
    eglTerminate(h->dpy);
    return false;
}

static const char *headless_init(headless *h) {
    memset(h, 0, sizeof(*h));
    const char *cext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    /* 1. a GPU device directly: NVIDIA and Mesa, no display server at all */
    if (get_dpy && has_ext(cext, "EGL_EXT_platform_device")) {
        PFNEGLQUERYDEVICESEXTPROC query =
            (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
        EGLDeviceEXT devs[8];
        EGLint nd = 0;
        if (query && query(8, devs, &nd)) {
            for (EGLint i = 0; i < nd; i++) {
                h->dpy = get_dpy(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
                if (h->dpy != EGL_NO_DISPLAY && try_context(h)) return (h->how = "EGL device"), NULL;
            }
        }
    }
    /* 2. Mesa's surfaceless platform */
    if (get_dpy && has_ext(cext, "EGL_MESA_platform_surfaceless")) {
        h->dpy = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
        if (h->dpy != EGL_NO_DISPLAY && try_context(h)) return (h->how = "surfaceless"), NULL;
    }
    /* 3. whatever the default display is (needs a running display server) */
    h->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (h->dpy != EGL_NO_DISPLAY && try_context(h)) return (h->how = "default display"), NULL;
    return "no usable EGL/OpenGL 3.3 context (tried EGL device, surfaceless, default display)";
}

static void headless_fini(headless *h) {
    if (h->dpy == EGL_NO_DISPLAY || !h->dpy) return;
    eglMakeCurrent(h->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (h->surf != EGL_NO_SURFACE) eglDestroySurface(h->dpy, h->surf);
    if (h->ctx != EGL_NO_CONTEXT) eglDestroyContext(h->dpy, h->ctx);
    eglTerminate(h->dpy);
}

/* ── pixels ──────────────────────────────────────────────────────────── */

/* RGBA float, bottom-up (GL order) → 8-bit RGB PNG, top-down. */
static bool write_png(const char *path, const float *rgba, int w, int h, int stride_px) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "neowall render: can't write %s: %s\n", path, strerror(errno));
        return false;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info  = png ? png_create_info_struct(png) : NULL;
    uint8_t *row    = malloc((size_t)w * 3);
    if (!png || !info || !row || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        free(row);
        fclose(f);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = h - 1; y >= 0; y--) {
        const float *src = rgba + (size_t)y * stride_px * 4;
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                float v = src[x * 4 + c];
                /* NaN shows magenta so it can't hide as "black" */
                if (!isfinite(v)) v = (c == 1) ? 0.0f : 1.0f;
                v                  = v < 0 ? 0 : (v > 1 ? 1 : v);
                row[x * 3 + c]     = (uint8_t)lrintf(v * 255.0f);
            }
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row);
    return fclose(f) == 0;
}

typedef struct {
    long nan, inf;
    int x0, y0, x1, y1; /* top-down bounding box of bad pixels */
} badness;

static badness scan(const float *rgba, int w, int h) {
    badness b = {0, 0, w, h, -1, -1};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float *p = rgba + ((size_t)y * w + x) * 4;
            bool nan = false, inf = false;
            for (int c = 0; c < 4; c++) {
                if (isnan(p[c])) nan = true;
                else if (isinf(p[c])) inf = true;
            }
            if (!nan && !inf) continue;
            b.nan += nan;
            b.inf += inf;
            int ty = h - 1 - y;
            if (x < b.x0) b.x0 = x;
            if (x > b.x1) b.x1 = x;
            if (ty < b.y0) b.y0 = ty;
            if (ty > b.y1) b.y1 = ty;
        }
    return b;
}

static float *read_texture(GLuint tex, int w, int h) {
    float *px = malloc((size_t)w * h * 4 * sizeof(float));
    if (!px) return NULL;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px);
    return px;
}

static void pass_filename(char *dst, size_t cap, const char *out, const char *pass) {
    char clean[64];
    size_t j = 0;
    for (size_t i = 0; pass[i] && j + 1 < sizeof(clean); i++) {
        char c = pass[i];
        clean[j++] = (c == ' ' || c == '/') ? '_' : (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    clean[j]        = '\0';
    const char *dot = strrchr(out, '.');
    size_t n        = dot ? (size_t)(dot - out) : strlen(out);
    snprintf(dst, cap, "%.*s.%s.png", (int)n, out, clean);
}

/* ── the run ─────────────────────────────────────────────────────────── */

/* iTimeOfDay / iSun / iDayFraction for a wall-clock instant, the way the
 * reactive sampler computes them (src/source/reactive.c: sample_time). */
static void pin_time_of_day(wallshader *s, double wall) {
    time_t t = (time_t)floor(wall);
    struct tm tm;
    localtime_r(&t, &tm);
    float secs = tm.tm_hour * 3600.0f + tm.tm_min * 60.0f + tm.tm_sec + (float)(wall - (double)t);
    float h    = secs / 3600.0f;
    float sun  = cosf((h - 13.0f) * (float)M_PI / 12.0f);
    sun        = (sun + 0.15f) / 1.15f;
    sun        = sun < 0 ? 0 : (sun > 1 ? 1 : sun);
    wallshader_set_override(s, "iTimeOfDay", secs / 86400.0f);
    wallshader_set_override(s, "iSun", sun);
    wallshader_set_override(s, "iDayFraction", (float)tm.tm_yday / 365.0f);
}

static void pin_calm_inputs(wallshader *s) {
    /* an idle machine on mains power, nothing playing */
    static const struct {
        const char *n;
        float v;
    } calm[] = {{"iBattery", 1.0f},     {"iCharging", 1.0f}, {"iCpu", 0.15f},
                {"iRam", 0.35f},        {"iLoad", 0.15f},    {"iCpuTemp", 0.25f},
                {"iCpuTempC", 46.0f},   {"iGpu", 0.10f},     {"iAudioActive", 0.0f},
                {"iAudioLevel", 0.0f},  {"iAudioBass", 0.0f}, {"iAudioMid", 0.0f},
                {"iAudioTreble", 0.0f}, {"iAudioBeat", 0.0f}};
    for (size_t i = 0; i < sizeof(calm) / sizeof(calm[0]); i++)
        wallshader_set_override(s, calm[i].n, calm[i].v);
}

typedef struct {
    double ms_sum[64];
    int ms_n;
} bench_acc;

int cmd_render(int argc, char *argv[]) {
    render_opts o;
    int pr = parse_args(argc, argv, &o);
    if (pr) return pr > 0 ? 0 : 1;

    log_set_level(o.quiet ? LOG_LEVEL_ERROR : LOG_LEVEL_WARN);
    state_file_set_readonly(true); /* an offline render is nobody's wallpaper */

    if (o.have_at) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        long cur  = tm.tm_hour * 3600L + tm.tm_min * 60L + tm.tm_sec;
        long want = o.at_h * 3600L + o.at_m * 60L;
        nw_clock_set_offset(want - cur);
    }

    headless hl;
    const char *err = headless_init(&hl);
    if (err) {
        fprintf(stderr, "neowall render: %s\n", err);
        return 77;
    }
    if (o.live) reactive_init();

    char resolved[4096];
    /* a file you point at wins (cwd-relative too, with or without extension);
     * a bare name like `tokyo_night` is then looked up in the shader dirs */
    const char *path = o.shader;
    if (realpath(o.shader, resolved)) {
        path = resolved;
    } else if (!strchr(o.shader, '.')) {
        static const char *const exts[] = {".neowall", ".glsl"};
        for (size_t i = 0; i < 2; i++) {
            char with[4200];
            snprintf(with, sizeof(with), "%s%s", o.shader, exts[i]);
            if (realpath(with, resolved)) {
                path = resolved;
                break;
            }
        }
    }

    wallshader *s = NULL;
    nw_result r   = wallshader_load(path, &s);
    if (nw_is_err(r) || !s) {
        const char *e = wallshader_last_errors();
        if (r.status == NW_ERR_IO)
            fprintf(stderr, "neowall render: can't find '%s' (a path, or a name in "
                            "~/.config/neowall/shaders)\n", o.shader);
        else
            fprintf(stderr, "neowall render: %s failed to compile\n%s", o.shader, e && *e ? e : "");
        headless_fini(&hl);
        return 1;
    }
    wallshader_set_persist(s, false);
    nw_engine *eng = wallshader_engine(s);
    int npass      = nw_engine_pass_count(eng);
    if (!o.live) pin_calm_inputs(s);
    for (int i = 0; i < o.n_set; i++) wallshader_set_override(s, o.set_name[i], o.set_value[i]);
    if (o.bench) nw_engine_set_profiling(eng, true);

    /* RGBA16F target: keeps NaN/Inf (an RGBA8 one would clamp them to black) */
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, o.w, o.h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "neowall render: framebuffer %dx%d not supported\n", o.w, o.h);
        wallshader_free(s);
        headless_fini(&hl);
        return 1;
    }

    if (!o.quiet)
        printf("%s  %dx%d  %d pass%s  [%s, %s]\n", o.shader, o.w, o.h, npass, npass == 1 ? "" : "es",
               (const char *)glGetString(GL_RENDERER), hl.how);

    /* sheet: render N moments into a grid; otherwise one frame at --time */
    int shots    = o.sheet > 0 ? o.sheet : 1;
    int cols     = shots == 1 ? 1 : (shots <= 4 ? 2 : (shots <= 9 ? 3 : 4));
    int rows     = (shots + cols - 1) / cols;
    int sw       = o.w, sh = o.h;
    float *sheet = NULL;
    if (shots > 1) {
        sheet = calloc((size_t)sw * cols * sh * rows * 4, sizeof(float));
        if (!sheet) {
            fprintf(stderr, "neowall render: out of memory for a %dx%d sheet\n", sw * cols, sh * rows);
            return 1;
        }
    }

    bench_acc ba     = {0};
    badness worst[64];
    int worst_frame[64];
    for (int i = 0; i < 64; i++) worst[i] = (badness){0, 0, 0, 0, -1, -1}, worst_frame[i] = -1;
    float *last = NULL;
    double step = 1.0 / o.fps;
    double base = 1000.0; /* fake monotonic "present" clock: exact dt every frame */
    int total   = o.frames;
    int shot    = 0;
    double wall0 = nw_clock_now_f();
    for (int f = 0; f < total; f++) {
        double t = total == 1 ? o.time : o.time * (double)f / (double)(total - 1);
        if (o.have_at || !o.live) pin_time_of_day(s, wall0 + t);
        wallshader_frame fr = {.width = o.w, .height = o.h, .time = t, .present_at = base + f * step};
        if (o.mouse[0] >= 0) {
            fr.mouse[0] = o.mouse[0] * (float)o.w;
            fr.mouse[1] = o.mouse[1] * (float)o.h;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, o.w, o.h);
        wallshader_draw(s, &fr);

        if (o.bench && f >= 3 && npass <= 64) {
            for (int p = 0; p < npass; p++) {
                double ms = nw_engine_pass_ms(eng, p);
                if (ms >= 0) ba.ms_sum[p] += ms;
            }
            ba.ms_n++;
        }
        if (o.check && npass <= 64 && (f % 10 == 0 || f == total - 1)) {
            for (int p = 0; p < npass; p++) {
                int pw = 0, ph = 0;
                GLuint pt = nw_engine_pass_texture(eng, p, &pw, &ph);
                if (!pt) pt = tex, pw = o.w, ph = o.h; /* the image pass */
                float *px = read_texture(pt, pw, ph);
                if (!px) continue;
                badness b = scan(px, pw, ph);
                if (b.nan + b.inf > worst[p].nan + worst[p].inf) worst[p] = b, worst_frame[p] = f;
                free(px);
            }
        }
        bool want_shot = shots > 1 ? (f == (int)lround((double)shot * (total - 1) / (double)(shots - 1)))
                                   : (f == total - 1);
        if (want_shot) {
            float *px = read_texture(tex, o.w, o.h);
            if (px && shots > 1) {
                int cx = shot % cols, cy = rows - 1 - shot / cols; /* GL rows are bottom-up */
                for (int y = 0; y < sh; y++)
                    memcpy(sheet + (((size_t)(cy * sh + y) * sw * cols) + (size_t)cx * sw) * 4,
                           px + (size_t)y * sw * 4, (size_t)sw * 4 * sizeof(float));
                free(px);
            } else {
                free(last);
                last = px;
            }
            shot++;
        }
    }
    glFinish();

    int rc = 0;
    bool ok = shots > 1 ? write_png(o.out, sheet, sw * cols, sh * rows, sw * cols)
                        : (last && write_png(o.out, last, o.w, o.h, o.w));
    if (!ok) rc = 1;
    else if (!o.quiet)
        printf("wrote %s%s\n", o.out, shots > 1 ? " (contact sheet)" : "");

    if (o.passes) {
        for (int p = 0; p < npass; p++) {
            int pw = 0, ph = 0;
            GLuint pt = nw_engine_pass_texture(eng, p, &pw, &ph);
            if (!pt) continue;
            float *px = read_texture(pt, pw, ph);
            char fn[4200];
            pass_filename(fn, sizeof(fn), o.out, nw_engine_pass_name(eng, p));
            if (px && write_png(fn, px, pw, ph, pw) && !o.quiet) printf("wrote %s  (%dx%d)\n", fn, pw, ph);
            free(px);
        }
    }

    if (o.bench && ba.ms_n > 0) {
        double tot = 0;
        printf("\nGPU time per frame (%dx%d, mean of %d frames):\n", o.w, o.h, ba.ms_n);
        for (int p = 0; p < npass && p < 64; p++) {
            double ms = ba.ms_sum[p] / ba.ms_n;
            tot += ms;
            printf("  %-14s %7.2f ms\n", nw_engine_pass_name(eng, p), ms);
        }
        printf("  %-14s %7.2f ms\n", "total", tot);
        printf("  busy: %4.0f%% of the GPU at 30 fps, %4.0f%% at 60 fps\n", tot * 30.0 / 10.0,
               tot * 60.0 / 10.0);
        if (tot * 30.0 / 10.0 > 25.0)
            printf("  tip: `shader_fps 20`, fewer nwFbm octaves, or `scale 0.5` on blur passes\n");
    }

    if (o.check) {
        bool any = false;
        printf("\nNaN/Inf check (%d frames):\n", total);
        for (int p = 0; p < npass && p < 64; p++) {
            badness *b = &worst[p];
            if (b->nan + b->inf == 0) {
                printf("  %-14s clean\n", nw_engine_pass_name(eng, p));
                continue;
            }
            any = true;
            printf("  %-14s %ld NaN, %ld Inf  (frame %d, t=%.2fs)  box x=%d..%d y=%d..%d (top-down px)\n",
                   nw_engine_pass_name(eng, p), b->nan, b->inf, worst_frame[p],
                   total == 1 ? o.time : o.time * worst_frame[p] / (double)(total - 1), b->x0, b->x1,
                   b->y0, b->y1);
        }
        if (any) {
            printf("  → the first pass listed is where it starts; later passes inherit it.\n"
                   "    Usual suspects: x/0, sqrt/log/pow of negatives, normalize(vec(0)),\n"
                   "    exp() of a large positive number. NaN pixels are magenta in the PNG.\n");
            rc = rc ? rc : 2;
        }
    }

    free(last);
    free(sheet);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    wallshader_free(s);
    if (o.live) reactive_shutdown();
    headless_fini(&hl);
    return rc;
}
