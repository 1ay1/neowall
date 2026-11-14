#include "wlr_internal.h"

/*
 * ============================================================================
 * WLR-LAYER-SHELL BACKEND
 * ============================================================================
 *
 * Backend implementation for wlroots-based compositors using the
 * zwlr_layer_shell_v1 protocol.
 *
 * SUPPORTED COMPOSITORS:
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
 * - iMouse support for shader interaction
 *
 * PRIORITY: 100 (preferred for wlroots compositors)
 *
 * NOTE: For KDE Plasma, use the kde-plasma backend instead which provides
 * proper click pass-through for desktop integration.
 */

#define BACKEND_NAME "wlr-layer-shell"
#define BACKEND_DESCRIPTION "wlroots layer shell protocol (Hyprland, Sway, River, etc.)"
#define BACKEND_PRIORITY 100

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
 * BACKEND INITIALIZATION
 * ============================================================================ */

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    wlr_backend_data_t *backend_data = data;

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        backend_data->layer_shell = wl_registry_bind(registry, name,
                                                     &zwlr_layer_shell_v1_interface,
                                                     version < 4 ? version : 4);
        backend_data->layer_shell_name = name;
        log_info("Bound to wlr-layer-shell");
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        backend_data->seat = wl_registry_bind(registry, name,
                                              &wl_seat_interface,
                                              version < 5 ? version : 5);
        backend_data->seat_name = name;
        wl_seat_add_listener(backend_data->seat, &wlr_seat_listener, backend_data);
        log_info("Bound to wl_seat for input events");
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                         uint32_t name) {
    wlr_backend_data_t *backend = data;
    (void)registry;
    if (!backend) return;

    if (name == backend->seat_name) {
        wlr_clear_input_focus(backend);
        wlr_destroy_pointer(backend);
#ifdef NEOWALL_HAVE_XKB
        if (backend->keyboard) {
            wl_keyboard_destroy(backend->keyboard);
            backend->keyboard = NULL;
        }
#endif
        if (backend->seat) wl_seat_destroy(backend->seat);
        backend->seat = NULL;
        backend->seat_name = 0;
        backend->has_pointer_cap = false;
        log_warn("Wayland seat global removed; input focus cleared");
    } else if (name == backend->layer_shell_name) {
        wlr_clear_input_focus(backend);
        if (backend->layer_shell) zwlr_layer_shell_v1_destroy(backend->layer_shell);
        backend->layer_shell = NULL;
        backend->layer_shell_name = 0;
        log_warn("wlr-layer-shell global removed");
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* ============================================================================
 * BACKEND OPERATIONS
 * ============================================================================ */

static void wlr_cleanup(void *data);

static void *wlr_backend_init(struct neowall_state *state) {
    wayland_t *wl = wayland_get();
    if (!state || !wl || !wl->display) {
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
    struct wl_registry *registry = wl_display_get_registry(wl->display);
    if (!registry) {
        log_error("Failed to get Wayland registry");
        free(backend_data);
        return NULL;
    }

    backend_data->registry = registry;
    wl_registry_add_listener(registry, &registry_listener, backend_data);
    if (wl_display_roundtrip(wl->display) < 0) {
        log_error("Wayland roundtrip failed during wlr backend initialization");
        wlr_cleanup(backend_data);
        return NULL;
    }

    /* Check if layer shell is available */
    if (!backend_data->layer_shell) {
        log_error("zwlr_layer_shell_v1 not available");
        wlr_cleanup(backend_data);
        return NULL;
    }

    backend_data->initialized = true;
    log_info("wlr-layer-shell backend initialized successfully");

    return backend_data;
}

static void wlr_cleanup(void *data) {
    if (!data) {
        return;
    }

    log_debug("Cleaning up wlr-layer-shell backend");

    wlr_backend_data_t *backend_data = data;
    wlr_clear_input_focus(backend_data);

    if (backend_data->pointer) {
        wlr_destroy_pointer(backend_data);
    }

#ifdef NEOWALL_HAVE_XKB
    if (backend_data->keyboard) {
        wl_keyboard_destroy(backend_data->keyboard);
        backend_data->keyboard = NULL;
    }
    if (backend_data->xkb_state)  xkb_state_unref(backend_data->xkb_state);
    if (backend_data->xkb_keymap) xkb_keymap_unref(backend_data->xkb_keymap);
    if (backend_data->xkb_ctx)    xkb_context_unref(backend_data->xkb_ctx);
#endif

    if (backend_data->seat) {
        wl_seat_destroy(backend_data->seat);
        backend_data->seat = NULL;
    }

    if (backend_data->layer_shell) {
        zwlr_layer_shell_v1_destroy(backend_data->layer_shell);
        backend_data->layer_shell = NULL;
    }
    if (backend_data->registry) {
        wl_registry_destroy(backend_data->registry);
        backend_data->registry = NULL;
    }

    free(backend_data);

    log_debug("wlr-layer-shell backend cleanup complete");
}

/* ---- fractional scale (wp_fractional_scale_v1) ----
 *
 * preferred_scale carries the compositor's chosen scale as a fixed-point value
 * in 120ths (180 == 1.5x). We record it on the output whose surface this is and
 * ask wayland_core to re-derive the buffer size at the true density; the render
 * path then draws a correctly-sized buffer and the viewport (set below) presents
 * it 1:1. Without this we would only ever see the integer wl_output.scale, which
 * the compositor rounds (1.5 -> 2), forcing an oversized buffer it must rescale. */
static void fractional_scale_handle_preferred(void *data,
                                              struct wp_fractional_scale_v1 *fs,
                                              uint32_t scale_120) {
    struct compositor_surface *surface = data;
    (void)fs;
    if (!surface || !surface->backend) {
        return;
    }
    wlr_backend_data_t *backend = surface->backend->data;
    if (!backend || !backend->state || scale_120 == 0) {
        return;
    }

    struct neowall_state *state = backend->state;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->compositor_surface == surface) {
            if (o->fractional_scale_120 != (int32_t)scale_120) {
                log_info("Output %s: fractional scale %u/120 (%.3fx)",
                         o->model[0] ? o->model : "unknown",
                         scale_120, scale_120 / 120.0);
                o->fractional_scale_120 = (int32_t)scale_120;
                wayland_apply_fractional_scale(o);
            }
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_handle_preferred,
};

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
        wl_surface,
        (struct wl_output *)config->output,
        layer,
        "neowall"
    );

    if (!surface_data->layer_surface) {
        log_error("Failed to create layer surface");
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

    /* Set keyboard interactivity. ON_DEMAND (not EXCLUSIVE) so a terminal
     * wallpaper receives keys only while the user has clicked/focused it, and
     * never steals input from real windows. */
    enum zwlr_layer_surface_v1_keyboard_interactivity kb_mode =
        config->keyboard_interactivity ?
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND :
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

    zwlr_layer_surface_v1_set_keyboard_interactivity(surface_data->layer_surface, kb_mode);

    /* Presentation hint. ASYNC lets the compositor scan out a frame the moment
     * it arrives (tearing, but no waiting); VSYNC pins presentation to the
     * refresh boundary.
     *
     * This MUST follow the user's `vsync` setting. Forcing ASYNC unconditionally
     * makes a wallpaper present at whatever rate the GPU happens to finish --
     * measured swinging between 60 and 131 FPS on a 3440x1440 display -- so the
     * frame interval is never the shader's dt and motion visibly jerks even
     * while the FPS counter reads a healthy 60. A wallpaper has no latency
     * requirement that would justify tearing, so honouring vsync is also the
     * better default.
     */
    if (wl && wl->tearing_control_manager) {
        struct wp_tearing_control_v1 *tearing = wp_tearing_control_manager_v1_get_tearing_control(
            wl->tearing_control_manager,
            wl_surface
        );
        surface->tearing_control = tearing;

        if (tearing) {
            bool want_tearing = !config->vsync;

            wp_tearing_control_v1_set_presentation_hint(
                tearing,
                want_tearing ? WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
                             : WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC
            );

            if (want_tearing) {
                log_info("Tearing control: async presentation (vsync off)");
            } else {
                log_info("Tearing control: vsync-locked presentation (smooth pacing)");
            }
        } else {
            log_error("Failed to create tearing control object");
        }
    } else {
        log_debug("Tearing control manager not available - FPS may be limited by compositor");
    }

    /* Create fractional-scale + viewport objects so this surface can render at
     * the output's TRUE fractional density. Both globals are optional; when
     * absent the surface falls back to the integer wl_output.scale path and
     * nothing here runs. The preferred_scale event that follows drives buffer
     * sizing via wayland_apply_fractional_scale(). */
    if (wl && wl->fractional_scale_manager) {
        struct wp_fractional_scale_v1 *fs =
            wp_fractional_scale_manager_v1_get_fractional_scale(
                wl->fractional_scale_manager, wl_surface);
        surface->fractional_scale = fs;
        if (fs) {
            wp_fractional_scale_v1_add_listener(fs, &fractional_scale_listener, surface);
        }
    }
    if (wl && wl->viewporter) {
        surface->viewport = wp_viewporter_get_viewport(wl->viewporter, wl_surface);
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

    /* Destroy fractional-scale + viewport objects if they exist */
    if (surface->fractional_scale) {
        wp_fractional_scale_v1_destroy((struct wp_fractional_scale_v1 *)surface->fractional_scale);
        surface->fractional_scale = NULL;
    }
    if (surface->viewport) {
        wp_viewport_destroy((struct wp_viewport *)surface->viewport);
        surface->viewport = NULL;
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
    if (surface->native_surface) {
        wl_surface_destroy((struct wl_surface *)surface->native_surface);
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

    /* Set keyboard interactivity. ON_DEMAND (not EXCLUSIVE) so a terminal
     * wallpaper receives keys only while the user has clicked/focused it, and
     * never steals input from real windows.
     *
     * EXCLUSIVE on a BACKGROUND-layer surface is a trap: the compositor routes
     * ALL keyboard input to the wallpaper for as long as it is mapped, so the
     * user cannot type into any window and — on compositors that honour it for
     * background layers — cannot reach their WM keybinds to recover. It must
     * never appear on this path; keep it identical to wlr_create_surface(). */
    enum zwlr_layer_surface_v1_keyboard_interactivity kb_mode =
        config->keyboard_interactivity ?
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND :
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

    zwlr_layer_surface_v1_set_keyboard_interactivity(surface_data->layer_surface, kb_mode);

    /* Update config cache */
    surface->config = *config;

    log_debug("wlr layer surface configured");

    return true;
}

/* Flip keyboard interactivity on a live layer surface.
 *
 * The surface is created before the wallpaper's type is known (the config load
 * that says "this output is a terminal" lands after the layer surface has been
 * created and committed), so a terminal wallpaper comes up keyboard-inert and
 * has to be made typeable afterwards. Re-committing is required: the layer
 * shell treats set_keyboard_interactivity as double-buffered state, so without
 * a wl_surface_commit the compositor never applies it.
 *
 * ON_DEMAND only — see wlr_configure_surface() for why EXCLUSIVE must never
 * appear on a BACKGROUND-layer surface. */
static bool wlr_set_keyboard_interactivity(struct compositor_surface *surface, bool enabled) {
    if (!surface) {
        return false;
    }

    wlr_surface_data_t *surface_data = surface->backend_data;
    if (!surface_data || !surface_data->layer_surface || !surface->native_surface) {
        return false;
    }

    zwlr_layer_surface_v1_set_keyboard_interactivity(
        surface_data->layer_surface,
        enabled ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
                : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit((struct wl_surface *)surface->native_surface);

    wayland_t *wl = wayland_get();
    if (wl && wl->display) {
        wl_display_flush(wl->display);
    }

    log_info("Layer surface keyboard interactivity -> %s",
             enabled ? "on-demand" : "none");

    return true;
}

static void wlr_commit_surface(struct compositor_surface *surface) {
    if (!surface || !surface->native_surface) {
        log_error("Invalid surface for commit");
        return;
    }

    struct wl_surface *wl_surface = (struct wl_surface *)surface->native_surface;

    /* Ensure opaque region is always set (prevents transparency) */
    wayland_t *wl = wayland_get();
    if (wl && wl->compositor) {
        struct wl_region *opaque_region = wl_compositor_create_region(wl->compositor);
        if (opaque_region) {
            wl_region_add(opaque_region, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(wl_surface, opaque_region);
            wl_region_destroy(opaque_region);
        }
    }

    wl_surface_commit(wl_surface);
}

static bool wlr_create_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface || !surface->native_surface) {
        log_error("Invalid surface for EGL window creation");
        return false;
    }

    log_debug("Creating EGL window: %dx%d", width, height);

    /* Create EGL window */
    struct wl_surface *wl_surface = (struct wl_surface *)surface->native_surface;
    surface->egl_window = wl_egl_window_create(wl_surface, width, height);
    if (!surface->egl_window) {
        log_error("Failed to create EGL window");
        return false;
    }

    surface->width = width;
    surface->height = height;

    log_debug("EGL window created successfully");

    return true;
}

static bool wlr_resize_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface || !surface->egl_window) {
        return false;
    }

    wl_egl_window_resize(surface->egl_window, width, height, 0, 0);
    return true;
}

static EGLNativeWindowType wlr_get_native_window(struct compositor_surface *surface) {
    if (!surface || !surface->egl_window) {
        return (EGLNativeWindowType)0;
    }
    return (EGLNativeWindowType)surface->egl_window;
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
           COMPOSITOR_CAP_MULTI_OUTPUT |
           COMPOSITOR_CAP_OCCLUSION;
}

/* ----- occlusion (wlr-foreign-toplevel-management) ----- */

static bool wlr_occlusion_init(void *data, struct neowall_state *state) {
    wlr_backend_data_t *b = data;
    wayland_t *wl = wayland_get();
    if (!b || !wl || !wl->display) {
        return false;
    }
    b->occlusion_active = wayland_occlusion_init(wl->display, state);
    return b->occlusion_active;
}

static void wlr_occlusion_update(void *data, struct neowall_state *state) {
    wlr_backend_data_t *b = data;
    if (b && b->occlusion_active) {
        wayland_occlusion_update(state);
    }
}

static void wlr_occlusion_cleanup(void *data) {
    wlr_backend_data_t *b = data;
    if (b && b->occlusion_active) {
        wayland_occlusion_cleanup();
        b->occlusion_active = false;
    }
}

/* Re-apply input config (currently just mouse_interaction) after a config load.
 * The seat's `capabilities` event already fired during the init roundtrip
 * — before config was parsed — so if the user set mouse_interaction=false
 * we have an unwanted wl_pointer binding to release. Conversely if they
 * enabled it but our binding is missing for some reason, this is the place
 * to (re-)acquire it. */
static void wlr_apply_input_config(void *data, struct neowall_state *state) {
    wlr_backend_data_t *b = data;
    if (!b || !state) {
        return;
    }
    bool want_pointer = atomic_load_explicit(&state->mouse_interaction, memory_order_acquire);

    if (!want_pointer && b->pointer) {
        wlr_destroy_pointer(b);
        log_info("Wayland pointer released after config load (mouse_interaction=false)");
        return;
    }

    if (want_pointer && !b->pointer && b->seat) {
        /* Only bind if the seat actually advertised a pointer. Binding without
         * the capability is a protocol violation and Wayland kills the
         * connection — fatal on headless/kiosk/CI seats with no input device. */
        if (!b->has_pointer_cap) {
            log_debug("mouse_interaction=true but seat has no pointer capability, "
                      "not binding pointer");
            return;
        }
        b->pointer = wl_seat_get_pointer(b->seat);
        if (b->pointer) {
            wl_pointer_add_listener(b->pointer, &wlr_pointer_listener, b);
            log_info("Wayland pointer bound after config load (mouse_interaction=true)");
        }
    }
}

static void wlr_on_output_added(void *data, void *output) {
    (void)data;
    (void)output;

    log_debug("Output added to wlr backend");
}

static void wlr_on_output_removed(void *data, void *output) {
    wlr_backend_data_t *backend = data;
    if (!backend) return;
    struct wl_output *removed = output;
    if (backend->ptr_output && backend->ptr_output->native_output == removed) {
        backend->ptr_buttons = 0;
        wlr_set_ptr_output(backend, NULL);
    }
    if (backend->kbd_output && backend->kbd_output->native_output == removed) {
        wlr_set_kbd_output(backend, NULL);
    }
    log_debug("Output removed from wlr backend; matching input focus cleared");
}

static void wlr_damage_surface(struct compositor_surface *surface,
                              int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!surface || !surface->native_surface) {
        return;
    }

    struct wl_surface *wl_surface = (struct wl_surface *)surface->native_surface;
    wl_surface_damage(wl_surface, x, y, width, height);
}

static void wlr_set_scale(struct compositor_surface *surface, int32_t scale) {
    if (!surface || !surface->native_surface) {
        return;
    }

    struct wl_surface *wl_surface = (struct wl_surface *)surface->native_surface;
    wl_surface_set_buffer_scale(wl_surface, scale);
}

/* Re-hint presentation on a live surface. The surface is created before the
 * output's config is attached, so the hint chosen at creation time is a guess;
 * this applies the real setting once it is known. */
static void wlr_set_vsync(struct compositor_surface *surface, bool vsync) {
    if (!surface || !surface->tearing_control) {
        return;
    }

    struct wp_tearing_control_v1 *tearing =
        (struct wp_tearing_control_v1 *)surface->tearing_control;

    wp_tearing_control_v1_set_presentation_hint(
        tearing,
        vsync ? WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC
              : WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
    );

    log_debug("Tearing control: %s presentation",
              vsync ? "vsync-locked" : "async");
}

/* ============================================================================
 * EVENT HANDLING OPERATIONS
 * ============================================================================ */

static int wlr_get_fd(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }
    return wl_display_get_fd(wl->display);
}

static bool wlr_prepare_events(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    struct wl_display *display = wl->display;

    /* Wayland requires prepare_read before poll() */
    while (wl_display_prepare_read(display) != 0) {
        /* If prepare failed, dispatch pending events and retry */
        if (wl_display_dispatch_pending(display) < 0) {
            return false;
        }
    }

    return true;
}

static bool wlr_read_events(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    /* Read events that were prepared */
    return wl_display_read_events(wl->display) >= 0;
}

static bool wlr_dispatch_events(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return false;
    }

    /* Dispatch all pending events */
    return wl_display_dispatch_pending(wl->display) >= 0;
}

