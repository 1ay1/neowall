#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdint.h>
#include <png.h>
#include <jpeglib.h>
#include "neowall/image/image.h"
#include "neowall/neowall.h"
#include "neowall/constants.h"

/* Forward declarations */
static struct image_data *image_scale_to_display(struct image_data *img, int32_t display_width, 
                                                   int32_t display_height, int mode);
static struct image_data *image_scale_bilinear(struct image_data *img, uint32_t new_width, uint32_t new_height);

/* Expand path with tilde */
static bool expand_path(const char *path, char *expanded, size_t size) {
    if (!path || !expanded || size == 0) {
        return false;
    }

    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~: HOME not set");
            return false;
        }

        size_t home_len = strlen(home);
        size_t path_len = strlen(path + 1);

        if (home_len + path_len + 1 > size) {
            log_error("Expanded path too long");
            return false;
        }

        /* Bounds already checked above, safe to use snprintf */
        int written = snprintf(expanded, size, "%s%s", home, path + 1);
        if (written < 0 || (size_t)written >= size) {
            log_error("Path expansion failed");
            return false;
        }
        return true;
    }

    /* No expansion needed */
    if (strlen(path) >= size) {
        log_error("Path too long");
        return false;
    }

    strncpy(expanded, path, size - 1);
    expanded[size - 1] = '\0';
    return true;
}

/* Detect image format from file extension */
enum image_format image_detect_format(const char *path) {
    if (!path) {
        return FORMAT_UNKNOWN;
    }

    const char *ext = strrchr(path, '.');
    if (!ext) {
        return FORMAT_UNKNOWN;
    }

    ext++; /* Skip the dot */

    if (strcasecmp(ext, "png") == 0) {
        return FORMAT_PNG;
    } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        return FORMAT_JPEG;
    }

    return FORMAT_UNKNOWN;
}

/* Load PNG image */
struct image_data *image_load_png(const char *path) {
    if (!path) {
        log_error("Invalid path for PNG loading");
        return NULL;
    }

    /* Expand path if needed */
    char expanded_path[MAX_PATH_LENGTH];
    if (!expand_path(path, expanded_path, sizeof(expanded_path))) {
        return NULL;
    }

