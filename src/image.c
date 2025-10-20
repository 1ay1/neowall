/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 *
 * Image loading for various formats
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#include "staticwall.h"

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

        strcpy(expanded, home);
        strcat(expanded, path + 1);
        return true;
    }

    /* No expansion needed */
    if (strlen(path) >= size) {
        log_error("Path too long");
        return false;
    }

    strcpy(expanded, path);
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
    strncpy(img->path, path, sizeof(img->path) - 1);

    /* Allocate pixel buffer */
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    img->pixels = malloc(row_bytes * height);
    if (!img->pixels) {
        log_error("Failed to allocate pixel buffer: %s", strerror(errno));
        free(img);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    /* Allocate row pointers */
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

    log_info("Loaded PNG image: %s (%ux%u)", expanded_path, width, height);

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

    /* Specify data source */
    jpeg_stdio_src(&cinfo, fp);

    /* Read JPEG header */
    jpeg_read_header(&cinfo, TRUE);

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
    strncpy(img->path, path, sizeof(img->path) - 1);

    /* Allocate pixel buffer (RGBA) */
    img->pixels = malloc(width * height * 4);
    if (!img->pixels) {
        log_error("Failed to allocate pixel buffer: %s", strerror(errno));
        free(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    /* Allocate temporary RGB row buffer */
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
            img->pixels[dst_idx + 3] = 255;                      /* A */
        }

        row++;
    }

    /* Clean up */
    free(row_buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    log_info("Loaded JPEG image: %s (%ux%u)", expanded_path, width, height);

    return img;
}

/* Load image (auto-detect format) */
struct image_data *image_load(const char *path) {
    if (!path) {
        log_error("Invalid path for image loading");
        return NULL;
    }

    enum image_format format = image_detect_format(path);

    switch (format) {
        case FORMAT_PNG:
            return image_load_png(path);

        case FORMAT_JPEG:
            return image_load_jpeg(path);

        case FORMAT_UNKNOWN:
        default:
            log_error("Unknown or unsupported image format: %s", path);
            return NULL;
    }
}

/* Free image data */
void image_free(struct image_data *img) {
    if (!img) {
        return;
    }

    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }

    free(img);
}