static compositor_flush_result_t wlr_flush(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return COMPOSITOR_FLUSH_FATAL;
    }

    if (wl_display_flush(wl->display) >= 0) {
        return COMPOSITOR_FLUSH_OK;
    }
    return errno == EAGAIN ? COMPOSITOR_FLUSH_BLOCKED : COMPOSITOR_FLUSH_FATAL;
}

static void wlr_cancel_read(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return;
    }

    wl_display_cancel_read(wl->display);
}

static int wlr_get_error(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl || !wl->display) {
        return -1;
    }

    return wl_display_get_error(wl->display);
}

static bool wlr_sync(void *data) {
    wlr_backend_data_t *backend = data;
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

static void *wlr_get_native_display(void *data) {
    wlr_backend_data_t *backend = data;
    wayland_t *wl = wayland_get();
    if (!backend || !wl) {
        return NULL;
    }
    return wl->display;
}

static EGLenum wlr_get_egl_platform(void *data) {
    (void)data;
    return EGL_PLATFORM_WAYLAND_KHR;
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

static const compositor_backend_ops_t wlr_backend_ops = {
    .init = wlr_backend_init,
    .cleanup = wlr_cleanup,
    .create_surface = wlr_create_surface,
    .destroy_surface = wlr_destroy_surface,
    /* Compositor-paced vsync rendering (shared, wayland_core.c) */
    .frame_request = wayland_frame_request,
    .frame_ready = wayland_frame_ready,
    .frame_cancel = wayland_frame_cancel,
    .configure_surface = wlr_configure_surface,
    .set_keyboard_interactivity = wlr_set_keyboard_interactivity,
    .commit_surface = wlr_commit_surface,
    .create_egl_window = wlr_create_egl_window,
    .destroy_egl_window = wlr_destroy_egl_window,
    .resize_egl_window = wlr_resize_egl_window,
    .get_native_window = wlr_get_native_window,
    .get_capabilities = wlr_get_capabilities,
    .on_output_added = wlr_on_output_added,
    .on_output_removed = wlr_on_output_removed,
    .damage_surface = wlr_damage_surface,
    .set_scale = wlr_set_scale,
    .set_vsync = wlr_set_vsync,
    /* Event handling operations */
    .get_fd = wlr_get_fd,
    .prepare_events = wlr_prepare_events,
    .read_events = wlr_read_events,
    .dispatch_events = wlr_dispatch_events,
    .flush = wlr_flush,
    .cancel_read = wlr_cancel_read,
    .get_error = wlr_get_error,
    .sync = wlr_sync,
    /* Display/EGL operations */
    .get_native_display = wlr_get_native_display,
    .get_egl_platform = wlr_get_egl_platform,
    /* Occlusion detection */
    .occlusion_init = wlr_occlusion_init,
    .occlusion_update = wlr_occlusion_update,
    .occlusion_cleanup = wlr_occlusion_cleanup,
    /* Input config (post-config-load hook) */
    .apply_input_config = wlr_apply_input_config,
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
