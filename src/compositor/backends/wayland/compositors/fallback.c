#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "compositor.h"
#include "compositor/backends/wayland.h"
#include "neowall.h"

/*
 * ============================================================================
 * FALLBACK BACKEND
 * ============================================================================
 *
 * Universal fallback backend that works on ANY Wayland compositor.
 *
 * This backend uses only core Wayland protocols that are guaranteed to be
 * available on every Wayland compositor. It creates simple surfaces without
 * any special positioning or layer management.
 *
 * APPROACH:
 * - Use wl_compositor to create basic surfaces
 * - Use wl_subsurface for positioning (if available)
 * - No layer management (compositor decides placement)
 * - No exclusive zones or keyboard control
 * - Best effort positioning
 *
 * SUPPORTED COMPOSITORS:
 * - Any Wayland compositor (universal compatibility)
 *
 * LIMITATIONS:
 * - Cannot guarantee background layer placement
 * - No z-order control
 * - May be visible above windows
 * - Limited per-output control
 * - Cannot prevent keyboard/mouse input
 *
 * PRIORITY: 10 (lowest - only used if no other backend works)
 *
 * This backend is a last resort that ensures NeoWall can at least display
 * something on any Wayland compositor, even if it's not ideal.
 */

#define BACKEND_NAME "fallback"
#define BACKEND_DESCRIPTION "Universal Wayland fallback (basic surface support)"
#define BACKEND_PRIORITY 10

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    struct wl_subcompositor *subcompositor;  /* Optional subsurface support */
    bool has_subsurface;
    bool initialized;
} fallback_backend_data_t;

/* Surface backend data */
typedef struct {
    struct wl_subsurface *subsurface;  /* If subsurface is available */
    struct wl_surface *parent_surface; /* Parent for subsurface */
    bool configured;
    bool is_subsurface;
} fallback_surface_data_t;

/* ============================================================================
 * REGISTRY HANDLING
 * ============================================================================ */

