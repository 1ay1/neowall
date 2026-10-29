/* The shader engine, rendering for real.
 *
 * A GL 3.3 core context with no display (Mesa surfaceless, llvmpipe), so the
 * whole path runs: parse -> assemble -> compile -> link -> uniforms -> buffer
 * passes with feedback -> image pass. Pixels are read back and checked.
 * Skips (exit 77) where no software GL is installed.
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
#include <unistd.h>

#include "gl_headless.h"
#include "neowall/log.h"
#include "neowall/shader/program_cache.h"
#include "neowall/shader/wallshader.h"

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

static void draw(wallshader *s, int frames, double t0, const wallshader_frame *base) {
    for (int f = 0; f < frames; f++) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        wallshader_frame fr = base ? *base : (wallshader_frame){0};
        fr.width = W;
        fr.height = H;
        fr.time = t0 + f / 60.0;
        wallshader_draw(s, &fr);
    }
    glFinish();
}

/* Build `src`, draw `frames` frames into the test target. */
static wallshader *run(const char *src, int frames, double t0) {
    wallshader *s = NULL;
    if (nw_is_err(wallshader_load_text(src, NULL, &s))) {
        printf("  compile failed:\n%s\n", wallshader_last_errors());
        return NULL;
    }
    draw(s, frames, t0, NULL);
    return s;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

int main(void) {
    const char *err = gl_headless_init();
    if (err) {
        printf("skip: %s\n", err);
        return 77;
    }
    printf("GL: %s\n", glGetString(GL_RENDERER));
    /* a private cache, so the run is the same every time */
    char cdir[] = "/tmp/nwcache-XXXXXX";
    if (mkdtemp(cdir)) setenv("XDG_CACHE_HOME", cdir, 1);
    log_quiet_errors(true);
    make_target();
    unsigned char p[4];

    /* 1. A Shadertoy mainImage: gradient from fragCoord. */
    wallshader *s = run(
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    c = vec4(f.x / iResolution.x, f.y / iResolution.y, 0.25, 1.0);\n"
        "}\n", 1, 0.0);
    CHECK(s != NULL, "plain mainImage compiles");
    if (s) {
        pixel(0, 0, p);
        CHECK(near(p[0], 2) && near(p[1], 4) && near(p[2], 64), "bottom-left %d %d %d", p[0], p[1], p[2]);
        pixel(W - 1, H - 1, p);
        CHECK(near(p[0], 253) && near(p[1], 251) && near(p[2], 64), "top-right %d %d %d", p[0], p[1], p[2]);
        CHECK(!wallshader_animated(s), "a gradient isn't animated");
        wallshader_free(s);
    }

    /* 2. iTime reaches the shader. */
    s = run("void mainImage(out vec4 c, in vec2 f) { c = vec4(fract(iTime), 0, 0, 1); }\n", 1, 0.5);
    CHECK(s != NULL, "iTime shader compiles");
    if (s) {
        pixel(5, 5, p);
        CHECK(near(p[0], 128), "iTime=0.5 gives red %d", p[0]);
        CHECK(wallshader_animated(s), "an iTime shader is animated");
        wallshader_free(s);
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
        "}\n", 16, 0.0);
    CHECK(s != NULL, "two-pass feedback shader compiles");
    if (s) {
        CHECK(wallshader_pass_count(s) == 2, "parsed %d passes", wallshader_pass_count(s));
        pixel(10, 10, p);
        CHECK(near(p[0], 64), "16 feedback frames accumulate to ~64, got %d", p[0]);
        wallshader_free(s);
    }

    /* 4. A broken shader fails cleanly, with an error mapped back to the
     *    author's file and line. */
    char bdir[] = "/tmp/nwgl-XXXXXX";
    if (mkdtemp(bdir)) {
        char bpath[256];
        snprintf(bpath, sizeof(bpath), "%s/broken.glsl", bdir);
        write_file(bpath, "void mainImage(out vec4 c, in vec2 f) {\n"
                          "    c = vec4(undefined_thing, 0, 0, 1) * iTimee;\n"
                          "}\n");
        wallshader *bad = NULL;
        nw_result br = wallshader_load(bpath, &bad);
        CHECK(nw_is_err(br) && !bad, "bad shader doesn't compile");
        const char *e = wallshader_last_errors();
        CHECK(strstr(e, "broken.glsl:2:") && strstr(e, "undefined_thing") && strstr(e, "^"),
              "error is file:line with a caret: %s", e);
        CHECK(strstr(e, "did you mean iTime?"), "error suggests iTime: %s", e);
        unlink(bpath);
        rmdir(bdir);
    }
    nw_result r;

    /* 5. Spanning: the image pass sees the whole span as iResolution and
     *    is shifted by its offset, so two outputs draw one scene. */
    s = run("void mainImage(out vec4 c, in vec2 f) { c = vec4(f.x / iResolution.x, 0, 0, 1); }\n", 0, 0);
    CHECK(s != NULL, "span shader compiles");
    if (s) {
        wallshader_frame span = {.span_w = W * 2, .span_h = H, .span_x = W, .span_y = 0};
        draw(s, 1, 0, &span);
        pixel(0, 0, p);
        /* right half of a 128-wide scene: x=64.5 -> 0.504 */
        CHECK(near(p[0], 128), "right output starts mid-scene, got %d", p[0]);
        wallshader_free(s);
    }

    /* 6. Reactive and window uniforms resolve and draw (values are live
     *    system data, so only check they're in range). */
    s = run("void mainImage(out vec4 c, in vec2 f) {\n"
            "    c = vec4(clamp(iCpu, 0.0, 1.0), float(iWindowCount) / 16.0, iSun, 1);\n"
            "}\n", 1, 0);
    CHECK(s != NULL, "reactive uniforms compile");
    if (s) {
        float rect[4] = {0, 0, 10, 10};
        wallshader_frame wf = {.windows = rect, .window_count = 1};
        draw(s, 1, 0, &wf);
        pixel(1, 1, p);
        CHECK(near(p[1], 16), "iWindowCount=1 reaches the shader, got %d", p[1]);
        wallshader_free(s);
    }

    /* 7. A manifest: channel bindings and a const uniform from the sidecar. */
    char dir[] = "/tmp/nwgl-XXXXXX";
    if (mkdtemp(dir)) {
        char glsl[256], man[256];
        snprintf(glsl, sizeof(glsl), "%s/m.glsl", dir);
        snprintf(man, sizeof(man), "%s/m.neowall", dir);
        write_file(glsl, "void mainImage(out vec4 c, in vec2 f) { c = vec4(uLevel, 0, 0, 1); }\n");
        write_file(man, "uniforms {\n  uLevel 0.5\n}\n");
        s = NULL;
        r = wallshader_load(glsl, &s);
        CHECK(nw_is_ok(r), "manifest shader loads: %s", wallshader_last_errors());
        if (s) {
            draw(s, 1, 0, NULL);
            pixel(3, 3, p);
            CHECK(near(p[0], 128), "manifest const uniform reaches the shader, got %d", p[0]);
            wallshader_free(s);
        }
        unlink(glsl);
        unlink(man);
        rmdir(dir);
    }

    /* 8. A shader built twice comes from the program cache the second time
     *    and draws the same. */
    {
        const char *src = "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.25, f.x / iResolution.x, 0.75, 1); }\n";
        unsigned h0, m0, h1, m1;
        program_cache_stats(&h0, &m0);
        s = run(src, 1, 0);
        unsigned char a[4], b2[4];
        pixel(40, 5, a);
        wallshader_free(s);
        program_cache_stats(&h1, &m1);
        CHECK(m1 > m0 || h1 > h0, "first build asks the cache");
        s = run(src, 1, 0);
        pixel(40, 5, b2);
        wallshader_free(s);
        unsigned h2, m2;
        program_cache_stats(&h2, &m2);
        CHECK(h2 > h1, "second build is a cache hit (%u -> %u)", h1, h2);
        CHECK(!memcmp(a, b2, 4), "cached program draws the same");
    }

    /* 9. The config's `channels` fill a channel the shader leaves open. */
    {
        GLuint tex;
        static const unsigned char green[4] = {0, 255, 0, 255};
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        s = run("void mainImage(out vec4 c, in vec2 f) { c = texture(iChannel2, vec2(0.5)); }\n", 0, 0);
        CHECK(s != NULL, "channel shader compiles");
        if (s) {
            wallshader_frame cf = {.channels = {0, 0, tex, 0}, .channel_w = {0, 0, 1, 0}, .channel_h = {0, 0, 1, 0}};
            draw(s, 1, 0, &cf);
            pixel(2, 2, p);
            CHECK(p[1] == 255 && p[0] == 0, "iChannel2 reads the config texture (%d %d %d)", p[0], p[1], p[2]);
            wallshader_free(s);
        }
        glDeleteTextures(1, &tex);
    }

    /* 10. Every shipped shader compiles and draws a frame on llvmpipe. */
    const char *sdir = getenv("NW_SHADER_DIR");
    if (sdir) {
        static const char *names[] = {
            "audio_bars", "audio_pulse", "circadian_sky", "fireflies", "garden",
            "living_aurora", "moon_ocean", "mouse_ripples", "neon_city", "plasma_touch",
            "rain_window", "scene_demo", "starfield_warp", "star_next", "system_deck",
            "system_monitor", "time", "window_forge", "window_light",
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.glsl", sdir, names[i]);
            s = NULL;
            r = wallshader_load(path, &s);
            CHECK(nw_is_ok(r), "%s compiles: %s", names[i], wallshader_last_errors());
            if (!s) continue;
            draw(s, 2, 10.0, NULL);
            CHECK(glGetError() == GL_NO_ERROR, "%s leaves no GL error", names[i]);
            wallshader_free(s);
        }
    }

    glDeleteFramebuffers(1, &g_fbo);
    glDeleteTextures(1, &g_tex);
    gl_headless_fini();
    printf("shader_gl: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
