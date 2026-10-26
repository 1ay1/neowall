/* Frame clock: stamps frames with the vblank they show at. Pure logic. */

#include <stdio.h>
#include <stdlib.h>

#include "neowall/output/frame_clock.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define R 16666667ull          /* 60 Hz */
#define BASE 1000000000000ull  /* an arbitrary monotonic origin */

static uint64_t g_frame = 0;   /* simulated vblank count, keeps running */

/* Simulate a display: frames drawn at vblank v + jitter show at v + lat*R.
 * Returns how many stamps were not exactly the real present time. */
static int run(nw_frame_clock *fc, int frames, int lat, uint64_t jitter_max,
               int miss_every, uint64_t ts_noise, int *bad_steps) {
    int wrong = 0;
    uint64_t prev_t = 0, prev_p = 0;
    *bad_steps = 0;
    srand(7);
    for (int i = 0; i < frames; i++) {
        uint64_t v = BASE + (g_frame++) * R;
        uint64_t now = v + (jitter_max ? (uint64_t)rand() % jitter_max : 0);
        int used;
        uint64_t t = nw_frame_clock_target(fc, now, &used);
        int l = lat + (miss_every && i % miss_every == 0 ? 1 : 0);
        uint64_t p = v + (uint64_t)l * R;
        uint64_t reported = p + (ts_noise ? (uint64_t)rand() % ts_noise : 0);
        nw_frame_clock_presented(fc, t, used, reported, R);
        if (i < 20 || !t) continue;           /* warm-up */
        if (t != p) wrong++;
        if (prev_t && (t - prev_t) != (p - prev_p)) (*bad_steps)++;
        prev_t = t; prev_p = p;
    }
    return wrong;
}

int main(void) {
    nw_frame_clock fc;
    int bad;

    nw_frame_clock_init(&fc);
    int used = -1;
    CHECK(nw_frame_clock_target(&fc, BASE, &used) == 0, "no grid yet -> 0 (wall clock)");

    /* Steady display, draws start up to 5 ms after the vblank, 2-frame
     * latency: every stamp is the exact present, every step exactly R. */
    nw_frame_clock_init(&fc);
    CHECK(run(&fc, 600, 2, 5000000, 0, 0, &bad) == 0, "steady: stamps exact");
    CHECK(bad == 0, "steady: %d uneven steps", bad);

    /* Same, with up to 3 ms of noise on the compositor's timestamps: the
     * grid is phase-locked, so stamps still step by exactly R. */
    nw_frame_clock_init(&fc);
    run(&fc, 600, 2, 5000000, 0, 3000000, &bad);
    CHECK(bad == 0, "timestamp noise: %d uneven steps", bad);

    /* A missed vblank every 50 frames: those frames show one refresh late,
     * but one vote can't move the latency, so everything else stays exact. */
    nw_frame_clock_init(&fc);
    int wrong = run(&fc, 600, 2, 5000000, 50, 0, &bad);
    CHECK(wrong <= 600 / 50 + 1, "misses: %d wrong stamps", wrong);

    /* Latency change (compositor adds a frame of buffering): it adapts. */
    nw_frame_clock_init(&fc);
    run(&fc, 200, 1, 2000000, 0, 0, &bad);
    CHECK(fc.latency == 1, "learned latency 1, got %d", fc.latency);
    run(&fc, 200, 3, 2000000, 0, 0, &bad);
    CHECK(fc.latency == 3, "relearned latency 3, got %d", fc.latency);

    /* Two draws in one refresh still move forward. */
    nw_frame_clock_init(&fc);
    uint64_t t0 = BASE + (g_frame += 10) * R;
    nw_frame_clock_presented(&fc, 0, 1, t0, R);
    uint64_t a = nw_frame_clock_target(&fc, t0 + R, &used);
    uint64_t b = nw_frame_clock_target(&fc, t0 + R + 1000, &used);
    CHECK(b == a + R, "second draw in a refresh steps one refresh");

    /* A reset keeps the clock monotonic. */
    nw_frame_clock_reset(&fc);
    nw_frame_clock_presented(&fc, 0, 1, t0, R);
    uint64_t c = nw_frame_clock_target(&fc, t0 + R, &used);
    CHECK(c > b, "monotonic across reset");

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
