#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* Image format types */
enum image_format {
    FORMAT_PNG,
    FORMAT_JPEG,
    FORMAT_UNKNOWN,
};

/* Image data structure */
struct image_data {
    uint8_t *pixels;        /* RGBA pixel data */
    uint32_t width;
    uint32_t height;
    uint32_t channels;      /* Number of channels (3 for RGB, 4 for RGBA) */
    enum image_format format;
    char path[4096];        /* OUTPUT_MAX_PATH_LENGTH */
};

/* Image loading functions */
struct image_data *image_load(const char *path, int32_t display_width, int32_t display_height, int mode);
void image_free(struct image_data *img);
void image_free_pixels(struct image_data *img);  /* Free pixel data only (after GPU upload) */
enum image_format image_detect_format(const char *path);

/* Image loaders for specific formats */
struct image_data *image_load_png(const char *path);
struct image_data *image_load_jpeg(const char *path);

#endif /* IMAGE_H */
