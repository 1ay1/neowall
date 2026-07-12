#ifdef HAVE_WAYLAND_BACKEND

/* Occlusion detection on wlroots compositors.
 *
 * Combines three independent signals (OR'd together):
 *
 *   1. Frame-callback watchdog (shared frame_watchdog.c). The compositor MAY
 *      stop sending frame callbacks when our surface is obscured.
 *
 *   2. wlr-foreign-toplevel-management state bits. Catches fullscreen and
 *      active-maximized windows on the output.
 *
 *   3. Hyprland IPC coverage (hyprland_coverage.c). Catches the tiled-mosaic
 *      case where many small windows together cover >=80% of the wallpaper
 *      region (monitor minus reserved zone). No-op outside Hyprland. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include "neowall/neowall.h"
#include "neowall/compositor/compositor.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wayland_occlusion.h"
#include "frame_watchdog.h"
#include "hyprland_coverage.h"
#include "occlusion_set.h"

/* ---------- per-toplevel state tracking ---------- */

typedef struct tracked_toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    uint32_t state;
    output_set outputs;   /* every wl_output this toplevel is currently on */
    struct tracked_toplevel *next;
} tracked_toplevel_t;

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static tracked_toplevel_t *toplevels = NULL;

static void free_toplevel(tracked_toplevel_t *tl) {
    output_set_free(&tl->outputs);
    zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
    free(tl);
}

static void remove_toplevel(tracked_toplevel_t *tl) {
    tracked_toplevel_t **pp = &toplevels;
    while (*pp) {
        if (*pp == tl) {
            *pp = tl->next;
            free_toplevel(tl);
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
    /* A toplevel may be visible on several outputs at once, so accumulate
     * rather than overwrite. */
    output_set_add(&tl->outputs, output);
}
static void tl_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d; (void)h;
    output_set_remove(&tl->outputs, output);
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
    output_set_init(&tl->outputs);
    tl->next = toplevels;
    toplevels = tl;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &tl_listener, tl);
}
static void mgr_finished(void *d, struct zwlr_foreign_toplevel_manager_v1 *m) {
    (void)d;
    /* Protocol contract: after `finished` we MUST destroy the proxy. */
    if (m) {
        zwlr_foreign_toplevel_manager_v1_destroy(m);
    }
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

/* Is any toplevel maximized or fullscreen on this wl_output?
 *
 * Subtlety: on tiling compositors (Hyprland in particular) every tiled
 * client gets STATE_MAXIMIZED advertised via wlr-foreign-toplevel, because
 * the tiling layout fills its allocated cell. Treating bare MAXIMIZED as
 * "covering" therefore causes a false positive any time *any* tiled window
 * exists, even on another workspace — the wallpaper stops rendering as
 * soon as the daemon starts.
 *
 * Real fullscreen genuinely covers the output, so we keep that signal
 * unconditional. For MAXIMIZED we additionally require ACTIVATED (the
 * window is focused on its workspace) — a maximized but inactive window
 * on a hidden workspace shouldn't pause us. The tiled-mosaic case where
 * many small windows together cover the wallpaper is handled by the
 * Hyprland IPC coverage path, not by this signal. */
static bool any_covering_toplevel_on(struct wl_output *output) {
    const uint32_t fs = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
    const uint32_t mx = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
    const uint32_t mn = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
    const uint32_t ac = 1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
    for (tracked_toplevel_t *tl = toplevels; tl; tl = tl->next) {
        if (!output_set_contains(&tl->outputs, output)) {
            continue;
        }
        if (tl->state & mn) {
            continue;
        }
        if (tl->state & fs) {
            return true;
        }
        if ((tl->state & mx) && (tl->state & ac)) {
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
    frame_watchdog_init(state);

    /* Bind wlr-foreign-toplevel-management for the per-window state signal.
     * Best-effort: if the compositor doesn't expose it, the other two signals
     * still work. */
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

        /* Keep the shared watchdog armed for this output. */
        frame_watchdog_arm(o);

        bool cb_says_occluded = frame_watchdog_output_occluded(o);
        bool toplevel_says_occluded =
            any_covering_toplevel_on((struct wl_output *)o->native_output);
        float threshold = o->config->pause_coverage_threshold;
        if (!(threshold > 0.0f && threshold <= 1.0f)) {
            threshold = 0.80f;
        }
        bool hypr_says_occluded = hyprland_output_covered(o, threshold);

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool nowocc = cb_says_occluded || toplevel_says_occluded || hypr_says_occluded;

        atomic_store_explicit(&o->occluded, nowocc, memory_order_release);

        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        if (was && !nowocc) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            log_info("Output %s un-occluded, rendering", name);
        } else if (!was && nowocc) {
            const char *why = cb_says_occluded
                ? "compositor stopped frame callbacks"
                : toplevel_says_occluded
                    ? "maximized/fullscreen window covering output"
                    : "tiled windows cover threshold of wallpaper region";
            log_info("Output %s occluded (%s), pausing", name, why);
        }
    }

    pthread_rwlock_unlock(&state->output_list_lock);
}

void wayland_occlusion_cleanup(void) {
    frame_watchdog_cleanup();
    while (toplevels) {
        tracked_toplevel_t *next = toplevels->next;
        free_toplevel(toplevels);
        toplevels = next;
    }
    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
        zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
        toplevel_manager = NULL;
    }
}

#endif /* HAVE_WAYLAND_BACKEND */
