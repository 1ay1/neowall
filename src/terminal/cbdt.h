/*
 * cbdt.h — dependency-free color-bitmap emoji extraction (CBDT/CBLC + sbix).
 *
 * stb_truetype rasterizes OUTLINE glyphs only; modern color-emoji fonts such as
 * NotoColorEmoji carry no `glyf`/`CFF ` table at all — the glyphs live as
 * embedded PNG strikes in a `CBDT` table indexed by `CBLC` (Google/Apple color
 * bitmaps) or `sbix` (Apple). stb_truetype's InitFont therefore rejects the
 * font outright. This tiny module parses the sfnt directory + `cmap` itself,
 * locates a codepoint's PNG strike, and decodes it (via the vendored stb_image)
 * into straight RGBA8 pixels — giving us real color emoji with zero external
 * dependencies, exactly like the rest of the terminal renderer.
 *
 * Trusted-input only, same contract as stb_truetype: we point this at bundled
 * or user-configured font files, never at network/attacker data.
 */
#ifndef NEOWALL_TERMINAL_CBDT_H
#define NEOWALL_TERMINAL_CBDT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct cbdt_font cbdt_font;

/* Parse a color-bitmap font from an in-memory file (the buffer is borrowed and
 * must outlive the returned handle). Returns NULL if the font has no usable
 * color-bitmap table (CBDT/CBLC or sbix) or is malformed. */
cbdt_font *cbdt_open(const uint8_t *data, size_t len);
void       cbdt_close(cbdt_font *f);

/* True if `cp` has a color glyph in this font. */
bool cbdt_has(const cbdt_font *f, uint32_t cp);

/* Decode the color glyph for `cp` into a freshly-malloc'd straight-alpha RGBA8
 * buffer (row-major, w*h*4 bytes). Caller frees with free(). Returns NULL if the
 * codepoint is absent or the strike can't be decoded. *out_w/*out_h receive the
 * native PNG pixel size (the strike ppem — e.g. 128x128 for NotoColorEmoji). */
uint8_t *cbdt_render(const cbdt_font *f, uint32_t cp, int *out_w, int *out_h);

/* Native strike size in pixels (ppem), for scale planning. */
int cbdt_ppem(const cbdt_font *f);

#endif /* NEOWALL_TERMINAL_CBDT_H */
