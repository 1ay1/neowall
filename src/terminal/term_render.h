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

/* Atlas coverage bitmap (GL_R8), and whether it grew since last clear. */
const uint8_t *term_render_atlas(const term_render *tr);
int            term_render_atlas_w(const term_render *tr);
int            term_render_atlas_h(const term_render *tr);
int            term_render_cell_w(const term_render *tr);
int            term_render_cell_h(const term_render *tr);
bool           term_render_atlas_dirty(const term_render *tr);
void           term_render_clear_atlas_dirty(term_render *tr);

/* Cursor (in cell coords) for the shader to draw a block/underline. */
void           term_render_cursor(const term_render *tr, int *x, int *y, bool *visible);

/* Resize the grid (and PTY). Rebuilds the cell buffer. */
nw_result      term_render_resize(term_render *tr, int cols, int rows);

/* Has the child process exited? */
bool           term_render_child_exited(const term_render *tr);

/* --- cell-record packing helpers (shared with the GLSL decode) --- */
/* r channel: atlas x (12 bits) | atlas y (12 bits) | flags(8): bit0 = has-glyph */
#define TERM_PACK_R(ax, ay, has) \
    (((uint32_t)((ax) & 0xFFFu) << 20) | ((uint32_t)((ay) & 0xFFFu) << 8) | ((has) ? 1u : 0u))
/* g channel: w (8) | h (8) | off_x+128 (8) | off_y+128 (8) */
#define TERM_PACK_G(w, h, ox, oy) \
    (((uint32_t)((w) & 0xFFu) << 24) | ((uint32_t)((h) & 0xFFu) << 16) | \
     ((uint32_t)(((ox) + 128) & 0xFFu) << 8) | ((uint32_t)(((oy) + 128) & 0xFFu)))
/* colour channels: r<<24 | g<<16 | b<<8 | low8 (0xFF for fg, attr for bg) */
#define TERM_PACK_COL(r, g, b, low) \
    (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | ((uint32_t)(low) & 0xFFu))

#endif /* NEOWALL_TERMINAL_TERM_RENDER_H */
