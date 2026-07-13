/* Unit tests for virtual-screen geometry (span.c).
 *
 * Spanning one shader across several monitors turns on two numbers per output:
 * the size of the virtual screen (iResolution) and the offset added to
 * gl_FragCoord. The offset's Y half is flipped, because gl_FragCoord counts up
 * from the bottom while output positions count down from the top — a horizontal
 * layout cannot catch that mistake (every off_y is 0 either way), so the stacked
 * and mixed-height layouts below are the ones that matter.
 *
 * The other thing these numbers can get wrong is their UNIT. span_rect is
 * logical, span_view is device, and every rect below carries the scale that
 * converts between them. The scale-1 cases are the whole of X11 and the whole
 * of any Wayland session that has not been told otherwise, so they are also the
 * regression bar: at scale 1 the two spaces coincide and the expected values are
 * plain pixels, exactly as they were before scale existed here.
 */
#include <stdbool.h>
#include <stdio.h>

#include "neowall/output/span.h"

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

#define CHECK_VIEW(v, w, h, ox, oy)                                            \
    do {                                                                       \
        CHECK((v).virt_w == (w));                                              \
        CHECK((v).virt_h == (h));                                              \
        CHECK((v).off_x == (ox));                                              \
        CHECK((v).off_y == (oy));                                              \
    } while (0)

/* Every slice must sit wholly inside the virtual screen. Catches a sign error
 * in either axis regardless of the layout — and, now that the two spaces are
 * distinct, catches a slice measured in the wrong one: the rect is logical, the
 * view is device, so the comparison only closes if the scale is applied. */
static void check_contained(const struct span_rect *rects, size_t count) {
    for (size_t i = 0; i < count; i++) {
        struct span_view v;
        if (!span_compute(rects, count, i, &v)) {
            continue;
        }
        int32_t s = rects[i].scale > 0 ? rects[i].scale : 1;
        CHECK(v.off_x >= 0);
        CHECK(v.off_y >= 0);
        CHECK(v.off_x + rects[i].logical_w * s <= v.virt_w);
        CHECK(v.off_y + rects[i].logical_h * s <= v.virt_h);
    }
}

/* A lone output must be untouched by spanning: the virtual screen is exactly it
 * and both offsets are zero, so the shader sees precisely what it saw before. */
static void test_single_monitor_is_identity(void) {
    struct span_rect rects[] = {{0, 0, 2560, 1440, 1}};
    struct span_view v;

    CHECK(span_compute(rects, 1, 0, &v));
    CHECK_VIEW(v, 2560, 1440, 0, 0);
    check_contained(rects, 1);
}

/* Even parked away from the origin, one output alone spans only itself. */
static void test_single_monitor_at_offset_is_identity(void) {
    struct span_rect rects[] = {{100, 50, 1920, 1080, 1}};
    struct span_view v;

    CHECK(span_compute(rects, 1, 0, &v));
    CHECK_VIEW(v, 1920, 1080, 0, 0);
}

static void test_horizontal_pair(void) {
    struct span_rect rects[] = {
        {0, 0, 2560, 1440, 1},
        {2560, 0, 2560, 1440, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 5120, 1440, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 5120, 1440, 2560, 0);
    check_contained(rects, 2);
}

/* The flip's real test. rects[0] is the TOP monitor, so in gl_FragCoord's
 * bottom-up space it is the one that must be pushed UP by a full screen height;
 * the bottom monitor is the one that sits at zero. */
static void test_vertical_pair_flips_y(void) {
    struct span_rect rects[] = {
        {0, 0, 2560, 1440, 1},     /* top */
        {0, 1440, 2560, 1440, 1},  /* bottom */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 2560, 2880, 0, 1440);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 2560, 2880, 0, 0);
    check_contained(rects, 2);
}

/* Top-aligned monitors of different heights: the shorter one's bottom edge is
 * ABOVE the virtual bottom, so it gets a non-zero off_y. A naive `off_y =
 * logical_y` would give it 0 and hang it off the wrong edge. */
