/* Unit tests for the EXIF Orientation parser + RGBA transform (issue #48).
 *
 * Headless: no libjpeg, no display server. We hand-craft minimal APP1 EXIF
 * blobs (Exif\0\0 + TIFF header + 1 IFD entry) in both byte orders, plus a
 * handful of malformed payloads that must safely fall back to identity. We
 * also paint a 3x2 (or 2x3) RGBA "letter" buffer for each of the 8 transforms
 * and verify every destination pixel lands at the EXIF-spec coordinate.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/image/exif.h"
#include "neowall/image/image.h"

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

/* Build a minimal EXIF APP1 payload with exactly one IFD0 entry: the
 * Orientation tag with the given value. `be` selects byte order. Layout:
 *   [0..5]   "Exif\0\0"
 *   [6..7]   "MM" or "II"
 *   [8..9]   0x002A
 *   [10..13] IFD0 offset (= 8, immediately after the TIFF header)
 *   [14..15] entry count (1)
 *   [16..27] IFD entry: tag=0x0112 type=SHORT count=1 value=<orientation>
 *   [28..29] next-IFD offset (0)
 * Returns total length written. Caller passes a buffer of >= 32 B.
 */
static size_t build_exif_blob(uint8_t *buf, int be, uint16_t orientation) {
    memcpy(buf, "Exif\0\0", 6);
    if (be) { buf[6] = 'M'; buf[7] = 'M'; } else { buf[6] = 'I'; buf[7] = 'I'; }

    /* TIFF magic 0x002A */
    if (be) { buf[8] = 0x00; buf[9] = 0x2A; } else { buf[8] = 0x2A; buf[9] = 0x00; }

    /* IFD0 offset = 8 (relative to TIFF header start, i.e. buf+6) */
    if (be) { buf[10] = 0; buf[11] = 0; buf[12] = 0; buf[13] = 8; }
    else    { buf[10] = 8; buf[11] = 0; buf[12] = 0; buf[13] = 0; }

    /* IFD entry count = 1 (located at TIFF+8 = buf+14) */
    if (be) { buf[14] = 0; buf[15] = 1; } else { buf[14] = 1; buf[15] = 0; }

    /* Entry: tag=0x0112 (Orientation), type=3 (SHORT), count=1, value=orientation */
    uint8_t *e = buf + 16;
    if (be) {
        e[0] = 0x01; e[1] = 0x12;                       /* tag */
        e[2] = 0x00; e[3] = 0x03;                       /* type */
        e[4] = 0; e[5] = 0; e[6] = 0; e[7] = 1;         /* count */
        e[8] = (uint8_t)(orientation >> 8);
        e[9] = (uint8_t)(orientation & 0xFF);
        e[10] = 0; e[11] = 0;                           /* value padding */
    } else {
        e[0] = 0x12; e[1] = 0x01;
        e[2] = 0x03; e[3] = 0x00;
        e[4] = 1; e[5] = 0; e[6] = 0; e[7] = 0;
        e[8] = (uint8_t)(orientation & 0xFF);
        e[9] = (uint8_t)(orientation >> 8);
        e[10] = 0; e[11] = 0;
    }

    /* Next-IFD offset = 0 (no IFD1) */
    buf[28] = 0; buf[29] = 0; buf[30] = 0; buf[31] = 0;
    return 32;
}

static void test_parser_well_formed_be(void) {
    uint8_t buf[64];
    for (uint16_t o = 1; o <= 8; o++) {
        size_t n = build_exif_blob(buf, 1, o);
        int got = exif_parse_orientation(buf, n);
        CHECK(got == (int)o);
    }
}

static void test_parser_well_formed_le(void) {
    uint8_t buf[64];
    for (uint16_t o = 1; o <= 8; o++) {
        size_t n = build_exif_blob(buf, 0, o);
        int got = exif_parse_orientation(buf, n);
        CHECK(got == (int)o);
    }
}

static void test_parser_no_orientation_tag(void) {
    /* Same skeleton but the IFD entry is some other tag (ImageWidth = 0x0100). */
    uint8_t buf[64];
    size_t n = build_exif_blob(buf, 1, 6);
    buf[16] = 0x01; buf[17] = 0x00;  /* tag = ImageWidth, not Orientation */
    int got = exif_parse_orientation(buf, n);
    CHECK(got == EXIF_ORIENT_TOP_LEFT);
}

