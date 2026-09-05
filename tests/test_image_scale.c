/* Regression tests for image scaling / fitting (src/image/image.c).
 *
 * Covers two bugs found by auditing the image path:
 *
 *  1. image_center_pad() heap-buffer-overflow. Its old guard was
 *     `width >= pad_width && height >= pad_height`, which only returned early
 *     when BOTH axes were oversized. A wider-but-shorter source (1200x298
 *     padded to 400x300) fell through to a copy loop that wrote img->width*4
 *     bytes per row at a pad_width*4 stride, running off the heap block.
 *
 *  2. Modes not reaching exact display size. The renderer draws every image
 *     mode as a plain fullscreen quad and relies on the decoded pixels already
 *     being exactly display-sized (calculate_vertex_coords_for_image() in
 *     render/render.c). Two early `return img` paths in image_scale_to_display()
 *     skipped the crop/pad/tile step. MODE_CENTER was worst: it returned the
 *     source dims from calculate_optimal_dimensions(), so the "already optimal"
 *     check fired every time and its crop/pad arm was dead code — `mode center`
 *     never actually showed 1:1 pixels, it got stretched by the quad.
 *
 * The interesting functions are static, so we include the .c directly.
 * Run under ASan to make test 1 meaningful.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "../src/image/image.c"

static int failures = 0;
static int checks = 0;

static void check(bool cond, const char *what) {
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static struct image_data *mk(uint32_t w, uint32_t h) {
    struct image_data *img = calloc(1, sizeof(*img));
    assert(img);
    img->width = w;
    img->height = h;
    img->format = FORMAT_PNG;
    img->channels = 4;
    img->pixels = malloc((size_t)w * h * 4);
    assert(img->pixels);
    memset(img->pixels, 0x7F, (size_t)w * h * 4);
    return img;
}

static void img_free(struct image_data *img) {
    if (!img) return;
    free(img->pixels);
    free(img);
}

/* ---- 1. center-pad must never write out of bounds ---------------------- */

static void test_center_pad_no_overflow(void) {
    printf("test_center_pad_no_overflow\n");

    /* Mixed axes in every combination around the pad target. Under ASan the
     * old code aborts on the wider-than-target rows. */
    static const uint32_t src[][2] = {
        {1200, 298},  /* wider, shorter  <- the original crash */
        {800,  300},  /* wider, equal    */
        {800,  400},  /* wider, taller   */
        {300,  400},  /* narrower, taller*/
        {400,  400},  /* equal,  taller  */
        {100,  100},  /* smaller both    */
        {1,    1},    /* degenerate      */
        {401,  301},  /* just over       */
        {399,  299},  /* just under      */
    };
    const uint32_t pw = 400, ph = 300;

    for (size_t i = 0; i < sizeof(src) / sizeof(src[0]); i++) {
        struct image_data *img = mk(src[i][0], src[i][1]);
        img = image_center_pad(img, pw, ph);
        char msg[128];
        snprintf(msg, sizeof(msg), "%ux%u padded to exactly %ux%u",
                 src[i][0], src[i][1], pw, ph);
        check(img && img->width == pw && img->height == ph, msg);
        img_free(img);
    }

    /* Padding is a no-op when already exact. */
    struct image_data *exact = mk(pw, ph);
    uint8_t *before = exact->pixels;
    exact = image_center_pad(exact, pw, ph);
    check(exact->pixels == before, "exact-size pad is a no-op");
    img_free(exact);
}

/* ---- 2. every mode must land on exact display size --------------------- */

static void test_modes_reach_exact_size(void) {
    printf("test_modes_reach_exact_size\n");

    static const uint32_t dims[] = { 50, 300, 400, 800, 1200 };
    static const int32_t displays[][2] = { {400, 300}, {300, 400} };
    static const int modes[] = { MODE_CENTER, MODE_STRETCH, MODE_FIT, MODE_FILL, MODE_TILE };
    static const char *names[] = { "center", "stretch", "fit", "fill", "tile" };

    size_t nd = sizeof(dims) / sizeof(dims[0]);

    for (size_t di = 0; di < sizeof(displays) / sizeof(displays[0]); di++) {
        int32_t dw = displays[di][0], dh = displays[di][1];
        for (size_t mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
            int bad = 0;
            for (size_t a = 0; a < nd; a++) {
                for (size_t b = 0; b < nd; b++) {
                    struct image_data *img = mk(dims[a], dims[b]);
                    img = image_scale_to_display(img, dw, dh, modes[mi]);
                    if (!img || (int32_t)img->width != dw || (int32_t)img->height != dh) {
                        if (bad == 0) {
                            printf("    %s: %ux%u -> %ux%u (want %dx%d)\n",
                                   names[mi], dims[a], dims[b],
                                   img ? img->width : 0, img ? img->height : 0, dw, dh);
                        }
                        bad++;
                    }
                    img_free(img);
                }
            }
            char msg[128];
            snprintf(msg, sizeof(msg), "mode %s always reaches %dx%d (%d bad)",
                     names[mi], dw, dh, bad);
            check(bad == 0, msg);
        }
    }
}

/* ---- 3. center mode keeps 1:1 pixels ----------------------------------- */

static void test_center_preserves_pixels(void) {
    printf("test_center_preserves_pixels\n");

    /* A 100x100 image on a 400x300 display must sit unscaled in the middle,
     * surrounded by black. If it were stretched (the old bug) the corners
     * would be image-coloured instead of black. */
    const int32_t dw = 400, dh = 300;
    struct image_data *img = mk(100, 100);
    memset(img->pixels, 0x7F, (size_t)100 * 100 * 4);
    img = image_scale_to_display(img, dw, dh, MODE_CENTER);

    check(img && (int32_t)img->width == dw && (int32_t)img->height == dh,
          "center: padded to display size");

    if (img && img->pixels) {
        const uint8_t *p = img->pixels;
        /* Top-left corner must be opaque black padding. */
        check(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255,
              "center: corner is opaque black padding");

        /* Middle pixel must still be the original 0x7F sample. */
        size_t mid = ((size_t)(dh / 2) * dw + (dw / 2)) * 4;
        check(p[mid] == 0x7F, "center: middle pixel preserved 1:1");
    }
    img_free(img);
}

int main(void) {
    log_set_level(0); /* silence expected error/debug chatter */

    test_center_pad_no_overflow();
    test_modes_reach_exact_size();
    test_center_preserves_pixels();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
