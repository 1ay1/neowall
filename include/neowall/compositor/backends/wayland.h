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
#include <wayland-cursor.h>
#include "xdg-output-unstable-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

/* Forward declaration */
struct neowall_state;
struct output_state;

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
    /* wp_presentation: real present-time feedback. The compositor reports the
     * actual framebuffer-flip timestamp + hardware refresh period per commit,
     * which the frame pacer phase-locks to (see output_pace_note_present). NULL
     * on compositors without the protocol — the pacer falls back to anchoring
     * on swap-completion time. presentation_clock is the clockid the timestamps
     * are in (clock_id event); we only route feedback to the pacer when it
     * matches CLOCK_MONOTONIC, which every real compositor advertises. */
    struct wp_presentation *presentation;
    uint32_t presentation_clock;
    bool presentation_clock_ok;
    /* wp_fractional_scale_manager_v1 + wp_viewporter: together they let a
     * fractional-scale output be rendered at its TRUE pixel size instead of
     * the integer-rounded wl_output.scale. The manager reports the exact scale
     * (in 120ths) via wp_fractional_scale_v1::preferred_scale; the viewport
     * sets the surface's logical destination size so the compositor presents
     * the fractionally-sized buffer 1:1 rather than rescaling an oversized one.
     * Either may be NULL (older compositors) — the backend falls back to the
     * integer wl_output.scale path when so. */
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_viewporter *viewporter;
    /* wp_cursor_shape_manager_v1: lets the COMPOSITOR draw its own themed
     * cursor (hyprcursor on Hyprland) instead of us rendering an XCursor
     * buffer. Fixes cursor-style mismatch over the wallpaper. May be NULL. */
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    /* Per-pointer shape device, created lazily on first pointer enter. */
    struct wp_cursor_shape_device_v1 *cursor_shape_device;
    
    /* Back-pointer to main neowall state */
    struct neowall_state *state;
    
    /* Cursor state for setting cursor on pointer enter.
     * Theme name and base size come from XCURSOR_THEME / XCURSOR_SIZE so we
     * honor the user's configuration instead of forcing a wlroots default.
     * The theme is (re)loaded lazily at the entered surface's buffer scale
     * for crisp HiDPI rendering. */
    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    char *cursor_theme_name;   /* strdup'd, may be NULL (libwayland-cursor picks default) */
    int cursor_base_size;      /* unscaled size in logical pixels */
    int cursor_loaded_scale;   /* scale factor the current theme was loaded at */

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

/**
 * Re-derive an output's buffer size from its fractional scale and present it 1:1.
 *
 * Called from the wp_fractional_scale_v1::preferred_scale handler once an
 * output's exact scale (in 120ths) is known. Sizes the EGL buffer to
 * round(logical * fractional_scale_120 / 120), sets the surface buffer_scale to
 * 1, and sets the wp_viewport destination to the logical size so the compositor
 * presents the fractionally-scaled buffer without rescaling. No-op if the output
 * has no viewport (compositor lacks the protocol) or no fractional scale yet.
 *
 * Caller may hold output_list_lock (read); this touches only the given output.
 */
void wayland_apply_fractional_scale(struct output_state *output);

/**
 * Request wp_presentation feedback for an output's next surface commit.
 *
 * Call once per rendered frame, immediately BEFORE the commit that publishes
 * it. When the compositor supports wp_presentation (and advertises a usable
 * CLOCK_MONOTONIC present clock) the reported flip timestamp + refresh period
 * are fed into that output's phase-locked frame pacer (output_pace_note_present).
 * No-op otherwise — the pacer falls back to swap-completion-time anchoring.
 */
void wayland_request_present_feedback(struct output_state *output);

#endif /* WAYLAND_H */
