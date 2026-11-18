/* See include/neowall/shader/shader_clock.h for the rationale. */

#include "neowall/shader/shader_clock.h"

/* Round a non-negative double to the nearest integer. Kept libm-free: the
 * module also links into small test binaries without -lm. */
static uint64_t round_u64(double x) {
    return x > 0 ? (uint64_t)(x + 0.5) : 0;
}

/* Clock reading as a double, so the integral keeps sub-microsecond parts
 * until the final rounding. */
static double ramp_clock(const shader_clock_ramp *r, uint64_t t_us) {
    if (!r->t0_us) return (double)t_us;

    double e  = (double)t_us - (double)r->t0_us;
    double d  = (double)r->duration_us;
    double c0 = (double)r->c0_us;
    double v0 = r->v0, v1 = r->v1;

    /* Before the ramp (a frame stamped earlier than the request): the clock
     * was moving at v0. */
    if (e <= 0) return c0 + v0 * e;
    /* After it: half the speed change happened over d (the mean of s is
     * 1/2), then v1 holds. */
    if (e >= d) return c0 + d * (v0 + v1) / 2 + v1 * (e - d);

    /* Integral of v0 + (v1 - v0) s(u) from 0 to u, with s = 3u^2 - 2u^3. */
    double u = e / d;
    return c0 + d * (v0 * u + (v1 - v0) * (u * u * u - u * u * u * u / 2));
}

uint64_t shader_ramp_sample(const shader_clock_ramp *r, uint64_t t_us) {
    return round_u64(ramp_clock(r, t_us));
}

double shader_ramp_speed(const shader_clock_ramp *r, uint64_t t_us) {
    if (!r->t0_us) return 1.0;
    if (t_us <= r->t0_us) return r->v0;
    if (t_us - r->t0_us >= r->duration_us) return r->v1;
    double u = (double)(t_us - r->t0_us) / (double)r->duration_us;
    return r->v0 + (r->v1 - r->v0) * (3 * u * u - 2 * u * u * u);
}

void shader_ramp_retarget(shader_clock_ramp *r, uint64_t t_us, float speed, uint32_t ease_ms) {
    double c = ramp_clock(r, t_us);
    double v = shader_ramp_speed(r, t_us);

    *r = (shader_clock_ramp){
        .t0_us       = t_us,
        .c0_us       = round_u64(c),
        .duration_us = round_u64((double)ease_ms * 1000.0 * (speed > v ? speed - v : v - speed)),
        .v0          = (float)v,
        .v1          = speed,
    };
    r->parked = speed == 0.0f && r->duration_us == 0;
}

void shader_ramp_hold(shader_clock_ramp *r, uint64_t t_us) {
    shader_ramp_retarget(r, t_us, 0.0f, 0);
    r->parked = false;
}

float shader_ramp_target(const shader_clock_ramp *r) {
    return r->t0_us ? r->v1 : 1.0f;
}

uint64_t shader_ramp_end(const shader_clock_ramp *r) {
    return r->t0_us ? r->t0_us + r->duration_us : 0;
}

bool shader_ramp_settled(const shader_clock_ramp *r, uint64_t t_us) {
    return !r->t0_us || (t_us >= r->t0_us && t_us - r->t0_us >= r->duration_us);
}

bool shader_ramp_running(const shader_clock_ramp *r, uint64_t t_us) {
    return !r->t0_us || (r->v1 == 1.0f && shader_ramp_settled(r, t_us));
}

bool shader_ramp_stopping(const shader_clock_ramp *r) {
    return r->t0_us && r->v1 == 0.0f && !r->parked;
}

uint64_t shader_ramp_fold_ms(shader_clock_ramp *r, uint64_t t_us) {
    if (!r->t0_us || !shader_ramp_running(r, t_us)) return 0;
    double end = (double)r->t0_us + (double)r->duration_us;
    double lag = end - ramp_clock(r, r->t0_us + r->duration_us);
    *r         = (shader_clock_ramp){0};
    return lag > 0 ? (uint64_t)(lag / 1000.0) : 0;
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
