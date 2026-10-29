/* Render shaders offscreen and say which come out black. Not a test:
 *   build/probe_shaders file.glsl ...
 * Prints, per shader: load status, passes, and the mean/max of a few frames. */
#define _POSIX_C_SOURCE 200809L
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_headless.h"
#include "neowall/log.h"
#include "neowall/shader/wallshader.h"
#include "neowall/shader/reactive.h"
#include <time.h>

static int W = 480, H = 200;

int main(int argc, char **argv) {
    if (getenv("PROBE_W")) W = atoi(getenv("PROBE_W"));
    if (getenv("PROBE_H")) H = atoi(getenv("PROBE_H"));
    const char *err = gl_headless_init();
    if (err) {
        printf("skip: %s\n", err);
        return 77;
    }
    log_set_level(LOG_LEVEL_ERROR);
    if (getenv("PROBE_REACTIVE")) {
        reactive_init();
        for (int i = 0; i < 6; i++) { reactive_sample(); { struct timespec ts = {0, 300000000}; nanosleep(&ts, NULL); }; }
    }
    printf("GL: %s\n", (const char *)glGetString(GL_RENDERER));
    GLuint fbo, tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    unsigned char *px = malloc((size_t)W * H * 4);
    int black = 0;
    for (int a = 1; a < argc; a++) {
        wallshader *s = NULL;
        if (nw_is_err(wallshader_load(argv[a], &s))) {
            printf("LOADFAIL %s\n%s", argv[a], wallshader_last_errors());
            continue;
        }
        for (int f = 0; f < 30; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, W, H);
            wallshader_frame fr = {.width = W, .height = H, .time = 3.0 + f / 60.0, .mouse = {W / 2.0f, H / 2.0f}};
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            wallshader_draw(s, &fr);
            if (getenv("PROBE_FRAMES") && f < 6) {
                glFinish();
                clock_gettime(CLOCK_MONOTONIC, &t1);
                printf("  frame %d: %.1f ms\n", f, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        double sum = 0;
        int mx = 0;
        for (int i = 0; i < W * H * 4; i += 4) {
            int v = px[i] + px[i + 1] + px[i + 2];
            sum += v;
            if (v > mx) mx = v;
        }
        double mean = sum / (W * H * 3);
        bool dark = mx < 8;
        black += dark;
        const char *base = strrchr(argv[a], '/');
        printf("%-6s %-34s passes=%d mean=%6.1f max=%3d glerr=%x\n", dark ? "BLACK" : "ok", base ? base + 1 : argv[a],
               wallshader_pass_count(s), mean, mx / 3, glGetError());
        wallshader_free(s);
    }
    gl_headless_fini();
    return black ? 1 : 0;
}
