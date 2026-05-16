#ifndef WAYLAND_FRAME_WATCHDOG_H
#define WAYLAND_FRAME_WATCHDOG_H

/* Generic per-output frame-callback watchdog for Wayland backends.
 *
 * Asks the compositor for a frame callback on each output's surface. If the
 * compositor stops sending them for OCCLUSION_TIMEOUT_MS, the surface is
 * presumed obscured (spec: compositors MAY throttle frame callbacks for
 * non-visible surfaces). Self-perpetuating: each done re-arms a new request.
 *
 * Used by KDE, GNOME, and fallback Wayland backends. The wlroots backend
 * uses this too plus additional signals. */

#include <stdbool.h>

struct neowall_state;
struct output_state;

bool frame_watchdog_init(struct neowall_state *state);

/* Refresh occlusion bits for every output that opted into pause_on_fullscreen.
 * Call after compositor dispatch. */
void frame_watchdog_update(struct neowall_state *state);

/* Returns true if the watchdog says this output is occluded. Does not change
 * state. Used by occlusion.c (wlr) to OR with other signals without writing
 * to o->occluded twice. */
bool frame_watchdog_output_occluded(struct output_state *o);

/* Ensure the watchdog is armed for this output. Called from update; exposed
 * so the wlr occlusion path can prime per-output entries too. */
void frame_watchdog_arm(struct output_state *o);

void frame_watchdog_cleanup(void);

#endif /* WAYLAND_FRAME_WATCHDOG_H */
