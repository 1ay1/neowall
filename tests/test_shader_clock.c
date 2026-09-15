/* Regression tests for the shader-clock rebase policy.
 *
 * Symptom: the wallpaper "keeps jumping back in time" -- a long-running shader
 * periodically snaps back to its opening state in full view of the user. For a
 * day-scale shader like garden.glsl that discards days of accumulated growth.
 *
 * Root cause was a state-machine leak, not arithmetic. `shader_hidden_since` is
 * documented as "0 while visible" and marks a window in which the clock may be
 * rebased unseen. It was set when an output became occluded but never cleared
 * when it became visible again, so after the first occlusion the marker stayed
 * set forever and the rebase condition degenerated into "every REBASE_SECONDS,
 * regardless of visibility".
 *
 * These tests pin the invariant that actually matters: a rebase may only happen
 * while the output is genuinely not being seen.
 *
 * GL-free and compositor-free, so this runs headless in CI.
 */

#include <stdio.h>
#include <string.h>

#include "neowall/shader/shader_clock.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define CHECK_MSG(cond, ...)                                                   \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

/* Mirrors SHADER_TIME_REBASE_SECONDS; kept local so the test does not depend
 * on the whole constants header (and so tuning the real value cannot silently
 * invalidate these cases). */
#define THRESHOLD 3600.0

/* ------------------------------------------------------------------ *
 * The reported bug
 * ------------------------------------------------------------------ */

/* The exact sequence a user performs: maximize a window, then restore it, then
 * keep working. Before the fix the third step rebased in plain sight. */
static void test_visible_output_never_rebases(void) {
    /* 1. Fullscreen app covers the wallpaper. Marker gets set. */
    shader_clock_state hidden = {
        .elapsed_seconds = 7200.0,
        .hidden_since_ms = 1000,
        .occluded_now = true,
        .paused_now = false,
        .spanned = false,
    };
    CHECK_MSG(shader_clock_should_rebase(&hidden, THRESHOLD),
              "an occluded output past the threshold is the one case where a "
              "rebase is legal");

    /* 2. User restores the window. The un-occlude path clears the marker. */
    shader_clock_state visible = hidden;
    visible.occluded_now = false;
    visible.hidden_since_ms = 0;
    CHECK_MSG(!shader_clock_should_rebase(&visible, THRESHOLD),
              "a visible output must never rebase");

    /* 3. The regression itself: marker stale (as if un-occlude forgot to clear
     * it) while the output is plainly visible. This must still refuse. */
    shader_clock_state stale = hidden;
    stale.occluded_now = false;   /* user is looking at it */
    stale.hidden_since_ms = 1000; /* but the marker was never cleared */
    CHECK_MSG(!shader_clock_should_rebase(&stale, THRESHOLD),
              "stale hidden marker must not authorise a rebase on a visible "
              "output -- this is the 'jumps back in time' bug");
}

/* Time alone is never sufficient: an output that has been visible since start
 * accumulates unbounded elapsed time and must still never jump. */
static void test_long_uptime_alone_does_not_rebase(void) {
    const double days[] = {3600.1, 7200.0, 86400.0, 604800.0, 2592000.0};
    for (size_t i = 0; i < sizeof(days) / sizeof(days[0]); i++) {
        shader_clock_state st = {
            .elapsed_seconds = days[i],
            .hidden_since_ms = 0,
            .occluded_now = false,
            .paused_now = false,
            .spanned = false,
        };
        CHECK_MSG(!shader_clock_should_rebase(&st, THRESHOLD),
                  "continuously visible output rebased after %.0fs", days[i]);
    }
}

/* ------------------------------------------------------------------ *
 * Threshold behaviour
 * ------------------------------------------------------------------ */

