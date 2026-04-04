#ifdef HAVE_WAYLAND_BACKEND

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include "neowall.h"
#include "compositor.h"
#include "compositor/backends/wayland.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

/* ============================================================================
 * WAYLAND OCCLUSION DETECTION
 * ============================================================================
 * Uses the wlr-foreign-toplevel-management-unstable-v1 protocol to track
 * fullscreen window state. Supported by Hyprland, Sway, River, and other
 * wlroots-based compositors.
 *
 * The protocol provides:
 * - Notification when toplevels are created/closed
 * - Per-toplevel state (fullscreen, maximized, activated, minimized)
 * - Which output a toplevel is on (output_enter/output_leave)
 *
 * We use this to set output->occluded when a fullscreen window covers it.
 */

/* Tracked toplevel window */
typedef struct tracked_toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    uint32_t state;              /* Bitmask of toplevel states */
    struct wl_output *output;    /* Current output (last entered) */
    bool has_output;             /* Whether we know the output */
    char app_id[128];
    struct tracked_toplevel *next;
} tracked_toplevel_t;

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static tracked_toplevel_t *toplevels = NULL;
static struct neowall_state *g_state = NULL;
static bool needs_recalculate = false;

/* Forward declarations */
static void recalculate_occlusion(void);
static void remove_toplevel(tracked_toplevel_t *tl);

/* ============================================================================
 * TOPLEVEL HANDLE LISTENERS
 * ============================================================================ */

static void toplevel_handle_title(void *data,
                                  struct zwlr_foreign_toplevel_handle_v1 *handle,
                                  const char *title) {
    (void)data;
    (void)handle;
    (void)title;
}

static void toplevel_handle_app_id(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *handle,
                                   const char *app_id) {
    tracked_toplevel_t *tl = data;
    (void)handle;
    if (app_id) {
        strncpy(tl->app_id, app_id, sizeof(tl->app_id) - 1);
        tl->app_id[sizeof(tl->app_id) - 1] = '\0';
    }
}

static void toplevel_handle_output_enter(void *data,
                                         struct zwlr_foreign_toplevel_handle_v1 *handle,
                                         struct wl_output *output) {
    tracked_toplevel_t *tl = data;
    (void)handle;
    tl->output = output;
    tl->has_output = true;
}

static void toplevel_handle_output_leave(void *data,
                                         struct zwlr_foreign_toplevel_handle_v1 *handle,
                                         struct wl_output *output) {
    tracked_toplevel_t *tl = data;
    (void)handle;
    if (tl->output == output) {
        tl->output = NULL;
        tl->has_output = false;
    }
}

static void toplevel_handle_state(void *data,
                                  struct zwlr_foreign_toplevel_handle_v1 *handle,
                                  struct wl_array *state) {
    tracked_toplevel_t *tl = data;
    (void)handle;

    tl->state = 0;
    uint32_t *s;
    wl_array_for_each(s, state) {
        if (*s <= 3) {
            tl->state |= (1u << *s);
        }
    }
}

static void toplevel_handle_done(void *data,
                                 struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data;
    (void)handle;
    /* State committed — flag for recalculation */
    needs_recalculate = true;
}

static void toplevel_handle_closed(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *handle) {
    tracked_toplevel_t *tl = data;
    (void)handle;
    remove_toplevel(tl);
    needs_recalculate = true;
}

static void toplevel_handle_parent(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *handle,
                                   struct zwlr_foreign_toplevel_handle_v1 *parent) {
    (void)data;
    (void)handle;
    (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
    .title = toplevel_handle_title,
    .app_id = toplevel_handle_app_id,
    .output_enter = toplevel_handle_output_enter,
    .output_leave = toplevel_handle_output_leave,
    .state = toplevel_handle_state,
    .done = toplevel_handle_done,
    .closed = toplevel_handle_closed,
    .parent = toplevel_handle_parent,
};

/* ============================================================================
 * MANAGER LISTENERS
 * ============================================================================ */

static void manager_handle_toplevel(void *data,
                                    struct zwlr_foreign_toplevel_manager_v1 *manager,
                                    struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data;
    (void)manager;

    tracked_toplevel_t *tl = calloc(1, sizeof(tracked_toplevel_t));
    if (!tl) {
        return;
    }

    tl->handle = handle;
    tl->next = toplevels;
    toplevels = tl;

    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_listener, tl);
}

