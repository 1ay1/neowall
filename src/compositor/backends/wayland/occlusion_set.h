#ifndef NEOWALL_WAYLAND_OCCLUSION_SET_H
#define NEOWALL_WAYLAND_OCCLUSION_SET_H

/* The set of outputs one toplevel currently occupies.
 *
 * wlr-foreign-toplevel-management sends one output_enter per output the
 * toplevel is visible on, and the protocol states plainly that "a toplevel may
 * be visible on multiple outputs"; output_leave is guaranteed to be preceded by
 * an output_enter for the same output. Occlusion must therefore be decided by
 * set membership, never against a single "most recent" handle.
 *
 * Elements are opaque wl_output* handles, so this unit stays free of Wayland
 * headers and can be exercised without a display. */

#include <stdbool.h>

#include "neowall/vec.h"

NW_VEC_DEFINE_STATIC(output_set, void *)

/* Idempotent. Returns false only on OOM, leaving the set unchanged. */
bool output_set_add(output_set *set, void *output);

/* Removes `output` if present; no-op otherwise. Order is not preserved. */
void output_set_remove(output_set *set, void *output);

bool output_set_contains(const output_set *set, const void *output);

#endif /* NEOWALL_WAYLAND_OCCLUSION_SET_H */