    FILE *fp = fopen(expanded_path, "rb");
    if (!fp) {
        log_error("Failed to open PNG file %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    /* Check PNG signature */
    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8) {
        log_error("Failed to read PNG signature from %s", expanded_path);
        fclose(fp);
        return NULL;
    }

    if (png_sig_cmp(sig, 0, 8)) {
        log_error("File %s is not a valid PNG", expanded_path);
        fclose(fp);
        return NULL;
    }

    /* Create PNG structures */
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL, NULL, NULL);
    if (!png_ptr) {
        log_error("Failed to create PNG read struct");
        fclose(fp);
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        log_error("Failed to create PNG info struct");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    /* Set up error handling */
    if (setjmp(png_jmpbuf(png_ptr))) {
        log_error("Error reading PNG file %s", expanded_path);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    /* Initialize PNG I/O */
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    /* Read PNG info */
    png_read_info(png_ptr, info_ptr);

    uint32_t width = png_get_image_width(png_ptr, info_ptr);
    uint32_t height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    log_debug("Loading PNG: %s (%ux%u, color_type=%d, bit_depth=%d)",
              expanded_path, width, height, color_type, bit_depth);

    /* Transform to RGBA */
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    png_read_update_info(png_ptr, info_ptr);

    /* Allocate image data */
    struct image_data *img = calloc(1, sizeof(struct image_data));
    if (!img) {
        log_error("Failed to allocate image data: %s", strerror(errno));
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    img->width = width;
    img->height = height;
    img->channels = 4; /* RGBA */
    img->format = FORMAT_PNG;
    snprintf(img->path, sizeof(img->path), "%s", path);

    /* Allocate pixel buffer */
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    /* Height and row_bytes are constrained by PNG format, overflow not possible */
    img->pixels = malloc(row_bytes * height);
    if (!img->pixels) {
        log_error("Failed to allocate pixel buffer: %s", strerror(errno));
        free(img);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    /* Allocate row pointers */
    /* sizeof(png_bytep) is pointer size (8 bytes), height constrained by format */
    png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        log_error("Failed to allocate row pointers: %s", strerror(errno));
        free(img->pixels);
        free(img);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = img->pixels + y * row_bytes;
    }

    /* Read image data */
    png_read_image(png_ptr, row_pointers);

    /* Clean up */
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    log_debug("Loaded PNG image: %s (%ux%u)", expanded_path, width, height);

    return img;
}

/* JPEG error handler */
struct jpeg_error_mgr_ext {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
    char error_msg[JMSG_LENGTH_MAX];
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;
    (*cinfo->err->format_message)(cinfo, err->error_msg);
    longjmp(err->setjmp_buffer, 1);
}

/* EXIF orientation values per TIFF/EP. 1 = identity. */
enum exif_orientation {
    EXIF_ORIENT_TOP_LEFT       = 1, /* identity */
    EXIF_ORIENT_TOP_RIGHT      = 2, /* mirror horizontal */
    EXIF_ORIENT_BOTTOM_RIGHT   = 3, /* rotate 180 */
    EXIF_ORIENT_BOTTOM_LEFT    = 4, /* mirror vertical */
    EXIF_ORIENT_LEFT_TOP       = 5, /* mirror horizontal + rotate 270 CW */
    EXIF_ORIENT_RIGHT_TOP      = 6, /* rotate 90 CW */
    EXIF_ORIENT_RIGHT_BOTTOM   = 7, /* mirror horizontal + rotate 90 CW */
    EXIF_ORIENT_LEFT_BOTTOM    = 8, /* rotate 270 CW (== 90 CCW) */
};

/* Little helper: read u16/u32 honoring TIFF byte order. */
static uint16_t exif_u16(const uint8_t *p, bool be) {
    return be ? (uint16_t)((p[0] << 8) | p[1])
              : (uint16_t)((p[1] << 8) | p[0]);
}
static uint32_t exif_u32(const uint8_t *p, bool be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

/* Parse an APP1 (EXIF) marker payload and return the Orientation tag value.
 * `data` is the bytes AFTER the marker length field (i.e. starting with the
 * "Exif\0\0" signature). Returns 1 (identity) if anything looks off — we treat
 * malformed EXIF as "no rotation" rather than failing the whole decode. */
static int exif_parse_orientation(const uint8_t *data, size_t len) {
    /* APP1 EXIF signature: "Exif\0\0" (6 bytes) followed by the TIFF header. */
    if (len < 6 + 8) return EXIF_ORIENT_TOP_LEFT;
    if (memcmp(data, "Exif\0\0", 6) != 0) return EXIF_ORIENT_TOP_LEFT;

    const uint8_t *tiff = data + 6;
    size_t tiff_len = len - 6;

    bool be;
    if (tiff[0] == 'M' && tiff[1] == 'M') be = true;
    else if (tiff[0] == 'I' && tiff[1] == 'I') be = false;
    else return EXIF_ORIENT_TOP_LEFT;

    if (exif_u16(tiff + 2, be) != 0x002A) return EXIF_ORIENT_TOP_LEFT;

    uint32_t ifd0_off = exif_u32(tiff + 4, be);
    if (ifd0_off + 2 > tiff_len) return EXIF_ORIENT_TOP_LEFT;

    uint16_t n_entries = exif_u16(tiff + ifd0_off, be);
    /* Each IFD entry is 12 bytes. Cap at a sane bound to avoid pathological files. */
    if (n_entries > 1024) return EXIF_ORIENT_TOP_LEFT;
    if ((size_t)ifd0_off + 2 + (size_t)n_entries * 12 > tiff_len) return EXIF_ORIENT_TOP_LEFT;

    for (uint16_t i = 0; i < n_entries; i++) {
        const uint8_t *e = tiff + ifd0_off + 2 + (size_t)i * 12;
        uint16_t tag  = exif_u16(e, be);
        uint16_t type = exif_u16(e + 2, be);
        uint32_t cnt  = exif_u32(e + 4, be);
        if (tag == 0x0112 /* Orientation */ && type == 3 /* SHORT */ && cnt >= 1) {
            /* SHORT inline value sits in the first 2 bytes of the value field;
             * for big-endian that's e+8, little-endian also e+8 (low byte first). */
            uint16_t v = exif_u16(e + 8, be);
            if (v >= 1 && v <= 8) return (int)v;
            return EXIF_ORIENT_TOP_LEFT;
        }
    }
    return EXIF_ORIENT_TOP_LEFT;
}

/* Apply an EXIF orientation to an RGBA pixel buffer in place (new buffer
 * allocated, old one freed). Width/height in `img` are updated when the
 * orientation transposes the axes. No-op for orientation 1. */
static void image_apply_exif_orientation(struct image_data *img, int orientation) {
    if (!img || !img->pixels || orientation == EXIF_ORIENT_TOP_LEFT) {
        return;
    }
    if (orientation < 1 || orientation > 8) {
        return;
    }

    const uint32_t w = img->width;
    const uint32_t h = img->height;
    const bool transpose = (orientation >= 5);
    const uint32_t nw = transpose ? h : w;
    const uint32_t nh = transpose ? w : h;

    /* Overflow guard. The product nw*nh*4 must fit in size_t (already validated
     * pre-rotation in the loader, but transpose changes which axis is the long
     * one, so re-check). On 64-bit (size_t == uint64_t) `nw > SIZE_MAX/4` is
     * tautologically false for uint32_t — only the nh-multiplied bound matters. */
    if (nh != 0 && (size_t)nw * 4 > SIZE_MAX / nh) {
        return;
    }
    uint8_t *dst = malloc((size_t)nw * nh * 4);
    if (!dst) {
        log_warn("EXIF orient: alloc failed, leaving image as-is");
        return;
    }

    const uint8_t *src = img->pixels;
    /* For each (sx,sy) source pixel, compute its destination (dx,dy) per the
     * EXIF transform, then copy the 4 RGBA bytes. Walking the source linearly
     * keeps cache behaviour predictable on the hot read side. */
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
    log_debug("Applied EXIF orientation %d (%ux%u -> %ux%u)",
              orientation, w, h, nw, nh);
}

/* Load JPEG image */
struct image_data *image_load_jpeg(const char *path) {
    if (!path) {
        log_error("Invalid path for JPEG loading");
        return NULL;
    }

    /* Expand path if needed */
    char expanded_path[MAX_PATH_LENGTH];
    if (!expand_path(path, expanded_path, sizeof(expanded_path))) {
        return NULL;
    }

    FILE *fp = fopen(expanded_path, "rb");
    if (!fp) {
        log_error("Failed to open JPEG file %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr_ext jerr;

    /* Set up error handling */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        log_error("JPEG error: %s (file: %s)", jerr.error_msg, expanded_path);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    /* Create decompression struct */
    jpeg_create_decompress(&cinfo);

    /* Ask libjpeg to keep APP1 markers around so we can read EXIF orientation
     * after the header is parsed. Must be called BEFORE jpeg_read_header. */
    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);

    /* Specify data source */
    jpeg_stdio_src(&cinfo, fp);

    /* Read JPEG header */
    jpeg_read_header(&cinfo, TRUE);

    /* Walk saved markers for an APP1 EXIF payload (issue #48). */
    int exif_orientation = EXIF_ORIENT_TOP_LEFT;
    for (jpeg_saved_marker_ptr m = cinfo.marker_list; m != NULL; m = m->next) {
        if (m->marker == JPEG_APP0 + 1 && m->data_length >= 6 &&
            memcmp(m->data, "Exif\0\0", 6) == 0) {
            exif_orientation = exif_parse_orientation(m->data, m->data_length);
            break;
        }
    }

    /* Force RGB output */
    cinfo.out_color_space = JCS_RGB;

    /* Start decompression */
    jpeg_start_decompress(&cinfo);

    uint32_t width = cinfo.output_width;
    uint32_t height = cinfo.output_height;
    uint32_t channels = cinfo.output_components;

    log_debug("Loading JPEG: %s (%ux%u, %u channels)",
              expanded_path, width, height, channels);

    if (channels != 3) {
        log_error("Unexpected number of channels in JPEG: %u", channels);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    /* Allocate image data */
    struct image_data *img = calloc(1, sizeof(struct image_data));
    if (!img) {
        log_error("Failed to allocate image data: %s", strerror(errno));
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    img->width = width;
    img->height = height;
    img->channels = 4; /* We'll convert RGB to RGBA */
    img->format = FORMAT_JPEG;
    snprintf(img->path, sizeof(img->path), "%s", path);

    /* Allocate pixel buffer (RGBA) - check for overflow */
    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / 4) {
        log_error("Image too large (potential overflow): %dx%d", width, height);
        free(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    img->pixels = malloc(pixel_count * 4);
    if (!img->pixels) {
        log_error("Failed to allocate pixel buffer: %s", strerror(errno));
        free(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    /* Allocate temporary RGB row buffer */
    /* JPEG width is int type, constrained by format (max 65535 typically) */
    size_t row_stride = width * 3;
    unsigned char *row_buffer = malloc(row_stride);
    if (!row_buffer) {
        log_error("Failed to allocate row buffer: %s", strerror(errno));
        free(img->pixels);
        free(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    /* Read scanlines and convert RGB to RGBA */
    uint32_t row = 0;
    while (cinfo.output_scanline < height) {
        unsigned char *row_ptr = row_buffer;
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);

        /* Convert RGB to RGBA */
        for (uint32_t x = 0; x < width; x++) {
            uint32_t src_idx = x * 3;
            uint32_t dst_idx = (row * width + x) * 4;

            img->pixels[dst_idx + 0] = row_buffer[src_idx + 0]; /* R */
            img->pixels[dst_idx + 1] = row_buffer[src_idx + 1]; /* G */
            img->pixels[dst_idx + 2] = row_buffer[src_idx + 2]; /* B */
            img->pixels[dst_idx + 3] = ALPHA_OPAQUE;                      /* A */
        }

        row++;
    }

    /* Clean up */
    free(row_buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    /* Honor EXIF Orientation tag BEFORE display-aware scaling so the scaler
     * sees the final visual dimensions (e.g. a portrait phone photo with
     * orientation=6 reports 4032x3024 in the file but is actually 3024x4032
     * once rotated). */
    if (exif_orientation != EXIF_ORIENT_TOP_LEFT) {
        image_apply_exif_orientation(img, exif_orientation);
    }

    log_debug("Loaded JPEG image: %s (%ux%u)", expanded_path, img->width, img->height);

    return img;
}

/* Load image from file (auto-detect format) with display-aware scaling */
struct image_data *image_load(const char *path, int32_t display_width, 
                              int32_t display_height, int mode) {
    if (!path) {
        log_error("Invalid path for image_load");
        return NULL;
    }

    enum image_format format = image_detect_format(path);
    struct image_data *img = NULL;
    
    switch (format) {
        case FORMAT_PNG:
            img = image_load_png(path);
            break;
        case FORMAT_JPEG:
            img = image_load_jpeg(path);
            break;
        default:
            log_error("Unsupported or unknown image format: %s", path);
            return NULL;
    }

    if (!img) {
        return NULL;
    }

    /* Scale image intelligently based on display dimensions and mode */
    if (display_width > 0 && display_height > 0) {
        img = image_scale_to_display(img, display_width, display_height, mode);
    }

    return img;
}

/* Free only pixel data, keeping metadata (for memory optimization after GPU upload) */
void image_free_pixels(struct image_data *img) {
    if (!img) {
        return;
    }

    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}

/* Free image data */
void image_free(struct image_data *img) {
    if (!img) {
        return;
    }

    image_free_pixels(img);
    free(img);
}

/* Calculate optimal dimensions based on display mode */
static void calculate_optimal_dimensions(uint32_t img_width, uint32_t img_height,
                                         int32_t display_width, int32_t display_height,
                                         enum wallpaper_mode mode,
                                         uint32_t *out_width, uint32_t *out_height) {
    float img_aspect = (float)img_width / (float)img_height;
    float display_aspect = (float)display_width / (float)display_height;
    
    switch (mode) {
        case MODE_FILL:
            /* Scale to fill display, maintaining aspect ratio (will crop) */
            if (img_aspect > display_aspect) {
                /* Image is wider - match height */
                *out_height = display_height;
                *out_width = (uint32_t)(display_height * img_aspect);
            } else {
                /* Image is taller - match width */
                *out_width = display_width;
                *out_height = (uint32_t)(display_width / img_aspect);
            }
            break;
            
        case MODE_FIT:
            /* Scale to fit inside display, maintaining aspect ratio */
            if (img_aspect > display_aspect) {
                /* Image is wider - match width */
                *out_width = display_width;
                *out_height = (uint32_t)(display_width / img_aspect);
            } else {
                /* Image is taller - match height */
                *out_height = display_height;
                *out_width = (uint32_t)(display_height * img_aspect);
            }
            break;
            
        case MODE_STRETCH:
            /* Stretch to exact display dimensions */
            *out_width = display_width;
            *out_height = display_height;
            break;
            
        case MODE_CENTER:
            /* Center mode: show at actual size (1:1 pixels), crop if too large */
            /* No scaling - keep original dimensions, will crop in rendering if needed */
            *out_width = img_width;
            *out_height = img_height;
            break;
            
        case MODE_TILE:
            /* For tile, only scale down if larger than display */
            if (img_width > (uint32_t)display_width || img_height > (uint32_t)display_height) {
                /* Scale to fit */
                if (img_aspect > display_aspect) {
                    *out_width = display_width;
                    *out_height = (uint32_t)(display_width / img_aspect);
                } else {
                    *out_height = display_height;
                    *out_width = (uint32_t)(display_height * img_aspect);
                }
            } else {
                /* Keep original size */
                *out_width = img_width;
                *out_height = img_height;
            }
            break;
            
        default:
            *out_width = img_width;
            *out_height = img_height;
            break;
    }
}

/* Pad image to exact dimensions with transparent borders (center positioning) */
static struct image_data *image_center_pad(struct image_data *img, uint32_t pad_width, uint32_t pad_height) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* No padding needed if already exact size or larger */
    if (img->width >= pad_width && img->height >= pad_height) {
        return img;
    }
    
    log_debug("Center-padding image from %ux%u to %ux%u",
             img->width, img->height, pad_width, pad_height);
    
    /* Allocate new pixel buffer filled with opaque black (R=0, G=0, B=0, A=255) */
    size_t new_size = (size_t)pad_width * pad_height * 4; /* RGBA */
    uint8_t *new_pixels = malloc(new_size);
    if (!new_pixels) {
        log_error("Failed to allocate memory for padded image");
        return img;
    }
    
    /* Fill with opaque black (important: alpha = 255 for opaque) */
    for (size_t i = 0; i < new_size; i += 4) {
        new_pixels[i] = 0;       /* R */
        new_pixels[i + 1] = 0;   /* G */
        new_pixels[i + 2] = 0;   /* B */
        new_pixels[i + 3] = 255; /* A - opaque */
    }
    
    /* Calculate position to center the image */
    uint32_t offset_x = (pad_width > img->width) ? (pad_width - img->width) / 2 : 0;
    uint32_t offset_y = (pad_height > img->height) ? (pad_height - img->height) / 2 : 0;
    
    /* Copy original image to center of new buffer */
    for (uint32_t y = 0; y < img->height; y++) {
        uint32_t dst_y = offset_y + y;
        uint32_t src_offset = y * img->width * 4;
        uint32_t dst_offset = (dst_y * pad_width + offset_x) * 4;
        memcpy(new_pixels + dst_offset, img->pixels + src_offset, img->width * 4);
    }
    
    /* Replace old pixels with padded ones */
    free(img->pixels);
    img->pixels = new_pixels;
    img->width = pad_width;
    img->height = pad_height;
    
    return img;
}

/* Tile image to fill exact dimensions by repeating the source image */
static struct image_data *image_tile_to_size(struct image_data *img, uint32_t target_width, uint32_t target_height) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* No tiling needed if already exact size or larger */
    if (img->width >= target_width && img->height >= target_height) {
        return img;
    }
    
    log_debug("Tiling image from %ux%u to fill %ux%u",
             img->width, img->height, target_width, target_height);
    
    /* Allocate new pixel buffer for tiled result */
    size_t new_size = (size_t)target_width * target_height * 4; /* RGBA */
    uint8_t *new_pixels = malloc(new_size);
    if (!new_pixels) {
        log_error("Failed to allocate memory for tiled image");
        return img;
    }
    
    /* Tile the image by copying it repeatedly */
    for (uint32_t dst_y = 0; dst_y < target_height; dst_y++) {
        uint32_t src_y = dst_y % img->height;  /* Wrap vertically */
        
        for (uint32_t dst_x = 0; dst_x < target_width; dst_x++) {
            uint32_t src_x = dst_x % img->width;  /* Wrap horizontally */
            
            /* Copy pixel from source to destination */
            uint32_t src_offset = (src_y * img->width + src_x) * 4;
            uint32_t dst_offset = (dst_y * target_width + dst_x) * 4;
            
            new_pixels[dst_offset + 0] = img->pixels[src_offset + 0]; /* R */
            new_pixels[dst_offset + 1] = img->pixels[src_offset + 1]; /* G */
            new_pixels[dst_offset + 2] = img->pixels[src_offset + 2]; /* B */
            new_pixels[dst_offset + 3] = img->pixels[src_offset + 3]; /* A */
        }
    }
    
    /* Replace old pixels with tiled ones */
    free(img->pixels);
    img->pixels = new_pixels;
    img->width = target_width;
    img->height = target_height;
    
    log_debug("Image tiled to %ux%u", target_width, target_height);
    return img;
}

/* Center-crop image to exact dimensions */
static struct image_data *image_center_crop(struct image_data *img, uint32_t crop_width, uint32_t crop_height) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* No crop needed if already exact size */
    if (img->width == crop_width && img->height == crop_height) {
        return img;
    }
    
    /* Calculate crop offsets (center crop) */
    uint32_t offset_x = (img->width > crop_width) ? (img->width - crop_width) / 2 : 0;
    uint32_t offset_y = (img->height > crop_height) ? (img->height - crop_height) / 2 : 0;
    
    /* Ensure we don't crop to larger than source */
    uint32_t actual_crop_width = (crop_width < img->width) ? crop_width : img->width;
    uint32_t actual_crop_height = (crop_height < img->height) ? crop_height : img->height;
    
    log_debug("Center-cropping image from %ux%u to %ux%u (offset: %u,%u)",
             img->width, img->height, actual_crop_width, actual_crop_height, offset_x, offset_y);
    
    /* Allocate new pixel buffer */
    size_t new_size = (size_t)actual_crop_width * actual_crop_height * 4; /* RGBA */
    uint8_t *new_pixels = malloc(new_size);
    if (!new_pixels) {
        log_error("Failed to allocate memory for cropped image");
        return img;
    }
    
    /* Copy cropped region row by row */
    for (uint32_t y = 0; y < actual_crop_height; y++) {
        uint32_t src_y = offset_y + y;
        uint32_t src_offset = (src_y * img->width + offset_x) * 4;
        uint32_t dst_offset = y * actual_crop_width * 4;
        memcpy(new_pixels + dst_offset, img->pixels + src_offset, actual_crop_width * 4);
    }
    
    /* Replace old pixels with cropped ones */
    free(img->pixels);
    img->pixels = new_pixels;
    img->width = actual_crop_width;
    img->height = actual_crop_height;
    
    return img;
}

/* Scale image to optimal size for display mode */
static struct image_data *image_scale_to_display(struct image_data *img, int32_t display_width, 
                                                   int32_t display_height, int mode) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* Calculate optimal dimensions for this display mode. Initialised here so
     * the static analyzer can see both are always defined even if a future
     * mode is added to calculate_optimal_dimensions without setting them. */
    uint32_t target_width = img->width, target_height = img->height;
    calculate_optimal_dimensions(img->width, img->height, display_width, display_height,
                                 mode, &target_width, &target_height);
    
    /* Only scale if dimensions changed */
    if (target_width == img->width && target_height == img->height) {
        log_debug("Image %ux%u already optimal for display %dx%d (mode=%d)",
                 img->width, img->height, display_width, display_height, mode);
        return img;
    }
    
    /* Only downscale for modes other than FILL/STRETCH (which need to fill display) */
    if (mode != MODE_FILL && mode != MODE_STRETCH) {
        if (target_width > img->width || target_height > img->height) {
            log_debug("Keeping original size %ux%u (would upscale to %ux%u)",
                     img->width, img->height, target_width, target_height);
            return img;
        }
    }
    
    log_debug("Scaling image from %ux%u to %ux%u for %dx%d display (mode=%d)",
             img->width, img->height, target_width, target_height, 
             display_width, display_height, mode);
    
    img = image_scale_bilinear(img, target_width, target_height);
    
    if (!img || !img->pixels) {
        return img;
    }
    
    /* Adjust image to exact display size for seamless transitions
     * All modes except TILE need to be exact display size for consistent rendering */
    switch (mode) {
        case MODE_FILL:
            /* Already scaled to fill, now crop excess to exact display size */
            img = image_center_crop(img, display_width, display_height);
            break;
            
        case MODE_FIT:
            /* Scaled to fit inside, now pad to exact display size with black borders */
            if (img->width < (uint32_t)display_width || img->height < (uint32_t)display_height) {
                img = image_center_pad(img, display_width, display_height);
            }
            break;
            
        case MODE_CENTER:
            /* No scaling (1:1 pixels), crop if larger or pad if smaller to exact display size */
            if (img->width > (uint32_t)display_width || img->height > (uint32_t)display_height) {
                img = image_center_crop(img, display_width, display_height);
            } else if (img->width < (uint32_t)display_width || img->height < (uint32_t)display_height) {
                img = image_center_pad(img, display_width, display_height);
            }
            /* else: already exact size, perfect! */
            break;
            
        case MODE_STRETCH:
            /* Already scaled to exact display size, no adjustment needed */
            break;
            
        case MODE_TILE:
            /* Physically tile the image to exact display size for seamless transitions
             * This makes transitions work perfectly while maintaining tile appearance */
            img = image_tile_to_size(img, display_width, display_height);
            break;
    }
    
    return img;
}

/* High-quality bilinear image scaling */
static struct image_data *image_scale_bilinear(struct image_data *img, uint32_t new_width, uint32_t new_height) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* Allocate new pixel buffer */
    size_t new_size = (size_t)new_width * new_height * 4;
    uint8_t *new_pixels = malloc(new_size);
    if (!new_pixels) {
        log_error("Failed to allocate scaled image buffer");
        return img;
    }
    
    float x_ratio = (float)(img->width - 1) / (float)new_width;
    float y_ratio = (float)(img->height - 1) / (float)new_height;
    
    /* Bilinear interpolation */
    for (uint32_t y = 0; y < new_height; y++) {
        for (uint32_t x = 0; x < new_width; x++) {
            float src_x = x * x_ratio;
            float src_y = y * y_ratio;
            
            uint32_t x1 = (uint32_t)src_x;
            uint32_t y1 = (uint32_t)src_y;
            uint32_t x2 = (x1 < img->width - 1) ? x1 + 1 : x1;
            uint32_t y2 = (y1 < img->height - 1) ? y1 + 1 : y1;
            
            float x_diff = src_x - x1;
            float y_diff = src_y - y1;
            
            /* Get the four surrounding pixels */
            size_t idx_tl = (y1 * img->width + x1) * 4;  /* Top-left */
            size_t idx_tr = (y1 * img->width + x2) * 4;  /* Top-right */
            size_t idx_bl = (y2 * img->width + x1) * 4;  /* Bottom-left */
            size_t idx_br = (y2 * img->width + x2) * 4;  /* Bottom-right */
            
            size_t dst_idx = (y * new_width + x) * 4;
            
            /* Interpolate each channel (RGBA) */
            for (int c = 0; c < 4; c++) {
                float tl = img->pixels[idx_tl + c];
                float tr = img->pixels[idx_tr + c];
                float bl = img->pixels[idx_bl + c];
                float br = img->pixels[idx_br + c];
                
                /* Bilinear interpolation formula */
                float top = tl * (1.0f - x_diff) + tr * x_diff;
                float bottom = bl * (1.0f - x_diff) + br * x_diff;
                float value = top * (1.0f - y_diff) + bottom * y_diff;
                
                new_pixels[dst_idx + c] = (uint8_t)(value + 0.5f);  /* Round */
            }
        }
    }
    
    /* Free old pixels and update image */
    free(img->pixels);
    img->pixels = new_pixels;
    img->width = new_width;
    img->height = new_height;
    
    return img;
}