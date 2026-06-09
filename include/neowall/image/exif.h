/* EXIF Orientation parsing + transform for JPEG decode.
 *
 * Internal to the image module. Split out from image.c so the parser/transform
 * can be unit-tested without dragging libjpeg, libpng, or the rest of neowall's
 * I/O surface into the test binary. (See tests/test_image_exif.c.)
 *
 * Parser: walks the TIFF IFD0 in an APP1 marker payload looking for tag 0x0112
 * (Orientation). Handles both byte orders ("MM" big-endian, "II" little).
 * Treats malformed EXIF as "no rotation" — never fails the decode.
 *
 * Transform: rewrites an RGBA buffer in place (new buffer allocated, old freed)
 * to honor one of the 8 EXIF orientation tags. Width/height in `img` are
 * updated when the transform transposes the axes.
 */
#ifndef NEOWALL_IMAGE_EXIF_H
#define NEOWALL_IMAGE_EXIF_H

#include <stddef.h>
#include <stdint.h>

#include "neowall/image/image.h"

/* EXIF Orientation tag values (TIFF/EP §4.6.4). 1 = identity. */
enum exif_orientation {
    EXIF_ORIENT_TOP_LEFT       = 1, /* identity */
    EXIF_ORIENT_TOP_RIGHT      = 2, /* mirror horizontal */
    EXIF_ORIENT_BOTTOM_RIGHT   = 3, /* rotate 180 */
    EXIF_ORIENT_BOTTOM_LEFT    = 4, /* mirror vertical */
    EXIF_ORIENT_LEFT_TOP       = 5, /* transpose */
    EXIF_ORIENT_RIGHT_TOP      = 6, /* rotate 90 CW */
    EXIF_ORIENT_RIGHT_BOTTOM   = 7, /* transverse */
    EXIF_ORIENT_LEFT_BOTTOM    = 8, /* rotate 270 CW */
};

/* Parse an APP1 (EXIF) marker payload and return the Orientation tag value.
 * `data` points at the bytes after the JPEG marker length field — i.e. the
 * first 6 bytes are "Exif\0\0" followed by a TIFF header. Returns
 * EXIF_ORIENT_TOP_LEFT (1) on any malformed input. */
int exif_parse_orientation(const uint8_t *data, size_t len);

/* Apply an EXIF orientation to an RGBA pixel buffer in place. On axis-
 * transposing orientations (5–8) the buffer is reallocated and img->width /
 * img->height are swapped. No-op for orientation 1 or invalid values. */
void image_apply_exif_orientation(struct image_data *img, int orientation);

#endif /* NEOWALL_IMAGE_EXIF_H */
