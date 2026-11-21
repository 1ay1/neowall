#ifndef WAYLAND_FRAME_WATCH_H
#define WAYLAND_FRAME_WATCH_H

/* The frame-callback watchdog's decisions, as a pure state machine over a
 * millisecond clock. No Wayland here, so it can be tested without a display
 * (tests/test_frame_watch.c); frame_watchdog.c does the protocol side.
 *
 * The idea: ask the compositor for a frame callback. A visible surface gets
 * one back within a frame or two; a surface the compositor isn't drawing
 * (covered by a fullscreen window) doesn't. One unanswered for longer than
 * FRAME_WATCH_TIMEOUT_MS means "covered".
 *
 * Two things make that robust rather than sticky:
 *
 *   - The occlusion clock runs from the FIRST unanswered request
 *     (waiting_since), not the latest. Replacing a request doesn't restart
 *     it, so re-probing never makes a covered output flap visible.
 *
 *   - A request outstanding for FRAME_WATCH_REPROBE_MS is replaced with a
 *     fresh one. Once we stop drawing we commit nothing else, so a callback
 *     the compositor dropped (or held across a surface state change) would
 *     otherwise keep us "covered" for good. */

#include <stdbool.h>
#include <stdint.h>

/* Unanswered this long: the compositor isn't drawing us. */
#define FRAME_WATCH_TIMEOUT_MS 500
/* After an answer, wait this long before asking again. Asking right away
 * made a loop at the refresh rate: a commit and a wakeup every frame. */
#define FRAME_WATCH_REARM_MS 250
/* Replace a request that has gone unanswered this long. */
#define FRAME_WATCH_REPROBE_MS 1000

typedef struct {
    bool outstanding;          /* a callback is in flight */
    uint64_t armed_at_ms;      /* when the in-flight one was requested */
    uint64_t waiting_since_ms; /* first request unanswered since the last answer; 0 = none */
    uint64_t last_done_ms;     /* when the last answer came; 0 = never */
} frame_watch;

typedef enum {
    FRAME_WATCH_IDLE,    /* nothing to do */
    FRAME_WATCH_ARM,     /* request a callback */
    FRAME_WATCH_REPROBE, /* drop the stale request and send a fresh one */
} frame_watch_action;

static inline frame_watch_action frame_watch_next(const frame_watch *w, uint64_t now) {
    if (w->outstanding) {
        return now - w->armed_at_ms >= FRAME_WATCH_REPROBE_MS ? FRAME_WATCH_REPROBE
                                                              : FRAME_WATCH_IDLE;
    }
    if (w->last_done_ms == 0 || now - w->last_done_ms >= FRAME_WATCH_REARM_MS) {
        return FRAME_WATCH_ARM;
    }
    return FRAME_WATCH_IDLE;
}

/* A request went out (a first one, or a replacement). */
static inline void frame_watch_armed(frame_watch *w, uint64_t now) {
    w->outstanding = true;
    w->armed_at_ms = now;
    if (w->waiting_since_ms == 0) w->waiting_since_ms = now;
}

/* The compositor answered: we were drawn. */
static inline void frame_watch_done(frame_watch *w, uint64_t now) {
    w->outstanding      = false;
    w->waiting_since_ms = 0;
    w->last_done_ms     = now ? now : 1;
}

static inline bool frame_watch_occluded(const frame_watch *w, uint64_t now) {
    return w->outstanding && w->waiting_since_ms != 0 &&
           now - w->waiting_since_ms > FRAME_WATCH_TIMEOUT_MS;
}

/* Forget everything: the next request starts a fresh measurement. */
static inline void frame_watch_reset(frame_watch *w) {
    *w = (frame_watch){0};
}

#endif /* WAYLAND_FRAME_WATCH_H */
