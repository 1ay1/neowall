#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "compositor.h"
#include "compositor/backends/wayland.h"
#include "neowall.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"

/*
 * ============================================================================
 * KDE PLASMA BACKEND (using wlr-layer-shell)
 * ============================================================================
 *
 * Backend implementation for KDE Plasma using the zwlr_layer_shell_v1 protocol.
 * This is similar to the wlr-layer-shell backend but with mouse input disabled
 * (empty input region) for proper KDE desktop integration.
 *
 * SUPPORTED COMPOSITORS:
 * - KDE Plasma (KWin)
 *
 * FEATURES:
 * - Background layer placement
 * - Per-output surfaces
 * - Empty input region (clicks pass through to KDE desktop shell)
 * - Start menu closes properly when clicking on desktop
 * - Right-click context menus work
 *
 * PROTOCOL: zwlr_layer_shell_v1 (wlr-layer-shell)
 * Priority: 110 (highest for KDE Plasma - preferred over generic wlr-layer-shell)
 *
 * NOTE: Mouse input (iMouse for shaders) is disabled in this backend to ensure
 * proper KDE desktop integration. Use the generic wlr-layer-shell backend if
 * you need iMouse support and can tolerate start menu issues.
 */

#define BACKEND_NAME "kde-plasma"
#define BACKEND_DESCRIPTION "KDE Plasma backend (wlr-layer-shell with click pass-through)"
#define BACKEND_PRIORITY 110

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    struct zwlr_layer_shell_v1 *layer_shell;
    bool initialized;
} kde_backend_data_t;

/* Surface backend data */
typedef struct {
    struct zwlr_layer_surface_v1 *layer_surface;
    bool configured;
} kde_surface_data_t;

/* ============================================================================
 * LAYER SURFACE CALLBACKS
 * ============================================================================ */

static void layer_surface_configure(void *data,
                                   struct zwlr_layer_surface_v1 *layer_surface,
                                   uint32_t serial,
                                   uint32_t width, uint32_t height) {
    struct compositor_surface *surface = data;

    log_debug("KDE layer surface configure: %ux%u (serial: %u)", width, height, serial);

    /* Acknowledge configuration */
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    /* Update surface dimensions */
    surface->width = width;
    surface->height = height;

    kde_surface_data_t *surface_data = surface->backend_data;
    if (surface_data) {
        surface_data->configured = true;
    }

    /* Call user callback if set */
    if (surface->on_configure) {
        surface->on_configure(surface, width, height);
    }
}

