#ifdef HAVE_WAYLAND_BACKEND

/* Occlusion detection for wlroots compositors.
 * Uses wlr-foreign-toplevel-management-v1 to mark outputs occluded by
 * fullscreen toplevels. Owned by the wlr-layer-shell backend. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include "neowall.h"
#include "compositor.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wayland_occlusion.h"

typedef struct tracked_toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    uint32_t state;
    struct wl_output *output;
    bool has_output;
    struct tracked_toplevel *next;
} tracked_toplevel_t;

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static tracked_toplevel_t *toplevels = NULL;
static struct neowall_state *g_state = NULL;
static bool needs_recalculate = false;

static void recalculate_occlusion(void);
static void remove_toplevel(tracked_toplevel_t *tl);

/* ----- toplevel handle listeners ----- */

static void tl_title(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *t) {
    (void)d; (void)h; (void)t;
}

static void tl_app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *a) {
    (void)d; (void)h; (void)a;
}

static void tl_output_enter(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d;
    (void)h;
    tl->output = output;
    tl->has_output = true;
}

static void tl_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                            struct wl_output *output) {
    tracked_toplevel_t *tl = d;
    (void)h;
    if (tl->output == output) {
        tl->output = NULL;
        tl->has_output = false;
    }
}

static void tl_state(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                     struct wl_array *state) {
    tracked_toplevel_t *tl = d;
    (void)h;
    tl->state = 0;
    uint32_t *s;
    wl_array_for_each(s, state) {
        if (*s <= 3) {
            tl->state |= (1u << *s);
        }
    }
}

static void tl_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)d; (void)h;
    needs_recalculate = true;
}

static void tl_closed(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    remove_toplevel(d);
    needs_recalculate = true;
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

/* ----- manager listeners ----- */

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
    log_info("Foreign toplevel manager finished");
    toplevel_manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener mgr_listener = {
    .toplevel = mgr_toplevel,
    .finished = mgr_finished,
};

/* ----- list helpers ----- */

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

/* ----- recalculation ----- */

static void recalculate_occlusion(void) {
    if (!g_state) {
        return;
    }

    pthread_rwlock_rdlock(&g_state->output_list_lock);

    for (struct output_state *o = g_state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen) {
            continue;
        }

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool now = false;

        for (tracked_toplevel_t *tl = toplevels; tl; tl = tl->next) {
            if (tl->has_output && tl->output == o->native_output &&
                (tl->state & (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN))) {
                now = true;
                break;
            }
        }

        atomic_store_explicit(&o->occluded, now, memory_order_release);

        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        if (was && !now) {
            o->needs_redraw = true;
            log_info("Output %s un-occluded, resuming rendering", name);
        } else if (!was && now) {
            log_info("Output %s occluded by fullscreen window, pausing rendering", name);
        }
    }

    pthread_rwlock_unlock(&g_state->output_list_lock);
}

/* ----- registry bind for the toplevel manager ----- */

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t version) {
    (void)d;
    if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        toplevel_manager = wl_registry_bind(r, name,
                                            &zwlr_foreign_toplevel_manager_v1_interface, v);
        log_debug("Bound to wlr-foreign-toplevel-management v%u", v);
    }
}

static void reg_global_remove(void *d, struct wl_registry *r, uint32_t name) {
    (void)d; (void)r; (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_global_remove,
};

/* ============================================================================
 * Public ops (called by wlr-layer-shell backend via the ops table).
 * The wl_display is supplied by the backend so we don't reach into
 * Wayland globals from here.
 * ============================================================================ */

bool wayland_occlusion_init(struct wl_display *display, struct neowall_state *state) {
    if (!display || !state) {
        return false;
    }

    g_state = state;

    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        return false;
    }

    wl_registry_add_listener(registry, &reg_listener, NULL);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    if (!toplevel_manager) {
        log_debug("wlr-foreign-toplevel-management not available");
        g_state = NULL;
        return false;
    }

    zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager, &mgr_listener, NULL);
    wl_display_roundtrip(display);

    return true;
}

void wayland_occlusion_update(struct neowall_state *state) {
    (void)state;
    if (needs_recalculate) {
        recalculate_occlusion();
        needs_recalculate = false;
    }
}

void wayland_occlusion_cleanup(void) {
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

    g_state = NULL;
    needs_recalculate = false;
}

#endif /* HAVE_WAYLAND_BACKEND */