static void manager_handle_finished(void *data,
                                    struct zwlr_foreign_toplevel_manager_v1 *manager) {
    (void)data;
    (void)manager;
    log_info("Foreign toplevel manager finished (compositor shutting down protocol)");
    toplevel_manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_handle_toplevel,
    .finished = manager_handle_finished,
};

/* ============================================================================
 * TOPLEVEL LIST MANAGEMENT
 * ============================================================================ */

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

/* ============================================================================
 * OCCLUSION RECALCULATION
 * ============================================================================ */

static void recalculate_occlusion(void) {
    if (!g_state) {
        return;
    }

    pthread_rwlock_rdlock(&g_state->output_list_lock);

    struct output_state *output = g_state->outputs;
    while (output) {
        if (!output->config->pause_on_fullscreen) {
            output = output->next;
            continue;
        }

        bool was_occluded = atomic_load_explicit(&output->occluded, memory_order_acquire);
        bool is_occluded = false;

        tracked_toplevel_t *tl = toplevels;
        while (tl) {
            /* Check if this toplevel is fullscreen on this output */
            if (tl->has_output && tl->output == output->native_output &&
                (tl->state & (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN))) {
                is_occluded = true;
                break;
            }
            tl = tl->next;
        }

        atomic_store_explicit(&output->occluded, is_occluded, memory_order_release);

        if (was_occluded && !is_occluded) {
            output->needs_redraw = true;
            const char *name = output->connector_name[0] ? output->connector_name : output->model;
            log_info("Output %s un-occluded, resuming rendering", name);
        } else if (!was_occluded && is_occluded) {
            const char *name = output->connector_name[0] ? output->connector_name : output->model;
            log_info("Output %s occluded by fullscreen window, pausing rendering", name);
        }

        output = output->next;
    }

    pthread_rwlock_unlock(&g_state->output_list_lock);
}

/* ============================================================================
 * REGISTRY BINDING (for foreign-toplevel protocol)
 * ============================================================================ */

static void occlusion_registry_handle_global(void *data, struct wl_registry *registry,
                                             uint32_t name, const char *interface,
                                             uint32_t version) {
    (void)data;
    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        /* Bind version 3 (supports parent event) or fall back to available version */
        uint32_t bind_version = version < 3 ? version : 3;
        toplevel_manager = wl_registry_bind(registry, name,
                                            &zwlr_foreign_toplevel_manager_v1_interface,
                                            bind_version);
        log_debug("Bound to wlr-foreign-toplevel-management v%u", bind_version);
    }
}

static void occlusion_registry_handle_global_remove(void *data,
                                                     struct wl_registry *registry,
                                                     uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener occlusion_registry_listener = {
    .global = occlusion_registry_handle_global,
    .global_remove = occlusion_registry_handle_global_remove,
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

bool occlusion_wayland_init(struct neowall_state *state) {
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }

    g_state = state;

    /* Do a registry roundtrip to discover the foreign-toplevel protocol */
    struct wl_registry *registry = wl_display_get_registry(wl->display);
    if (!registry) {
        return false;
    }

    wl_registry_add_listener(registry, &occlusion_registry_listener, NULL);
    wl_display_roundtrip(wl->display);
    wl_registry_destroy(registry);

    if (!toplevel_manager) {
        log_debug("wlr-foreign-toplevel-management not available from compositor");
        return false;
    }

    zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager, &manager_listener, NULL);

    /* Do another roundtrip to receive initial toplevel list */
    wl_display_roundtrip(wl->display);

    return true;
}

void occlusion_wayland_update(struct neowall_state *state) {
    (void)state;

    /* Recalculate if any toplevel state changed since last update.
     * The Wayland event dispatch already happened in the main loop,
     * so our listeners have been called and needs_recalculate is set. */
    if (needs_recalculate) {
        recalculate_occlusion();
        needs_recalculate = false;
    }
}

void occlusion_wayland_cleanup(void) {
    /* Free all tracked toplevels */
    tracked_toplevel_t *tl = toplevels;
    while (tl) {
        tracked_toplevel_t *next = tl->next;
        zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
        free(tl);
        tl = next;
    }
    toplevels = NULL;

    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
        zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
        toplevel_manager = NULL;
    }

    g_state = NULL;
    needs_recalculate = false;
}

#endif /* HAVE_WAYLAND_BACKEND */
