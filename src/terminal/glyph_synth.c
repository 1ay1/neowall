/*
 * glyph_synth.c — procedural box-drawing / block / braille rasterizer.
 *
 * All output is an 8-bit coverage bitmap the size of one cell (cell_w*cell_h,
 * row-major). Coordinates: x grows right, y grows down, (0,0) = cell top-left.
 * A filled pixel is 0xFF, empty 0x00 (a couple of shade glyphs use midtones).
 */
#include "glyph_synth.h"

#include <string.h>
#include <math.h>

/* ---- tiny raster helpers (all clamp to the cell) ---------------------- */

static inline void px(uint8_t *o, int W, int H, int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    o[(size_t)y * W + x] = v;
}

/* Fill the rectangle [x0,x1) x [y0,y1) with v. Bounds-clamped. */
static void fill_rect(uint8_t *o, int W, int H, int x0, int y0, int x1, int y1, uint8_t v) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            o[(size_t)y * W + x] = v;
}

static void clear(uint8_t *o, int W, int H) { memset(o, 0, (size_t)W * H); }

/* Line thickness for box-drawing: ~1/8 of the cell, min 1px, and thicker for
 * the "heavy" variants. Centered on the cell's mid axis. */
static int light_thick(int cell) { int t = cell / 8; return t < 1 ? 1 : t; }
static int heavy_thick(int cell) { int t = cell / 4; return t < 2 ? 2 : t; }

/* Horizontal / vertical strokes centered on the cell mid-line. */
static void hstroke(uint8_t *o, int W, int H, int x0, int x1, int thick) {
    int cy = H / 2;
    int y0 = cy - thick / 2;
    fill_rect(o, W, H, x0, y0, x1, y0 + thick, 0xFF);
}
static void vstroke(uint8_t *o, int W, int H, int y0, int y1, int thick) {
    int cx = W / 2;
    int x0 = cx - thick / 2;
    fill_rect(o, W, H, x0, y0, x0 + thick, y1, 0xFF);
}

/* ---- range test ------------------------------------------------------- */

bool glyph_synth_has(uint32_t cp) {
    return (cp >= 0x2500 && cp <= 0x259F) ||   /* box-drawing + block elements */
           (cp >= 0x2800 && cp <= 0x28FF);     /* braille patterns */
}

/* ---- block elements U+2580..259F -------------------------------------- */

