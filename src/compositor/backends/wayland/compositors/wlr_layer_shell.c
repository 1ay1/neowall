#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "compositor.h"
#include "neowall.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"

/*
 * ============================================================================
 * WLR-LAYER-SHELL BACKEND
 * ============================================================================
 *
 * Backend implementation for wlroots-based compositors using the
 * zwlr_layer_shell_v1 protocol.
 *
 * SUPPORTED COMPOSITORS:
 * - KDE Plasma (KWin) - Full support, recommended backend
 * - Hyprland
 * - Sway
 * - River
 * - Wayfire
 * - Any wlroots-based compositor
 *
 * FEATURES:
 * - Background layer placement
 * - Per-output surfaces
 * - Exclusive zones
 * - Keyboard interactivity control
 * - Surface anchoring
 *
 * PRIORITY: 100 (highest - preferred for wlroots compositors)
 */

#define BACKEND_NAME "wlr-layer-shell"
#define BACKEND_DESCRIPTION "wlroots layer shell protocol (KDE, Hyprland, Sway, River, etc.)"
#define BACKEND_PRIORITY 100

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    struct zwlr_layer_shell_v1 *layer_shell;
    bool initialized;
} wlr_backend_data_t;

/* Surface backend data */
typedef struct {
    struct zwlr_layer_surface_v1 *layer_surface;
    bool configured;
} wlr_surface_data_t;

/* ============================================================================
 * LAYER SURFACE CALLBACKS
 * ============================================================================ */

