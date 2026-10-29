/* The output and render layers, end to end, with no display.
 *
 * A fake compositor backend hands out pbuffer surfaces on a surfaceless Mesa
 * (llvmpipe) EGL display, so output.c and render.c run unchanged: config
 * applied, images decoded and uploaded, drawn with each fit mode, a shader on
 * the same output, a directory cycle stepping, and a bad image leaving the
 * old one on screen. Pixels are read back. Skips (77) without software GL.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "neowall/neowall.h"
#include "neowall/compositor/compositor.h"
#include "neowall/output/output.h"
#include "neowall/config/config.h"
#include "hyprland_coverage.h"

/* ---- the daemon-side pieces this test doesn't link ---- */
void app_request_output_redraw(struct output_state *o) {
    if (o) atomic_store(&o->needs_redraw, true);
}
int hyprland_output_windows(const struct output_state *o, nw_window_rect *out, int max) {
    (void)o; (void)out; (void)max;
    return 0;
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { W = 80, H = 40 };
static EGLDisplay g_dpy;
static EGLConfig g_cfg;

/* ---- a backend whose "windows" are pbuffers ---- */
static struct compositor_surface *fake_create_surface(void *data, const compositor_surface_config_t *c) {
    (void)data; (void)c;
    struct compositor_surface *s = calloc(1, sizeof(*s));
    if (s) s->egl_surface = EGL_NO_SURFACE;
    return s;
}
static void fake_destroy_surface(struct compositor_surface *s) { free(s); }
static bool fake_create_egl_window(struct compositor_surface *s, int32_t w, int32_t h) {
    s->width = w;
    s->height = h;
    return true;
}
static void fake_destroy_egl_window(struct compositor_surface *s) { (void)s; }

static const compositor_backend_ops_t fake_ops = {
    .create_surface = fake_create_surface,
    .destroy_surface = fake_destroy_surface,
    .create_egl_window = fake_create_egl_window,
    .destroy_egl_window = fake_destroy_egl_window,
};
static struct compositor_backend fake_backend = {.name = "fake", .ops = &fake_ops};

static const char *egl_up(void) {
    setenv("__EGL_VENDOR_LIBRARY_FILENAMES", "/usr/share/glvnd/egl_vendor.d/50_mesa.json", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    setenv("LP_NUM_THREADS", "0", 0);   /* llvmpipe threads aren't TSan-instrumented */
    PFNEGLGETPLATFORMDISPLAYEXTPROC get =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get) return "no eglGetPlatformDisplayEXT";
    g_dpy = get(0x31DD /* SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, NULL, NULL)) return "no surfaceless display";
    eglBindAPI(EGL_OPENGL_API);
    const EGLint a[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, a, &g_cfg, 1, &n) || n < 1) return "no pbuffer config";
    return NULL;
}

/* compositor_surface_create_egl calls eglCreateWindowSurface, which a
 * surfaceless display can't do; give the output a pbuffer instead. */
static bool attach_pbuffer(struct output_state *o) {
    const EGLint pa[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(g_dpy, g_cfg, pa);
    if (s == EGL_NO_SURFACE) return false;
    o->compositor_surface->egl_surface = s;
    o->compositor_surface->width = W;
    o->compositor_surface->height = H;
    return eglMakeCurrent(g_dpy, s, s, o->state->egl_context);
}

/* A w x h PNG with a left half of `l` and a right half of `r` (0xRRGGBB). */
static void write_png(const char *path, int w, int h, unsigned l, unsigned r) {
    FILE *f = fopen(path, "wb");
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, i, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, i);
    unsigned char *row = malloc((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned c = x < w / 2 ? l : r;
            row[x * 3] = (unsigned char)(c >> 16);
            row[x * 3 + 1] = (unsigned char)(c >> 8);
            row[x * 3 + 2] = (unsigned char)c;
        }
        png_write_row(p, row);
    }
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &i);
    free(row);
    fclose(f);
}

static unsigned read_rgb(int x, int y) {
    unsigned char px[4] = {0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return (unsigned)px[0] << 16 | (unsigned)px[1] << 8 | px[2];
}

static bool close_rgb(unsigned a, unsigned b) {
    for (int s = 0; s < 24; s += 8) {
        int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
        if (d < -3 || d > 3) return false;
    }
    return true;
}

static bool draw(struct output_state *o) {
    atomic_store(&o->needs_redraw, true);
    bool ok = output_render_frame(o);
    glFinish();
    return ok;
}

int main(void) {
    const char *err = egl_up();
    if (err) {
        printf("skip: %s\n", err);
        return 77;
    }
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                         EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};

    struct neowall_state st;
    memset(&st, 0, sizeof(st));
    pthread_rwlock_init(&st.output_list_lock, NULL);
    pthread_mutex_init(&st.state_mutex, NULL);
    pthread_mutex_init(&st.state_file_lock, NULL);
    st.egl_display = g_dpy;
    st.egl_config = g_cfg;
    st.egl_context = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ca);
    st.compositor_backend = &fake_backend;
    atomic_store(&st.running, true);
    if (st.egl_context == EGL_NO_CONTEXT) {
        printf("skip: no GL 3.3 context\n");
        return 77;
    }

    char dir[] = "/tmp/nw-render-test-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    setenv("HOME", dir, 1);
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("XDG_STATE_HOME", dir, 1);
    char red_blue[600], green[600], cyc[600], bad[600], shader[600];
    snprintf(red_blue, sizeof(red_blue), "%s/rb.png", dir);
    snprintf(cyc, sizeof(cyc), "%s/cycle", dir);
    mkdir(cyc, 0755);
    snprintf(green, sizeof(green), "%s/cycle/1-green.png", dir);
    snprintf(bad, sizeof(bad), "%s/bad.png", dir);
    snprintf(shader, sizeof(shader), "%s/s.glsl", dir);
    write_png(red_blue, 160, 80, 0xff0000, 0x0000ff);
    write_png(green, 32, 32, 0x00ff00, 0x00ff00);
    char cyc2[700];
    snprintf(cyc2, sizeof(cyc2), "%s/cycle/2-white.png", dir);
    write_png(cyc2, 32, 32, 0xffffff, 0xffffff);
    FILE *f = fopen(bad, "w"); fputs("not a png", f); fclose(f);
    f = fopen(shader, "w");
    fputs("void mainImage(out vec4 c, in vec2 p){ c = vec4(1.0, 0.5, 0.0, 1.0); }\n", f);
    fclose(f);

    struct output_state *o = output_create(&st, NULL, 1);
    CHECK(o != NULL, "output_create");
    if (!o) return 1;
    st.outputs = o;
    st.output_count = 1;
    snprintf(o->connector_name, sizeof(o->connector_name), "TEST-1");
    snprintf(o->model, sizeof(o->model), "Test");
    o->width = o->pixel_width = o->logical_width = W;
    o->height = o->pixel_height = o->logical_height = H;
    o->configured = true;

    compositor_surface_config_t sc = compositor_surface_config_default(NULL);
    o->compositor_surface = compositor_surface_create(&fake_backend, &sc);
    CHECK(o->compositor_surface && attach_pbuffer(o), "surface + pbuffer");
    CHECK(output_init_render(o), "output_init_render");

    /* 1. A stretched image: left half red, right half blue. */
    struct wallpaper_config c;
    memset(&c, 0, sizeof(c));
    c.type = WALLPAPER_IMAGE;
    c.mode = MODE_STRETCH;
    c.transition = TRANSITION_NONE;
    c.shader_speed = 1.0f;
    c.vsync = true;
    snprintf(c.path, sizeof(c.path), "%s", red_blue);
    CHECK(output_apply_config(o, &c), "apply image config");
    CHECK(draw(o), "render image");
    CHECK(close_rgb(read_rgb(5, H / 2), 0xff0000), "left is red: %06x", read_rgb(5, H / 2));
    CHECK(close_rgb(read_rgb(W - 5, H / 2), 0x0000ff), "right is blue: %06x", read_rgb(W - 5, H / 2));

    /* 2. Fit mode keeps the 2:1 aspect on a 2:1 output: still edge to edge. */
    c.mode = MODE_FIT;
    CHECK(output_apply_config(o, &c), "apply fit");
    CHECK(draw(o), "render fit");
    CHECK(close_rgb(read_rgb(5, H / 2), 0xff0000), "fit: left red %06x", read_rgb(5, H / 2));

    /* 3. A broken image is refused and the old one stays on screen. */
    snprintf(c.path, sizeof(c.path), "%s", bad);
    output_apply_config(o, &c);
    CHECK(draw(o), "render after bad image");
    CHECK(close_rgb(read_rgb(5, H / 2), 0xff0000), "old image kept: %06x", read_rgb(5, H / 2));

    /* 4. A shader on the same output. */
    struct wallpaper_config s;
    memset(&s, 0, sizeof(s));
    s.type = WALLPAPER_SHADER;
    s.shader_speed = 1.0f;
    s.vsync = true;
    snprintf(s.shader_path, sizeof(s.shader_path), "%s", shader);
    CHECK(output_apply_config(o, &s), "apply shader");
    CHECK(draw(o), "render shader");
    CHECK(close_rgb(read_rgb(W / 2, H / 2), 0xff8000), "shader orange: %06x", read_rgb(W / 2, H / 2));

    /* 4b. Something else on the shared context (transitions, the FPS
     *     watermark, another output) leaves blending on between frames. A
     *     half-transparent shader must still draw opaque over whatever was
     *     there: the shader path has to set the state it needs, not trust a
     *     cache of what it set last frame. */
    FILE *ts = fopen(shader, "w");
    fputs("void mainImage(out vec4 c, in vec2 p){ c = vec4(1.0, 0.0, 0.0, 0.25); }\n", ts);
    fclose(ts);
    CHECK(output_apply_config(o, &s), "apply translucent shader");
    CHECK(draw(o), "render translucent shader");
    /* a swap crossfades from the old shader; that's covered in 4c */
    CHECK(o->clock_fade_start > 0, "a shader swap starts a crossfade");
    o->clock_fade_start = 1;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    CHECK(draw(o), "render after foreign GL state");
    CHECK(close_rgb(read_rgb(W / 2, H / 2), 0xff0000),
          "shader draws opaque despite leftover blend: %06x", read_rgb(W / 2, H / 2));
    glDisable(GL_BLEND);

    /* 4c. Mid-crossfade the old shader still shows through; once it's done
     *     only the new one is left. */
    ts = fopen(shader, "w");
    fputs("void mainImage(out vec4 c, in vec2 p){ c = vec4(0.0, 0.0, 1.0, 1.0); }\n", ts);
    fclose(ts);
    CHECK(output_apply_config(o, &s), "apply blue shader");
    CHECK(draw(o), "first frame of the new shader");
    unsigned mid = read_rgb(W / 2, H / 2);
    CHECK((mid >> 16) > 0x80 && (mid & 0xff) < 0x80, "old red still shows at the start: %06x", mid);
    o->clock_fade_start = 1; /* long ago: the fade is over */
    CHECK(draw(o), "frame after the fade");
    CHECK(close_rgb(read_rgb(W / 2, H / 2), 0x0000ff), "only the new shader is left: %06x",
          read_rgb(W / 2, H / 2));
    CHECK(o->clock_fade_tex == 0, "fade texture released");

    /* 5. Back to images, from a directory cycle. */
    struct wallpaper_config d;
    memset(&d, 0, sizeof(d));
    d.type = WALLPAPER_IMAGE;
    d.mode = MODE_FILL;
    d.transition = TRANSITION_NONE;
    d.duration = 60;
    d.shader_speed = 1.0f;
    char *paths[2] = {green, cyc2};
    d.cycle = true;
    d.cycle_paths = paths;
    d.cycle_count = 2;
    snprintf(d.path, sizeof(d.path), "%s", green);
    CHECK(output_apply_config(o, &d), "apply cycle");
    CHECK(draw(o), "render cycle[0]");
    unsigned first = read_rgb(W / 2, H / 2);
    output_cycle_wallpaper(o);
    CHECK(draw(o), "render cycle[1]");
    unsigned second = read_rgb(W / 2, H / 2);
    CHECK(close_rgb(first, 0x00ff00) || close_rgb(first, 0xffffff), "cycle shows a cycle image: %06x", first);
    CHECK(!close_rgb(first, second), "cycling changes the picture: %06x -> %06x", first, second);

    /* 6. Every transition: start a cycle step with it, then draw at the
     *    start, the middle and the end. Solid green -> solid white (or back),
     *    so the ends are the two images and the middle is neither, except
     *    slides, whose middle is half of each. */
    static const struct { enum transition_type t; const char *name; } trans[] = {
        {TRANSITION_FADE, "fade"}, {TRANSITION_SLIDE_LEFT, "slide_left"},
        {TRANSITION_SLIDE_RIGHT, "slide_right"}, {TRANSITION_GLITCH, "glitch"},
        {TRANSITION_PIXELATE, "pixelate"},
    };
    for (size_t i = 0; i < sizeof(trans) / sizeof(trans[0]); i++) {
        d.transition = trans[i].t;
        d.transition_duration = 10.0f;   /* long: we drive progress by hand */
        CHECK(output_apply_config(o, &d), "apply cycle with %s", trans[i].name);
        CHECK(draw(o), "%s: settle", trans[i].name);
        unsigned from = read_rgb(W / 2, H / 2);
        output_cycle_wallpaper(o);
        CHECK(o->transition_start_time > 0, "%s: cycling starts a transition", trans[i].name);

        o->transition_progress = 0.0f;
        CHECK(draw(o), "%s: draw at 0", trans[i].name);
        unsigned at0 = read_rgb(W / 2, H / 2);
        o->transition_progress = 0.5f;
        CHECK(draw(o), "%s: draw at 0.5", trans[i].name);
        unsigned left = read_rgb(2, H / 2), right = read_rgb(W - 3, H / 2), mid = read_rgb(W / 2, H / 2);
        o->transition_progress = 1.0f;
        CHECK(draw(o), "%s: draw at 1", trans[i].name);
        unsigned at1 = read_rgb(W / 2, H / 2);

        CHECK(close_rgb(at0, from), "%s: starts on the old image (%06x vs %06x)", trans[i].name, at0, from);
        CHECK(!close_rgb(at1, from), "%s: ends on the new image (%06x)", trans[i].name, at1);
        if (trans[i].t == TRANSITION_SLIDE_LEFT || trans[i].t == TRANSITION_SLIDE_RIGHT) {
            CHECK(!close_rgb(left, right), "%s: halfway shows both images (%06x | %06x)",
                  trans[i].name, left, right);
        } else if (trans[i].t == TRANSITION_FADE) {
            CHECK(!close_rgb(mid, from) && !close_rgb(mid, at1),
                  "fade: halfway is a blend (%06x between %06x and %06x)", mid, from, at1);
        }
        CHECK(glGetError() == GL_NO_ERROR, "%s: no GL error", trans[i].name);
        o->transition_start_time = 0;   /* let the next one start clean */
    }

    output_destroy(o);
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(g_dpy, st.egl_context);
    eglTerminate(g_dpy);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fails++;

    printf("output_render: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
