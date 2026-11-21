/* The frame-callback watchdog's state machine (frame_watch.h), on a fake
 * clock: when it asks, when it re-asks, and when it calls an output covered.
 * The regressions it pins: a covered output must come back once the
 * compositor answers, re-probing must not make a covered output flap
 * visible, and a request the compositor drops must not freeze it forever. */

#include <stdio.h>

#include "frame_watch.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

/* What the protocol side does with an action. */
static void step(frame_watch *w, uint64_t now) {
    frame_watch_action a = frame_watch_next(w, now);
    if (a == FRAME_WATCH_ARM || a == FRAME_WATCH_REPROBE) frame_watch_armed(w, now);
}

static void test_visible(void) {
    frame_watch w = {0};
    uint64_t t    = 1000;
    CHECK(frame_watch_next(&w, t) == FRAME_WATCH_ARM, "a fresh watch asks at once");
    /* A visible surface: every request answered within a frame. */
    for (int i = 0; i < 200; i++, t += 50) {
        step(&w, t);
        CHECK(!frame_watch_occluded(&w, t), "answered output is visible (t=%llu)",
              (unsigned long long)t);
        if (w.outstanding) frame_watch_done(&w, t + 16);
    }
}

static void test_rearm_throttle(void) {
    frame_watch w = {0};
    step(&w, 1000);
    frame_watch_done(&w, 1010);
    CHECK(frame_watch_next(&w, 1100) == FRAME_WATCH_IDLE, "no re-ask right after an answer");
    CHECK(frame_watch_next(&w, 1010 + FRAME_WATCH_REARM_MS) == FRAME_WATCH_ARM,
          "re-ask once the rearm interval passed");
}

static void test_cover_then_uncover(void) {
    frame_watch w = {0};
    uint64_t t    = 5000;
    step(&w, t);
    /* Covered: nothing comes back. */
    CHECK(!frame_watch_occluded(&w, t + FRAME_WATCH_TIMEOUT_MS), "not covered at the boundary");
    CHECK(frame_watch_occluded(&w, t + FRAME_WATCH_TIMEOUT_MS + 1), "covered past the timeout");

    /* Stays covered across re-probes: the clock runs from the FIRST
     * unanswered request, so replacing it never reads as visible. */
    for (uint64_t now = t + FRAME_WATCH_TIMEOUT_MS + 1; now < t + 10000; now += 100) {
        step(&w, now);
        CHECK(frame_watch_occluded(&w, now), "stays covered while unanswered (now=%llu)",
              (unsigned long long)now);
    }
    CHECK(w.waiting_since_ms == t, "re-probes keep the first request's time");

    /* The window closes; the compositor answers the (re-probed) request. */
    uint64_t back = t + 10000;
    frame_watch_done(&w, back);
    CHECK(!frame_watch_occluded(&w, back), "uncovered the moment it answers");
    CHECK(!frame_watch_occluded(&w, back + 100000), "and stays so with no request out");
    step(&w, back + FRAME_WATCH_REARM_MS);
    CHECK(!frame_watch_occluded(&w, back + FRAME_WATCH_REARM_MS + FRAME_WATCH_TIMEOUT_MS),
          "a fresh request starts a fresh measurement");
}

static void test_reprobe(void) {
    frame_watch w = {0};
    step(&w, 100);
    CHECK(frame_watch_next(&w, 100 + FRAME_WATCH_REPROBE_MS - 1) == FRAME_WATCH_IDLE,
          "no re-probe before the interval");
    CHECK(frame_watch_next(&w, 100 + FRAME_WATCH_REPROBE_MS) == FRAME_WATCH_REPROBE,
          "a request unanswered for the interval is replaced");
    /* Dropped callback: the replacement is answered, the output comes back. */
    step(&w, 100 + FRAME_WATCH_REPROBE_MS);
    CHECK(w.armed_at_ms == 100 + FRAME_WATCH_REPROBE_MS, "replacement is timed from now");
    frame_watch_done(&w, 100 + FRAME_WATCH_REPROBE_MS + 5);
    CHECK(!frame_watch_occluded(&w, 100 + FRAME_WATCH_REPROBE_MS + 5),
          "a dropped callback costs one re-probe, not a frozen wallpaper");
}

static void test_reset(void) {
    frame_watch w = {0};
    step(&w, 100);
    CHECK(frame_watch_occluded(&w, 100 + FRAME_WATCH_TIMEOUT_MS + 1), "covered");
    frame_watch_reset(&w);
    CHECK(!frame_watch_occluded(&w, 100000), "reset forgets it");
    CHECK(frame_watch_next(&w, 100000) == FRAME_WATCH_ARM, "and asks afresh");
}

static void test_done_at_zero(void) {
    /* last_done_ms == 0 means "never"; an answer at t=0 must still count. */
    frame_watch w = {0};
    step(&w, 0);
    frame_watch_done(&w, 0);
    CHECK(frame_watch_next(&w, 10) == FRAME_WATCH_IDLE, "an answer at t=0 still throttles");
}

int main(void) {
    test_visible();
    test_rearm_throttle();
    test_cover_then_uncover();
    test_reprobe();
    test_reset();
    test_done_at_zero();
    printf("frame_watch: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