static void layer_surface_configure(void *data,
                                   struct zwlr_layer_surface_v1 *layer_surface,
                                   uint32_t serial,
                                   uint32_t width, uint32_t height) {
    struct compositor_surface *surface = data;
    
    (void)layer_surface;
    
    log_debug("Layer surface configure: %ux%u (serial: %u)", width, height, serial);
    
    /* Acknowledge configuration */
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    
    /* Update surface dimensions */
    surface->width = width;
    surface->height = height;
    
    wlr_surface_data_t *backend_data = surface->backend_data;
    if (backend_data) {
        backend_data->configured = true;
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
    
    log_info("Layer surface closed by compositor");
    
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
    wlr_backend_data_t *backend_data = data;
    
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        backend_data->layer_shell = wl_registry_bind(registry, name,
                                                     &zwlr_layer_shell_v1_interface,
                                                     version < 4 ? version : 4);
        log_debug("Bound to zwlr_layer_shell_v1 (version %u)", version);
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

static void *wlr_backend_init(struct neowall_state *state) {
    if (!state || !state->display) {
        log_error("Invalid state for wlr-layer-shell backend");
        return NULL;
    }
    
    log_debug("Initializing wlr-layer-shell backend");
    
    /* Allocate backend data */
    wlr_backend_data_t *backend_data = calloc(1, sizeof(wlr_backend_data_t));
    if (!backend_data) {
        log_error("Failed to allocate wlr backend data");
        return NULL;
    }
    
    backend_data->state = state;
    
    /* Get layer shell global */
    struct wl_registry *registry = wl_display_get_registry(state->display);
    if (!registry) {
        log_error("Failed to get Wayland registry");
        free(backend_data);
        return NULL;
    }
    
    wl_registry_add_listener(registry, &registry_listener, backend_data);
    wl_display_roundtrip(state->display);
    wl_registry_destroy(registry);
    
    /* Check if layer shell is available */
    if (!backend_data->layer_shell) {
        log_error("zwlr_layer_shell_v1 not available");
        free(backend_data);
        return NULL;
    }
    
    backend_data->initialized = true;
    log_info("wlr-layer-shell backend initialized successfully");
    
    return backend_data;
}

static void wlr_backend_cleanup(void *data) {
    if (!data) {
        return;
    }
    
    log_debug("Cleaning up wlr-layer-shell backend");
    
    wlr_backend_data_t *backend_data = data;
    
    if (backend_data->layer_shell) {
        zwlr_layer_shell_v1_destroy(backend_data->layer_shell);
    }
    
    free(backend_data);
    
    log_debug("wlr-layer-shell backend cleanup complete");
}

static struct compositor_surface *wlr_create_surface(void *data,
                                                     const compositor_surface_config_t *config) {
    if (!data || !config) {
        log_error("Invalid parameters for surface creation");
        return NULL;
    }
    
    wlr_backend_data_t *backend_data = data;
    
    if (!backend_data->initialized || !backend_data->layer_shell) {
        log_error("Backend not properly initialized");
        return NULL;
    }
    
    log_debug("Creating wlr layer surface");
    
    /* Allocate surface structure */
    struct compositor_surface *surface = calloc(1, sizeof(struct compositor_surface));
    if (!surface) {
        log_error("Failed to allocate compositor surface");
        return NULL;
    }
    
    /* Allocate backend-specific data */
    wlr_surface_data_t *surface_data = calloc(1, sizeof(wlr_surface_data_t));
    if (!surface_data) {
        log_error("Failed to allocate wlr surface data");
        free(surface);
        return NULL;
    }
    
    /* Create base Wayland surface */
    surface->wl_surface = wl_compositor_create_surface(backend_data->state->compositor);
    if (!surface->wl_surface) {
        log_error("Failed to create Wayland surface");
        free(surface_data);
        free(surface);
        return NULL;
    }
    
    /* Set opaque region to cover entire surface (prevents transparency) */
    struct wl_region *opaque_region = wl_compositor_create_region(backend_data->state->compositor);
    if (opaque_region) {
        wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_set_opaque_region(surface->wl_surface, opaque_region);
        wl_region_destroy(opaque_region);
    }
    
    /* Map layer value */
    enum zwlr_layer_shell_v1_layer layer;
    switch (config->layer) {
        case COMPOSITOR_LAYER_BACKGROUND:
            layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
            break;
        case COMPOSITOR_LAYER_BOTTOM:
            layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
            break;
        case COMPOSITOR_LAYER_TOP:
            layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
            break;
        case COMPOSITOR_LAYER_OVERLAY:
            layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
            break;
        default:
            layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    }
    
    /* Create layer surface */
    surface_data->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        backend_data->layer_shell,
        surface->wl_surface,
        config->output,
        layer,
        "neowall"
    );
    
    if (!surface_data->layer_surface) {
        log_error("Failed to create layer surface");
        wl_surface_destroy(surface->wl_surface);
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
    surface->output = config->output;
    surface->config = *config;
    surface->egl_surface = EGL_NO_SURFACE;
    surface->egl_window = NULL;
    surface->scale = 1;
    
    /* Configure layer surface immediately to avoid protocol errors */
    /* Set size */
    zwlr_layer_surface_v1_set_size(surface_data->layer_surface,
                                   config->width, config->height);
    
    /* Set anchor */
    uint32_t anchor = 0;
    if (config->anchor & COMPOSITOR_ANCHOR_TOP)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (config->anchor & COMPOSITOR_ANCHOR_BOTTOM)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (config->anchor & COMPOSITOR_ANCHOR_LEFT)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (config->anchor & COMPOSITOR_ANCHOR_RIGHT)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    
    zwlr_layer_surface_v1_set_anchor(surface_data->layer_surface, anchor);
    
    /* Set exclusive zone */
    zwlr_layer_surface_v1_set_exclusive_zone(surface_data->layer_surface,
                                            config->exclusive_zone);
    
    /* Set keyboard interactivity */
    enum zwlr_layer_surface_v1_keyboard_interactivity kb_mode =
        config->keyboard_interactivity ?
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE :
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    
    zwlr_layer_surface_v1_set_keyboard_interactivity(surface_data->layer_surface, kb_mode);
    
    /* Enable tearing control for immediate presentation (bypasses compositor vsync) */
    if (backend_data->state->tearing_control_manager) {
        surface->tearing_control = wp_tearing_control_manager_v1_get_tearing_control(
            backend_data->state->tearing_control_manager,
            surface->wl_surface
        );
        
        if (surface->tearing_control) {
            /* Set presentation hint to async (immediate/tearing allowed) */
            wp_tearing_control_v1_set_presentation_hint(
                surface->tearing_control,
                WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
            );
            log_info("Enabled tearing control for immediate presentation (bypasses compositor FPS limits)");
        } else {
            log_error("Failed to create tearing control object");
        }
    } else {
        log_debug("Tearing control manager not available - FPS may be limited by compositor");
    }
    
    log_debug("wlr layer surface created and configured successfully");
    
    return surface;
}

static void wlr_destroy_surface(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }
    
    log_debug("Destroying wlr layer surface");
    
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
        wlr_surface_data_t *surface_data = surface->backend_data;
        
        if (surface_data->layer_surface) {
            zwlr_layer_surface_v1_destroy(surface_data->layer_surface);
        }
        
        free(surface_data);
    }
    
    /* Destroy base Wayland surface */
    if (surface->wl_surface) {
        wl_surface_destroy(surface->wl_surface);
    }
    
    free(surface);
    
    log_debug("wlr layer surface destroyed");
}