static void test_mixed_heights_top_aligned(void) {
    struct span_rect rects[] = {
        {0, 0, 2560, 1440, 1},
        {2560, 0, 1920, 1080, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 4480, 1440, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 4480, 1440, 2560, 360);
    check_contained(rects, 2);
}

/* Bottom-aligned instead: now the short one sits at off_y 0 and the tall one
 * still does too — but the box is the tall one's height. */
static void test_mixed_heights_bottom_aligned(void) {
    struct span_rect rects[] = {
        {0, 0, 2560, 1440, 1},
        {2560, 360, 1920, 1080, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 4480, 1440, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 4480, 1440, 2560, 0);
    check_contained(rects, 2);
}

/* X11 lets a monitor sit left of the origin. The box's origin moves with it and
 * every offset stays non-negative, so the left monitor — not the one at x=0 —
 * is the one that lands at off_x 0. */
static void test_negative_x_offset(void) {
    struct span_rect rects[] = {
        {-2560, 0, 2560, 1440, 1},
        {0, 0, 2560, 1440, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 5120, 1440, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 5120, 1440, 2560, 0);
    check_contained(rects, 2);
}

/* Same, stacked upward: the monitor above the origin is the one that gets the
 * high off_y, and the origin monitor drops to 0. */
static void test_negative_y_offset(void) {
    struct span_rect rects[] = {
        {0, -1440, 2560, 1440, 1},  /* above origin */
        {0, 0, 2560, 1440, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 2560, 2880, 0, 1440);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 2560, 2880, 0, 0);
    check_contained(rects, 2);
}

/* An L-shape exercises both axes at once. */
static void test_mixed_arrangement(void) {
    struct span_rect rects[] = {
        {0, 0, 1920, 1080, 1},
        {1920, 0, 2560, 1440, 1},
        {1920, 1440, 1280, 1024, 1},
    };
    struct span_view v;

    /* Box: x 0..4480, y 0..2464 */
    CHECK(span_compute(rects, 3, 0, &v));
    CHECK_VIEW(v, 4480, 2464, 0, 2464 - 1080);
    CHECK(span_compute(rects, 3, 1, &v));
    CHECK_VIEW(v, 4480, 2464, 1920, 2464 - 1440);
    CHECK(span_compute(rects, 3, 2, &v));
    CHECK_VIEW(v, 4480, 2464, 1920, 0);
    check_contained(rects, 3);
}

/* An output that has not been configured yet has no size. Letting it into the
 * box would drag the origin to (0,0) and shift every real output's slice. */
static void test_unsized_output_is_ignored(void) {
    struct span_rect rects[] = {
        {0, 0, 0, 0, 1},
        {2560, 0, 2560, 1440, 1},
        {5120, 0, 2560, 1440, 1},
    };
    struct span_view v;

    CHECK(span_compute(rects, 3, 1, &v));
    CHECK_VIEW(v, 5120, 1440, 0, 0);
    CHECK(span_compute(rects, 3, 2, &v));
    CHECK_VIEW(v, 5120, 1440, 2560, 0);

    /* Nothing to compute for the unsized one itself. */
    CHECK(!span_compute(rects, 3, 0, &v));
}

/* A HiDPI monitor beside a plain one. In the compositor's LOGICAL layout the two
 * are the same size and sit side by side; in device pixels the left one is twice
 * as wide as it is logically, and a box built out of device extents would have
 * the right-hand monitor's rect (x=1920, w=1920) falling wholly INSIDE the left
 * one's (x=0, w=3840) — the box would collapse to one monitor and the right-hand
 * output would render a slice of a scene nobody was drawing.
 *
 * Both outputs draw the same 3840x1080 logical scene; each is handed it at its
 * own density, so their iResolutions differ and neither is the sum of the two
 * framebuffers. Derived by hand:
 *   logical box:  x 0..1920 u 1920..3840 = 0..3840; y 0..1080 -> 3840x1080
 *   A (scale 2):  virt 7680x2160; off_x (0-0)*2 = 0
 *                 off_y (1080 - (0 + 1080)) * 2 = 0
 *   B (scale 1):  virt 3840x1080; off_x (1920-0)*1 = 1920
 *                 off_y (1080 - (0 + 1080)) * 1 = 0
 * A's slice is uv.x 0..0.5 of the scene and B's is 0.5..1.0: continuous across
 * the bezel, and the same logical distance per screen centimetre on both. */
static void test_mixed_scale_hidpi_beside_sdr(void) {
    struct span_rect rects[] = {
        {0, 0, 1920, 1080, 2},     /* A: logical 1920x1080 @ (0,0), 3840x2160 device */
        {1920, 0, 1920, 1080, 1},  /* B: logical 1920x1080 @ (1920,0), 1920x1080 device */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 7680, 2160, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 3840, 1080, 1920, 0);
    check_contained(rects, 2);

    /* Neither output's virtual screen is its own framebuffer, so neither trips
     * the "group is just me" guard in outputs_update_spans() and drops out of
     * the span. That guard firing for one of a pair is the visible symptom:
     * one monitor renders the whole scene standalone while the other renders a
     * slice of it. */
    CHECK(span_compute(rects, 2, 0, &v));
    CHECK(!(v.virt_w == 1920 * 2 && v.virt_h == 1080 * 2));
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK(!(v.virt_w == 1920 && v.virt_h == 1080));
}

/* Mixed scales AND the Y flip at once: the flip is applied in logical space and
 * scaled afterwards, so a bug that scales before flipping shows up here.
 *   logical box:  x 0..1920 u 0..2560 = 0..2560
 *                 y 0..1080 u 1080..2520 = 0..2520  -> 2560x2520
 *   top (scale 2):  virt 5120x5040; off_x 0
 *                   off_y (2520 - (0 + 1080)) * 2 = 1440 * 2 = 2880
 *   bottom (sc 1):  virt 2560x2520; off_x 0
 *                   off_y (2520 - (1080 + 1440)) * 1 = 0 */
static void test_mixed_scale_stacked_flips_y_in_logical_space(void) {
    struct span_rect rects[] = {
        {0, 0, 1920, 1080, 2},     /* top:    logical 1920x1080, 3840x2160 device */
        {0, 1080, 2560, 1440, 1},  /* bottom: logical 2560x1440, 2560x1440 device */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 5120, 5040, 0, 2880);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 2560, 2520, 0, 0);
    check_contained(rects, 2);
}

/* Uniform scale factors straight out of the answer: a pair of scale-2 monitors
 * gives exactly what the same pair at scale 1 gives, times two, in both axes and
 * both offsets. This is the sanity check on the whole scheme — where all outputs
 * agree on a scale, logical and device differ only by that constant. */
static void test_uniform_scale_is_a_constant_factor(void) {
    struct span_rect one[] = {
        {0, 0, 2560, 1440, 1},
        {2560, 0, 2560, 1440, 1},
    };
    struct span_rect two[] = {
        {0, 0, 2560, 1440, 2},
        {2560, 0, 2560, 1440, 2},
    };
    struct span_view a, b;

    for (size_t i = 0; i < 2; i++) {
        CHECK(span_compute(one, 2, i, &a));
        CHECK(span_compute(two, 2, i, &b));
        CHECK(b.virt_w == a.virt_w * 2);
        CHECK(b.virt_h == a.virt_h * 2);
        CHECK(b.off_x == a.off_x * 2);
        CHECK(b.off_y == a.off_y * 2);
    }
    check_contained(two, 2);
}

/* THE REGRESSION BAR. At scale 1 logical and device are the same space, so every
 * view must be plain pixels — bit for bit what span_compute returned before it
 * knew what a scale was. An output that has not yet heard a scale event carries
 * 0, which must read as 1 rather than collapsing the scene to nothing. */
static void test_scale_one_and_unset_are_the_pixel_identity(void) {
    struct span_rect ones[] = {
        {0, 0, 2560, 1440, 1},
        {2560, 0, 1920, 1080, 1},
    };
    struct span_rect unset[] = {
        {0, 0, 2560, 1440, 0},
        {2560, 0, 1920, 1080, 0},
    };
    struct span_view v;

    /* Identical to test_mixed_heights_top_aligned, in device pixels. */
    CHECK(span_compute(ones, 2, 0, &v));
    CHECK_VIEW(v, 4480, 1440, 0, 0);
    CHECK(span_compute(ones, 2, 1, &v));
    CHECK_VIEW(v, 4480, 1440, 2560, 360);

    CHECK(span_compute(unset, 2, 0, &v));
    CHECK_VIEW(v, 4480, 1440, 0, 0);
    CHECK(span_compute(unset, 2, 1, &v));
    CHECK_VIEW(v, 4480, 1440, 2560, 360);
    check_contained(unset, 2);
}

/* `xrandr --same-as` with UNEQUAL resolutions: two outputs on one origin, the
 * smaller nested inside the larger. X shows the smaller one the top-left crop of
 * the region, and that is exactly what the box arithmetic hands it.
 *   box: x 0..1920, y 0..1080 (the union) -> 1920x1080
 *   big:   virt 1920x1080; off_x 0; off_y 1080 - (0 + 1080) = 0
 *          -> its own size at zero offset, i.e. the whole scene. The caller's
 *             degenerate guard short-circuits this to standalone, which draws
 *             the identical picture.
 *   small: virt 1920x1080; off_x 0; off_y 1080 - (0 + 720) = 360
 *          -> rows 360..1080 counting up from the bottom = the TOP 720 rows, and
 *             columns 0..1280 = the LEFT 1280. The top-left crop. */
static void test_nested_clone_gets_the_matching_crop(void) {
    struct span_rect rects[] = {
        {0, 0, 1920, 1080, 1},  /* big */
        {0, 0, 1280, 720, 1},   /* clone of it, smaller mode, same origin */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 1920, 1080, 0, 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK_VIEW(v, 1920, 1080, 0, 360);
    check_contained(rects, 2);
}

/* And the clone pair must not poison the rest of the group. A third, ordinary
 * tiled output beside the cloned pair goes on spanning exactly as it would have
 * without them — a nested rect widens no box and moves no origin.
 *   box: x 0..1920 u 0..1280 u 1920..3840 = 0..3840; y 0..1080 -> 3840x1080
 *   big:   off_x 0;    off_y 1080 - 1080 = 0     (now 3840 wide != its own 1920:
 *                                                 spans, rather than degenerate)
 *   small: off_x 0;    off_y 1080 - 720  = 360
 *   tiled: off_x 1920; off_y 1080 - 1080 = 0 */
static void test_nested_clone_does_not_disable_the_group(void) {
    struct span_rect rects[] = {
        {0, 0, 1920, 1080, 1},     /* big */
        {0, 0, 1280, 720, 1},      /* its clone */
        {1920, 0, 1920, 1080, 1},  /* an ordinary tiled monitor beside them */
    };
    struct span_view v;

    CHECK(span_compute(rects, 3, 0, &v));
    CHECK_VIEW(v, 3840, 1080, 0, 0);
    CHECK(span_compute(rects, 3, 1, &v));
    CHECK_VIEW(v, 3840, 1080, 0, 360);
    CHECK(span_compute(rects, 3, 2, &v));
    CHECK_VIEW(v, 3840, 1080, 1920, 0);
    check_contained(rects, 3);
}

static void test_rejects_bad_input(void) {
    struct span_rect rects[] = {{0, 0, 2560, 1440, 1}};
    struct span_view v;

    CHECK(!span_compute(NULL, 1, 0, &v));
    CHECK(!span_compute(rects, 1, 0, NULL));
    CHECK(!span_compute(rects, 1, 1, &v));
    CHECK(!span_compute(rects, 0, 0, &v));
}

/* FRACTIONAL SCALE. A wlroots compositor at scale 1.5 hands a 2560x1440 panel a
 * LOGICAL box that is not a whole multiple of its framebuffer: 2560/1.5 rounds
 * to 1707x960, while the framebuffer stays 2560x1440. The old code scaled the
 * shared logical box by an INTEGER scale, which cannot represent 1.5 and would
 * seam the two heads; the fix scales by the real device/logical ratio, so each
 * head's own logical extent maps exactly onto its own framebuffer.
 *
 * Two such panels side by side. Logical box = 1707 + 1707 = 3414 wide, 960 tall.
 *   left  (device 2560x1440): its logical extent 1707 must map to 2560 device,
 *         so virt = round(3414 * 2560/1707) = 5120, off_x 0.
 *   right (device 2560x1440): off_x = round(1707 * 2560/1707) = 2560, and
 *         off_x + device_w (2560) = 5120 = virt, i.e. it abuts the left head
 *         with NO gap and NO overlap. That exact-abut is the whole point. */
static void test_fractional_scale_abuts_without_seam(void) {
    struct span_rect rects[] = {
        /* logical_x, logical_y, logical_w, logical_h, scale, device_w, device_h */
        {0,    0, 1707, 960, 2, 2560, 1440},  /* left  @ 1.5x */
        {1707, 0, 1707, 960, 2, 2560, 1440},  /* right @ 1.5x */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK_VIEW(v, 5120, 1440, 0, 0);
    /* The invariant that no integer scale could satisfy here: this head's slice
     * ends exactly where the virtual screen does. */
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK(v.off_x + rects[1].device_w == v.virt_w);
    CHECK_VIEW(v, 5120, 1440, 2560, 0);
}

/* Mixed density that is genuinely fractional on one side: a 1.5x panel beside a
 * 1x panel. The logical box is built from each output's real logical size, and
 * each is mapped into its own framebuffer by its own ratio, so the scale-1 head
 * is untouched while the fractional head still abuts it exactly.
 *   left  @1.5x: logical 1707x960, device 2560x1440
 *   right @1x:   logical 1920x1080, device 1920x1080, at logical x 1707
 *   box: 1707 + 1920 = 3627 wide, max height 1080
 *   left:  virt_w round(3627 * 2560/1707) = 5439, off_x 0
 *   right: virt_w round(3627 * 1920/1920) = 3627, off_x round(1707*1920/1920)=1707,
 *          and off_x + 1920 = 3627 = virt_w: exact abut on the scale-1 side too. */
static void test_fractional_beside_integer(void) {
    struct span_rect rects[] = {
        {0,    0, 1707, 960,  2, 2560, 1440},  /* @1.5x */
        {1707, 0, 1920, 1080, 1, 1920, 1080},  /* @1x   */
    };
    struct span_view v;

    CHECK(span_compute(rects, 2, 0, &v));
    CHECK(v.virt_w == 5439);
    CHECK(v.off_x == 0);
    CHECK(span_compute(rects, 2, 1, &v));
    CHECK(v.virt_w == 3627);
    CHECK(v.off_x + rects[1].device_w == v.virt_w);  /* the scale-1 head abuts exactly */
}

int main(void) {
    test_single_monitor_is_identity();
    test_single_monitor_at_offset_is_identity();
    test_horizontal_pair();
    test_vertical_pair_flips_y();
    test_mixed_heights_top_aligned();
    test_mixed_heights_bottom_aligned();
    test_negative_x_offset();
    test_negative_y_offset();
    test_mixed_arrangement();
    test_unsized_output_is_ignored();
    test_mixed_scale_hidpi_beside_sdr();
    test_mixed_scale_stacked_flips_y_in_logical_space();
    test_uniform_scale_is_a_constant_factor();
    test_scale_one_and_unset_are_the_pixel_identity();
    test_nested_clone_gets_the_matching_crop();
    test_nested_clone_does_not_disable_the_group();
    test_rejects_bad_input();
    test_fractional_scale_abuts_without_seam();
    test_fractional_beside_integer();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