static void registry_handle_global(void *data, struct wl_registry *registry,
                                  uint32_t name, const char *interface,
                                  uint32_t version) {
    fallback_backend_data_t *backend_data = data;

    if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        backend_data->subcompositor = wl_registry_bind(registry, name,
                                                       &wl_subcompositor_interface,
                                                       version);
        backend_data->has_subsurface = true;
        log_debug("Bound to wl_subcompositor (version %u)", version);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                         uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* ============================================================================
 * BACKEND OPERATIONS
 * ============================================================================ */

static void *fallback_backend_init(struct neowall_state *state) {
    wayland_t *wl = wayland_get();
    if (!state || !wl || !wl->display) {
        log_error("Invalid state for fallback backend");
        return NULL;
    }

    log_debug("Initializing fallback backend");
    log_info("Using fallback backend - limited features available");
    log_info("Consider using a compositor with wlr-layer-shell support for better integration");

    /* Allocate backend data */
    fallback_backend_data_t *backend_data = calloc(1, sizeof(fallback_backend_data_t));
    if (!backend_data) {
        log_error("Failed to allocate fallback backend data");
        return NULL;
    }

    backend_data->state = state;
    backend_data->has_subsurface = false;

    /* Try to get optional subsurface support */
    struct wl_registry *registry = wl_display_get_registry(wl->display);
    if (registry) {
        wl_registry_add_listener(registry, &registry_listener, backend_data);
        wl_display_roundtrip(wl->display);
        wl_registry_destroy(registry);
    }

    if (backend_data->has_subsurface) {
        log_info("Subsurface support available - will use for positioning");
    } else {
        log_info("No subsurface support - surfaces may not position correctly");
    }

    backend_data->initialized = true;
    log_info("Fallback backend initialized successfully");

    return backend_data;
}

static void fallback_backend_cleanup(void *data) {
    if (!data) {
        return;
    }

    log_debug("Cleaning up fallback backend");

    fallback_backend_data_t *backend_data = data;

    if (backend_data->subcompositor) {
        wl_subcompositor_destroy(backend_data->subcompositor);
    }

    free(backend_data);

    log_debug("Fallback backend cleanup complete");
}

static struct compositor_surface *fallback_create_surface(void *data,
                                                         const compositor_surface_config_t *config) {
    if (!data || !config) {
        log_error("Invalid parameters for fallback surface creation");
        return NULL;
    }

    fallback_backend_data_t *backend_data = data;

    if (!backend_data->initialized) {
        log_error("Backend not properly initialized");
        return NULL;
    }

    log_debug("Creating fallback surface");

    /* Allocate surface structure */
    struct compositor_surface *surface = calloc(1, sizeof(struct compositor_surface));
    if (!surface) {
        log_error("Failed to allocate compositor surface");
        return NULL;
    }

    /* Allocate backend-specific data */
    fallback_surface_data_t *surface_data = calloc(1, sizeof(fallback_surface_data_t));
    if (!surface_data) {
        log_error("Failed to allocate fallback surface data");
        free(surface);
        return NULL;
    }

    /* Create base Wayland surface */
    wayland_t *wl = wayland_get();
    surface->wl_surface = wl_compositor_create_surface(wl->compositor);
    if (!surface->wl_surface) {
        log_error("Failed to create Wayland surface");
        free(surface_data);
        free(surface);
        return NULL;
    }

    /* Set opaque region to cover entire surface (prevents transparency) */
    struct wl_region *opaque_region = wl_compositor_create_region(wl->compositor);
    if (opaque_region) {
        wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_set_opaque_region(surface->wl_surface, opaque_region);
        wl_region_destroy(opaque_region);
        log_debug("Set opaque region for fallback surface");
    }

    /* Try to create subsurface if available */
    if (backend_data->has_subsurface && backend_data->subcompositor) {
        /* For subsurface, we need a parent surface
         * In a real implementation, this would be an existing root surface
         * For now, we'll create a simple surface
         */
        log_debug("Creating subsurface for positioning");

        surface_data->parent_surface = wl_compositor_create_surface(wl->compositor);
        if (surface_data->parent_surface) {
            surface_data->subsurface = wl_subcompositor_get_subsurface(
                backend_data->subcompositor,
                surface->wl_surface,
                surface_data->parent_surface
            );

            if (surface_data->subsurface) {
                /* Place subsurface below parent */
                wl_subsurface_place_below(surface_data->subsurface, surface_data->parent_surface);
                wl_subsurface_set_desync(surface_data->subsurface);
                surface_data->is_subsurface = true;
                log_debug("Subsurface created successfully");
            } else {
                log_debug("Failed to create subsurface, using regular surface");
                wl_surface_destroy(surface_data->parent_surface);
                surface_data->parent_surface = NULL;
            }
        }
    }

    /* Initialize surface structure */
    surface->backend_data = surface_data;
    surface->output = config->output;
    surface->config = *config;
    surface->egl_surface = EGL_NO_SURFACE;
    surface->egl_window = NULL;
    surface->scale = 1;
    surface_data->configured = true;  /* No configuration needed for basic surfaces */

    log_debug("Fallback surface created successfully");
    log_info("Note: Fallback backend cannot guarantee wallpaper placement");

    return surface;
}

static void fallback_destroy_surface(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }

    log_debug("Destroying fallback surface");

    /* Destroy EGL window if it exists */
    if (surface->egl_window) {
        wl_egl_window_destroy(surface->egl_window);
        surface->egl_window = NULL;
    }

    /* Destroy backend-specific data */
    if (surface->backend_data) {
        fallback_surface_data_t *surface_data = surface->backend_data;

        if (surface_data->subsurface) {
            wl_subsurface_destroy(surface_data->subsurface);
        }

        if (surface_data->parent_surface) {
            wl_surface_destroy(surface_data->parent_surface);
        }

        free(surface_data);
    }

    /* Destroy base Wayland surface */
    if (surface->wl_surface) {
        wl_surface_destroy(surface->wl_surface);
    }

    free(surface);

    log_debug("Fallback surface destroyed");
}

