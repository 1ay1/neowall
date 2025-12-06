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

/*
 * ============================================================================
 * GNOME SHELL BACKEND
 * ============================================================================
 *
 * Backend implementation for GNOME Shell/Mutter using subsurface fallback
 * method since GNOME doesn't expose layer-shell or plasma-shell protocols.
 *
 * SUPPORTED COMPOSITORS:
 * - GNOME Shell (Mutter)
 * - Any Wayland compositor without layer shell support
 *
 * APPROACH:
 * Since GNOME doesn't provide a standard way to create wallpapers, we use
 * a fallback approach:
 * 1. Create a fullscreen window
 * 2. Make it always-below other windows (if possible)
 * 3. Remove decorations and make it non-interactive
 * 4. Position it behind all other windows
 *
 * LIMITATIONS:
 * - May not always stay behind windows (compositor-dependent)
 * - Cannot guarantee true background layer placement
 * - May be visible in alt-tab/overview
 * - Keyboard focus issues possible
 *
 * PROTOCOL: Standard Wayland + xdg-shell
 * Priority: 80 (for GNOME/Mutter)
 *
 * TODO: This is a stub implementation. Full implementation requires:
 * 1. xdg-shell window creation
 * 2. Window configuration (fullscreen, no decorations)
 * 3. Z-order management (keep below)
 * 4. Input region configuration (pass-through clicks)
 */

#define BACKEND_NAME "gnome-shell"
#define BACKEND_DESCRIPTION "GNOME Shell/Mutter subsurface fallback"
#define BACKEND_PRIORITY 80

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    struct wl_shell *wl_shell;           /* Legacy wl_shell (deprecated) */
    void *xdg_wm_base;                   /* TODO: struct xdg_wm_base* */
    bool initialized;
    bool use_xdg_shell;                  /* Use xdg-shell vs legacy wl_shell */
} gnome_backend_data_t;

/* Surface backend data */
typedef struct {
    void *xdg_surface;                   /* TODO: struct xdg_surface* */
    void *xdg_toplevel;                  /* TODO: struct xdg_toplevel* */
    bool configured;
} gnome_surface_data_t;

/* ============================================================================
 * BACKEND OPERATIONS
 * ============================================================================ */

static void *gnome_backend_init(struct neowall_state *state) {
    wayland_t *wl = wayland_get();
    if (!state || !wl || !wl->display) {
        log_error("Invalid state for GNOME Shell backend");
        return NULL;
    }

    log_debug("Initializing GNOME Shell backend");

    /* TODO: Implement GNOME Shell subsurface fallback
     *
     * Implementation strategy:
     *
     * 1. Check for xdg_wm_base interface (modern way)
     * 2. Fall back to wl_shell if xdg not available (legacy)
     * 3. Create fullscreen window positioned behind everything
     * 4. Configure input region to be empty (pass-through)
     * 5. Set window type hints if available
     *
     * Note: GNOME doesn't officially support wallpaper replacement
     * by third-party apps, so this is inherently a workaround.
     */

    log_info("GNOME Shell backend is not yet implemented (stub)");
    log_info("This backend requires xdg-shell protocol for window creation");
    log_info("Note: GNOME has limited support for custom wallpaper daemons");

    /* Allocate backend data */
    gnome_backend_data_t *backend_data = calloc(1, sizeof(gnome_backend_data_t));
    if (!backend_data) {
        log_error("Failed to allocate GNOME backend data");
        return NULL;
    }

    backend_data->state = state;
    backend_data->use_xdg_shell = true;

    /* TODO: Bind to xdg_wm_base or wl_shell */

    /* Return NULL to indicate backend is not available (stub) */
    free(backend_data);
    return NULL;
}

static void gnome_backend_cleanup(void *data) {
    if (!data) {
        return;
    }

    log_debug("Cleaning up GNOME Shell backend");

    gnome_backend_data_t *backend_data = data;

    /* TODO: Cleanup xdg-shell resources */
    if (backend_data->xdg_wm_base) {
        /* xdg_wm_base_destroy(backend_data->xdg_wm_base); */
    }

    if (backend_data->wl_shell) {
        /* wl_shell_destroy(backend_data->wl_shell); */
    }

    free(backend_data);
}

