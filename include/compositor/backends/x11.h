#ifndef COMPOSITOR_BACKENDS_X11_H
#define COMPOSITOR_BACKENDS_X11_H

#include "compositor.h"

/*
 * ============================================================================
 * X11 BACKEND FOR TILING WINDOW MANAGERS
 * ============================================================================
 *
 * Header file for the X11 compositor backend targeting tiling window managers.
 *
 * This backend provides wallpaper functionality for X11-based systems,
 * particularly optimized for tiling window managers that respect EWMH hints.
 *
 * SUPPORTED WINDOW MANAGERS:
 * - i3/i3-gaps
 * - bspwm
 * - dwm
 * - awesome
 * - xmonad
 * - qtile
 * - herbstluftwm
 *
 * FEATURES:
 * - Desktop window type (_NET_WM_WINDOW_TYPE_DESKTOP)
 * - Proper stacking below all windows (_NET_WM_STATE_BELOW)
 * - Multi-monitor support via XRandR
 * - EGL rendering via EGL_PLATFORM_X11_KHR
 * - Sticky windows across all workspaces
 * - Skip taskbar/pager hints
 *
 * USAGE:
 * The backend is automatically registered and selected when:
 * 1. DISPLAY environment variable is set
 * 2. X11 display connection succeeds
 * 3. No Wayland compositor is detected (or Wayland fails)
 */

/* Forward declarations */
struct neowall_state;
struct compositor_backend;

/**
 * Initialize X11 backend
 *
 * This function checks if X11 is available and initializes the backend.
 * It will return NULL if X11 is not available or initialization fails.
 *
 * @param state Global NeoWall state
 * @return Compositor backend handle, or NULL on failure
 */
struct compositor_backend *compositor_backend_x11_init(struct neowall_state *state);

/**
 * Check if X11 is available
 *
 * Quick check to see if X11 display can be opened.
 * Useful for backend selection logic.
 *
 * @return true if X11 is available, false otherwise
 */
bool compositor_backend_x11_available(void);

/**
 * Get native X11 window from compositor surface
 *
 * Helper function to get the underlying X11 Window from a compositor surface.
 * Returns 0 if the surface is not an X11 surface.
 *
 * @param surface Compositor surface
 * @return X11 Window handle, or 0 if not X11
 */
unsigned long compositor_surface_get_x11_window(struct compositor_surface *surface);

/**
 * Get X11 connection file descriptor
 *
 * Returns the file descriptor for the X11 connection, which can be used
 * with poll/select for event-driven X11 event processing.
 *
 * @param backend Compositor backend (must be X11 backend)
 * @return X11 connection file descriptor, or -1 on error
 */
int x11_backend_get_fd(struct compositor_backend *backend);

/**
 * Handle pending X11 events
 *
 * Process all pending X11 events including mouse events (button press/release,
 * motion), keyboard events, expose events, and structure notifications.
 * This should be called when the X11 file descriptor is ready for reading.
 *
 * @param backend Compositor backend (must be X11 backend)
 * @return true on success, false on error
 */
bool x11_backend_handle_events(struct compositor_backend *backend);

#endif /* COMPOSITOR_BACKENDS_X11_H */