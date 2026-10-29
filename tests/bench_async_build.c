/* How long the caller is blocked starting a cold shader build, sync vs
 * async, and how long until the async one is ready. Not a test:
 *   build/bench_async_build shader.glsl */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "gl_headless.h"
#include "neowall/log.h"
#include "neowall/shader/wallshader.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    setenv("NEOWALL_NO_PROGRAM_CACHE", "1", 1); /* always cold */
    if (gl_headless_init()) return 77;
    log_set_level(LOG_LEVEL_ERROR);

    double t = now_ms();
    wallshader *s = NULL;
    if (nw_is_err(wallshader_load(argv[1], &s))) return 1;
    printf("sync:  caller blocked %.1f ms\n", now_ms() - t);
    wallshader_free(s);

    /* a different text, so the driver's own cache can't help */
    t = now_ms();
    wallshader_pending *p = NULL;
    if (nw_is_err(wallshader_load_start(argv[1], &p))) return 1;
    double start = now_ms() - t, worst = start;
    int polls = 0;
    nw_build_state st;
    while ((st = wallshader_load_poll(p, &s)) == NW_BUILD_PENDING) {
        double t0 = now_ms();
        struct timespec ts = {0, 16000000}; /* one frame */
        nanosleep(&ts, NULL);
        (void)t0;
        polls++;
        double t1 = now_ms();
        st = wallshader_load_poll(p, &s);
        double d = now_ms() - t1;
        if (d > worst) worst = d;
        if (st != NW_BUILD_PENDING) break;
    }
    printf("async: start %.1f ms, longest poll %.1f ms, ready after %.1f ms (%d frames)\n", start, worst,
           now_ms() - t, polls);
    wallshader_load_cancel(p);
    if (st == NW_BUILD_DONE) wallshader_free(s);
    gl_headless_fini();
    return 0;
}
