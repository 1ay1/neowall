/* Time how long each shader takes to build, cold and from the program cache.
 * Not a test: `build/bench_shader_build [dir]`. Uses the real GPU unless
 * told otherwise (gl_headless picks Mesa only if the env asks). */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gl_headless.h"
#include "neowall/log.h"
#include "neowall/shader/program_cache.h"
#include "neowall/shader/wallshader.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static double build_ms(const char *path) {
    double t = now_ms();
    wallshader *s = NULL;
    if (nw_is_err(wallshader_load(path, &s))) return -1;
    double d = now_ms() - t;
    wallshader_free(s);
    return d;
}

/* CPU time to issue one frame (glFinish excluded): what the loop thread
 * pays per frame per output. */
static double frame_us(const char *path) {
    wallshader *s = NULL;
    if (nw_is_err(wallshader_load(path, &s))) return -1;
    GLuint fbo, tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 180, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    double total = 0;
    const int N = 200;
    for (int i = 0; i < N + 10; i++) {
        wallshader_frame f = {.width = 320, .height = 180, .time = i / 60.0};
        double t = now_ms();
        wallshader_draw(s, &f);
        if (i >= 10) total += now_ms() - t;
        glFinish();
    }
    wallshader_free(s);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return total * 1000.0 / N;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "examples/shaders";
    char cdir[] = "/tmp/nwbench-XXXXXX";
    if (!mkdtemp(cdir)) return 1;
    setenv("XDG_CACHE_HOME", cdir, 1);
    const char *err = gl_headless_init();
    if (err) {
        printf("skip: %s\n", err);
        return 77;
    }
    log_set_level(LOG_LEVEL_ERROR);
    printf("GL: %s\n%-20s %9s %9s %9s\n", (const char *)glGetString(GL_RENDERER), "shader", "cold ms",
           "cached ms", "frame us");
    DIR *d = opendir(dir);
    struct dirent *de;
    double tc = 0, tw = 0, tf = 0;
    while (d && (de = readdir(d))) {
        size_t n = strlen(de->d_name);
        if (n < 6 || strcmp(de->d_name + n - 5, ".glsl")) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        double c = build_ms(path), w = build_ms(path), fu = frame_us(path);
        printf("%-20.*s %9.1f %9.1f %9.1f\n", (int)n - 5, de->d_name, c, w, fu);
        tc += c;
        tw += w;
        tf += fu;
    }
    if (d) closedir(d);
    printf("%-20s %9.1f %9.1f %9.1f\n", "total", tc, tw, tf);
    gl_headless_fini();
    char rm[64];
    snprintf(rm, sizeof(rm), "rm -rf %s", cdir);
    return system(rm) == 0 ? 0 : 1;
}
