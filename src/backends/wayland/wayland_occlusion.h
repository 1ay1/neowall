#ifndef WAYLAND_OCCLUSION_H
#define WAYLAND_OCCLUSION_H

/* Wayland-side occlusion detection (wlr-foreign-toplevel-management).
 * Internal to the Wayland backend; not part of the public abstraction. */

#include <stdbool.h>

struct wl_display;
struct neowall_state;

bool wayland_occlusion_init(struct wl_display *display, struct neowall_state *state);
void wayland_occlusion_update(struct neowall_state *state);
void wayland_occlusion_cleanup(void);

#endif /* WAYLAND_OCCLUSION_H */
