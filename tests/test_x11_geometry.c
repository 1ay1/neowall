/* Unit tests for the X11 multi-monitor geometry helpers (x11_geometry.c).
 *
 * Pure integer math, no Xlib / display server — runs headless in CI. Covers
 * the per-output occlusion coverage test, the per-output mouse mapping, and
 * the rect-equality used by hotplug reconcile. Includes the exact dual-monitor
 * layout from issue #44 (left VGA-0 primary at +1920+0, right DVI-D-0 at +0+0).
 */
#include <stdio.h>
#include <string.h>

#include "x11_geometry.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

/* The #44 layout: two 1920x1200 monitors side by side. DVI-D-0 sits at the
 * origin, VGA-0 (primary) to its right. */
static const x11_rect_t DVI = { 0, 0, 1920, 1200 };
static const x11_rect_t VGA = { 1920, 0, 1920, 1200 };

static void test_coverage(void) {
    /* A fullscreen window exactly over VGA occludes VGA but not DVI. */
    x11_rect_t fs_vga[] = { { 1920, 0, 1920, 1200 } };
    CHECK(x11_rect_is_covered(VGA, fs_vga, 1));
    CHECK(!x11_rect_is_covered(DVI, fs_vga, 1));

    /* And vice versa. */
    x11_rect_t fs_dvi[] = { { 0, 0, 1920, 1200 } };
    CHECK(x11_rect_is_covered(DVI, fs_dvi, 1));
    CHECK(!x11_rect_is_covered(VGA, fs_dvi, 1));

    /* A window spanning both screens occludes both. */
    x11_rect_t fs_both[] = { { 0, 0, 3840, 1200 } };
    CHECK(x11_rect_is_covered(DVI, fs_both, 1));
    CHECK(x11_rect_is_covered(VGA, fs_both, 1));

    /* No fullscreen windows -> nothing occluded. */
    CHECK(!x11_rect_is_covered(DVI, NULL, 0));
    CHECK(!x11_rect_is_covered(VGA, fs_vga, 0));
}

static void test_coverage_threshold(void) {
    x11_rect_t mon = { 0, 0, 1000, 1000 };  /* 1,000,000 px */

    /* 95% coverage (950x1000) clears the 90% bar. */
    x11_rect_t big[] = { { 0, 0, 950, 1000 } };
    CHECK(x11_rect_is_covered(mon, big, 1));

    /* 80% coverage (800x1000) does not. */
    x11_rect_t small[] = { { 0, 0, 800, 1000 } };
    CHECK(!x11_rect_is_covered(mon, small, 1));

    /* Exactly 90% is "covered" (>=). */
    x11_rect_t exact[] = { { 0, 0, 900, 1000 } };
    CHECK(x11_rect_is_covered(mon, exact, 1));

    /* Coverage is per-window, NOT summed: two half-windows that each cover 50%
     * must NOT add up to "occluded". */
    x11_rect_t halves[] = { { 0, 0, 500, 1000 }, { 500, 0, 500, 1000 } };
    CHECK(!x11_rect_is_covered(mon, halves, 2));

    /* A zero-area output is never covered. */
    x11_rect_t zero = { 0, 0, 0, 0 };
    CHECK(!x11_rect_is_covered(zero, big, 1));
}

static void test_mouse_mapping(void) {
    int lx, ly;

    /* Pointer at root (100,50) lands inside DVI at the same local coords. */
    CHECK(x11_mouse_to_output(DVI, 100, 50, &lx, &ly));
    CHECK(lx == 100 && ly == 50);

    /* That same root point is NOT over VGA -> (-1,-1). */
    CHECK(!x11_mouse_to_output(VGA, 100, 50, &lx, &ly));
    CHECK(lx == -1 && ly == -1);

    /* Pointer at root (2000,50): outside DVI, inside VGA at local (80,50). */
    CHECK(!x11_mouse_to_output(DVI, 2000, 50, &lx, &ly));
    CHECK(x11_mouse_to_output(VGA, 2000, 50, &lx, &ly));
    CHECK(lx == 80 && ly == 50);

    /* Boundary: x == width is OUTSIDE (half-open rect). */
    CHECK(x11_mouse_to_output(DVI, 1919, 600, &lx, &ly));
    CHECK(!x11_mouse_to_output(DVI, 1920, 600, &lx, &ly));

    /* Top-left corner is inside; one pixel up/left is not. */
    CHECK(x11_mouse_to_output(VGA, 1920, 0, &lx, &ly));
    CHECK(lx == 0 && ly == 0);
    CHECK(!x11_mouse_to_output(VGA, 1919, 0, &lx, &ly));
}

static void test_rect_equal(void) {
    CHECK(x11_rect_equal(DVI, DVI));
    CHECK(!x11_rect_equal(DVI, VGA));

    /* Same origin, different size -> not equal (a mode change). */
    x11_rect_t dvi_resized = { 0, 0, 1280, 1024 };
    CHECK(!x11_rect_equal(DVI, dvi_resized));

    /* Same size, different origin -> not equal (a move). */
    x11_rect_t dvi_moved = { 10, 0, 1920, 1200 };
    CHECK(!x11_rect_equal(DVI, dvi_moved));
}

int main(void) {
    test_coverage();
    test_coverage_threshold();
    test_mouse_mapping();
    test_rect_equal();

    printf("x11_geometry: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
