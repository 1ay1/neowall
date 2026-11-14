#ifndef NEOWALL_X11_GEOMETRY_H
#define NEOWALL_X11_GEOMETRY_H

/* Pure, Xlib-free geometry helpers for the X11 backend.
 *
 * Multi-monitor correctness lives here so it can be unit-tested headlessly
 * (no X server). The X11 backend wraps these with the Xlib calls that fetch
 * the real rects; the math itself is plain integer arithmetic. */

#include <stdbool.h>

/* An axis-aligned rectangle in root (screen) coordinates. */
typedef struct {
    int x, y, w, h;
} x11_rect_t;

/* Coverage threshold for occlusion: a window must cover at least
 * NUM/DEN of an output's rect to occlude it. 9/10 = 90%. */
#define X11_COVERAGE_NUM 9
#define X11_COVERAGE_DEN 10

/* True if the target rect is covered (>= the coverage threshold) by any of the
 * `count` candidate rects. A zero-area target is never covered. */
bool x11_rect_is_covered(x11_rect_t target, const x11_rect_t *covers, int count);

/* Translate a root-space pointer position to an output's local coordinates.
 * Writes the local x/y when the pointer is inside the output rect and returns
 * true; writes (-1,-1) and returns false when the pointer is elsewhere. The
 * -1 sentinel is what render.c maps to "center of this output". */
bool x11_mouse_to_output(x11_rect_t output, int root_x, int root_y,
                         int *out_x, int *out_y);

/* True if two rects describe the same placement (origin + size). Used by the
 * hotplug reconcile to tell whether an existing output still matches a freshly
 * enumerated monitor. */
bool x11_rect_equal(x11_rect_t a, x11_rect_t b);

#endif /* NEOWALL_X11_GEOMETRY_H */