static struct compositor_surface *gnome_create_surface(void *data,
                                                       const compositor_surface_config_t *config) {
    if (!data || !config) {
        log_error("Invalid parameters for GNOME surface creation");
        return NULL;
    }

    log_debug("Creating GNOME Shell surface");

    gnome_backend_data_t *backend_data = data;

    /* TODO: Implement GNOME Shell surface creation
     *
     * xdg-shell approach:
     * 1. Create base wl_surface
     * 2. Get xdg_surface from xdg_wm_base
     * 3. Get xdg_toplevel from xdg_surface
     * 4. Configure as fullscreen on target output
     * 5. Remove decorations (CSD)
     * 6. Set empty input region
     * 7. Commit surface
     *
     * Window properties to set:
     * - Fullscreen on specific output
     * - No decorations
     * - No keyboard focus
     * - Pass-through mouse events
     * - Always below other windows (if possible)
     */

    if (!backend_data->use_xdg_shell) {
        log_error("Legacy wl_shell not supported in this stub");
        return NULL;
    }

    log_error("GNOME Shell surface creation not implemented");
    return NULL;
}

static void gnome_destroy_surface(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }

    log_debug("Destroying GNOME Shell surface");

    if (surface->backend_data) {
        gnome_surface_data_t *surface_data = surface->backend_data;

        if (surface_data->xdg_toplevel) {
            /* xdg_toplevel_destroy(surface_data->xdg_toplevel); */
        }

        if (surface_data->xdg_surface) {
            /* xdg_surface_destroy(surface_data->xdg_surface); */
        }

        free(surface_data);
    }

    if (surface->egl_window) {
        wl_egl_window_destroy(surface->egl_window);
    }

    if (surface->wl_surface) {
        wl_surface_destroy(surface->wl_surface);
    }

    free(surface);
}

static bool gnome_configure_surface(struct compositor_surface *surface,
                                   const compositor_surface_config_t *config) {
    if (!surface || !config) {
        log_error("Invalid parameters for GNOME surface configuration");
        return false;
    }

    log_debug("Configuring GNOME Shell surface");

    /* TODO: Implement GNOME Shell surface configuration
     *
     * Configuration steps:
     * 1. Set fullscreen mode on target output
     * 2. Configure input region (empty for click pass-through)
     * 3. Set window hints (stay below, skip taskbar, etc.)
     * 4. Handle compositor configure events
     */

    surface->config = *config;

    return true;
}

static void gnome_commit_surface(struct compositor_surface *surface) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for commit");
        return;
    }

    wl_surface_commit(surface->wl_surface);
}

static bool gnome_create_egl_window(struct compositor_surface *surface,
                                   int32_t width, int32_t height) {
    if (!surface || !surface->wl_surface) {
        log_error("Invalid surface for EGL window creation");
        return false;
    }

    log_debug("Creating EGL window for GNOME surface: %dx%d", width, height);

    surface->egl_window = wl_egl_window_create(surface->wl_surface, width, height);
    if (!surface->egl_window) {
        log_error("Failed to create EGL window");
        return false;
    }

    surface->width = width;
    surface->height = height;

    return true;
}

static void gnome_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }

    if (surface->egl_window) {
        wl_egl_window_destroy(surface->egl_window);
        surface->egl_window = NULL;
    }
}

static compositor_capabilities_t gnome_get_capabilities(void *data) {
    (void)data;

    /* Limited capabilities - subsurface fallback */
    return COMPOSITOR_CAP_SUBSURFACES;
}

static void gnome_on_output_added(void *data, struct wl_output *output) {
    (void)data;
    (void)output;

    log_debug("Output added to GNOME backend");
}

static void gnome_on_output_removed(void *data, struct wl_output *output) {
    (void)data;
    (void)output;

    log_debug("Output removed from GNOME backend");
}

/* ============================================================================
 * EVENT HANDLING OPERATIONS
 * ============================================================================ */

static int gnome_get_fd(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }
    return wl_display_get_fd(wl->display);
}

static bool gnome_prepare_events(void *data) {
    gnome_backend_data_t *backend = data;
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

static bool gnome_read_events(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    return wl_display_read_events(wl->display) >= 0;
}

static bool gnome_dispatch_events(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    return wl_display_dispatch_pending(wl->display) >= 0;
}

static bool gnome_flush(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    int ret = wl_display_flush(wl->display);
    if (ret < 0 && errno != EAGAIN) {
        return false;
    }
    return true;
}

static void gnome_cancel_read(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return;
    }

    wl_display_cancel_read(wl->display);
}

static int gnome_get_error(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }

    return wl_display_get_error(wl->display);
}

static bool gnome_sync(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    /* Flush pending requests and wait for server to process */
    if (wl_display_flush(wl->display) < 0) {
        return false;
    }
    if (wl_display_roundtrip(wl->display) < 0) {
        return false;
    }
    return true;
}

static void *gnome_get_native_display(void *data) {
    gnome_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl) {
        return NULL;
    }
    return wl->display;
}

