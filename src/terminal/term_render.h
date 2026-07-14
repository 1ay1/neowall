/*
 * term_render.h — the CPU-side bridge that turns a live terminal into the two
 * byte buffers the shader engine uploads as textures.
 *
 * This module owns a `terminal` (PTY + parser + reader thread) and a
 * `glyph_atlas` (font rasterizer), and each frame resolves the terminal's cell
 * grid into a compact per-cell record — glyph atlas placement + fg/bg colour +
 * attributes — that the GL side uploads verbatim into an integer texture. The
 * shader then, per fragment, finds its cell, reads the record, samples the
 * atlas coverage at the resolved glyph UV, and tints it.
 *
 * It is deliberately GL-free (exactly like glyph_atlas.c): it produces plain
 * host buffers and metadata, so it stays headlessly unit-testable and the GL
 * upload lives in the shader engine that owns the context. The AUDIO channel
 * source is the structural template — CPU snapshot -> host buffer -> the
 * engine's per-frame glTexSubImage2D.
 *
 * Cell record layout (one RGBA32UI texel per grid cell, .r/.g/.b/.a = uint32):
 *   r: atlas glyph rect, packed  (x<<20 | y<<8 | ... ) — see TERM_PACK_* below
 *   g: glyph draw offset + size   (off_x, off_y, w, h) 8 bits each (biased)
 *   b: foreground colour RGBA8    (r<<24 | g<<16 | b<<8 | 0xFF)
 *   a: background colour + attrs   (r<<24 | g<<16 | b<<8 | attr8)
 * All colours are resolved to literal RGB on the CPU (palette applied), so the
 * shader needs no palette texture. Empty/space cells encode a zero glyph rect.
 */
#ifndef NEOWALL_TERMINAL_TERM_RENDER_H
#define NEOWALL_TERMINAL_TERM_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "neowall/result.h"

typedef struct term_render term_render;

typedef struct term_render_opts {
    const char *cmd;         /* command to run (required), e.g. "htop" */
    int         cols, rows;  /* grid size */
    int         cell_w, cell_h; /* glyph cell pixel size for the atlas */
    const uint8_t *font_data;   /* optional in-memory font (NULL = system font). Must outlive. */
    size_t         font_len;
    const char    *font_path;   /* optional path to a font file (NULL = search). */
    const char    *font_bold_path;   /* optional bold face (NULL = synth). */
    const char    *font_italic_path; /* optional italic face (NULL = synth). */
    const char    *cwd;         /* optional working directory for the child. */
    const char    *term_env;    /* optional TERM value (NULL = xterm-256color). */
    /* optional default fg/bg override (each -1 = use built-in). RGB packed 0xRRGGBB. */
    long           default_fg;
    long           default_bg;
} term_render_opts;

/* Spawn the terminal + build the atlas. NULL on failure. */
term_render *term_render_create(const term_render_opts *opts, nw_result *err_out);
void         term_render_destroy(term_render *tr);

/* Pull a fresh snapshot from the terminal and resolve every changed cell into
 * the cell buffer, rasterizing any newly-seen glyph into the atlas. Returns
 * true if the cell buffer changed since the last update (upload needed). */
bool term_render_update(term_render *tr);

/* Cell buffer: cols*rows RGBA32UI texels, row-major (row 0 = top line).
 * Valid until the next update. Width/height in cells via the cols/rows getters. */
const uint32_t *term_render_cells(const term_render *tr); /* 4 uint32 per cell */
int             term_render_cols(const term_render *tr);
int             term_render_rows(const term_render *tr);

/* Rows [y0,y1) of the cell buffer that changed in the last update() that
 * returned true. The uploader pushes only this band (glTexSubImage2D sub-rect)
 * instead of the whole grid. y1<=y0 means no rows changed. */
void            term_render_cells_dirty_rows(const term_render *tr, int *y0, int *y1);

/* Atlas coverage bitmap (GL_R8), and whether it grew since last clear. */
const uint8_t *term_render_atlas(const term_render *tr);
int            term_render_atlas_w(const term_render *tr);
int            term_render_atlas_h(const term_render *tr);
int            term_render_cell_w(const term_render *tr);
int            term_render_cell_h(const term_render *tr);
bool           term_render_atlas_dirty(const term_render *tr);
void           term_render_atlas_dirty_rows(const term_render *tr, int *y0, int *y1);
void           term_render_clear_atlas_dirty(term_render *tr);

/* Color-emoji atlas (GL_RGBA8, same dimensions as the coverage atlas). NULL if
 * no color-emoji font is present, in which case the shader's color branch is
 * never taken (no cell ever sets TERM_FLAG_COLOR). */
const uint8_t *term_render_color_atlas(const term_render *tr);
bool           term_render_color_atlas_dirty(const term_render *tr);
void           term_render_color_atlas_dirty_rows(const term_render *tr, int *y0, int *y1);
void           term_render_clear_color_atlas_dirty(term_render *tr);

/* Cursor (in cell coords) for the shader to draw a block/underline. */
void           term_render_cursor(const term_render *tr, int *x, int *y, bool *visible);

/* Resize the grid (and PTY). Rebuilds the cell buffer. */
nw_result      term_render_resize(term_render *tr, int cols, int rows);

/* Has the child process exited? */
bool           term_render_child_exited(const term_render *tr);

/* --- input (host → child) ------------------------------------------------
 * The host feeds raw pixel coordinates (relative to the wallpaper's top-left)
 * and this maps them to cells via cell_w/cell_h before forwarding.
 */

/* Forward a pointer event at pixel (px,py). button/pressed/motion as in
 * term_mouse(). Returns true if a report was sent (app had mouse enabled). */
bool           term_render_mouse(term_render *tr, int px, int py,
                                 int button, bool pressed, bool motion);

/* True if the child enabled any mouse reporting mode. */
bool           term_render_wants_mouse(const term_render *tr);

/* Write raw key bytes to the child (already encoded by the caller). */
bool           term_render_write(term_render *tr, const void *bytes, size_t len);

/* --- cell-record packing helpers (shared with the GLSL decode) --- */
/* r channel: atlas x (12 bits) | atlas y (12 bits) | flags(8): bit0 = has-glyph,
 * bit1 = color glyph (sample the RGBA color atlas, don't tint coverage). */
#define TERM_PACK_R(ax, ay, has) \
    (((uint32_t)((ax) & 0xFFFu) << 20) | ((uint32_t)((ay) & 0xFFFu) << 8) | ((has) ? 1u : 0u))
#define TERM_FLAG_COLOR 2u
/* g channel: w (8) | h (8) | off_x+128 (8) | off_y+128 (8) */
#define TERM_PACK_G(w, h, ox, oy) \
    (((uint32_t)((w) & 0xFFu) << 24) | ((uint32_t)((h) & 0xFFu) << 16) | \
     ((uint32_t)(((ox) + 128) & 0xFFu) << 8) | ((uint32_t)(((oy) + 128) & 0xFFu)))
/* colour channels: r<<24 | g<<16 | b<<8 | low8 (0xFF for fg, attr for bg) */
#define TERM_PACK_COL(r, g, b, low) \
    (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | ((uint32_t)(low) & 0xFFu))

#endif /* NEOWALL_TERMINAL_TERM_RENDER_H */
