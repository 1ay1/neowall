/* See include/neowall/shader/shader_clock.h for the rationale. */

#include "neowall/shader/shader_clock.h"

shader_pause_clock shader_pause_begin(uint64_t now_us, uint32_t duration_ms) {
    return (shader_pause_clock){
        .start_us = now_us, .duration_ms = duration_ms, .parked = duration_ms == 0};
}

uint64_t shader_pause_sample(const shader_pause_clock *pause, uint64_t now_us, bool *final_frame) {
    *final_frame = false;
    if (!pause->start_us || now_us < pause->start_us) return now_us;

    uint64_t elapsed  = now_us - pause->start_us;
    uint64_t duration = (uint64_t)pause->duration_ms * 1000;
    if (elapsed >= duration) {
        *final_frame = true;
        return pause->start_us + duration / 3;
    }

    /* Integrate the velocity (1-u)^2. That preserves the incoming speed
     * at u=0 and brings it to zero at u=1. */
    double u = (double)elapsed / (double)duration;
    return pause->start_us + (uint64_t)((double)duration * u * (1.0 - u + u * u / 3.0));
}

bool shader_clock_should_rebase(const shader_clock_state *st, double threshold_seconds) {
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

bool shader_clock_should_fade_rebase(const shader_clock_state *st, double threshold_seconds) {
    if (!st) return false;
    if (!(st->elapsed_seconds > threshold_seconds)) return false;
    if (st->spanned) return false;
    if (st->occluded_now || st->paused_now) return false;
    return true;
}

bool shader_clock_defers_to_group(const shader_clock_state *st) {
    return st && st->spanned;
}

/* Backward steps smaller than this are float32 representation noise, not a
 * glitch. A 60 FPS frame is 16.7ms, so a millisecond of slack is invisible
 * while still catching anything a viewer could perceive. */
#define SHADER_CLOCK_EPSILON 0.001

shader_clock_event shader_clock_track(shader_clock_tracker *tr, double current_time,
                                      uint64_t epoch_ms, bool rebased) {
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

    tr->last_time     = current_time;
    tr->last_epoch_ms = epoch_ms;
    tr->frames++;
    tr->primed = true;
    return ev;
}

const char *shader_clock_event_name(shader_clock_event ev) {
    switch (ev) {
    case SHADER_CLOCK_OK: return "ok";
    case SHADER_CLOCK_JUMP_REBASE: return "rebase";
    case SHADER_CLOCK_JUMP_EPOCH: return "epoch-changed";
    case SHADER_CLOCK_JUMP_UNKNOWN: return "UNKNOWN";
    }
    return "?";
}
