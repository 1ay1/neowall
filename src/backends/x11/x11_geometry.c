/* Pure geometry helpers for the X11 backend. No Xlib, no globals — see header. */

#include "x11_geometry.h"

bool x11_rect_is_covered(x11_rect_t t, const x11_rect_t *covers, int count) {
    if (t.w <= 0 || t.h <= 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        /* Intersection of the candidate with the target. */
        int ix = covers[i].x > t.x ? covers[i].x : t.x;
        int iy = covers[i].y > t.y ? covers[i].y : t.y;
        int ir = (covers[i].x + covers[i].w) < (t.x + t.w)
                 ? (covers[i].x + covers[i].w) : (t.x + t.w);
        int ib = (covers[i].y + covers[i].h) < (t.y + t.h)
                 ? (covers[i].y + covers[i].h) : (t.y + t.h);
        int iw = ir - ix, ih = ib - iy;
        if (iw > 0 && ih > 0 &&
            (long)iw * ih * X11_COVERAGE_DEN >=
            (long)t.w * t.h * X11_COVERAGE_NUM) {
            return true;
        }
    }
    return false;
}

bool x11_mouse_to_output(x11_rect_t o, int root_x, int root_y,
                         int *out_x, int *out_y) {
    int lx = root_x - o.x;
    int ly = root_y - o.y;
    bool inside = lx >= 0 && ly >= 0 &&
                  (o.w <= 0 || lx < o.w) &&
                  (o.h <= 0 || ly < o.h);
    if (inside) {
        *out_x = lx;
        *out_y = ly;
        return true;
    }
    *out_x = -1;
    *out_y = -1;
    return false;
}

bool x11_rect_equal(x11_rect_t a, x11_rect_t b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
