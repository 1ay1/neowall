/* libFuzzer harness for image loading.
 *
 * Any PNG or JPEG in a wallpaper folder reaches this, and a downloaded
 * wallpaper pack is untrusted. The bytes go to a temp file and through the
 * real image_load path: format sniffing, libpng/libjpeg with neowall's error
 * recovery, the size/stride checks, EXIF orientation, and the scale/crop to
 * the display size and mode.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "neowall/image/image.h"

static char g_png[64], g_jpg[64];

static void remove_scratch(void) {
    unlink(g_png);
    unlink(g_jpg);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0;
    if (!g_png[0]) {
        snprintf(g_png, sizeof(g_png), "/tmp/nw-fuzz-image-%d.png", (int)getpid());
        snprintf(g_jpg, sizeof(g_jpg), "/tmp/nw-fuzz-image-%d.jpg", (int)getpid());
        atexit(remove_scratch);
    }
    /* the format is picked by extension, so name the file by its magic bytes:
     * that way inputs that look like JPEG reach libjpeg and the rest libpng.
     * The last three bytes choose display size and mode (seeds keep their
     * real headers at the front). */
    const char *path = (data[0] == 0xFF && data[1] == 0xD8) ? g_jpg : g_png;
    int mode = data[size - 1] % 5;
    int w = 1 + (data[size - 2] % 64), h = 1 + (data[size - 3] % 64);

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, size, f);
    fclose(f);

    struct image_data *img = image_load(path, w, h, mode);
    unlink(path);
    if (img) {
        /* a decoded image must be self-consistent */
        if (img->width == 0 || img->height == 0 || !img->pixels) __builtin_trap();
        volatile uint8_t sink = img->pixels[(size_t)img->width * img->height * 4 - 1];
        (void)sink;
        image_free(img);
    }
    return 0;
}
