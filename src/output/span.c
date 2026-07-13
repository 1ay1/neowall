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

/* Integer scale of a rect, never below 1. Used only for the logical*scale
 * fallback when the real device size is unknown. */
static int32_t s_or_1(const struct span_rect *r) {
    return rect_scale(r);
}

/* Map `v` logical pixels into device pixels at the ratio device/logical,
 * rounded to nearest. 64-bit intermediate: v and device are display-sized
 * (< 2^20), so v*device fits comfortably and never overflows. logical is
 * guaranteed > 0 by the rect_valid() gate before this is reached. */
static int32_t scale_ratio(int32_t v, int32_t device, int32_t logical) {
    if (logical <= 0) {
        return v;
    }
    int64_t num = (int64_t)v * (int64_t)device;
    /* Round to nearest, halves away from zero. v and off are >= 0 here. */
    return (int32_t)((num + logical / 2) / logical);
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
     * device. Scale by THIS output's real device-per-logical ratio, not a bare
     * integer scale: under fractional scaling the framebuffer is rounded
     * independently of logical*scale, and using the real device size keeps
     * off + my_device_size == virt exact, so my slice abuts my neighbour's with
     * no seam. When device_w/h are unset (X11, or before configure) fall back to
     * logical*scale, which is the identity on X11 and exact under integer scale.
     *
     * The ratio is applied so that my own logical extent maps EXACTLY onto my
     * framebuffer: virt = round(box * device / logical), off = round(offset *
     * device / logical). Because box >= logical and offset < box, both stay
     * within int32 for any real display. */
    int32_t dev_w = me->device_w > 0 ? me->device_w : me->logical_w * s_or_1(me);
    int32_t dev_h = me->device_h > 0 ? me->device_h : me->logical_h * s_or_1(me);

    out->virt_w = scale_ratio(box_w, dev_w, me->logical_w);
    out->virt_h = scale_ratio(box_h, dev_h, me->logical_h);
    out->off_x = scale_ratio(off_x, dev_w, me->logical_w);
    out->off_y = scale_ratio(off_y, dev_h, me->logical_h);
    return true;
}

