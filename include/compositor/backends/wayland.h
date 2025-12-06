#ifndef WAYLAND_H
#define WAYLAND_H

/**
 * ============================================================================
 * WAYLAND - Backend-Specific Wayland State
 * ============================================================================
 *
 * This header defines the Wayland-specific global state that was previously
 * stored directly in neowall_state. Moving these types here achieves:
 *
 * 1. True compositor abstraction - core neowall_state is platform-agnostic
 * 2. Clean separation - Wayland types only in Wayland backend code
 * 3. X11 equality - X11 backend doesn't need to know about Wayland types
 *
 * This struct is managed by wayland_core.c and accessed by Wayland compositor
 * backend implementations (wlr_layer_shell, kde_plasma, gnome_shell, fallback).
 *
 * NOTE: This header should ONLY be included by Wayland backend code!
 * Core neowall code should NOT include this header.
 */

#include <wayland-client.h>
#include "xdg-output-unstable-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"

/* Forward declaration */
struct neowall_state;

/**
 * Wayland state - platform-specific objects
 *
 * These are the Wayland-specific objects that were previously in neowall_state.
 * Now they're encapsulated in this structure, managed by wayland_core.c.
 */
typedef struct wayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct wp_tearing_control_manager_v1 *tearing_control_manager;
    
    /* Back-pointer to main neowall state */
    struct neowall_state *state;
    
    /* Initialization flag */
    bool initialized;
} wayland_t;

/**
 * Get the Wayland state
 *
 * @return Pointer to the Wayland state, or NULL if Wayland not initialized
 *
 * This function is implemented in wayland_core.c and provides access to the
 * Wayland objects for the Wayland backend implementations.
 */
wayland_t *wayland_get(void);

/**
 * Initialize Wayland
 *
 * @param state The main neowall state
 * @return true on success, false on failure
 *
 * Connects to the Wayland display and initializes all global objects.
 */
bool wayland_init(struct neowall_state *state);

/**
 * Cleanup Wayland
 *
 * Disconnects from the Wayland display and cleans up all global objects.
 */
void wayland_cleanup(void);

/**
 * Check if Wayland is initialized
 *
 * @return true if Wayland is initialized and available
 */
bool wayland_available(void);

#endif /* WAYLAND_H */
