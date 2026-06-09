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

/* Drop the watchdog entry for this output. MUST be called from output_destroy
 * (or any other path that frees an output_state) — otherwise we leak the
 * entry and, worse, hold a dangling pointer that the next arm/cleanup will
 * dereference. The output's pending wl_callback (if any) is destroyed here. */
void frame_watchdog_remove(struct output_state *o);

/* --- per-frame render throttle (compositor-paced rendering) ---
 * Arm with each swap; render_allowed() gates the next frame on the
 * compositor's frame callback. Prevents rendering frames the compositor
 * will never present (output off / throttled / overlapped). A 200ms safety
 * timeout guarantees forward progress if a callback is withheld. */
void frame_watchdog_throttle_arm(struct output_state *o);
bool frame_watchdog_render_allowed(struct output_state *o);

void frame_watchdog_cleanup(void);

#endif /* WAYLAND_FRAME_WATCHDOG_H */