static bool fallback_configure_surface(struct compositor_surface *surface,
                                      const compositor_surface_config_t *config) {
    if (!surface || !config) {
        log_error("Invalid parameters for fallback surface configuration");
        return false;
    }

    log_debug("Configuring fallback surface");
    log_info("Note: Fallback backend ignores layer, anchor, and exclusive zone settings");

    /* Update config cache */
    surface->config = *config;

    /* For subsurfaces, try to position based on anchor flags */
    fallback_surface_data_t *surface_data = surface->backend_data;
    if (surface_data && surface_data->is_subsurface && surface_data->subsurface) {
        /* Position subsurface at origin (limited positioning available) */
        wl_subsurface_set_position(surface_data->subsurface, 0, 0);
    }

    /* Set input region to empty (click pass-through) */
    if (surface->wl_surface) {
        wayland_t *wl = wayland_get();

        if (wl && wl->compositor) {
            struct wl_region *region = wl_compositor_create_region(wl->compositor);
            if (region) {
                /* Empty region = no input */
                wl_surface_set_input_region(surface->wl_surface, region);
                wl_region_destroy(region);
                log_debug("Set empty input region for click pass-through");
            }
        }
    }

    return true;
}

static void fallback_commit_surface(struct compositor_surface *surface) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for commit");
        return;
    }

    /* Ensure opaque region is always set (prevents transparency) */
    wayland_t *wl = wayland_get();
    if (wl && wl->compositor) {
        struct wl_region *opaque_region = wl_compositor_create_region(wl->compositor);
        if (opaque_region) {
            wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(surface->wl_surface, opaque_region);
            wl_region_destroy(opaque_region);
        }
    }

    wl_surface_commit(surface->wl_surface);

    /* If subsurface, also commit parent */
    fallback_surface_data_t *surface_data = surface->backend_data;
    if (surface_data && surface_data->parent_surface) {
        wl_surface_commit(surface_data->parent_surface);
    }
}

static bool fallback_create_egl_window(struct compositor_surface *surface,
                                      int32_t width, int32_t height) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for EGL window creation");
        return false;
    }

    log_debug("Creating EGL window for fallback surface: %dx%d", width, height);

    /* Create EGL window */
    surface->egl_window = wl_egl_window_create(surface->wl_surface, width, height);
    if (!surface->egl_window) {
        log_error("Failed to create EGL window");
        return false;
    }

    surface->width = width;
    surface->height = height;

    log_debug("EGL window created successfully");

    return true;
}

static void fallback_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }

    if (surface->egl_window) {
        log_debug("Destroying EGL window");
        wl_egl_window_destroy(surface->egl_window);
        surface->egl_window = NULL;
    }
}

static compositor_capabilities_t fallback_get_capabilities(void *data) {
    fallback_backend_data_t *backend_data = data;

    /* Basic capabilities */
    compositor_capabilities_t caps = COMPOSITOR_CAP_NONE;

    if (backend_data && backend_data->has_subsurface) {
        caps |= COMPOSITOR_CAP_SUBSURFACES;
    }

    return caps;
}

static void fallback_on_output_added(void *data, struct wl_output *output) {
    (void)data;
    (void)output;

    log_debug("Output added to fallback backend");
}

static void fallback_on_output_removed(void *data, struct wl_output *output) {
    (void)data;
    (void)output;

    log_debug("Output removed from fallback backend");
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

/* ============================================================================
 * EVENT HANDLING OPERATIONS
 * ============================================================================ */

static int fallback_get_fd(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }
    return wl_display_get_fd(wl->display);
}

static bool fallback_prepare_events(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    struct wl_display *display = wl->display;

    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
            return false;
        }
    }

    return true;
}

