#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdint.h>
#include <png.h>
#include <jpeglib.h>
#include "staticwall.h"

/* Forward declarations */
static struct image_data *image_scale_to_display(struct image_data *img, int32_t display_width, 
                                                   int32_t display_height, enum wallpaper_mode mode);
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
    strncpy(img->path, path, sizeof(img->path) - 1);

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

/* Load image from file (auto-detect format) with display-aware scaling */
struct image_data *image_load(const char *path, int32_t display_width, 
                              int32_t display_height, enum wallpaper_mode mode) {
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

/* Scale image to optimal size for display mode */
static struct image_data *image_scale_to_display(struct image_data *img, int32_t display_width, 
                                                   int32_t display_height, enum wallpaper_mode mode) {
    if (!img || !img->pixels) {
        return img;
    }
    
    /* Calculate optimal dimensions for this display mode */
    uint32_t target_width, target_height;
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
    
    log_info("Scaling image from %ux%u to %ux%u for %dx%d display (mode=%d)",
             img->width, img->height, target_width, target_height, 
             display_width, display_height, mode);
    
    return image_scale_bilinear(img, target_width, target_height);
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