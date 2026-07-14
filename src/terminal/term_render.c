/*
 * term_render.c — resolve a live terminal's cell grid into GPU-ready buffers.
 *
 * Owns a `terminal` and a `glyph_atlas`. Each update():
 *   1. term_snapshot() copies a frame-coherent grid (mutex-guarded internally).
 *   2. skip if the terminal's epoch is unchanged (nothing drew) AND the atlas
 *      didn't change — the shader keeps the previous texture.
 *   3. for each cell: resolve the codepoint's atlas slot (rasterizing on first
 *      touch), resolve fg/bg through the 256-colour palette, pack four uint32.
 *
 * GL-free by design: produces host buffers the shader engine uploads.
 */
#define _DEFAULT_SOURCE
#include "term_render.h"
#include "neowall/terminal/terminal.h"
#include "glyph_atlas.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- default xterm 256-colour palette (index -> RGB) ------------------- */

static void palette_rgb(uint8_t idx, uint8_t *r, uint8_t *g, uint8_t *b) {
    /* 0..15: standard + bright ANSI */
    static const uint8_t base16[16][3] = {
        {  0,  0,  0}, {205,  0,  0}, {  0,205,  0}, {205,205,  0},
        {  0,  0,238}, {205,  0,205}, {  0,205,205}, {229,229,229},
        {127,127,127}, {255,  0,  0}, {  0,255,  0}, {255,255,  0},
        { 92, 92,255}, {255,  0,255}, {  0,255,255}, {255,255,255},
    };
    if (idx < 16) {
        *r = base16[idx][0]; *g = base16[idx][1]; *b = base16[idx][2];
        return;
    }
    if (idx < 232) {
        /* 6x6x6 colour cube: index = 16 + 36r + 6g + b, each 0..5 */
        int c = idx - 16;
        int ri = c / 36, gi = (c / 6) % 6, bi = c % 6;
        static const uint8_t steps[6] = {0, 95, 135, 175, 215, 255};
        *r = steps[ri]; *g = steps[gi]; *b = steps[bi];
        return;
    }
    /* 232..255: 24-step greyscale ramp */
    int v = 8 + (idx - 232) * 10;
    *r = *g = *b = (uint8_t)v;
}

/* Default fg/bg when a cell says TERM_COLOR_DEFAULT. */
static const uint8_t kDefaultFg[3] = {200, 200, 200};
static const uint8_t kDefaultBg[3] = {  0,   0,   0};

static void resolve_color(const term_color *c, const uint8_t def[3],
                          uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (c->kind) {
        case TERM_COLOR_RGB:     *r = c->r; *g = c->g; *b = c->b; break;
        case TERM_COLOR_INDEXED: palette_rgb(c->idx, r, g, b); break;
        case TERM_COLOR_DEFAULT:
        default:                 *r = def[0]; *g = def[1]; *b = def[2]; break;
    }
}

/* ------------------------------------------------------------------------ */

struct term_render {
    terminal    *term;
    glyph_atlas *atlas;
    int          cols, rows;
    int          cell_w, cell_h;
    uint32_t    *cells;       /* cols*rows*4 uint32 */
    uint64_t     last_epoch;
    bool         have_frame;  /* cells populated at least once */
    int          cursor_x, cursor_y;
    bool         cursor_vis;
};

static bool alloc_cells(term_render *tr) {
    free(tr->cells);
    tr->cells = calloc((size_t)tr->cols * (size_t)tr->rows * 4, sizeof(uint32_t));
    return tr->cells != NULL;
}

