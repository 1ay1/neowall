/* EXIF Orientation parser + RGBA-buffer transform.
 *
 * Split out of image.c so it can be unit-tested without libjpeg/libpng deps.
 * Logging deliberately stubbed to fprintf so the test binary doesn't need to
 * pull in the rest of neowall just to print one warn line on alloc failure. */
#include "neowall/image/exif.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TIFF byte-order helpers. `be` selects big-endian ("MM"). */
static uint16_t exif_u16(const uint8_t *p, int be) {
    return be ? (uint16_t)((p[0] << 8) | p[1])
              : (uint16_t)((p[1] << 8) | p[0]);
}
static uint32_t exif_u32(const uint8_t *p, int be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

int exif_parse_orientation(const uint8_t *data, size_t len) {
    if (!data) return EXIF_ORIENT_TOP_LEFT;
    /* APP1 EXIF signature ("Exif\0\0", 6 B) + minimal TIFF header (8 B). */
    if (len < 6 + 8) return EXIF_ORIENT_TOP_LEFT;
    if (memcmp(data, "Exif\0\0", 6) != 0) return EXIF_ORIENT_TOP_LEFT;

    const uint8_t *tiff = data + 6;
    size_t tiff_len = len - 6;

    int be;
    if (tiff[0] == 'M' && tiff[1] == 'M') be = 1;
    else if (tiff[0] == 'I' && tiff[1] == 'I') be = 0;
    else return EXIF_ORIENT_TOP_LEFT;

    if (exif_u16(tiff + 2, be) != 0x002A) return EXIF_ORIENT_TOP_LEFT;

    uint32_t ifd0_off = exif_u32(tiff + 4, be);
    if ((size_t)ifd0_off + 2 > tiff_len) return EXIF_ORIENT_TOP_LEFT;

    uint16_t n_entries = exif_u16(tiff + ifd0_off, be);
    /* Each IFD entry is 12 B. Cap to dodge pathological files. */
    if (n_entries > 1024) return EXIF_ORIENT_TOP_LEFT;
    if ((size_t)ifd0_off + 2 + (size_t)n_entries * 12 > tiff_len) return EXIF_ORIENT_TOP_LEFT;

    for (uint16_t i = 0; i < n_entries; i++) {
        const uint8_t *e = tiff + ifd0_off + 2 + (size_t)i * 12;
        uint16_t tag  = exif_u16(e, be);
        uint16_t type = exif_u16(e + 2, be);
        uint32_t cnt  = exif_u32(e + 4, be);
        if (tag == 0x0112 /* Orientation */ && type == 3 /* SHORT */ && cnt >= 1) {
            /* SHORT inline value lives in the first 2 B of the value field
             * (offset +8); endian handling is the same as any other u16 read. */
            uint16_t v = exif_u16(e + 8, be);
            if (v >= 1 && v <= 8) return (int)v;
            return EXIF_ORIENT_TOP_LEFT;
        }
    }
    return EXIF_ORIENT_TOP_LEFT;
}

void image_apply_exif_orientation(struct image_data *img, int orientation) {
    if (!img || !img->pixels || orientation == EXIF_ORIENT_TOP_LEFT) return;
    if (orientation < 1 || orientation > 8) return;

    const uint32_t w = img->width;
    const uint32_t h = img->height;
    const int transpose = (orientation >= 5);
    const uint32_t nw = transpose ? h : w;
    const uint32_t nh = transpose ? w : h;

    /* Overflow guard. The product nw*nh*4 must fit in size_t. On 64-bit
     * `(size_t)nw*4 > SIZE_MAX/nh` covers everything we'd reach in practice;
     * the wider bound was already checked once in the caller (pre-rotation)
     * but transpose changes which axis is the long one, so re-check here. */
    if (nh != 0 && (size_t)nw * 4 > SIZE_MAX / nh) return;
    uint8_t *dst = malloc((size_t)nw * nh * 4);
    if (!dst) {
        fprintf(stderr, "EXIF orient: alloc failed, leaving image as-is\n");
        return;
    }

    const uint8_t *src = img->pixels;
    /* Walk the source linearly; compute the destination (dx,dy) per the EXIF
     * transform table. Cache-friendly on the read side, scatter on the write. */
    for (uint32_t sy = 0; sy < h; sy++) {
        for (uint32_t sx = 0; sx < w; sx++) {
            uint32_t dx = 0, dy = 0;
            switch (orientation) {
                case EXIF_ORIENT_TOP_RIGHT:    dx = w - 1 - sx; dy = sy;            break;
                case EXIF_ORIENT_BOTTOM_RIGHT: dx = w - 1 - sx; dy = h - 1 - sy;    break;
                case EXIF_ORIENT_BOTTOM_LEFT:  dx = sx;         dy = h - 1 - sy;    break;
                case EXIF_ORIENT_LEFT_TOP:     dx = sy;         dy = sx;            break;
                case EXIF_ORIENT_RIGHT_TOP:    dx = h - 1 - sy; dy = sx;            break;
                case EXIF_ORIENT_RIGHT_BOTTOM: dx = h - 1 - sy; dy = w - 1 - sx;    break;
                case EXIF_ORIENT_LEFT_BOTTOM:  dx = sy;         dy = w - 1 - sx;    break;
                default:                       dx = sx;         dy = sy;            break;
            }
            size_t s = ((size_t)sy * w + sx) * 4;
            size_t d = ((size_t)dy * nw + dx) * 4;
            dst[d + 0] = src[s + 0];
            dst[d + 1] = src[s + 1];
            dst[d + 2] = src[s + 2];
            dst[d + 3] = src[s + 3];
        }
    }

    free(img->pixels);
    img->pixels = dst;
    img->width  = nw;
    img->height = nh;
}
