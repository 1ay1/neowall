/*
 * glyph_synth.h — procedural rasterization of the "TUI drawing" Unicode ranges.
 *
 * Box-drawing (U+2500..257F), block elements (U+2580..259F), and braille
 * (U+2800..28FF) are the glyphs TUIs (htop/btop/rockbottom/tmux) use to draw
 * meters, graphs and sparklines. A general TrueType rasterizer draws them at
 * the font's own metrics into a tight ink box positioned by bearing, so they
 * do NOT tile edge-to-edge: partial blocks shrink, braille collapses to a few
 * dots, box lines leave seams between cells. Every serious terminal (kitty,
 * foot, wezterm, btop's own drawing) therefore SYNTHESIZES these ranges to fill
 * the whole cell exactly. This module is that synthesizer.
 *
 * It writes an 8-bit coverage bitmap that spans the FULL cell_w x cell_h, so
 * the caller records off_x = off_y = 0 and the glyph aligns to the cell grid
 * with no gaps. Pure CPU, no dependency, headlessly testable.
 */
#ifndef NEOWALL_TERMINAL_GLYPH_SYNTH_H
#define NEOWALL_TERMINAL_GLYPH_SYNTH_H

#include <stdbool.h>
#include <stdint.h>

/* True if `cp` is one of the ranges this module synthesizes. */
bool glyph_synth_has(uint32_t cp);

/* Rasterize `cp` into `out` (cell_w*cell_h, 8-bit coverage, row-major, stride =
 * cell_w). `out` must be pre-zeroed by the caller OR this fully overwrites it.
 * Returns true if it drew the glyph (cp in a synthesized range), false if not
 * (caller should fall back to the font). */
bool glyph_synth_render(uint32_t cp, uint8_t *out, int cell_w, int cell_h);

#endif /* NEOWALL_TERMINAL_GLYPH_SYNTH_H */
