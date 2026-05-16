#ifdef HAVE_WAYLAND_BACKEND

/* Occlusion detection on wlroots compositors.
 *
 * We combine three independent signals because no single one is reliable
 * across every compositor + layout:
 *
 *   1. Frame-callback watchdog. The wl_surface.frame request asks the
 *      compositor to ping us when it's ready to present a new frame. The spec
 *      says compositors MAY withhold these when the surface is obscured.
 *      Catches true-fullscreen on Hyprland and off-screen/minimized cases.
 *
 *   2. Toplevel coverage via wlr-foreign-toplevel-management. Tracks per-window
 *      MAXIMIZED / FULLSCREEN state on each output. Catches the Hyprland gap
 *      where the compositor keeps pinging frame callbacks for obscured
 *      surfaces.
 *
 *   3. Hyprland IPC coverage (hyprland_coverage.c). When running under
 *      Hyprland, query its socket for window geometry and rasterize coverage
 *      per output. Catches the "mosaic of small non-maximized windows covers
 *      the wallpaper" case that signals 1 and 2 miss. No-op elsewhere.
 *
 * The output is occluded if ANY signal says so. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include "neowall.h"
#include "compositor.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wayland_occlusion.h"
#include "hyprland_coverage.h"

/* If no frame callback arrives for this long, declare the surface occluded.
 * At 60 Hz a callback should land every ~16 ms; 500 ms is ~30 missed frames
 * — well past any reasonable jitter while still feeling responsive. */
#define OCCLUSION_TIMEOUT_MS 500

/* ---------- per-output frame-callback watchdog ---------- */

typedef struct watched_output {
    struct output_state *output;
    struct wl_callback *callback;       /* in-flight frame callback, or NULL */
    uint64_t last_done_ms;              /* monotonic ms of last frame.done */
    bool armed;                         /* have we ever received a frame.done? */
    struct watched_output *next;
} watched_output_t;

static watched_output_t *watched = NULL;
static const struct wl_callback_listener frame_listener;
static void arm_callback(watched_output_t *w);

static void on_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    (void)time;
    watched_output_t *w = data;
    if (cb) {
        wl_callback_destroy(cb);
    }
    w->callback = NULL;
    w->last_done_ms = get_time_ms();
    w->armed = true;
    arm_callback(w);   /* self-perpetuating heartbeat */
}

static const struct wl_callback_listener frame_listener = {
    .done = on_frame_done,
};

static void arm_callback(watched_output_t *w) {
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

static watched_output_t *watched_for(struct output_state *o) {
    for (watched_output_t *w = watched; w; w = w->next) {
        if (w->output == o) {
            return w;
        }
    }
    return NULL;
}

static watched_output_t *watched_add(struct output_state *o) {
    watched_output_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->output = o;
    w->last_done_ms = get_time_ms();
    w->next = watched;
    watched = w;
    return w;
}

static void watched_free_all(void) {
    while (watched) {
        watched_output_t *next = watched->next;
        if (watched->callback) {
            wl_callback_destroy(watched->callback);
        }
        free(watched);
        watched = next;
    }
}

/* ---------- per-toplevel state tracking ---------- */

typedef struct tracked_toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    uint32_t state;
    struct wl_output *output;
    bool has_output;
    struct tracked_toplevel *next;
} tracked_toplevel_t;

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static tracked_toplevel_t *toplevels = NULL;

static void remove_toplevel(tracked_toplevel_t *tl) {
    tracked_toplevel_t **pp = &toplevels;
    while (*pp) {
        if (*pp == tl) {
            *pp = tl->next;
            zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
            free(tl);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void tl_title(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *t) {
    (void)d; (void)h; (void)t;
}
static void tl_app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *a) {
    (void)d; (void)h; (void)a;
}
static void tl_output_enter(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d; (void)h;
    tl->output = output;
    tl->has_output = true;
}
static void tl_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d; (void)h;
    if (tl->output == output) {
        tl->output = NULL;
        tl->has_output = false;
    }
}
static void tl_state(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                     struct wl_array *state) {
    tracked_toplevel_t *tl = d; (void)h;
    tl->state = 0;
    uint32_t *s;
    wl_array_for_each(s, state) {
        if (*s <= 3) {
            tl->state |= (1u << *s);
        }
    }
}
static void tl_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) { (void)d; (void)h; }
static void tl_closed(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    remove_toplevel(d);
}
static void tl_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                      struct zwlr_foreign_toplevel_handle_v1 *p) {
    (void)d; (void)h; (void)p;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener tl_listener = {
    .title = tl_title,
    .app_id = tl_app_id,
    .output_enter = tl_output_enter,
    .output_leave = tl_output_leave,
    .state = tl_state,
    .done = tl_done,
    .closed = tl_closed,
    .parent = tl_parent,
};

static void mgr_toplevel(void *d, struct zwlr_foreign_toplevel_manager_v1 *m,
                         struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)d; (void)m;
    tracked_toplevel_t *tl = calloc(1, sizeof(*tl));
    if (!tl) {
        return;
    }
    tl->handle = handle;
    tl->next = toplevels;
    toplevels = tl;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &tl_listener, tl);
}
static void mgr_finished(void *d, struct zwlr_foreign_toplevel_manager_v1 *m) {
    (void)d; (void)m;
    toplevel_manager = NULL;
}
static const struct zwlr_foreign_toplevel_manager_v1_listener mgr_listener = {
    .toplevel = mgr_toplevel,
    .finished = mgr_finished,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t version) {
    (void)d;
    if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        toplevel_manager = wl_registry_bind(r, name,
                                            &zwlr_foreign_toplevel_manager_v1_interface, v);
    }
}
static void reg_global_remove(void *d, struct wl_registry *r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_global_remove,
};