static void test_threshold(void) {
    shader_clock_state st = {
        .elapsed_seconds = 0.0,
        .hidden_since_ms = 1000,
        .occluded_now = true,
        .paused_now = false,
        .spanned = false,
    };

    /* Below and exactly at the threshold: precision is not yet at risk, so a
     * discontinuity buys nothing. */
    st.elapsed_seconds = 0.0;
    CHECK(!shader_clock_should_rebase(&st, THRESHOLD));
    st.elapsed_seconds = 60.0;
    CHECK(!shader_clock_should_rebase(&st, THRESHOLD));
    st.elapsed_seconds = THRESHOLD;
    CHECK(!shader_clock_should_rebase(&st, THRESHOLD));

    /* Just past it, while hidden: allowed. */
    st.elapsed_seconds = THRESHOLD + 0.1;
    CHECK(shader_clock_should_rebase(&st, THRESHOLD));
}

/* ------------------------------------------------------------------ *
 * The other way to be unobserved
 * ------------------------------------------------------------------ */

/* `neowall pause-shader` freezes the animation; the user is not watching it
 * advance, so a rebase there is equally invisible. */
static void test_paused_counts_as_hidden(void) {
    shader_clock_state st = {
        .elapsed_seconds = 7200.0,
        .hidden_since_ms = 1000,
        .occluded_now = false,
        .paused_now = true,
        .spanned = false,
    };
    CHECK(shader_clock_should_rebase(&st, THRESHOLD));

    /* But resuming without clearing the marker must not authorise one. */
    st.paused_now = false;
    CHECK(!shader_clock_should_rebase(&st, THRESHOLD));
}

/* ------------------------------------------------------------------ *
 * Spanned outputs
 * ------------------------------------------------------------------ */

/* Members of a spanned group share one epoch. An individual member must not
 * rebase on its own or the seam between halves shows a phase gap; the group
 * path owns that decision. */
static void test_spanned_defers_to_group(void) {
    shader_clock_state st = {
        .elapsed_seconds = 7200.0,
        .hidden_since_ms = 1000,
        .occluded_now = true,
        .paused_now = false,
        .spanned = true,
    };

    /* The window is legal... */
    CHECK(shader_clock_should_rebase(&st, THRESHOLD));
    /* ...but this member yields the actual clock change to the group. */
    CHECK(shader_clock_defers_to_group(&st));

    st.spanned = false;
    CHECK(!shader_clock_defers_to_group(&st));
}

/* ------------------------------------------------------------------ *
 * Robustness
 * ------------------------------------------------------------------ */

static void test_degenerate_inputs(void) {
    CHECK(!shader_clock_should_rebase(NULL, THRESHOLD));
    CHECK(!shader_clock_defers_to_group(NULL));

    /* A zeroed state must be inert rather than accidentally permissive. */
    shader_clock_state zero;
    memset(&zero, 0, sizeof(zero));
    CHECK(!shader_clock_should_rebase(&zero, THRESHOLD));
}

/* Exhaustive truth table over the boolean inputs: the ONLY states that may
 * authorise a rebase are those where live state still says we are unobserved.
 * This closes the door on a future edit reintroducing the bug by loosening one
 * condition. */