/* Returns true if handled. Blocks fill fractions of the cell edge-to-edge. */
static bool render_block(uint32_t cp, uint8_t *o, int W, int H) {
    switch (cp) {
        case 0x2580: fill_rect(o, W, H, 0, 0, W, H / 2, 0xFF); return true;             /* upper half */
        case 0x2581: fill_rect(o, W, H, 0, H - H/8, W, H, 0xFF); return true;           /* lower 1/8 */
        case 0x2582: fill_rect(o, W, H, 0, H - H/4, W, H, 0xFF); return true;           /* lower 1/4 */
        case 0x2583: fill_rect(o, W, H, 0, H - (3*H)/8, W, H, 0xFF); return true;       /* lower 3/8 */
        case 0x2584: fill_rect(o, W, H, 0, H / 2, W, H, 0xFF); return true;             /* lower half */
        case 0x2585: fill_rect(o, W, H, 0, H - (5*H)/8, W, H, 0xFF); return true;       /* lower 5/8 */
        case 0x2586: fill_rect(o, W, H, 0, H - (3*H)/4, W, H, 0xFF); return true;       /* lower 3/4 */
        case 0x2587: fill_rect(o, W, H, 0, H - (7*H)/8, W, H, 0xFF); return true;       /* lower 7/8 */
        case 0x2588: fill_rect(o, W, H, 0, 0, W, H, 0xFF); return true;                 /* full block */
        case 0x2589: fill_rect(o, W, H, 0, 0, (7*W)/8, H, 0xFF); return true;           /* left 7/8 */
        case 0x258A: fill_rect(o, W, H, 0, 0, (3*W)/4, H, 0xFF); return true;           /* left 3/4 */
        case 0x258B: fill_rect(o, W, H, 0, 0, (5*W)/8, H, 0xFF); return true;           /* left 5/8 */
        case 0x258C: fill_rect(o, W, H, 0, 0, W / 2, H, 0xFF); return true;             /* left half */
        case 0x258D: fill_rect(o, W, H, 0, 0, (3*W)/8, H, 0xFF); return true;           /* left 3/8 */
        case 0x258E: fill_rect(o, W, H, 0, 0, W / 4, H, 0xFF); return true;             /* left 1/4 */
        case 0x258F: fill_rect(o, W, H, 0, 0, W / 8, H, 0xFF); return true;             /* left 1/8 */
        case 0x2590: fill_rect(o, W, H, W / 2, 0, W, H, 0xFF); return true;             /* right half */
        /* shades: uniform coverage levels */
        case 0x2591: fill_rect(o, W, H, 0, 0, W, H, 0x40); return true;                 /* light shade */
        case 0x2592: fill_rect(o, W, H, 0, 0, W, H, 0x80); return true;                 /* medium shade */
        case 0x2593: fill_rect(o, W, H, 0, 0, W, H, 0xC0); return true;                 /* dark shade */
        case 0x2594: fill_rect(o, W, H, 0, 0, W, H/8, 0xFF); return true;               /* upper 1/8 */
        case 0x2595: fill_rect(o, W, H, W - W/8, 0, W, H, 0xFF); return true;           /* right 1/8 */
        /* quadrants U+2596..259F */
        case 0x2596: fill_rect(o, W, H, 0, H/2, W/2, H, 0xFF); return true;             /* lower-left */
        case 0x2597: fill_rect(o, W, H, W/2, H/2, W, H, 0xFF); return true;             /* lower-right */
        case 0x2598: fill_rect(o, W, H, 0, 0, W/2, H/2, 0xFF); return true;             /* upper-left */
        case 0x2599: /* UL + LL + LR (all but UR) */
            fill_rect(o, W, H, 0, 0, W/2, H, 0xFF);
            fill_rect(o, W, H, W/2, H/2, W, H, 0xFF); return true;
        case 0x259A: /* UL + LR */
            fill_rect(o, W, H, 0, 0, W/2, H/2, 0xFF);
            fill_rect(o, W, H, W/2, H/2, W, H, 0xFF); return true;
        case 0x259B: /* UL + UR + LL (all but LR) */
            fill_rect(o, W, H, 0, 0, W, H/2, 0xFF);
            fill_rect(o, W, H, 0, H/2, W/2, H, 0xFF); return true;
        case 0x259C: /* UL + UR + LR (all but LL) */
            fill_rect(o, W, H, 0, 0, W, H/2, 0xFF);
            fill_rect(o, W, H, W/2, H/2, W, H, 0xFF); return true;
        case 0x259D: fill_rect(o, W, H, W/2, 0, W, H/2, 0xFF); return true;             /* upper-right */
        case 0x259E: /* UR + LL */
            fill_rect(o, W, H, W/2, 0, W, H/2, 0xFF);
            fill_rect(o, W, H, 0, H/2, W/2, H, 0xFF); return true;
        case 0x259F: /* UR + LL + LR (all but UL) */
            fill_rect(o, W, H, W/2, 0, W, H/2, 0xFF);
            fill_rect(o, W, H, 0, H/2, W, H, 0xFF); return true;
        default: return false;
    }
}

/* ---- box-drawing U+2500..257F ----------------------------------------- */

/* Most box glyphs are combinations of four half-strokes (up/down/left/right)
 * meeting at the cell centre, each independently light/heavy, plus dashes,
 * arcs, and diagonals. We handle the common TUI subset richly and approximate
 * the rest by their stroke composition. */