term_render *term_render_create(const term_render_opts *opts, nw_result *err_out) {
    if (!opts || !opts->cmd) {
        if (err_out) *err_out = nw_err(NW_ERR_INVALID_ARG, "term_render: null opts/cmd");
        return NULL;
    }

    term_render *tr = calloc(1, sizeof(*tr));
    if (!tr) {
        if (err_out) *err_out = nw_err(NW_ERR_OOM, "term_render: OOM");
        return NULL;
    }

    tr->cols   = opts->cols > 0 ? opts->cols : 100;
    tr->rows   = opts->rows > 0 ? opts->rows : 30;
    tr->cell_w = opts->cell_w > 0 ? opts->cell_w : 9;
    tr->cell_h = opts->cell_h > 0 ? opts->cell_h : 18;

    /* Load font: explicit in-memory buffer, else a path, else system search. */
    const uint8_t *font_data = opts->font_data;
    size_t         font_len  = opts->font_len;
    uint8_t       *file_buf  = NULL;
    if (!font_data && opts->font_path && opts->font_path[0]) {
        FILE *f = fopen(opts->font_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                file_buf = malloc((size_t)sz);
                if (file_buf && fread(file_buf, 1, (size_t)sz, f) == (size_t)sz) {
                    font_data = file_buf;
                    font_len  = (size_t)sz;
                } else {
                    free(file_buf);
                    file_buf = NULL;
                }
            }
            fclose(f);
        }
    }

    tr->atlas = glyph_atlas_create(font_data, font_len, tr->cell_w, tr->cell_h);
    free(file_buf); /* atlas copies what it needs; system-font path owns its own */
    if (!tr->atlas) {
        if (err_out) *err_out = nw_err(NW_ERR_IO, "term_render: no usable font");
        free(tr);
        return NULL;
    }

    if (!alloc_cells(tr)) {
        if (err_out) *err_out = nw_err(NW_ERR_OOM, "term_render: cell buffer OOM");
        glyph_atlas_destroy(tr->atlas);
        free(tr);
        return NULL;
    }

    term_spawn_opts so = {
        .cmd = opts->cmd, .cols = tr->cols, .rows = tr->rows,
        .scrollback = 0, .term_env = NULL,
    };
    nw_result r = term_spawn(&so, &tr->term);
    if (nw_is_err(r)) {
        if (err_out) *err_out = r;
        glyph_atlas_destroy(tr->atlas);
        free(tr->cells);
        free(tr);
        return NULL;
    }

    if (err_out) *err_out = nw_ok();
    return tr;
}

void term_render_destroy(term_render *tr) {
    if (!tr) return;
    if (tr->term)  term_destroy(tr->term);
    if (tr->atlas) glyph_atlas_destroy(tr->atlas);
    free(tr->cells);
    free(tr);
}

bool term_render_update(term_render *tr) {
    if (!tr) return false;
    const term_frame *f = term_snapshot(tr->term);
    if (!f) return false;

    tr->cursor_x = f->cursor_x;
    tr->cursor_y = f->cursor_y;
    tr->cursor_vis = f->cursor_visible;

    /* Grid resized underneath us (child SIGWINCH echo, etc.) — resync. */
    if (f->cols != tr->cols || f->rows != tr->rows) {
        tr->cols = f->cols;
        tr->rows = f->rows;
        if (!alloc_cells(tr)) return false;
        tr->have_frame = false;
    }

    if (tr->have_frame && f->epoch == tr->last_epoch) {
        return false; /* nothing changed */
    }
    tr->last_epoch = f->epoch;
    tr->have_frame = true;

    const term_cell *src = f->cells;
    uint32_t *dst = tr->cells;
    int n = tr->cols * tr->rows;
    for (int i = 0; i < n; i++) {
        const term_cell *c = &src[i];
        uint32_t *o = &dst[i * 4];

        /* wide-char right half: draw nothing, inherit bg only. */
        bool tail = (c->attr & TERM_ATTR_WIDE_TAIL) != 0;

        uint8_t fr, fg, fb, br, bg, bb;
        resolve_color(&c->fg, kDefaultFg, &fr, &fg, &fb);
        resolve_color(&c->bg, kDefaultBg, &br, &bg, &bb);

        /* reverse video: swap fg/bg */
        if (c->attr & TERM_ATTR_REVERSE) {
            uint8_t tr_ = fr, tg = fg, tb = fb;
            fr = br; fg = bg; fb = bb;
            br = tr_; bg = tg; bb = tb;
        }

        bool has_glyph = false;
        uint16_t ax = 0, ay = 0, gw = 0, gh = 0;
        int16_t ox = 0, oy = 0;
        if (!tail && c->cp != 0 && c->cp != ' ' &&
            !(c->attr & TERM_ATTR_INVISIBLE)) {
            const glyph_slot *s = glyph_atlas_get(tr->atlas, c->cp);
            if (s && s->valid && s->w > 0 && s->h > 0) {
                has_glyph = true;
                ax = s->x; ay = s->y; gw = s->w; gh = s->h;
                ox = s->off_x; oy = s->off_y;
            }
        }

        uint8_t attr8 = (uint8_t)(c->attr & 0xFF);
        o[0] = TERM_PACK_R(ax, ay, has_glyph);
        o[1] = TERM_PACK_G(gw, gh, ox, oy);
        o[2] = TERM_PACK_COL(fr, fg, fb, 0xFF);
        o[3] = TERM_PACK_COL(br, bg, bb, attr8);
    }
    return true;
}