static void test_parser_malformed(void) {
    /* NULL / zero-length / too-short / bad signature / bad byte-order /
     * bad TIFF magic / out-of-range orientation value / out-of-bounds IFD
     * offset / absurd entry count — all must return identity, never crash. */
    CHECK(exif_parse_orientation(NULL, 0) == EXIF_ORIENT_TOP_LEFT);
    CHECK(exif_parse_orientation((const uint8_t *)"", 0) == EXIF_ORIENT_TOP_LEFT);

    uint8_t tiny[8] = {'E','x','i','f',0,0,'M','M'};
    CHECK(exif_parse_orientation(tiny, sizeof(tiny)) == EXIF_ORIENT_TOP_LEFT);

    uint8_t buf[64];
    size_t n;

    /* Bad signature */
    n = build_exif_blob(buf, 1, 6);
    buf[0] = 'X';
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);

    /* Bad byte-order tag */
    n = build_exif_blob(buf, 1, 6);
    buf[6] = 'X'; buf[7] = 'X';
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);

    /* Bad TIFF magic */
    n = build_exif_blob(buf, 1, 6);
    buf[8] = 0xFF; buf[9] = 0xFF;
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);

    /* Out-of-bounds IFD offset */
    n = build_exif_blob(buf, 1, 6);
    buf[10] = 0xFF; buf[11] = 0xFF; buf[12] = 0xFF; buf[13] = 0xFF;
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);

    /* Absurd entry count (> 1024 cap) */
    n = build_exif_blob(buf, 1, 6);
    buf[14] = 0xFF; buf[15] = 0xFF;
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);

    /* Orientation value out of range [1..8] */
    n = build_exif_blob(buf, 1, 99);
    CHECK(exif_parse_orientation(buf, n) == EXIF_ORIENT_TOP_LEFT);
}

/* ---------- transform tests ---------- */

/* Paint a w×h RGBA buffer where each pixel's R channel encodes (y*w + x),
 * G encodes x, B encodes y. Alpha = 0xFF. Lets us read back any pixel after
 * a transform and recover its original source coordinate uniquely. */
static struct image_data *make_test_img(uint32_t w, uint32_t h) {
    struct image_data *img = calloc(1, sizeof(*img));
    img->width = w;
    img->height = h;
    img->channels = 4;
    img->format = FORMAT_JPEG;
    img->pixels = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            img->pixels[i + 0] = (uint8_t)(y * w + x);
            img->pixels[i + 1] = (uint8_t)x;
            img->pixels[i + 2] = (uint8_t)y;
            img->pixels[i + 3] = 0xFF;
        }
    }
    return img;
}

static void free_test_img(struct image_data *img) {
    if (!img) return;
    free(img->pixels);
    free(img);
}

static void px_at(const struct image_data *img, uint32_t x, uint32_t y,
                  uint8_t *r, uint8_t *g, uint8_t *b) {
    size_t i = ((size_t)y * img->width + x) * 4;
    *r = img->pixels[i + 0];
    *g = img->pixels[i + 1];
    *b = img->pixels[i + 2];
}

/* Verify: for the given orientation, the pixel that started at source
 * coordinates (sx, sy) in a w×h buffer ends up at (dx, dy) in the (nw×nh)
 * destination, matching the EXIF spec. */
static void check_pixel(int orientation,
                        uint32_t w, uint32_t h,
                        uint32_t sx, uint32_t sy,
                        uint32_t dx, uint32_t dy)
{
    struct image_data *img = make_test_img(w, h);
    image_apply_exif_orientation(img, orientation);

    /* Width/height transpose for orientations 5–8. */
    uint32_t nw = (orientation >= 5) ? h : w;
    uint32_t nh = (orientation >= 5) ? w : h;
    CHECK(img->width == nw);
    CHECK(img->height == nh);

    uint8_t r, g, b;
    px_at(img, dx, dy, &r, &g, &b);
    CHECK(g == (uint8_t)sx);  /* G channel was source x */
    CHECK(b == (uint8_t)sy);  /* B channel was source y */

    free_test_img(img);
}

