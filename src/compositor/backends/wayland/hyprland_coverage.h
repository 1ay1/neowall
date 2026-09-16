#ifndef HYPRLAND_COVERAGE_H
#define HYPRLAND_COVERAGE_H

/* Hyprland-specific tiled-mosaic occlusion signal.
 * Reads window geometry via Hyprland's IPC socket and computes per-output
 * coverage. Only active when HYPRLAND_INSTANCE_SIGNATURE is set. */

#include <stdbool.h>

struct neowall_state;
struct output_state;

bool hyprland_coverage_available(void);

/* Refresh internal snapshot if older than throttle window. Cheap to call. */
void hyprland_coverage_refresh(void);

/* Returns true if windows cover >= threshold fraction of the output.
 * Uses the most recently refreshed snapshot. */
bool hyprland_output_covered(const struct output_state *o, float threshold);

/* One window, in wallpaper-local pixels: origin at the output's top-left,
 * y increasing downward (the compositor's convention, not GL's). */
typedef struct {
    float x, y, w, h;
    bool focused;
} nw_window_rect;

/* Copy the windows visible on this output's active workspace into `out`.
 *
 * This is the same snapshot the occlusion path already refreshes every 500ms,
 * so asking for it costs a memcpy rather than an IPC round-trip. Rects are
 * clipped to the output and expressed relative to it, so a shader can compare
 * them directly against gl_FragCoord.
 *
 * Returns the number written, never more than `max`. Zero when Hyprland IPC is
 * unavailable -- callers must treat "no windows" as a normal state rather than
 * an error, because that is what every non-Hyprland compositor reports. */
int hyprland_output_windows(const struct output_state *o,
                            nw_window_rect *out, int max);

#endif /* HYPRLAND_COVERAGE_H */
