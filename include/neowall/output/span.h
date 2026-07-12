/* Virtual-screen geometry for spanning one shader across several outputs.
 *
 * Split out of the render path (as src/output/cycle_select.c was split out of
 * output.c) so the bounding-box arithmetic and the Y-axis flip below can be
 * unit-tested without EGL, GL, or a compositor. Nothing here touches the GPU.
 */
#ifndef NEOWALL_OUTPUT_SPAN_H
#define NEOWALL_OUTPUT_SPAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One output's placement in the screen layout, top-left origin, LOGICAL pixels.
 *
 * Two coordinate spaces meet in this file and they are not interchangeable:
 *
 *   LOGICAL  the compositor's global layout space, the one every output's
 *            position is quoted in. A scale-2 monitor is HALF as wide here as
 *            it is in its own framebuffer. Only this space is shared, so it is
 *            the only space a bounding box over several outputs means anything
 *            in. All four fields below are logical.
 *   DEVICE   one output's own framebuffer, where gl_FragCoord lives. Private to
 *            that output, and scaled by `scale` from logical. struct span_view
 *            is entirely device pixels.
 *
 * Mixing them silently is what this naming is here to prevent: with a logical
 * origin and a device extent, a scale-2 monitor's rect SWALLOWS its neighbour's
 * instead of abutting it, the box collapses onto the one output, and the others
 * are left rendering slices of a scene nobody is drawing. Nothing downstream can
 * catch that — the resulting rects are a shape a real layout can also have (a
 * cloned pair of unequal size nests exactly the same way), so the names and the
 * caller's derivation are the only guard there is. The caller is the seam: see
 * outputs_update_spans() in src/output/output.c, which divides the physical
 * output size by the same scale it passes here.
 */
struct span_rect {
    int32_t logical_x;
    int32_t logical_y;
    int32_t logical_w;
    int32_t logical_h;
    /* Device pixels per logical pixel on THIS output (wl_output scale; 1 on
     * X11). Non-positive is read as 1, which is what an output that has not
     * heard a scale event yet carries. */
    int32_t scale;
};

/* What one output must feed its shader to draw its slice of the virtual scene.
 * DEVICE pixels, at this output's own scale: gl_FragCoord and iResolution are
 * both framebuffer quantities, so a scale-2 output is handed a virtual screen
 * twice the size of the one its scale-1 neighbour is handed. Same scene, same
 * logical extent, sampled at each output's own density. */
struct span_view {
    int32_t virt_w;     /* iResolution.x: bounding box of every rect, at my scale */
    int32_t virt_h;     /* iResolution.y */
    int32_t off_x;      /* added to gl_FragCoord.x */
    int32_t off_y;      /* added to gl_FragCoord.y */
};

/* Virtual view for rects[index] within the bounding box of all `count` rects.
 *
 * The box is computed in LOGICAL pixels and the answer is returned in
 * rects[index]'s OWN device pixels: with mixed scales there is no single device
 * space to share, so every output is given the same logical scene at its own
 * density.
 *
 * Rects with a non-positive width or height are ignored: an output that has not
 * been configured yet carries no size, and letting it into the box would drag
 * the origin to (0,0) and shift every other output's slice.
 *
 * off_x is the plain horizontal distance from the box's left edge, but off_y is
 * NOT the matching vertical distance: gl_FragCoord's origin is the BOTTOM-left
 * of the window, while span_rect's y grows downward from the TOP of the box. So
 * the offset that lands this window's bottom row at the right height in the
 * virtual scene is measured from the box's BOTTOM edge, i.e. the box height less
 * the rect's bottom edge. Get this backwards and a horizontal layout still looks
 * right (every off_y is 0) while a stacked one renders the halves swapped.
 *
 * Returns false (leaving *out untouched) if index is out of range, if rects[index]
 * is itself degenerate, or if no rect is valid.
 */
bool span_compute(const struct span_rect *rects, size_t count, size_t index,
                  struct span_view *out);

#endif /* NEOWALL_OUTPUT_SPAN_H */
