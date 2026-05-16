#ifdef HAVE_WAYLAND_BACKEND

/* Generic Wayland frame-callback watchdog. See frame_watchdog.h. */

#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <wayland-client.h>
#include "neowall.h"
#include "compositor.h"
#include "frame_watchdog.h"

#define OCCLUSION_TIMEOUT_MS 500

typedef struct watched_output {
    struct output_state *output;
    struct wl_callback *callback;
    uint64_t last_done_ms;
    bool armed;
    struct watched_output *next;
} watched_output_t;

static watched_output_t *watched = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static const struct wl_callback_listener frame_listener;

static void arm_locked(watched_output_t *w);

static void on_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    (void)time;
    watched_output_t *w = data;
    pthread_mutex_lock(&lock);
    if (cb) {
        wl_callback_destroy(cb);
    }
    w->callback = NULL;
    w->last_done_ms = get_time_ms();
    w->armed = true;
    arm_locked(w);
    pthread_mutex_unlock(&lock);
}

static const struct wl_callback_listener frame_listener = {
    .done = on_frame_done,
};

static void arm_locked(watched_output_t *w) {
    if (!w || !w->output || !w->output->compositor_surface || w->callback) {
        return;
    }
    struct wl_surface *surf =
        (struct wl_surface *)w->output->compositor_surface->native_surface;
    if (!surf) {
        return;
    }
    w->callback = wl_surface_frame(surf);
    if (w->callback) {
        wl_callback_add_listener(w->callback, &frame_listener, w);
        wl_surface_commit(surf);
    }
}

static watched_output_t *find_locked(struct output_state *o) {
    for (watched_output_t *w = watched; w; w = w->next) {
        if (w->output == o) return w;
    }
    return NULL;
}

static watched_output_t *add_locked(struct output_state *o) {
    watched_output_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->output = o;
    w->last_done_ms = get_time_ms();
    w->next = watched;
    watched = w;
    return w;
}

bool frame_watchdog_init(struct neowall_state *state) {
    (void)state;
    return true;
}

void frame_watchdog_arm(struct output_state *o) {
    if (!o) return;
    pthread_mutex_lock(&lock);
    watched_output_t *w = find_locked(o);
    if (!w) {
        w = add_locked(o);
    }
    if (w && !w->callback) {
        arm_locked(w);
    }
    pthread_mutex_unlock(&lock);
}

bool frame_watchdog_output_occluded(struct output_state *o) {
    if (!o) return false;
    pthread_mutex_lock(&lock);
    watched_output_t *w = find_locked(o);
    bool occ = false;
    if (w && w->armed) {
        occ = (get_time_ms() - w->last_done_ms) > OCCLUSION_TIMEOUT_MS;
    }
    pthread_mutex_unlock(&lock);
    return occ;
}

void frame_watchdog_update(struct neowall_state *state) {
    if (!state) return;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen) continue;
        if (!o->compositor_surface || !o->compositor_surface->native_surface) continue;

        frame_watchdog_arm(o);

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool nowocc = frame_watchdog_output_occluded(o);

        atomic_store_explicit(&o->occluded, nowocc, memory_order_release);

        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        if (was && !nowocc) {
            o->needs_redraw = true;
            log_info("Output %s un-occluded, rendering", name);
        } else if (!was && nowocc) {
            log_info("Output %s occluded (compositor stopped frame callbacks), pausing", name);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

void frame_watchdog_cleanup(void) {
    pthread_mutex_lock(&lock);
    while (watched) {
        watched_output_t *next = watched->next;
        if (watched->callback) {
            wl_callback_destroy(watched->callback);
        }
        free(watched);
        watched = next;
    }
    pthread_mutex_unlock(&lock);
}

#endif /* HAVE_WAYLAND_BACKEND */