static void layer_surface_closed(void *data,
                                struct zwlr_layer_surface_v1 *layer_surface) {
    struct compositor_surface *surface = data;

    (void)layer_surface;

    log_info("KDE layer surface closed by compositor");

    /* Call user callback if set */
    if (surface->on_closed) {
        surface->on_closed(surface);
    }
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* ============================================================================
 * REGISTRY HANDLING
 * ============================================================================ */

static void registry_handle_global(void *data, struct wl_registry *registry,
                                  uint32_t name, const char *interface,
                                  uint32_t version) {
    kde_backend_data_t *backend_data = data;

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        backend_data->layer_shell = wl_registry_bind(registry, name,
                                                     &zwlr_layer_shell_v1_interface,
                                                     version < 4 ? version : 4);
        log_info("KDE backend: Bound to wlr-layer-shell");
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

static void *kde_backend_init(struct neowall_state *state) {
    wayland_t *wl = wayland_get();
    if (!state || !wl || !wl->display) {
        log_error("Invalid state for KDE backend");
        return NULL;
    }

    log_debug("Initializing KDE Plasma backend");

    /* Allocate backend data */
    kde_backend_data_t *backend_data = calloc(1, sizeof(kde_backend_data_t));
    if (!backend_data) {
        log_error("Failed to allocate KDE backend data");
        return NULL;
    }

    backend_data->state = state;

    /* Get layer shell global */
    struct wl_registry *registry = wl_display_get_registry(wl->display);
    if (!registry) {
        log_error("Failed to get Wayland registry");
        free(backend_data);
        return NULL;
    }

    wl_registry_add_listener(registry, &registry_listener, backend_data);
    wl_display_roundtrip(wl->display);
    wl_registry_destroy(registry);

    /* Check if layer shell is available */
    if (!backend_data->layer_shell) {
        log_error("zwlr_layer_shell_v1 not available for KDE backend");
        free(backend_data);
        return NULL;
    }

    backend_data->initialized = true;
    log_info("KDE Plasma backend initialized successfully (click pass-through enabled)");

    return backend_data;
}

static void kde_backend_cleanup(void *data) {
    if (!data) {
        return;
    }

    log_debug("Cleaning up KDE Plasma backend");

    kde_backend_data_t *backend_data = data;

    if (backend_data->layer_shell) {
        zwlr_layer_shell_v1_destroy(backend_data->layer_shell);
    }

    free(backend_data);

    log_debug("KDE Plasma backend cleanup complete");
}

static struct compositor_surface *kde_create_surface(void *data,
                                                     const compositor_surface_config_t *config) {
    if (!data || !config) {
        log_error("Invalid parameters for KDE surface creation");
        return NULL;
    }

    kde_backend_data_t *backend_data = data;

    if (!backend_data->initialized || !backend_data->layer_shell) {
        log_error("KDE backend not properly initialized");
        return NULL;
    }

    log_debug("Creating KDE layer surface");

    /* Allocate surface structure */
    struct compositor_surface *surface = calloc(1, sizeof(struct compositor_surface));
    if (!surface) {
        log_error("Failed to allocate compositor surface");
        return NULL;
    }

    /* Allocate backend-specific data */
    kde_surface_data_t *surface_data = calloc(1, sizeof(kde_surface_data_t));
    if (!surface_data) {
        log_error("Failed to allocate KDE surface data");
        free(surface);
        return NULL;
    }

    /* Create base Wayland surface */
    wayland_t *wl = wayland_get();
    struct wl_surface *wl_surface = wl_compositor_create_surface(wl->compositor);
    if (!wl_surface) {
        log_error("Failed to create Wayland surface");
        free(surface_data);
        free(surface);
        return NULL;
    }
    surface->native_surface = wl_surface;

    /* Set opaque region to cover entire surface (prevents transparency) */
    struct wl_region *opaque_region = wl_compositor_create_region(wl->compositor);
    if (opaque_region) {
        wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_set_opaque_region(wl_surface, opaque_region);
        wl_region_destroy(opaque_region);
    }

    /* Use BOTTOM layer for KDE - BACKGROUND layer with empty input region
     * causes KDE to destroy the surface. BOTTOM layer is above BACKGROUND
     * but still below windows, and may handle input differently. */
    enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;

    /* Create layer surface */
    surface_data->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        backend_data->layer_shell,
        wl_surface,
        (struct wl_output *)config->output,
        layer,
        "neowall"
    );

    if (!surface_data->layer_surface) {
        log_error("Failed to create KDE layer surface");
        wl_surface_destroy(wl_surface);
        free(surface_data);
        free(surface);
        return NULL;
    }

    /* Add listener */
    zwlr_layer_surface_v1_add_listener(surface_data->layer_surface,
                                      &layer_surface_listener,
                                      surface);

    /* Initialize surface structure */
    surface->backend_data = surface_data;
    surface->native_output = config->output;
    surface->config = *config;
    surface->egl_surface = EGL_NO_SURFACE;
    surface->egl_window = NULL;
    surface->scale = 1;

    /* Configure layer surface */
    zwlr_layer_surface_v1_set_size(surface_data->layer_surface,
                                   config->width, config->height);

    /* Set anchor to fill screen */
    uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(surface_data->layer_surface, anchor);

    /* Set exclusive zone to -1 (don't reserve space) */
    zwlr_layer_surface_v1_set_exclusive_zone(surface_data->layer_surface, -1);

    /* Disable keyboard interactivity */
    zwlr_layer_surface_v1_set_keyboard_interactivity(surface_data->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    /* Set empty input region so all clicks pass through to KDE desktop.
     * This fixes start menu not closing when clicking on desktop.
     * Combined with BOTTOM layer (not BACKGROUND), KDE should not
     * destroy the surface when clicked.
     */
    struct wl_region *input_region = wl_compositor_create_region(wl->compositor);
    if (input_region) {
        /* Empty region = no input (all clicks pass through) */
        wl_surface_set_input_region(wl_surface, input_region);
        wl_region_destroy(input_region);
        log_info("KDE surface: Empty input region set (clicks pass through)");
    }

    /* Enable tearing control for immediate presentation */
    if (wl->tearing_control_manager) {
        struct wp_tearing_control_v1 *tearing = wp_tearing_control_manager_v1_get_tearing_control(
            wl->tearing_control_manager,
            wl_surface
        );
        surface->tearing_control = tearing;

        if (tearing) {
            wp_tearing_control_v1_set_presentation_hint(
                tearing,
                WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
            );
            log_debug("KDE surface: Tearing control enabled");
        }
    }

    /* Commit to apply configuration */
    wl_surface_commit(wl_surface);

    log_debug("KDE layer surface created and configured successfully");

    return surface;
}

static void kde_destroy_surface(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }

    log_debug("Destroying KDE layer surface");

    /* Destroy tearing control if it exists */
    if (surface->tearing_control) {
        wp_tearing_control_v1_destroy(surface->tearing_control);
        surface->tearing_control = NULL;
    }

    /* Destroy EGL window if it exists */
    if (surface->egl_window) {
        wl_egl_window_destroy(surface->egl_window);
        surface->egl_window = NULL;
    }

    /* Destroy backend-specific data */
    if (surface->backend_data) {
        kde_surface_data_t *surface_data = surface->backend_data;

        if (surface_data->layer_surface) {
            zwlr_layer_surface_v1_destroy(surface_data->layer_surface);
        }

        free(surface_data);
    }

    /* Destroy base Wayland surface */
    if (surface->native_surface) {
        wl_surface_destroy((struct wl_surface *)surface->native_surface);
    }

    free(surface);

    log_debug("KDE layer surface destroyed");
}

static bool kde_configure_surface(struct compositor_surface *surface,
                                 const compositor_surface_config_t *config) {
    if (!surface || !config) {
        return false;
    }

    kde_surface_data_t *surface_data = surface->backend_data;
    if (!surface_data || !surface_data->layer_surface) {
        return false;
    }

    log_debug("Configuring KDE surface: %dx%d", config->width, config->height);

    /* Update size */
    zwlr_layer_surface_v1_set_size(surface_data->layer_surface,
                                   config->width, config->height);

    /* Update config cache */
    surface->config = *config;

    return true;
}

static void kde_commit_surface(struct compositor_surface *surface) {
    if (!surface || !surface->native_surface) {
        return;
    }

    wl_surface_commit((struct wl_surface *)surface->native_surface);
    surface->committed = true;
}

static bool kde_create_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface || !surface->native_surface) {
        log_error("Invalid surface for EGL window creation");
        return false;
    }

    log_debug("Creating EGL window for KDE surface: %dx%d", width, height);

    /* Destroy existing EGL window if present */
    if (surface->egl_window) {
        wl_egl_window_destroy(surface->egl_window);
    }

    /* Create new EGL window */
    struct wl_surface *wl_surface = (struct wl_surface *)surface->native_surface;
    surface->egl_window = wl_egl_window_create(wl_surface, width, height);
    if (!surface->egl_window) {
        log_error("Failed to create EGL window");
        return false;
    }

    surface->width = width;
    surface->height = height;

    return true;
}

