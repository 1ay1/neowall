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

#endif /* HYPRLAND_COVERAGE_H */
