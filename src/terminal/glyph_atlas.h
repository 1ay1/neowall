/*
 * glyph_atlas.h — a dynamic, font-rasterized glyph atlas for the terminal.
 *
 * Loads a REAL TrueType/OpenType font (bundled default, or a user-configured
 * path) and rasterizes glyphs on demand into a single GPU-ready coverage
 * texture, caching codepoint -> atlas cell. This is how kitty/alacritty work:
 * a TUI touches a small working set of glyphs (a few hundred), so the atlas
 * fills once and then every frame is pure lookups.
 *
 * Rasterization is done by stb_truetype (a single vendored public-domain
 * header — src/terminal/vendor/stb_truetype.h — NOT an external dependency;
 * it lives in our tree like the pre-generated Wayland protocol sources). This
 * gives us any glyph in the font — box-drawing, blocks, arrows, CJK, emoji
 * outlines — with real antialiasing, without hand-encoding a single bitmap.
 *
 * Security note: stb_truetype does no bounds-checking on font offsets, so it
 * must only ever be pointed at a TRUSTED font (our bundled one, or a path the
 * user explicitly configured) — never network- or attacker-supplied data. The
 * wallpaper never loads a font from an untrusted source, so this is safe.
 *
 * This module is CPU-side only: it produces an 8-bit coverage bitmap for the
 * whole atlas plus a per-codepoint UV cache. The GL upload lives in the shader
 * engine (it owns the GL context), keeping this unit headlessly testable.
 */
#ifndef NEOWALL_TERMINAL_GLYPH_ATLAS_H
#define NEOWALL_TERMINAL_GLYPH_ATLAS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct glyph_atlas glyph_atlas;

/* One glyph's placement in the atlas, in TEXEL coordinates, plus the offset to
 * draw it at within a terminal cell (fonts aren't cell-aligned; we center the
 * advance box and record where the ink sits). */
typedef struct glyph_slot {
    uint16_t x, y;        /* top-left in the atlas bitmap */
    uint16_t w, h;        /* glyph bitmap size */
    int16_t  off_x, off_y;/* pixel offset from cell top-left to glyph top-left */
    bool     valid;
} glyph_slot;

/* Create an atlas that rasterizes `font` at a cell size of cell_w x cell_h
 * pixels. `font_data`/`font_len` is the in-memory font file (owned by caller,
 * must outlive the atlas). If font_data is NULL the bundled default font is
 * used. Returns NULL on failure (bad font, OOM). */
glyph_atlas *glyph_atlas_create(const uint8_t *font_data, size_t font_len,
                                int cell_w, int cell_h);

/* Like glyph_atlas_create, but also loads optional bold/italic faces (NULL =
 * none; bold/italic cells then fall back to the regular face) and a built-in
 * system fallback chain for glyphs the primary font lacks (CJK/symbols). */
glyph_atlas *glyph_atlas_create_ex(const uint8_t *font_data, size_t font_len,
                                   const char *bold_path, const char *italic_path,
                                   int cell_w, int cell_h);

void glyph_atlas_destroy(glyph_atlas *a);

/* Look up (rasterizing + inserting on first touch) the slot for a codepoint.
 * Returns a borrowed pointer into the atlas's cache (stable for the atlas's
 * lifetime). On atlas-full or un-rasterizable glyph, returns a blank slot
 * (valid=false) — the renderer draws nothing, never garbage. */
const glyph_slot *glyph_atlas_get(glyph_atlas *a, uint32_t cp);

/* As above, but selects the bold/italic face for the glyph (falling back to
 * regular when that face isn't loaded). Bold/italic variants cache separately. */
const glyph_slot *glyph_atlas_get_styled(glyph_atlas *a, uint32_t cp,
                                         bool bold, bool italic);

/* Atlas bitmap accessors (single-channel, 8-bit coverage, row-major). The
 * shader engine uploads this to a GL_R8 texture. `dirty` is set whenever new
 * glyphs were rasterized since the last clear, so the uploader can re-push only
 * when needed. */
const uint8_t *glyph_atlas_bitmap(const glyph_atlas *a);
int            glyph_atlas_width(const glyph_atlas *a);
int            glyph_atlas_height(const glyph_atlas *a);
int            glyph_atlas_cell_w(const glyph_atlas *a);
int            glyph_atlas_cell_h(const glyph_atlas *a);
bool           glyph_atlas_dirty(const glyph_atlas *a);
void           glyph_atlas_clear_dirty(glyph_atlas *a);

#endif /* NEOWALL_TERMINAL_GLYPH_ATLAS_H */