static bool render_box(uint32_t cp, uint8_t *o, int W, int H) {
    int cx = W / 2, cy = H / 2;
    int lt = light_thick(W < H ? W : H);
    int hv = heavy_thick(W < H ? W : H);

    switch (cp) {
        /* straight lines */
        case 0x2500: hstroke(o, W, H, 0, W, lt); return true;          /* ─ light horiz */
        case 0x2501: hstroke(o, W, H, 0, W, hv); return true;          /* ━ heavy horiz */
        case 0x2502: vstroke(o, W, H, 0, H, lt); return true;          /* │ light vert */
        case 0x2503: vstroke(o, W, H, 0, H, hv); return true;          /* ┃ heavy vert */

        /* dashed horizontals (2/3/4 dash) — draw as segments */
        case 0x2504: case 0x2505: case 0x2508: case 0x2509: {
            int t = (cp == 0x2505 || cp == 0x2509) ? hv : lt;
            int segs = (cp <= 0x2505) ? 3 : 4;
            int gap = W / (segs * 3 + 1);
            if (gap < 1) gap = 1;
            int y0 = cy - t/2;
            for (int i = 0; i < segs; i++) {
                int x0 = i * (W / segs) + gap;
                int x1 = (i + 1) * (W / segs) - gap;
                fill_rect(o, W, H, x0, y0, x1, y0 + t, 0xFF);
            }
            return true;
        }
        /* dashed verticals */
        case 0x2506: case 0x2507: case 0x250A: case 0x250B: {
            int t = (cp == 0x2507 || cp == 0x250B) ? hv : lt;
            int segs = (cp <= 0x2507) ? 3 : 4;
            int gap = H / (segs * 3 + 1);
            if (gap < 1) gap = 1;
            int x0 = cx - t/2;
            for (int i = 0; i < segs; i++) {
                int y0 = i * (H / segs) + gap;
                int y1 = (i + 1) * (H / segs) - gap;
                fill_rect(o, W, H, x0, y0, x0 + t, y1, 0xFF);
            }
            return true;
        }

        /* corners (light). half-strokes from centre to the two open edges. */
        case 0x250C: hstroke(o, W, H, cx, W, lt); vstroke(o, W, H, cy, H, lt); return true; /* ┌ */
        case 0x2510: hstroke(o, W, H, 0, cx+lt, lt); vstroke(o, W, H, cy, H, lt); return true; /* ┐ */
        case 0x2514: hstroke(o, W, H, cx, W, lt); vstroke(o, W, H, 0, cy+lt, lt); return true; /* └ */
        case 0x2518: hstroke(o, W, H, 0, cx+lt, lt); vstroke(o, W, H, 0, cy+lt, lt); return true; /* ┘ */

        /* tees (light) */
        case 0x251C: vstroke(o, W, H, 0, H, lt); hstroke(o, W, H, cx, W, lt); return true; /* ├ */
        case 0x2524: vstroke(o, W, H, 0, H, lt); hstroke(o, W, H, 0, cx+lt, lt); return true; /* ┤ */
        case 0x252C: hstroke(o, W, H, 0, W, lt); vstroke(o, W, H, cy, H, lt); return true; /* ┬ */
        case 0x2534: hstroke(o, W, H, 0, W, lt); vstroke(o, W, H, 0, cy+lt, lt); return true; /* ┴ */
        case 0x253C: hstroke(o, W, H, 0, W, lt); vstroke(o, W, H, 0, H, lt); return true; /* ┼ cross */

        /* rounded corners — approximate with the same square corners (a small
         * radius is invisible at cell size and keeps graphs seamless). */
        case 0x256D: hstroke(o, W, H, cx, W, lt); vstroke(o, W, H, cy, H, lt); return true; /* ╭ */
        case 0x256E: hstroke(o, W, H, 0, cx+lt, lt); vstroke(o, W, H, cy, H, lt); return true; /* ╮ */
        case 0x256F: hstroke(o, W, H, 0, cx+lt, lt); vstroke(o, W, H, 0, cy+lt, lt); return true; /* ╯ */
        case 0x2570: hstroke(o, W, H, cx, W, lt); vstroke(o, W, H, 0, cy+lt, lt); return true; /* ╰ */

        /* diagonals + cross */
        case 0x2571: /* ╱ */
            for (int y = 0; y < H; y++) { int x = W - 1 - (y * W) / H; for (int t=0;t<lt;t++) px(o,W,H,x+t,y,0xFF); }
            return true;
        case 0x2572: /* ╲ */
            for (int y = 0; y < H; y++) { int x = (y * W) / H; for (int t=0;t<lt;t++) px(o,W,H,x+t,y,0xFF); }
            return true;
        case 0x2573: /* ╳ */
            for (int y = 0; y < H; y++) {
                int a = (y * W) / H, b = W - 1 - a;
                for (int t=0;t<lt;t++){ px(o,W,H,a+t,y,0xFF); px(o,W,H,b+t,y,0xFF); }
            }
            return true;

        /* heavy straight-only variants worth covering (fall back below else) */
        case 0x250F: hstroke(o, W, H, cx, W, hv); vstroke(o, W, H, cy, H, hv); return true; /* ┏ */
        case 0x2513: hstroke(o, W, H, 0, cx+hv, hv); vstroke(o, W, H, cy, H, hv); return true; /* ┓ */
        case 0x2517: hstroke(o, W, H, cx, W, hv); vstroke(o, W, H, 0, cy+hv, hv); return true; /* ┗ */
        case 0x251B: hstroke(o, W, H, 0, cx+hv, hv); vstroke(o, W, H, 0, cy+hv, hv); return true; /* ┛ */
        case 0x254B: hstroke(o, W, H, 0, W, hv); vstroke(o, W, H, 0, H, hv); return true; /* ╋ heavy cross */

        /* double lines — approximate as a single light line (seamless, legible) */
        case 0x2550: hstroke(o, W, H, 0, W, lt); return true; /* ═ */
        case 0x2551: vstroke(o, W, H, 0, H, lt); return true; /* ║ */

        default:
            /* Any other box-drawing codepoint: decompose by whether the glyph
             * conventionally has each arm. Rather than a full table, fall back
             * to the font for the long tail — the ranges above cover everything
             * htop/btop/rockbottom actually draw. */
            return false;
    }
}

