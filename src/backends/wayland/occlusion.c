#ifdef HAVE_WAYLAND_BACKEND

/* Occlusion detection on wlroots compositors.
 *
 * Signals, combined per output:
 *
 *   1. Frame-callback watchdog (shared frame_watchdog.c). The compositor MAY
 *      stop sending frame callbacks when our surface is obscured.
 *
 *   2. Windows. On Hyprland, its IPC (hyprland_coverage.c): fullscreen and
 *      tiled-mosaic coverage of the active workspace. Elsewhere, or while that
 *      IPC isn't answering, wlr-foreign-toplevel-management state bits:
 *      fullscreen and active-maximized windows on the output.
 *
 * Covered = 1 OR 2. Only output_set_occluded writes the result.
 *
 * Nothing here can be trusted to produce an event when a window goes away
 * (a held frame callback, a throttled IPC refresh, a compositor that sends
 * nothing), so the loop also re-runs this on a steady tick while anything is
 * covered (app/subscribe.c). That tick, not an event, is what guarantees an
 * uncovered output comes back. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include "neowall/state/state.h"
#include "neowall/surface/surface.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wayland_occlusion.h"
#include "frame_watchdog.h"
#include "hyprland_coverage.h"
#include "occlusion_set.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/output.h"

/* ---------- per-toplevel state tracking ---------- */

typedef struct tracked_toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    uint32_t state;         /* applied on `done`, as the protocol says */
    uint32_t pending_state; /* the latest `state`, not yet applied */
    output_set outputs;     /* every wl_output this toplevel is currently on */
    struct tracked_toplevel *next;
} tracked_toplevel_t;

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static tracked_toplevel_t *toplevels                             = NULL;

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
    (void)d;
    (void)h;
    (void)t;
}
static void tl_app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *a) {
    (void)d;
    (void)h;
    (void)a;
}
static void tl_output_enter(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d;
    (void)h;
    /* A toplevel may be visible on several outputs at once, so accumulate
     * rather than overwrite. */
    output_set_add(&tl->outputs, output);
}
static void tl_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d;
    (void)h;
    output_set_remove(&tl->outputs, output);
}
static void tl_state(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *state) {
    tracked_toplevel_t *tl = d;
    (void)h;
    tl->pending_state = 0;
    uint32_t *s;
    wl_array_for_each(s, state) {
        if (*s <= 3) {
            tl->pending_state |= (1u << *s);
        }
    }
}
static void tl_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
    tracked_toplevel_t *tl = d;
    (void)h;
    /* `done` ends an atomic batch: a window going from fullscreen to
     * minimised is one change, never a moment of "fullscreen and not". */
    tl->state = tl->pending_state;
}
static void tl_closed(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    remove_toplevel(d);
}
static void tl_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                      struct zwlr_foreign_toplevel_handle_v1 *p) {
    (void)d;
    (void)h;
    (void)p;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener tl_listener = {
    .title        = tl_title,
    .app_id       = tl_app_id,
    .output_enter = tl_output_enter,
    .output_leave = tl_output_leave,
    .state        = tl_state,
    .done         = tl_done,
    .closed       = tl_closed,
    .parent       = tl_parent,
};

static void mgr_toplevel(void *d, struct zwlr_foreign_toplevel_manager_v1 *m,
                         struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)d;
    (void)m;
    tracked_toplevel_t *tl = calloc(1, sizeof(*tl));
    if (!tl) {
        return;
    }
    tl->handle = handle;
    output_set_init(&tl->outputs);
    tl->next  = toplevels;
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
    /* No more news about any of them: a window that was fullscreen when the
     * stream ended would otherwise cover its output forever. */
    while (toplevels) {
        tracked_toplevel_t *next = toplevels->next;
        free_toplevel(toplevels);
        toplevels = next;
    }
}
static const struct zwlr_foreign_toplevel_manager_v1_listener mgr_listener = {
    .toplevel = mgr_toplevel,
    .finished = mgr_finished,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface,
                       uint32_t version) {
    (void)d;
    if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        toplevel_manager =
            wl_registry_bind(r, name, &zwlr_foreign_toplevel_manager_v1_interface, v);
    }
}
static void reg_global_remove(void *d, struct wl_registry *r, uint32_t name) {
    (void)d;
    (void)r;
    (void)name;
}
static const struct wl_registry_listener reg_listener = {
    .global        = reg_global,
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

    /* Every output gets an answer every time, so none can be left holding a
     * stale "covered": not opted in, or no surface yet, means uncovered. */
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen || !o->compositor_surface ||
            !o->compositor_surface->native_surface) {
            frame_watchdog_remove(o);
            output_set_occluded(o, false, NULL);
            continue;
        }

        /* The frame-callback signal: only outputs that animate are watched;
         * for the rest it is simply "visible". */
        bool cb_says_occluded = frame_watchdog_check(o);

        float threshold = o->config->pause_coverage_threshold;
        if (!(threshold > 0.0f && threshold <= 1.0f)) {
            threshold = 0.80f;
        }
        /* Where Hyprland answers, it is the authority on windows: it knows
         * workspaces, where foreign-toplevel only knows "on this output". A
         * fullscreen window on a workspace we switched away from is still
         * FULLSCREEN there, and trusting that kept the wallpaper paused on
         * an empty desktop. Its snapshot covers fullscreen (the window is
         * the monitor's size) as well as tiled mosaics. Elsewhere, or while
         * its IPC is failing, foreign-toplevel is what we have. */
        bool hypr_covered = false;
        bool hypr_known   = hyprland_output_check(o, threshold, &hypr_covered);
        bool window_says_occluded =
            hypr_known ? hypr_covered
                       : any_covering_toplevel_on((struct wl_output *)o->native_output);

        const char *why = cb_says_occluded ? "compositor stopped drawing it"
                          : hypr_known     ? "windows cover the wallpaper"
                                           : "fullscreen or maximized window";
        output_set_occluded(o, cb_says_occluded || window_says_occluded, why);
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