static void kde_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface || !surface->egl_window) {
        return;
    }

    wl_egl_window_destroy(surface->egl_window);
    surface->egl_window = NULL;
}

static bool kde_resize_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface || !surface->egl_window) {
        return false;
    }

    wl_egl_window_resize(surface->egl_window, width, height, 0, 0);
    surface->width = width;
    surface->height = height;

    return true;
}

static EGLNativeWindowType kde_get_native_window(struct compositor_surface *surface) {
    if (!surface || !surface->egl_window) {
        return (EGLNativeWindowType)0;
    }
    return (EGLNativeWindowType)surface->egl_window;
}

static compositor_capabilities_t kde_get_capabilities(void *data) {
    (void)data;
    return COMPOSITOR_CAP_LAYER_SHELL |
           COMPOSITOR_CAP_EXCLUSIVE_ZONE |
           COMPOSITOR_CAP_ANCHOR |
           COMPOSITOR_CAP_MULTI_OUTPUT;
}

static void kde_on_output_added(void *data, void *output) {
    (void)data;
    (void)output;
    log_debug("KDE backend: output added");
}

static void kde_on_output_removed(void *data, void *output) {
    (void)data;
    (void)output;
    log_debug("KDE backend: output removed");
}

static void kde_damage_surface(struct compositor_surface *surface,
                              int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!surface || !surface->native_surface) {
        return;
    }

    wl_surface_damage((struct wl_surface *)surface->native_surface, x, y, width, height);
}