/* Is any toplevel maximized or fullscreen on this wl_output? */
static bool any_covering_toplevel_on(struct wl_output *output) {
    const uint32_t fs = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
    const uint32_t mx = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
    const uint32_t mn = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
    for (tracked_toplevel_t *tl = toplevels; tl; tl = tl->next) {
        if (!tl->has_output || tl->output != output) {
            continue;
        }
        if (tl->state & mn) {
            continue;
        }
        if (tl->state & (fs | mx)) {
            return true;
        }
    }
    return false;
}

/* ---------- public ops ---------- */

bool wayland_occlusion_init(struct wl_display *display, struct neowall_state *state) {
    if (!display || !state) {
        return false;
    }
    /* Bind wlr-foreign-toplevel-management for the per-window state signal.
     * This is best-effort: if the compositor doesn't expose it (very rare on
     * wlroots), we fall back to frame-callback-only. */
    struct wl_registry *registry = wl_display_get_registry(display);
    if (registry) {
        wl_registry_add_listener(registry, &reg_listener, NULL);
        wl_display_roundtrip(display);
        wl_registry_destroy(registry);
    }
    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager, &mgr_listener, NULL);
        wl_display_roundtrip(display);
    }
    return true;
}

void wayland_occlusion_update(struct neowall_state *state) {
    if (!state) {
        return;
    }
    uint64_t now = get_time_ms();

    /* Refresh Hyprland snapshot (self-throttled). No-op elsewhere. */
    hyprland_coverage_refresh();

    pthread_rwlock_rdlock(&state->output_list_lock);

    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen) {
            continue;
        }
        if (!o->compositor_surface || !o->compositor_surface->native_surface) {
            continue;
        }

        /* Ensure a watchdog entry + in-flight frame callback for this output. */
        watched_output_t *w = watched_for(o);
        if (!w) {
            w = watched_add(o);
            if (!w) {
                continue;
            }
        }
        if (!w->callback) {
            arm_callback(w);
        }

        /* Signal 1: frame-callback silence. Only trust after we've seen the
         * first done, otherwise we'd false-positive at startup. */
        bool cb_says_occluded =
            w->armed && (now - w->last_done_ms) > OCCLUSION_TIMEOUT_MS;

        /* Signal 2: any maximized/fullscreen toplevel on this output. */
        bool toplevel_says_occluded =
            any_covering_toplevel_on((struct wl_output *)o->native_output);

        /* Signal 3: Hyprland-specific tiled coverage (>=80% of monitor). */
        bool hypr_says_occluded = hyprland_output_covered(o, 0.80f);

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool nowocc = cb_says_occluded || toplevel_says_occluded || hypr_says_occluded;

        atomic_store_explicit(&o->occluded, nowocc, memory_order_release);

        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        if (was && !nowocc) {
            o->needs_redraw = true;
            log_info("Output %s un-occluded, rendering", name);
        } else if (!was && nowocc) {
            const char *why = cb_says_occluded
                ? "compositor stopped frame callbacks"
                : toplevel_says_occluded
                    ? "maximized/fullscreen window covering output"
                    : "tiled windows cover >=80% of output";
            log_info("Output %s occluded (%s), pausing", name, why);
        }
    }

    pthread_rwlock_unlock(&state->output_list_lock);
}

void wayland_occlusion_cleanup(void) {
    watched_free_all();
    while (toplevels) {
        tracked_toplevel_t *next = toplevels->next;
        zwlr_foreign_toplevel_handle_v1_destroy(toplevels->handle);
        free(toplevels);
        toplevels = next;
    }
    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
        zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
        toplevel_manager = NULL;
    }
}

#endif /* HAVE_WAYLAND_BACKEND */