const uint32_t *term_render_cells(const term_render *tr) { return tr ? tr->cells : NULL; }
int term_render_cols(const term_render *tr) { return tr ? tr->cols : 0; }
int term_render_rows(const term_render *tr) { return tr ? tr->rows : 0; }

const uint8_t *term_render_atlas(const term_render *tr) {
    return tr ? glyph_atlas_bitmap(tr->atlas) : NULL;
}
int term_render_atlas_w(const term_render *tr) { return tr ? glyph_atlas_width(tr->atlas) : 0; }
int term_render_atlas_h(const term_render *tr) { return tr ? glyph_atlas_height(tr->atlas) : 0; }
int term_render_cell_w(const term_render *tr) { return tr ? tr->cell_w : 0; }
int term_render_cell_h(const term_render *tr) { return tr ? tr->cell_h : 0; }
bool term_render_atlas_dirty(const term_render *tr) {
    return tr ? glyph_atlas_dirty(tr->atlas) : false;
}
void term_render_clear_atlas_dirty(term_render *tr) {
    if (tr) glyph_atlas_clear_dirty(tr->atlas);
}

void term_render_cursor(const term_render *tr, int *x, int *y, bool *visible) {
    if (!tr) return;
    if (x) *x = tr->cursor_x;
    if (y) *y = tr->cursor_y;
    if (visible) *visible = tr->cursor_vis;
}

nw_result term_render_resize(term_render *tr, int cols, int rows) {
    if (!tr) return nw_err(NW_ERR_INVALID_ARG, "term_render_resize: null");
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    nw_result r = term_resize(tr->term, cols, rows);
    if (nw_is_err(r)) return r;
    tr->cols = cols;
    tr->rows = rows;
    if (!alloc_cells(tr)) return nw_err(NW_ERR_OOM, "term_render_resize: OOM");
    tr->have_frame = false;
    return nw_ok();
}

bool term_render_child_exited(const term_render *tr) {
    return tr ? term_child_exited(tr->term, NULL) : true;
}

bool term_render_mouse(term_render *tr, int px, int py,
                       int button, bool pressed, bool motion) {
    if (!tr || !tr->term) return false;
    int cw = tr->cell_w > 0 ? tr->cell_w : 9;
    int ch = tr->cell_h > 0 ? tr->cell_h : 18;
    int cx = px / cw;
    int cy = py / ch;
    return term_mouse(tr->term, cx, cy, button, pressed, motion);
}

bool term_render_wants_mouse(const term_render *tr) {
    return tr ? term_wants_mouse(tr->term) : false;
}

bool term_render_write(term_render *tr, const void *bytes, size_t len) {
    if (!tr || !tr->term) return false;
    return nw_is_ok(term_write(tr->term, bytes, len));
}