static void test_transform_identity(void) {
    /* Orientation 1 = no-op: (sx,sy) → (sx,sy). */
    check_pixel(EXIF_ORIENT_TOP_LEFT, 3, 2, 0, 0, 0, 0);
    check_pixel(EXIF_ORIENT_TOP_LEFT, 3, 2, 2, 1, 2, 1);
}

static void test_transform_mirror_h(void) {
    /* Orientation 2 = mirror horizontal: (sx,sy) → (w-1-sx, sy). */
    check_pixel(EXIF_ORIENT_TOP_RIGHT, 3, 2, 0, 0, 2, 0);
    check_pixel(EXIF_ORIENT_TOP_RIGHT, 3, 2, 2, 1, 0, 1);
}

static void test_transform_rotate_180(void) {
    /* Orientation 3 = rotate 180: (sx,sy) → (w-1-sx, h-1-sy). */
    check_pixel(EXIF_ORIENT_BOTTOM_RIGHT, 3, 2, 0, 0, 2, 1);
    check_pixel(EXIF_ORIENT_BOTTOM_RIGHT, 3, 2, 2, 1, 0, 0);
}

static void test_transform_mirror_v(void) {
    /* Orientation 4 = mirror vertical: (sx,sy) → (sx, h-1-sy). */
    check_pixel(EXIF_ORIENT_BOTTOM_LEFT, 3, 2, 0, 0, 0, 1);
    check_pixel(EXIF_ORIENT_BOTTOM_LEFT, 3, 2, 2, 1, 2, 0);
}

static void test_transform_transpose(void) {
    /* Orientation 5 = transpose (main diagonal): (sx,sy) → (sy, sx). */
    check_pixel(EXIF_ORIENT_LEFT_TOP, 3, 2, 0, 0, 0, 0);
    check_pixel(EXIF_ORIENT_LEFT_TOP, 3, 2, 2, 1, 1, 2);
}

static void test_transform_rotate_90_cw(void) {
    /* Orientation 6 = rotate 90 CW: (sx,sy) → (h-1-sy, sx). The portrait-
     * phone-photo case — this is the headline bug from issue #48. */
    check_pixel(EXIF_ORIENT_RIGHT_TOP, 3, 2, 0, 0, 1, 0);
    check_pixel(EXIF_ORIENT_RIGHT_TOP, 3, 2, 2, 1, 0, 2);
}

static void test_transform_transverse(void) {
    /* Orientation 7 = transverse (anti-diagonal): (sx,sy) → (h-1-sy, w-1-sx). */
    check_pixel(EXIF_ORIENT_RIGHT_BOTTOM, 3, 2, 0, 0, 1, 2);
    check_pixel(EXIF_ORIENT_RIGHT_BOTTOM, 3, 2, 2, 1, 0, 0);
}

static void test_transform_rotate_270_cw(void) {
    /* Orientation 8 = rotate 270 CW / 90 CCW: (sx,sy) → (sy, w-1-sx). */
    check_pixel(EXIF_ORIENT_LEFT_BOTTOM, 3, 2, 0, 0, 0, 2);
    check_pixel(EXIF_ORIENT_LEFT_BOTTOM, 3, 2, 2, 1, 1, 0);
}

static void test_transform_noop_safety(void) {
    /* Out-of-range orientation must NOT touch the buffer. */
    struct image_data *img = make_test_img(2, 2);
    uint8_t *old_pixels = img->pixels;
    image_apply_exif_orientation(img, 99);
    CHECK(img->pixels == old_pixels);
    CHECK(img->width == 2 && img->height == 2);
    free_test_img(img);

    /* NULL img / NULL pixels = no crash. */
    image_apply_exif_orientation(NULL, 6);
    struct image_data empty = {0};
    image_apply_exif_orientation(&empty, 6);  /* pixels==NULL */
    CHECK(1);  /* survived */
}

int main(void) {
    test_parser_well_formed_be();
    test_parser_well_formed_le();
    test_parser_no_orientation_tag();
    test_parser_malformed();

    test_transform_identity();
    test_transform_mirror_h();
    test_transform_rotate_180();
    test_transform_mirror_v();
    test_transform_transpose();
    test_transform_rotate_90_cw();
    test_transform_transverse();
    test_transform_rotate_270_cw();
    test_transform_noop_safety();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
