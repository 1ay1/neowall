/* wlr-layer-shell backend: types shared by the surface and input halves */

#ifndef NEOWALL_WLR_INTERNAL_H
#define NEOWALL_WLR_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#ifdef NEOWALL_HAVE_XKB
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#endif
#include "neowall/surface/surface.h"
#include "neowall/backends/wayland.h"
#include "neowall/state/state.h"
#include "neowall/source/reactive.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "wayland_occlusion.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/output.h"

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    struct zwlr_layer_shell_v1 *layer_shell;
    uint32_t layer_shell_name;
    struct wl_registry *registry;
    struct wl_seat *seat;
    uint32_t seat_name;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    bool initialized;
    bool occlusion_active;
    bool has_pointer_cap; /* seat advertised WL_SEAT_CAPABILITY_POINTER */

    /* Pointer tracking for terminal-wallpaper input forwarding. The pointer
     * `enter` binds these to the output under the cursor; motion updates the
     * surface-local pixel position; button/axis forward mouse reports. */
    struct output_state *ptr_output; /* ref-held output currently under pointer */
    int ptr_x, ptr_y;                /* buffer-pixel position */
    uint8_t ptr_buttons;             /* terminal buttons 0..2 currently held */

    /* Keyboard focus + xkb state for terminal-wallpaper typing. kbd_output is
     * the terminal surface that currently holds keyboard focus (via ON_DEMAND
     * interactivity + a click), NULL when unfocused. */
    struct output_state *kbd_output;
#ifdef NEOWALL_HAVE_XKB
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
#endif
} wlr_backend_data_t;

/* Surface backend data */
typedef struct {
    struct zwlr_layer_surface_v1 *layer_surface;
    bool configured;
} wlr_surface_data_t;

void wlr_set_ptr_output(wlr_backend_data_t *backend, struct output_state *output);
void wlr_set_kbd_output(wlr_backend_data_t *backend, struct output_state *output);
void wlr_clear_input_focus(wlr_backend_data_t *backend);
void wlr_destroy_pointer(wlr_backend_data_t *backend);
extern const struct wl_pointer_listener wlr_pointer_listener;
extern const struct wl_seat_listener wlr_seat_listener;

#endif /* NEOWALL_WLR_INTERNAL_H */
