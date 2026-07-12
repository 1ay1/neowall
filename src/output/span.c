/* Pure virtual-screen geometry. See include/neowall/output/span.h. */
#include "neowall/output/span.h"

static bool rect_valid(const struct span_rect *r) {
    return r->logical_w > 0 && r->logical_h > 0;
}

/* Device pixels per logical pixel. An output that has not heard a scale event
 * yet carries 0; X11 always carries 1. Both mean "device == logical". */
static int32_t rect_scale(const struct span_rect *r) {
    return r->scale > 0 ? r->scale : 1;
}

bool span_compute(const struct span_rect *rects, size_t count, size_t index,
                  struct span_view *out) {
    if (!rects || !out || index >= count) {
        return false;
    }

    const struct span_rect *me = &rects[index];
    if (!rect_valid(me)) {
        return false;
    }

    /* Logical, because that is the only space the outputs share: a device-pixel
     * box over mixed scales is not a box over anything. See span.h. */
    int32_t min_x = me->logical_x;
    int32_t min_y = me->logical_y;
    int32_t max_x = me->logical_x + me->logical_w;
    int32_t max_y = me->logical_y + me->logical_h;

    for (size_t i = 0; i < count; i++) {
        const struct span_rect *r = &rects[i];
        if (i == index || !rect_valid(r)) {
            continue;
        }
        if (r->logical_x < min_x) min_x = r->logical_x;
        if (r->logical_y < min_y) min_y = r->logical_y;
        if (r->logical_x + r->logical_w > max_x) max_x = r->logical_x + r->logical_w;
        if (r->logical_y + r->logical_h > max_y) max_y = r->logical_y + r->logical_h;
    }

    int32_t box_w = max_x - min_x;
    int32_t box_h = max_y - min_y;
    int32_t off_x = me->logical_x - min_x;
    /* Flip: gl_FragCoord counts up from the window's bottom, span_rect's y counts
     * down from the box's top. See span.h. */
    int32_t off_y = box_h - ((me->logical_y - min_y) + me->logical_h);

    /* Into MY framebuffer. Everything above is logical, everything below is
     * device, and my scale is the only scale that may appear here: gl_FragCoord
     * on this output knows no other. */
    const int32_t s = rect_scale(me);
    out->virt_w = box_w * s;
    out->virt_h = box_h * s;
    out->off_x = off_x * s;
    out->off_y = off_y * s;
    return true;
}