/* ---- braille U+2800..28FF --------------------------------------------- */

/* Braille is a 2-wide x 4-tall dot matrix. The low 8 bits of (cp - 0x2800)
 * select dots in this layout:
 *     1 4      bit0 bit3
 *     2 5      bit1 bit4
 *     3 6      bit2 bit5
 *     7 8      bit6 bit7
 * Each set dot is drawn as an anti-aliased FILLED CIRCLE centred in its
 * 2-wide x 4-tall sub-cell, with a gap around it — exactly how a real font
 * renders braille (kitty/foot/wezterm show discrete dots, not solid quads).
 * The dot radius is a share of the sub-cell so dense glyphs (U+28FF) still
 * read as a rich textured fill while sparse ones read as a dotted plot. */
static bool render_braille(uint32_t cp, uint8_t *o, int W, int H) {
    unsigned bits = cp - 0x2800;

    /* Sub-cell grid: 2 columns x 4 rows. Dot centres sit at the middle of
     * each sub-cell; radius fills most of it, leaving a thin inter-dot gap so
     * dots read as discrete (like a real braille font) not a solid block. */
    float subw = (float)W / 2.0f;
    float subh = (float)H / 4.0f;
    float rad = 0.44f * (subw < subh ? subw : subh);  /* round, gapped */
    if (rad < 1.0f) rad = 1.0f;
    float aa = 1.0f;                                   /* edge softness in px */

    /* bit -> (col,row) */
    static const int col_of[8] = {0,0,0,1,1,1,0,1};
    static const int row_of[8] = {0,1,2,0,1,2,3,3};

    for (int b = 0; b < 8; b++) {
        if (!(bits & (1u << b))) continue;
        float cxf = ((float)col_of[b] + 0.5f) * subw;
        float cyf = ((float)row_of[b] + 0.5f) * subh;
        int x0 = (int)(cxf - rad - aa), x1 = (int)(cxf + rad + aa) + 1;
        int y0 = (int)(cyf - rad - aa), y1 = (int)(cyf + rad + aa) + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W) x1 = W;
        if (y1 > H) y1 = H;
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                float dx = (float)x + 0.5f - cxf;
                float dy = (float)y + 0.5f - cyf;
                float d = dx * dx + dy * dy;
                float dist = d > 0.0f ? sqrtf(d) : 0.0f;
                float cov = (rad + aa - dist) / (2.0f * aa);  /* 1 inside, 0 outside */
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
                uint8_t v = (uint8_t)(cov * 255.0f + 0.5f);
                /* max-blend so overlapping AA doesn't darken */
                uint8_t *p = &o[(size_t)y * W + x];
                if (v > *p) *p = v;
            }
        }
    }
    return true;
}

/* ---- dispatch --------------------------------------------------------- */

bool glyph_synth_render(uint32_t cp, uint8_t *out, int cell_w, int cell_h) {
    if (!out || cell_w <= 0 || cell_h <= 0) return false;
    if (!glyph_synth_has(cp)) return false;

    clear(out, cell_w, cell_h);

    if (cp >= 0x2800 && cp <= 0x28FF) return render_braille(cp, out, cell_w, cell_h);
    if (cp >= 0x2580 && cp <= 0x259F) return render_block(cp, out, cell_w, cell_h);
    if (cp >= 0x2500 && cp <= 0x257F) {
        if (render_box(cp, out, cell_w, cell_h)) return true;
        /* not in our box subset — let the font handle it */
        return false;
    }
    return false;
}