static void test_full_truth_table(void) {
    for (int marker = 0; marker <= 1; marker++) {
        for (int occ = 0; occ <= 1; occ++) {
            for (int paused = 0; paused <= 1; paused++) {
                shader_clock_state st = {
                    .elapsed_seconds = THRESHOLD + 1.0,
                    .hidden_since_ms = marker ? 1000 : 0,
                    .occluded_now = occ != 0,
                    .paused_now = paused != 0,
                    .spanned = false,
                };

                bool expected = marker && (occ || paused);
                bool got = shader_clock_should_rebase(&st, THRESHOLD);

                CHECK_MSG(got == expected,
                          "marker=%d occluded=%d paused=%d: expected %s, got %s",
                          marker, occ, paused,
                          expected ? "rebase" : "no rebase",
                          got ? "rebase" : "no rebase");

                /* The invariant, stated directly: never while being watched. */
                if (!occ && !paused) {
                    CHECK_MSG(!got, "rebased while output was visible "
                                    "(marker=%d)", marker);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Jump tracking
 * ------------------------------------------------------------------ */

static void test_tracker_normal_advance(void) {
    shader_clock_tracker tr;
    memset(&tr, 0, sizeof(tr));

    /* A normal 60 FPS run must never report an event. */
    double t = 0.0;
    for (int i = 0; i < 600; i++) {
        CHECK(shader_clock_track(&tr, t, 1000, false) == SHADER_CLOCK_OK);
        t += 1.0 / 60.0;
    }
    CHECK(tr.jumps == 0);
    CHECK(tr.frames == 600);
}

static void test_tracker_flags_backward_step(void) {
    shader_clock_tracker tr;
    memset(&tr, 0, sizeof(tr));

    shader_clock_track(&tr, 7200.0, 1000, false);

    /* Same epoch, time went backwards: nobody moved the epoch, so this is the
     * mystery case that most needs reporting. */
    CHECK(shader_clock_track(&tr, 0.0, 1000, false) == SHADER_CLOCK_JUMP_UNKNOWN);
    CHECK(tr.jumps == 1);
}

static void test_tracker_attributes_causes(void) {
    shader_clock_tracker tr;
    memset(&tr, 0, sizeof(tr));

    /* Deliberate rebase is expected, and named as such. */
    shader_clock_track(&tr, 7200.0, 1000, false);
    CHECK(shader_clock_track(&tr, 0.0, 2000, true) == SHADER_CLOCK_JUMP_REBASE);

    /* An epoch change (reload, span join) is distinguishable from a mystery. */
    memset(&tr, 0, sizeof(tr));
    shader_clock_track(&tr, 500.0, 1000, false);
    CHECK(shader_clock_track(&tr, 10.0, 9999, false) == SHADER_CLOCK_JUMP_EPOCH);
}

static void test_tracker_tolerates_float_noise(void) {
    shader_clock_tracker tr;
    memset(&tr, 0, sizeof(tr));

    /* Sub-millisecond wobble is representation noise, not a visible glitch. */
    shader_clock_track(&tr, 100.0, 1000, false);
    CHECK(shader_clock_track(&tr, 100.0 - 0.0001, 1000, false) == SHADER_CLOCK_OK);
    CHECK(tr.jumps == 0);

    /* A perceptible step back is reported. */
    CHECK(shader_clock_track(&tr, 90.0, 1000, false) == SHADER_CLOCK_JUMP_UNKNOWN);
}

static void test_tracker_first_sample_is_never_a_jump(void) {
    shader_clock_tracker tr;
    memset(&tr, 0, sizeof(tr));
    /* Starting at a large iTime (restored state) is not a backward step. */
    CHECK(shader_clock_track(&tr, 99999.0, 1000, false) == SHADER_CLOCK_OK);
    CHECK(tr.jumps == 0);

    CHECK(shader_clock_track(NULL, 0.0, 0, false) == SHADER_CLOCK_OK);
}

static void test_event_names(void) {
    CHECK(strcmp(shader_clock_event_name(SHADER_CLOCK_OK), "ok") == 0);
    CHECK(strcmp(shader_clock_event_name(SHADER_CLOCK_JUMP_REBASE), "rebase") == 0);
    CHECK(strcmp(shader_clock_event_name(SHADER_CLOCK_JUMP_EPOCH), "epoch-changed") == 0);
    CHECK(strcmp(shader_clock_event_name(SHADER_CLOCK_JUMP_UNKNOWN), "UNKNOWN") == 0);
}

int main(void) {
    test_visible_output_never_rebases();
    test_long_uptime_alone_does_not_rebase();
    test_threshold();
    test_paused_counts_as_hidden();
    test_spanned_defers_to_group();
    test_degenerate_inputs();
    test_full_truth_table();
    test_tracker_normal_advance();
    test_tracker_flags_backward_step();
    test_tracker_attributes_causes();
    test_tracker_tolerates_float_noise();
    test_tracker_first_sample_is_never_a_jump();
    test_event_names();

    printf("shader_clock: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