static void kde_set_scale(struct compositor_surface *surface, int32_t scale) {
    if (!surface || !surface->native_surface || scale < 1) {
        return;
    }

    wl_surface_set_buffer_scale((struct wl_surface *)surface->native_surface, scale);
    surface->scale = scale;
}

/* ============================================================================
 * EVENT HANDLING OPERATIONS
 * ============================================================================ */

static int kde_get_fd(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return -1;
    }
    return wl_display_get_fd(wl->display);
}

static bool kde_prepare_events(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }

    while (wl_display_prepare_read(wl->display) != 0) {
        if (wl_display_dispatch_pending(wl->display) < 0) {
            return false;
        }
    }
    return true;
}

static bool kde_read_events(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }
    return wl_display_read_events(wl->display) >= 0;
}

static bool kde_dispatch_events(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }
    return wl_display_dispatch_pending(wl->display) >= 0;
}

static bool kde_flush(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }

    int ret = wl_display_flush(wl->display);
    if (ret < 0 && errno != EAGAIN) {
        return false;
    }
    return true;
}

static void kde_cancel_read(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (wl && wl->display) {
        wl_display_cancel_read(wl->display);
    }
}

static int kde_get_error(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return -1;
    }
    return wl_display_get_error(wl->display);
}

static bool kde_sync(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl || !wl->display) {
        return false;
    }

    if (wl_display_flush(wl->display) < 0) {
        return false;
    }
    if (wl_display_roundtrip(wl->display) < 0) {
        return false;
    }
    return true;
}

static void *kde_get_native_display(void *data) {
    (void)data;
    wayland_t *wl = wayland_get();
    if (!wl) {
        return NULL;
    }
    return wl->display;
}

static EGLenum kde_get_egl_platform(void *data) {
    (void)data;
    return EGL_PLATFORM_WAYLAND_KHR;
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

static const compositor_backend_ops_t kde_backend_ops = {
    .init = kde_backend_init,
    .cleanup = kde_backend_cleanup,
    .create_surface = kde_create_surface,
    .destroy_surface = kde_destroy_surface,
    .configure_surface = kde_configure_surface,
    .commit_surface = kde_commit_surface,
    .create_egl_window = kde_create_egl_window,
    .destroy_egl_window = kde_destroy_egl_window,
    .resize_egl_window = kde_resize_egl_window,
    .get_native_window = kde_get_native_window,
    .get_capabilities = kde_get_capabilities,
    .on_output_added = kde_on_output_added,
    .on_output_removed = kde_on_output_removed,
    .damage_surface = kde_damage_surface,
    .set_scale = kde_set_scale,

    /* Event handling operations */
    .get_fd = kde_get_fd,
    .prepare_events = kde_prepare_events,
    .read_events = kde_read_events,
    .dispatch_events = kde_dispatch_events,
    .flush = kde_flush,
    .cancel_read = kde_cancel_read,
    .get_error = kde_get_error,
    .sync = kde_sync,
    .get_native_display = kde_get_native_display,
    .get_egl_platform = kde_get_egl_platform,
};

struct compositor_backend *compositor_backend_kde_plasma_init(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state for KDE backend registration");
        return NULL;
    }

    log_debug("Registering KDE Plasma backend");

    /* Register backend in registry */
    if (!compositor_backend_register(BACKEND_NAME,
                                     BACKEND_DESCRIPTION,
                                     BACKEND_PRIORITY,
                                     &kde_backend_ops)) {
        log_error("Failed to register KDE Plasma backend");
        return NULL;
    }

    log_debug("KDE Plasma backend registered successfully");

    return NULL;
}

/*
 * ============================================================================
 * IMPLEMENTATION NOTES
 * ============================================================================
 *
 * This backend provides KDE Plasma-specific support using wlr-layer-shell:
 *
 * KEY DIFFERENCE FROM wlr-layer-shell BACKEND:
 * - Empty input region is set, so all mouse clicks pass through to KDE
 * - This fixes the start menu not closing when clicking on desktop
 * - iMouse shader support is NOT available (mouse position defaults to center)
 *
 * FEATURES:
 * - BACKGROUND layer for proper wallpaper placement
 * - Full GPU-accelerated shader support
 * - Per-output surfaces
 * - Tearing control for smooth rendering
 * - Click pass-through for KDE desktop integration
 *
 * If you need iMouse support for interactive shaders, use the generic
 * wlr-layer-shell backend instead (at the cost of start menu issues).
 */