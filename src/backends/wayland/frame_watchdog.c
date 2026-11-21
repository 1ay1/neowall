#ifdef HAVE_WAYLAND_BACKEND

/* Generic Wayland frame-callback watchdog: the protocol side. Every decision
 * (when to ask, when to re-ask, what counts as covered) is in frame_watch.h,
 * so it can be tested on its own. See frame_watchdog.h for the contract. */

#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <wayland-client.h>
#include "neowall/state/state.h"
#include "neowall/surface/surface.h"
#include "neowall/shader/wallshader.h"
#include "frame_watch.h"
#include "frame_watchdog.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/output.h"

typedef struct watched_output {
    struct output_state *output;
    struct wl_callback *callback; /* the request in flight, or NULL */
    frame_watch state;
    struct watched_output *next;
} watched_output_t;

/* Everything here runs on the loop thread (the occlusion check, output
 * removal, and the callbacks, which are dispatched from the compositor fd).
 * The lock only keeps that honest if a future caller isn't. */
static watched_output_t *watched = NULL;
static pthread_mutex_t lock      = PTHREAD_MUTEX_INITIALIZER;

static void drop_callback_locked(watched_output_t *w) {
    if (w->callback) {
        /* A destroyed proxy never dispatches, so a late `done` for it can't
         * reach `w` after it's freed or count for a request it didn't answer. */
        wl_callback_destroy(w->callback);
        w->callback = NULL;
    }
    w->state.outstanding = false;
}

static void on_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    (void)time;
    watched_output_t *w = data;
    pthread_mutex_lock(&lock);
    if (w->callback == cb) {
        w->callback = NULL;
        frame_watch_done(&w->state, get_time_ms());
    }
    wl_callback_destroy(cb);
    pthread_mutex_unlock(&lock);
}

static const struct wl_callback_listener frame_listener = {
    .done = on_frame_done,
};

static struct wl_surface *surface_of(const struct output_state *o) {
    if (!o || !o->compositor_surface) return NULL;
    return (struct wl_surface *)o->compositor_surface->native_surface;
}

/* Send a fresh request, replacing any in flight. */
static void request_locked(watched_output_t *w, uint64_t now) {
    struct wl_surface *surf = surface_of(w->output);
    if (!surf) return;
    drop_callback_locked(w);
    struct wl_callback *cb = wl_surface_frame(surf);
    if (!cb) return;
    wl_callback_add_listener(cb, &frame_listener, w);
    w->callback = cb;
    /* An explicit commit, so the request is attached even when nothing else
     * commits (a covered, paused or settled output). No damage: a covered
     * surface's damage could make the compositor repaint (and leave direct
     * scanout) under a fullscreen game. */
    wl_surface_commit(surf);
    frame_watch_armed(&w->state, now);
}

static watched_output_t *find_locked(const struct output_state *o) {
    for (watched_output_t *w = watched; w; w = w->next) {
        if (w->output == o) return w;
    }
    return NULL;
}

static watched_output_t *add_locked(struct output_state *o) {
    watched_output_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->output = o;
    w->next   = watched;
    watched   = w;
    return w;
}

bool frame_watchdog_init(struct neowall_state *state) {
    (void)state;
    return true;
}

bool frame_watchdog_wanted(const struct output_state *o) {
    if (!o || !o->config || !o->config->pause_on_fullscreen) return false;
    if (!surface_of(o)) return false;
    /* Only something that animates has anything to pause. A static image or
     * a shader that painted its one frame costs nothing while covered. */
    if (!wallpaper_is_animated(o->config->type) || !o->shader) return false;
    return o->config->type == WALLPAPER_TERMINAL || wallshader_animated(o->shader);
}

void frame_watchdog_arm(struct output_state *o) {
    if (!o) return;
    uint64_t now = get_time_ms();
    pthread_mutex_lock(&lock);
    watched_output_t *w = find_locked(o);
    if (!w) w = add_locked(o);
    if (w) {
        switch (frame_watch_next(&w->state, now)) {
        case FRAME_WATCH_ARM:
        case FRAME_WATCH_REPROBE: request_locked(w, now); break;
        case FRAME_WATCH_IDLE: break;
        }
    }
    pthread_mutex_unlock(&lock);
}

bool frame_watchdog_output_occluded(struct output_state *o) {
    if (!o) return false;
    pthread_mutex_lock(&lock);
    const watched_output_t *w = find_locked(o);
    bool occ                  = w && frame_watch_occluded(&w->state, get_time_ms());
    pthread_mutex_unlock(&lock);
    return occ;
}

void frame_watchdog_remove(struct output_state *o) {
    if (!o) return;
    pthread_mutex_lock(&lock);
    for (watched_output_t **pp = &watched; *pp; pp = &(*pp)->next) {
        watched_output_t *cur = *pp;
        if (cur->output == o) {
            *pp = cur->next;
            drop_callback_locked(cur);
            free(cur);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
}

bool frame_watchdog_check(struct output_state *o) {
    if (!frame_watchdog_wanted(o)) {
        /* Not watched: drop any old measurement, so a request left over from
         * before can't read as "covered" the moment it is watched again. */
        frame_watchdog_remove(o);
        return false;
    }
    frame_watchdog_arm(o);
    return frame_watchdog_output_occluded(o);
}

void frame_watchdog_update(struct neowall_state *state) {
    if (!state) return;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        output_set_occluded(o, frame_watchdog_check(o), "compositor stopped drawing it");
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

void frame_watchdog_cleanup(void) {
    pthread_mutex_lock(&lock);
    while (watched) {
        watched_output_t *next = watched->next;
        drop_callback_locked(watched);
        free(watched);
        watched = next;
    }
    pthread_mutex_unlock(&lock);
}

#endif /* HAVE_WAYLAND_BACKEND */
