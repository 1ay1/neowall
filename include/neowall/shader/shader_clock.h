/* Shader-clock rebase policy.
 *
 * iTime is a float32 uniform counting seconds since a shader started. Left to
 * grow, it loses frame resolution: past ~a day of uptime the gap between
 * consecutive frames falls below the mantissa's step and animation visibly
 * quantises. The fix is to occasionally rebase the clock to zero.
 *
 * But a rebase IS a discontinuity -- the shader snaps back to its opening
 * state. Doing that in front of the user is the "wallpaper jumps back in time"
 * bug: a day-scale shader like garden.glsl loses days of growth in one frame.
 * So a rebase is only ever legal inside a window where nobody can see it: the
 * output is occluded (a fullscreen window covers it) or its animation is
 * explicitly paused.
 *
 * The rule is factored out here, away from GL and compositor state, precisely
 * so it can be exhaustively unit-tested -- see tests/test_shader_clock.c. The
 * original bug was not in the arithmetic but in the STATE MACHINE feeding it:
 * the "currently hidden" marker was set on occlusion and never cleared on
 * un-occlusion, so the predicate silently became "always true" and fired in
 * plain view every REBASE_SECONDS thereafter.
 */

#ifndef NEOWALL_SHADER_CLOCK_H
#define NEOWALL_SHADER_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* Everything the rebase decision depends on, in one struct with no GL or
 * compositor types, so a test can construct any situation directly. */
typedef struct {
    double elapsed_seconds;     /* current iTime for this output */
    uint64_t hidden_since_ms;   /* when it stopped being visible; 0 while visible */
    bool occluded_now;          /* is it covered *right now* */
    bool paused_now;            /* is its animation frozen right now */
    bool spanned;               /* part of a multi-output spanned group */
} shader_clock_state;

/* True when the clock may be reset to zero without anyone seeing it.
 *
 * Requires all of:
 *   - enough elapsed time that precision is actually at risk,
 *   - a marker saying we entered a hidden window,
 *   - and corroboration from LIVE state that we are still hidden.
 *
 * The last condition is deliberate redundancy. The marker alone was the
 * original bug; requiring present-tense invisibility means that even if some
 * future code path forgets to clear the marker, the worst outcome is a missed
 * rebase (harmless, gradual precision loss) rather than a visible time jump.
 */
bool shader_clock_should_rebase(const shader_clock_state *st, double threshold_seconds);

/* True when a spanned output should adopt the group's shared epoch instead of
 * rebasing on its own. Members of a span must agree on iTime or the seam
 * between two halves of one scene shows a phase gap. */
bool shader_clock_defers_to_group(const shader_clock_state *st);

/* ---------------------------------------------------------------- *
 * Jump tracking (diagnostics)
 *
 * iTime must advance monotonically while a shader is on screen. Any backward
 * step is a visible "jumps back in time" glitch. Rather than reason about the
 * handful of places that can move the epoch, watch the OUTPUT value itself and
 * report every regression with the state that produced it -- that catches
 * causes nobody has thought of yet, including ones outside this file.
 *
 * One tracker per output; zero-initialise it and feed it every frame.
 */
typedef struct {
    double last_time;        /* previous iTime handed to the shader */
    uint64_t last_epoch_ms;  /* epoch it was derived from */
    uint64_t frames;         /* frames observed */
    uint64_t jumps;          /* backward steps seen so far */
    bool primed;             /* has at least one sample */
} shader_clock_tracker;

/* Why a given frame's iTime moved backwards. */
typedef enum {
    SHADER_CLOCK_OK = 0,        /* advanced normally */
    SHADER_CLOCK_JUMP_REBASE,   /* the deliberate precision rebase */
    SHADER_CLOCK_JUMP_EPOCH,    /* the epoch changed under us (reload/span) */
    SHADER_CLOCK_JUMP_UNKNOWN,  /* went backwards with no epoch change */
} shader_clock_event;

/* Feed one frame. Returns what happened, and updates the tracker.
 *
 * `rebased` says whether this frame performed the intentional rebase, so an
 * expected discontinuity is not reported as a mystery. A tolerance absorbs
 * float32 noise; only a real regression is flagged.
 */
shader_clock_event shader_clock_track(shader_clock_tracker *tr,
                                      double current_time,
                                      uint64_t epoch_ms,
                                      bool rebased);

/* Human-readable name for a tracked event, for logging. */
const char *shader_clock_event_name(shader_clock_event ev);

#endif /* NEOWALL_SHADER_CLOCK_H */