static bool fallback_read_events(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    return wl_display_read_events(wl->display) >= 0;
}

static bool fallback_dispatch_events(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    return wl_display_dispatch_pending(wl->display) >= 0;
}

static bool fallback_flush(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    struct wl_display *display = wl->display;

    if (wl_display_flush(display) < 0) {
        if (errno == EAGAIN) {
            return true;
        }
        return false;
    }

    return true;
}

static void fallback_cancel_read(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return;
    }

    wl_display_cancel_read(wl->display);
}

static int fallback_get_error(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }

    return wl_display_get_error(wl->display);
}

static void *fallback_get_native_display(void *data) {
    fallback_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl) {
        return NULL;
    }
    return wl->display;
}

static EGLenum fallback_get_egl_platform(void *data) {
    (void)data;
    return EGL_PLATFORM_WAYLAND_KHR;
}

static const compositor_backend_ops_t fallback_backend_ops = {
    .init = fallback_backend_init,
    .cleanup = fallback_backend_cleanup,
    .create_surface = fallback_create_surface,
    .destroy_surface = fallback_destroy_surface,
    .configure_surface = fallback_configure_surface,
    .commit_surface = fallback_commit_surface,
    .create_egl_window = fallback_create_egl_window,
    .destroy_egl_window = fallback_destroy_egl_window,
    .get_capabilities = fallback_get_capabilities,
    .on_output_added = fallback_on_output_added,
    .on_output_removed = fallback_on_output_removed,
    /* Event handling operations */
    .get_fd = fallback_get_fd,
    .prepare_events = fallback_prepare_events,
    .read_events = fallback_read_events,
    .dispatch_events = fallback_dispatch_events,
    .flush = fallback_flush,
    .cancel_read = fallback_cancel_read,
    .get_error = fallback_get_error,
    /* Display/EGL operations */
    .get_native_display = fallback_get_native_display,
    .get_egl_platform = fallback_get_egl_platform,
};

struct compositor_backend *compositor_backend_fallback_init(struct neowall_state *state) {
    (void)state;

    /* Register backend in registry */
    compositor_backend_register(BACKEND_NAME,
                                BACKEND_DESCRIPTION,
                                BACKEND_PRIORITY,
                                &fallback_backend_ops);

    return NULL; /* Actual initialization happens in select_backend() */
}

/*
 * ============================================================================
 * IMPLEMENTATION NOTES
 * ============================================================================
 *
 * The fallback backend is designed to work on ANY Wayland compositor by using
 * only the core Wayland protocols that are guaranteed to be available.
 *
 * WHAT IT DOES:
 * - Creates basic wl_surface for rendering
 * - Uses wl_subsurface if available for some positioning control
 * - Sets empty input region for click pass-through
 * - Creates EGL windows for GPU rendering
 *
 * WHAT IT CANNOT DO:
 * - Cannot guarantee background layer placement (no layer-shell)
 * - Cannot control z-order relative to windows
 * - Cannot set exclusive zones
 * - Cannot prevent keyboard focus
 * - May appear above windows or in window lists
 *
 * WHEN TO USE:
 * - When no other backend is available
 * - For testing on uncommon compositors
 * - As a proof-of-concept that rendering works
 *
 * BETTER ALTERNATIVES:
 * - wlr-layer-shell: Use on Hyprland, Sway, River, etc.
 * - KDE Plasma Shell: Use on KDE Plasma
 * - xdg-shell fullscreen: Better than this on GNOME (when implemented)
 *
 * USER EXPERIENCE:
 * Users should be warned that the fallback backend provides degraded
 * functionality. The wallpaper may:
 * - Appear above windows
 * - Show up in alt-tab
 * - Accept keyboard/mouse focus
 * - Not cover entire screen
 * - Behave inconsistently across compositors
 *
 * This is intentionally a "last resort" option with the lowest priority.
 */