static bool wlr_configure_surface(struct compositor_surface *surface,
                                 const compositor_surface_config_t *config) {
    if (!surface || !config) {
        log_error("Invalid parameters for surface configuration");
        return false;
    }
    
    wlr_surface_data_t *surface_data = surface->backend_data;
    if (!surface_data || !surface_data->layer_surface) {
        log_error("Invalid surface data for configuration");
        return false;
    }
    
    log_debug("Configuring wlr layer surface");
    
    /* Set size */
    zwlr_layer_surface_v1_set_size(surface_data->layer_surface,
                                   config->width, config->height);
    
    /* Set anchor */
    uint32_t anchor = 0;
    if (config->anchor & COMPOSITOR_ANCHOR_TOP)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (config->anchor & COMPOSITOR_ANCHOR_BOTTOM)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (config->anchor & COMPOSITOR_ANCHOR_LEFT)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (config->anchor & COMPOSITOR_ANCHOR_RIGHT)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    
    zwlr_layer_surface_v1_set_anchor(surface_data->layer_surface, anchor);
    
    /* Set exclusive zone */
    zwlr_layer_surface_v1_set_exclusive_zone(surface_data->layer_surface,
                                            config->exclusive_zone);
    
    /* Set keyboard interactivity */
    enum zwlr_layer_surface_v1_keyboard_interactivity kb_mode =
        config->keyboard_interactivity ?
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE :
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    
    zwlr_layer_surface_v1_set_keyboard_interactivity(surface_data->layer_surface, kb_mode);
    
    /* Update config cache */
    surface->config = *config;
    
    log_debug("wlr layer surface configured");
    
    return true;
}

static void wlr_commit_surface(struct compositor_surface *surface) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for commit");
        return;
    }
    
    /* Ensure opaque region is always set (prevents transparency) */
    wlr_backend_data_t *backend_data = surface->backend->data;
    if (backend_data && backend_data->state && backend_data->state->compositor) {
        struct wl_region *opaque_region = wl_compositor_create_region(backend_data->state->compositor);
        if (opaque_region) {
            wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(surface->wl_surface, opaque_region);
            wl_region_destroy(opaque_region);
        }
    }
    
    wl_surface_commit(surface->wl_surface);
}

static bool wlr_create_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for EGL window creation");
        return false;
    }
    
    log_debug("Creating EGL window: %dx%d", width, height);
    
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

static void wlr_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }
    
    if (surface->egl_window) {
        log_debug("Destroying EGL window");
        wl_egl_window_destroy(surface->egl_window);
        surface->egl_window = NULL;
    }
}

static compositor_capabilities_t wlr_get_capabilities(void *data) {
    (void)data;
    
    return COMPOSITOR_CAP_LAYER_SHELL |
           COMPOSITOR_CAP_EXCLUSIVE_ZONE |
           COMPOSITOR_CAP_KEYBOARD_INTERACTIVITY |
           COMPOSITOR_CAP_ANCHOR |
           COMPOSITOR_CAP_MULTI_OUTPUT;
}

static void wlr_on_output_added(void *data, struct wl_output *output) {
    (void)data;
    (void)output;
    
    log_debug("Output added to wlr backend");
}

static void wlr_on_output_removed(void *data, struct wl_output *output) {
    (void)data;
    (void)output;
    
    log_debug("Output removed from wlr backend");
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

static const compositor_backend_ops_t wlr_backend_ops = {
    .init = wlr_backend_init,
    .cleanup = wlr_backend_cleanup,
    .create_surface = wlr_create_surface,
    .destroy_surface = wlr_destroy_surface,
    .configure_surface = wlr_configure_surface,
    .commit_surface = wlr_commit_surface,
    .create_egl_window = wlr_create_egl_window,
    .destroy_egl_window = wlr_destroy_egl_window,
    .get_capabilities = wlr_get_capabilities,
    .on_output_added = wlr_on_output_added,
    .on_output_removed = wlr_on_output_removed,
};

struct compositor_backend *compositor_backend_wlr_layer_shell_init(struct neowall_state *state) {
    (void)state;
    
    /* Register backend in registry */
    compositor_backend_register(BACKEND_NAME,
                                BACKEND_DESCRIPTION,
                                BACKEND_PRIORITY,
                                &wlr_backend_ops);
    
    return NULL; /* Actual initialization happens in select_backend() */
}