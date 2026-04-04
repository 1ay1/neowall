#ifndef OCCLUSION_H
#define OCCLUSION_H

#include <stdbool.h>

struct neowall_state;

/**
 * Initialize occlusion detection.
 *
 * Detects the current display server backend and sets up monitoring for
 * fullscreen window state changes. On Wayland, uses the wlr-foreign-toplevel
 * protocol. On X11, uses EWMH property queries.
 *
 * Events arrive on the existing compositor fd, so no additional poll fd needed.
 *
 * @param state The global neowall state
 * @return true if occlusion detection is available, false otherwise
 */
bool occlusion_init(struct neowall_state *state);

/**
 * Update occlusion state for all outputs.
 *
 * Called from the event loop after dispatching compositor events.
 * Checks current window state and updates output->occluded flags.
 * When an output transitions from occluded to visible, sets needs_redraw.
 *
 * @param state The global neowall state
 */
void occlusion_update(struct neowall_state *state);

/**
 * Cleanup occlusion detection resources.
 *
 * @param state The global neowall state
 */
void occlusion_cleanup(struct neowall_state *state);

#endif /* OCCLUSION_H */
