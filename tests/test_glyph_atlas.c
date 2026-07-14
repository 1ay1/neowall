/*
 * test_glyph_atlas.c — the terminal's font-rasterized glyph atlas.
 *
 * Loads a real system monospace font and rasterizes glyphs on demand. Asserts
 * that letters and — crucially — box-drawing / block-element glyphs (which
 * htop/btop/tmux draw their entire UI with) produce actual ink in the atlas.
 * If no system font is found the test SKIPs (exit 77), so CI on a font-less
 * container doesn't spuriously fail. No GPU needed: this is the CPU-side
 * rasterizer only.
 */
#include "glyph_atlas.h"

#include <stdio.h>

static int g_fails = 0;

static long ink_of(glyph_atlas *a, uint32_t cp) {
    const glyph_slot *s = glyph_atlas_get(a, cp);
    if (!s->valid) return -1;
    const uint8_t *bm = glyph_atlas_bitmap(a);
    int W = glyph_atlas_width(a);
    long sum = 0;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            sum += bm[(size_t)(s->y + y) * W + (s->x + x)];
    return sum;
}

static void expect_ink(glyph_atlas *a, uint32_t cp, const char *name) {
    long ink = ink_of(a, cp);
    if (ink <= 0) {
        printf("  FAIL: %s (U+%04X) produced no ink (%ld)\n", name, cp, ink);
        g_fails++;
    }
}

int main(void) {
    glyph_atlas *a = glyph_atlas_create(NULL, 0, 10, 20);
    if (!a) {
        printf("glyph_atlas: no system monospace font found — SKIP\n");
        return 77;   /* meson treats 77 as skip */
    }

    /* letters */
    expect_ink(a, 'A', "letter A");
    expect_ink(a, 'g', "letter g");
    expect_ink(a, '0', "digit 0");

    /* the glyphs that make a TUI legible */
    expect_ink(a, 0x2588, "FULL BLOCK");
    expect_ink(a, 0x2502, "BOX VERTICAL");
    expect_ink(a, 0x2500, "BOX HORIZONTAL");
    expect_ink(a, 0x250C, "BOX DOWN+RIGHT corner");
    expect_ink(a, 0x2593, "DARK SHADE");
    expect_ink(a, 0x2591, "LIGHT SHADE");
    expect_ink(a, 0x25BD, "DOWN TRIANGLE (htop sort indicator)");

    /* space is valid but has no ink — must not be reported as a failure and
     * must be a valid slot (so the renderer advances the cursor). */
    const glyph_slot *sp = glyph_atlas_get(a, ' ');
    if (!sp->valid) { printf("  FAIL: space slot invalid\n"); g_fails++; }

    /* caching: second lookup returns the same placement */
    const glyph_slot *a1 = glyph_atlas_get(a, 'A');
    const glyph_slot *a2 = glyph_atlas_get(a, 'A');
    if (a1->x != a2->x || a1->y != a2->y) { printf("  FAIL: 'A' not cached stably\n"); g_fails++; }

    glyph_atlas_destroy(a);
    printf("glyph_atlas: %s\n", g_fails ? "FAILURES" : "all checks passed");
    return g_fails ? 1 : 0;
}