static EGLenum gnome_get_egl_platform(void *data) {
    (void)data;
    return EGL_PLATFORM_WAYLAND_KHR;
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

static const compositor_backend_ops_t gnome_backend_ops = {
    .init = gnome_backend_init,
    .cleanup = gnome_backend_cleanup,
    .create_surface = gnome_create_surface,
    .destroy_surface = gnome_destroy_surface,
    .configure_surface = gnome_configure_surface,
    .commit_surface = gnome_commit_surface,
    .create_egl_window = gnome_create_egl_window,
    .destroy_egl_window = gnome_destroy_egl_window,
    .get_capabilities = gnome_get_capabilities,
    .on_output_added = gnome_on_output_added,
    .on_output_removed = gnome_on_output_removed,

    /* Event handling operations */
    .get_fd = gnome_get_fd,
    .prepare_events = gnome_prepare_events,
    .read_events = gnome_read_events,
    .dispatch_events = gnome_dispatch_events,
    .flush = gnome_flush,
    .cancel_read = gnome_cancel_read,
    .get_error = gnome_get_error,
    .sync = gnome_sync,
    .get_native_display = gnome_get_native_display,
    .get_egl_platform = gnome_get_egl_platform,
};

struct compositor_backend *compositor_backend_gnome_shell_init(struct neowall_state *state) {
    (void)state;

    /* Register backend in registry */
    compositor_backend_register(BACKEND_NAME,
                                BACKEND_DESCRIPTION,
                                BACKEND_PRIORITY,
                                &gnome_backend_ops);

    return NULL; /* Actual initialization happens in select_backend() */
}

/*
 * ============================================================================
 * IMPLEMENTATION NOTES
 * ============================================================================
 *
 * GNOME Shell Wallpaper Challenges:
 *
 * 1. GNOME doesn't expose layer-shell or similar protocols
 * 2. Native wallpaper is managed by gnome-settings-daemon
 * 3. No official API for third-party wallpaper daemons
 *
 * Possible Approaches:
 *
 * A. xdg-shell Fullscreen Window (This stub):
 *    - Create fullscreen window on each output
 *    - Configure as "always below" if possible
 *    - Set empty input region for click pass-through
 *    - Limitations: May appear in alt-tab, not guaranteed to stay below
 *
 * B. GNOME Shell Extension:
 *    - Write GNOME Shell extension to add background layer
 *    - Extension exposes custom protocol
 *    - NeoWall connects via this protocol
 *    - Advantages: True integration, proper z-order
 *    - Disadvantages: Requires extension installation
 *
 * C. Replace gnome-settings-daemon:
 *    - Take over GSettings key for wallpaper
 *    - Disable g-s-d wallpaper module
 *    - Act as wallpaper provider
 *    - Advantages: Integrates with GNOME settings
 *    - Disadvantages: Complex, may break on updates
 *
 * D. Subsurface Below Root:
 *    - Create subsurface below root window
 *    - Hope compositor doesn't reorder
 *    - Very fragile, not recommended
 *
 * Recommended Implementation:
 *
 * Use approach A (xdg-shell fullscreen) with these refinements:
 *
 * 1. xdg_toplevel configuration:
 *    xdg_toplevel_set_fullscreen(toplevel, output);
 *    xdg_toplevel_set_app_id(toplevel, "neowall-background");
 *    xdg_toplevel_set_title(toplevel, "NeoWall Background");
 *
 * 2. Input region (pass-through):
 *    struct wl_region *region = wl_compositor_create_region(compositor);
 *    // Don't add any rectangles - empty region
 *    wl_surface_set_input_region(surface, region);
 *    wl_region_destroy(region);
 *
 * 3. Window hints via GSettings (if detected as GNOME):
 *    - Set skip_taskbar hint
 *    - Set below hint
 *    - Set sticky hint (on all workspaces)
 *
 * 4. Handle configure events:
 *    static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
 *                                      uint32_t serial) {
 *        xdg_surface_ack_configure(xdg_surface, serial);
 *        // Commit surface to apply configuration
 *    }
 *
 * 5. Handle toplevel events:
 *    static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
 *                                       int32_t width, int32_t height,
 *                                       struct wl_array *states) {
 *        // Update surface size
 *        surface->width = width;
 *        surface->height = height;
 *    }
 *
 * References:
 * - xdg-shell spec: https://wayland.app/protocols/xdg-shell
 * - GNOME Shell source: https://gitlab.gnome.org/GNOME/gnome-shell
 * - Weston example: https://github.com/wayland-project/weston/blob/main/clients/window.c
 */
