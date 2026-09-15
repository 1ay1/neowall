/* See include/neowall/shader/shader_clock.h for the rationale. */

#include "neowall/shader/shader_clock.h"

bool shader_clock_should_rebase(const shader_clock_state *st,
                                double threshold_seconds) {
    if (!st) return false;

    /* Not yet at risk of losing float32 frame resolution. */
    if (!(st->elapsed_seconds > threshold_seconds)) return false;

    /* Never entered a hidden window, so there is no unobserved moment to hide
     * the discontinuity in. */
    if (st->hidden_since_ms == 0) return false;

    /* Corroborate the marker against live state. A marker left set by a path
     * that forgot to clear it must not be enough on its own -- that is exactly
     * how the clock came to reset in full view of the user. */
    if (!st->occluded_now && !st->paused_now) return false;

    return true;
}

bool shader_clock_defers_to_group(const shader_clock_state *st) {
    return st && st->spanned;
}

/* Backward steps smaller than this are float32 representation noise, not a
 * glitch. A 60 FPS frame is 16.7ms, so a millisecond of slack is invisible
 * while still catching anything a viewer could perceive. */
#define SHADER_CLOCK_EPSILON 0.001

shader_clock_event shader_clock_track(shader_clock_tracker *tr,
                                      double current_time,
                                      uint64_t epoch_ms,
                                      bool rebased) {
    if (!tr) return SHADER_CLOCK_OK;

    shader_clock_event ev = SHADER_CLOCK_OK;

    if (tr->primed) {
        double delta = current_time - tr->last_time;
        if (delta < -SHADER_CLOCK_EPSILON) {
            /* Time went backwards. Attribute it. */
            if (rebased) {
                ev = SHADER_CLOCK_JUMP_REBASE;
            } else if (epoch_ms != tr->last_epoch_ms) {
                ev = SHADER_CLOCK_JUMP_EPOCH;
            } else {
                ev = SHADER_CLOCK_JUMP_UNKNOWN;
            }
            tr->jumps++;
        }
    }

    tr->last_time = current_time;
    tr->last_epoch_ms = epoch_ms;
    tr->frames++;
    tr->primed = true;
    return ev;
}

const char *shader_clock_event_name(shader_clock_event ev) {
    switch (ev) {
    case SHADER_CLOCK_OK:           return "ok";
    case SHADER_CLOCK_JUMP_REBASE:  return "rebase";
    case SHADER_CLOCK_JUMP_EPOCH:   return "epoch-changed";
    case SHADER_CLOCK_JUMP_UNKNOWN: return "UNKNOWN";
    }
    return "?";
}
