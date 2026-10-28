/* The shader engine, rendering for real.
 *
 * A GL 3.3 core context with no display (Mesa surfaceless, llvmpipe), so the
 * whole path runs: parse -> wrap -> compile -> link -> uniforms -> buffer
 * passes with ping-pong feedback -> image pass. Pixels are read back and
 * checked. Skips (exit 77) where no software GL is installed.
 */
#define _POSIX_C_SOURCE 200809L
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_headless.h"
#include "neowall/shader/shader_multipass.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { W = 64, H = 32 };
static GLuint g_fbo, g_tex;

static void make_target(void) {
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
}

/* RGBA8 of pixel (x, y), y from the bottom like gl_FragCoord. */
static void pixel(int x, int y, unsigned char out[4]) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

static bool near(int got, int want) { return abs(got - want) <= 2; }

/* Build, compile and draw `frames` frames of `src` into the test target. */
static multipass_shader_t *run(const char *src, int frames, float t0) {
    multipass_shader_t *s = multipass_create(src);
    if (!s) return NULL;
    if (!multipass_init_gl(s, W, H) || !multipass_compile_all(s)) {
        char *err = multipass_get_all_errors(s);
        printf("  compile failed: %s\n", err ? err : "?");
        free(err);
        multipass_destroy(s);
        return NULL;
    }
    for (int f = 0; f < frames; f++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        multipass_render(s, t0 + (float)f / 60.0f, 0, 0, false);
    }
    glFinish();
    return s;
}

int main(void) {
    const char *err = gl_headless_init();
    if (err) {
        printf("skip: %s\n", err);
        return 77;
    }
    printf("GL: %s\n", glGetString(GL_RENDERER));
    make_target();
    unsigned char p[4];

    /* 1. A Shadertoy mainImage, wrapped and run: gradient from fragCoord. */
    multipass_shader_t *s = run(
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    c = vec4(f.x / iResolution.x, f.y / iResolution.y, 0.25, 1.0);\n"
        "}\n", 1, 0.0f);
    CHECK(s != NULL, "plain mainImage compiles");
    if (s) {
        pixel(0, 0, p);
        CHECK(near(p[0], 2) && near(p[1], 4) && near(p[2], 64), "bottom-left %d %d %d", p[0], p[1], p[2]);
        pixel(W - 1, H - 1, p);
        CHECK(near(p[0], 253) && near(p[1], 251) && near(p[2], 64), "top-right %d %d %d", p[0], p[1], p[2]);
        multipass_destroy(s);
    }

    /* 2. iTime reaches the shader. */
    s = run("void mainImage(out vec4 c, in vec2 f) { c = vec4(fract(iTime), 0, 0, 1); }\n", 1, 0.5f);
    CHECK(s != NULL, "iTime shader compiles");
    if (s) {
        pixel(5, 5, p);
        CHECK(near(p[0], 128), "iTime=0.5 gives red %d", p[0]);
        multipass_destroy(s);
    }

    /* 3. A buffer pass feeding the image, with self-feedback: each frame
     *    BufA adds 1/64 to itself, so after N frames red is N/64. */
    s = run(
        "// --- Buffer A ---\n"
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    vec4 prev = texelFetch(iChannel0, ivec2(f), 0);\n"
        "    c = vec4(prev.r + 1.0/64.0, 0, 0, 1);\n"
        "}\n"
        "// --- Image ---\n"
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    c = texelFetch(iChannel0, ivec2(f), 0);\n"
        "}\n", 16, 0.0f);
    CHECK(s != NULL, "two-pass feedback shader compiles");
    if (s) {
        CHECK(s->pass_count == 2 && s->has_buffers, "parsed %d passes", s->pass_count);
        pixel(10, 10, p);
        CHECK(near(p[0], 64), "16 feedback frames accumulate to ~64, got %d", p[0]);
        multipass_destroy(s);
    }

    /* 4. A broken shader fails cleanly, with an error that names the line. */
    multipass_shader_t *bad = multipass_create(
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    c = vec4(undefined_thing, 0, 0, 1);\n"
        "}\n");
    CHECK(bad != NULL, "bad shader still parses");
    if (bad) {
        bool ok = multipass_init_gl(bad, W, H) && multipass_compile_all(bad);
        CHECK(!ok, "bad shader doesn't compile");
        char *e = multipass_get_all_errors(bad);
        CHECK(e && strstr(e, "undefined_thing"), "error mentions the bad identifier: %s", e ? e : "(null)");
        free(e);
        multipass_destroy(bad);
    }

    /* 5. Every shipped shader compiles and draws a frame on llvmpipe. */
    const char *dir = getenv("NW_SHADER_DIR");
    if (dir) {
        static const char *names[] = {
            "audio_bars", "audio_pulse", "circadian_sky", "fireflies", "garden",
            "living_aurora", "moon_ocean", "mouse_ripples", "neon_city", "plasma_touch",
            "rain_window", "scene_demo", "starfield_warp", "star_next", "system_deck",
            "system_monitor", "time", "window_forge", "window_light",
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.glsl", dir, names[i]);
            FILE *f = fopen(path, "rb");
            if (!f) { CHECK(0, "missing %s", path); continue; }
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            rewind(f);
            char *src = malloc((size_t)n + 1);
            size_t got = src ? fread(src, 1, (size_t)n, f) : 0;
            fclose(f);
            if (!src) continue;
            src[got] = '\0';
            s = run(src, 2, 10.0f);
            CHECK(s != NULL, "%s compiles and renders", names[i]);
            if (s) {
                CHECK(glGetError() == GL_NO_ERROR, "%s leaves no GL error", names[i]);
                multipass_destroy(s);
            }
            free(src);
        }
    }

    glDeleteFramebuffers(1, &g_fbo);
    glDeleteTextures(1, &g_tex);
    gl_headless_fini();
    printf("multipass_gl: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
