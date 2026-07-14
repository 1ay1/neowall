/*
 * test_glyph_synth.c — headless coverage test for the procedural box/block/
 * braille rasterizer. No font, no GPU: feeds codepoints and asserts the 8-bit
 * coverage bitmap has ink where the glyph should draw and none where it
 * shouldn't. Verifies the ranges htop/btop/rockbottom actually draw.
 */
#include "glyph_synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

#define CHECKF(cond, fmt, ...) do { \
    if (!(cond)) { printf("FAIL: " fmt "\n", __VA_ARGS__); failures++; } \
} while (0)

/* Average coverage (0..255) over the whole cell. */
static int ink_avg(const uint8_t *o, int w, int h) {
    long s = 0;
    for (int i = 0; i < w * h; i++) s += o[i];
    return (int)(s / (w * h));
}

/* Coverage of a sub-rectangle, as a percentage. */
static int ink_pct(const uint8_t *o, int W, int x0, int y0, int x1, int y1) {
    long s = 0; int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) { s += (o[y * W + x] > 127); n++; }
    return n ? (int)(100 * s / n) : 0;
}

int main(void) {
    const int W = 27, H = 54;   /* 9x18 cell * SS3, as term_render uses */
    uint8_t *o = malloc((size_t)W * H);

    /* --- range membership --- */
    CHECK(glyph_synth_has(0x2500), "U+2500 (box) should be synthesized");
    CHECK(glyph_synth_has(0x2588), "U+2588 (block) should be synthesized");
    CHECK(glyph_synth_has(0x28FF), "U+28FF (braille) should be synthesized");
    CHECK(!glyph_synth_has('A'), "ASCII 'A' must NOT be synthesized");
    CHECK(!glyph_synth_has(0x4E2D), "CJK must NOT be synthesized");

    /* --- full block U+2588: entire cell inked --- */
    memset(o, 0, (size_t)W * H);
    CHECK(glyph_synth_render(0x2588, o, W, H), "render U+2588");
    CHECKF(ink_avg(o, W, H) > 250, "full block should be ~100%% ink, got %d",
          ink_avg(o, W, H));

    /* --- lower half block U+2584: bottom half inked, top empty --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x2584, o, W, H);
    CHECK(ink_pct(o, W, 0, 0, W, H/2) < 5, "U+2584 top half must be empty");
    CHECK(ink_pct(o, W, 0, H/2, W, H) > 90, "U+2584 bottom half must be inked");

    /* --- left half block U+258C: left inked, right empty --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x258C, o, W, H);
    CHECK(ink_pct(o, W, 0, 0, W/2, H) > 90, "U+258C left half must be inked");
    CHECK(ink_pct(o, W, W/2, 0, W, H) < 5, "U+258C right half must be empty");

    /* --- light horizontal line U+2500: a stroke on the mid row, nothing at top --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x2500, o, W, H);
    CHECK(ink_pct(o, W, 0, H/2 - 1, W, H/2 + 2) > 50,
          "U+2500 should ink the horizontal mid-line");
    CHECK(ink_pct(o, W, 0, 0, W, 3) < 5, "U+2500 top rows must be empty");

    /* --- braille full U+28FF: all 8 dots -> substantial ink across the cell --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x28FF, o, W, H);
    CHECKF(ink_avg(o, W, H) > 60, "U+28FF (8 dots) should have rich ink, got %d",
          ink_avg(o, W, H));

    /* --- braille single dot U+2801: sparse ink (one of 8 cells) --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x2801, o, W, H);
    int one = ink_avg(o, W, H);
    CHECKF(one > 0 && one < 60, "U+2801 (1 dot) should be sparse, got %d", one);

    /* --- braille bottom-fill U+28C0 (dots 7,8): ink only in the bottom row --- */
    memset(o, 0, (size_t)W * H);
    glyph_synth_render(0x28C0, o, W, H);
    CHECK(ink_pct(o, W, 0, 0, W, H/2) < 5, "U+28C0 top half must be empty");
    CHECK(ink_pct(o, W, 0, (3*H)/4, W, H) > 20, "U+28C0 bottom row must have dots");

    /* --- monotonic ink: more braille dots => more ink --- */
    memset(o, 0, (size_t)W * H); glyph_synth_render(0x2803, o, W, H); /* 2 dots */
    int two = ink_avg(o, W, H);
    CHECKF(two > one, "2 braille dots (%d) should out-ink 1 dot (%d)", two, one);

    free(o);
    if (failures == 0) { printf("glyph_synth: ALL PASS\n"); return 0; }
    printf("glyph_synth: %d FAILURE(S)\n", failures);
    return 1;
}
