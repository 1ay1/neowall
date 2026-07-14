/*
 * glyph_atlas.c — dynamic font rasterization into a coverage atlas.
 *
 * Uses stb_truetype (vendored, public domain) to rasterize glyphs on demand.
 * Packs them into one bitmap with a simple shelf/skyline row packer, which is
 * more than enough for a terminal's small glyph working set. A hash map caches
 * codepoint -> slot so each glyph is rasterized at most once.
 */
#include "glyph_atlas.h"
#include "glyph_synth.h"
#include "cbdt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Vendored single-header rasteriser. Only THIS translation unit defines the
 * implementation. See vendor/stb_truetype.h header for the public-domain
 * notice; it is trusted-input-only (we feed it a system font or a
 * user-configured path, never untrusted data). */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "vendor/stb_truetype.h"

/* Well-known monospace TrueType paths, tried in order when no font is given.
 * DejaVu Sans Mono is first because it has excellent box-drawing + block-
 * element coverage (exactly what htop/btop/tmux draw their UI with) and ships
 * on essentially every Linux desktop. All are redistributable and read-only —
 * trusted input for stb_truetype. */
static const char *const kDefaultFontPaths[] = {
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/LiberationMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/TTF/Hack-Regular.ttf",
    NULL,
};

/* Fallback fonts tried (in order) when the primary lacks a glyph — covers the
 * CJK / symbol / emoji-outline codepoints DejaVu Mono is missing. All optional;
 * absent files are skipped. */
static const char *const kFallbackFontPaths[] = {
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    NULL,
};

#define MAX_FALLBACK 6

/* Color-emoji fonts (CBDT/CBLC or sbix). stb_truetype can't touch these (no
 * outline table), so they load through the self-contained cbdt reader and
 * rasterize into the RGBA color atlas. First present one wins. */
static const char *const kColorEmojiFontPaths[] = {
    "/usr/share/fonts/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf",
    "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf",
    "/usr/share/fonts/TTF/NotoColorEmoji.ttf",
    "/usr/share/fonts/twitter-color-emoji/TwitterColorEmoji-SVGinOT.ttf",
    "/usr/share/fonts/TTF/Twemoji.ttf",
    "/usr/share/fonts/apple-emoji/AppleColorEmoji.ttf",
    NULL,
};

/* A single rasterizable face: font info + its own scale/metrics. */
typedef struct {
    stbtt_fontinfo font;
    const uint8_t *data;
    bool           owns;
    bool           ready;
    float          scale;
    int            ascent_px;
} face;

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

/* ---- atlas geometry ---- */
#define ATLAS_W 2048
#define ATLAS_H 2048

/* ---- hash map: (codepoint, style) -> slot index ---- */
#define MAP_CAP 8192   /* power of two; load kept < 0.7 (styled keys triple the set) */

/* Style bits folded into the map key so bold/italic variants of the same cp
 * cache separately. Bit 30 = bold, bit 31 = italic (cp is a 21-bit scalar). */
#define STYLE_BOLD   (1u << 30)
#define STYLE_ITALIC (1u << 31)

typedef struct {
    uint32_t key;      /* 0 = empty; else cp | style bits */
    uint32_t slot;     /* index into slots[] */
} map_entry;

struct glyph_atlas {
    face regular;
    face bold;
    face italic;
    face fallback[MAX_FALLBACK];
    int  fallback_count;

    /* Color-emoji source (CBDT/CBLC or sbix). NULL if no emoji font present. */
    cbdt_font *emoji;
    uint8_t   *emoji_data;   /* owned font buffer backing `emoji` */

    int    cell_w, cell_h;

    uint8_t *bitmap;     /* ATLAS_W * ATLAS_H, 8-bit coverage */
    bool     dirty;
    int      dirty_y0, dirty_y1;  /* row range touched since last clear (half-open) */

    /* Color emoji atlas: RGBA8, straight alpha, own shelf packer + dirty rows. */
    uint8_t *color_bitmap;               /* ATLAS_W * ATLAS_H * 4 */
    bool     color_dirty;
    int      color_dirty_y0, color_dirty_y1;
    int      cshelf_x, cshelf_y, cshelf_h;

    /* shelf packer cursor */
    int shelf_x, shelf_y, shelf_h;

    glyph_slot *slots;
    int         slot_count, slot_cap;

    map_entry map[MAP_CAP];
};

/* ---- hash ---- */
static inline uint32_t hash_cp(uint32_t key) {
    key *= 2654435761u;   /* Knuth multiplicative */
    return key & (MAP_CAP - 1);
}

static int map_find(glyph_atlas *a, uint32_t key) {
    uint32_t h = hash_cp(key);
    for (int probe = 0; probe < MAP_CAP; probe++) {
        map_entry *e = &a->map[h];
        if (e->key == 0) return -1;        /* empty slot -> not present */
        if (e->key == key) return (int)e->slot;
        h = (h + 1) & (MAP_CAP - 1);
    }
    return -1;
}

static void map_insert(glyph_atlas *a, uint32_t key, uint32_t slot) {
    uint32_t h = hash_cp(key);
    for (int probe = 0; probe < MAP_CAP; probe++) {
        map_entry *e = &a->map[h];
        if (e->key == 0 || e->key == key) { e->key = key; e->slot = slot; return; }
        h = (h + 1) & (MAP_CAP - 1);
    }
    /* map full: silently drop (working set exceeded MAP_CAP, absurd for a
     * terminal). Lookup will just re-rasterize each time — correctness holds. */
}

/* ---- shelf packer: reserve a w x h rectangle in the atlas ---- */
static bool atlas_reserve(glyph_atlas *a, int w, int h, uint16_t *ox, uint16_t *oy) {
    if (w > ATLAS_W) return false;
    if (a->shelf_x + w > ATLAS_W) {
        /* new shelf */
        a->shelf_y += a->shelf_h;
        a->shelf_x = 0;
        a->shelf_h = 0;
    }
    if (a->shelf_y + h > ATLAS_H) return false;   /* atlas full */
    *ox = (uint16_t)a->shelf_x;
    *oy = (uint16_t)a->shelf_y;
    a->shelf_x += w;
    if (h > a->shelf_h) a->shelf_h = h;
    return true;
}

/* ---- construction ---- */

/* Init a single face from an owned/borrowed font buffer at the cell height.
 * Returns false (face.ready stays 0) if the buffer isn't a usable font. */
static bool face_init(face *fc, const uint8_t *data, size_t len, bool owns, int cell_h) {
    memset(fc, 0, sizeof(*fc));
    if (!data || !len) return false;
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&fc->font, data, off)) {
        if (owns) free((void *)data);
        return false;
    }
    fc->data = data; fc->owns = owns;
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&fc->font, &ascent, &descent, &line_gap);
    fc->scale = stbtt_ScaleForPixelHeight(&fc->font, (float)cell_h);
    fc->ascent_px = (int)(ascent * fc->scale + 0.5f);
    fc->ready = true;
    return true;
}

/* Load a face from a file path (owned buffer). Silent no-op if unreadable. */
static bool face_init_path(face *fc, const char *path, int cell_h) {
    if (!path || !path[0]) { memset(fc, 0, sizeof(*fc)); return false; }
    size_t len = 0;
    uint8_t *buf = read_file(path, &len);
    if (!buf) { memset(fc, 0, sizeof(*fc)); return false; }
    return face_init(fc, buf, len, true, cell_h);
}

static void face_free(face *fc) {
    if (fc && fc->ready && fc->owns && fc->data) free((void *)fc->data);
}

/* Extend the dirty row range to cover [y0, y1). The GL uploader then re-pushes
 * only these rows instead of the whole 2048-row atlas. */
static void mark_dirty_rows(glyph_atlas *a, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > ATLAS_H) y1 = ATLAS_H;
    if (y0 >= y1) return;
    if (!a->dirty) { a->dirty_y0 = y0; a->dirty_y1 = y1; }
    else {
        if (y0 < a->dirty_y0) a->dirty_y0 = y0;
        if (y1 > a->dirty_y1) a->dirty_y1 = y1;
    }
    a->dirty = true;
}

/* Color-atlas equivalents of the coverage shelf packer / dirty tracker. */
static bool color_reserve(glyph_atlas *a, int w, int h, uint16_t *ox, uint16_t *oy) {
    if (w > ATLAS_W) return false;
    if (a->cshelf_x + w > ATLAS_W) {
        a->cshelf_y += a->cshelf_h;
        a->cshelf_x = 0;
        a->cshelf_h = 0;
    }
    if (a->cshelf_y + h > ATLAS_H) return false;
    *ox = (uint16_t)a->cshelf_x;
    *oy = (uint16_t)a->cshelf_y;
    a->cshelf_x += w;
    if (h > a->cshelf_h) a->cshelf_h = h;
    return true;
}

static void mark_color_dirty_rows(glyph_atlas *a, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > ATLAS_H) y1 = ATLAS_H;
    if (y0 >= y1) return;
    if (!a->color_dirty) { a->color_dirty_y0 = y0; a->color_dirty_y1 = y1; }
    else {
        if (y0 < a->color_dirty_y0) a->color_dirty_y0 = y0;
        if (y1 > a->color_dirty_y1) a->color_dirty_y1 = y1;
    }
    a->color_dirty = true;
}

/* Box-filter downscale of a straight-alpha RGBA source into a dst rect. Alpha
 * is premultiplied during averaging so translucent edges don't bleed the
 * (undefined) colour of fully-transparent source texels, then un-premultiplied
 * back to straight alpha for the atlas (the shader composites straight). */
static void rgba_downscale(const uint8_t *src, int sw, int sh,
                           uint8_t *dst, int dw, int dh) {
    for (int dy = 0; dy < dh; dy++) {
        int sy0 = (int)((int64_t)dy * sh / dh);
        int sy1 = (int)((int64_t)(dy + 1) * sh / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int dx = 0; dx < dw; dx++) {
            int sx0 = (int)((int64_t)dx * sw / dw);
            int sx1 = (int)((int64_t)(dx + 1) * sw / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t ar = 0, ag = 0, ab = 0, aa = 0; int cnt = 0;
            for (int y = sy0; y < sy1 && y < sh; y++) {
                const uint8_t *row = src + (size_t)y * sw * 4;
                for (int x = sx0; x < sx1 && x < sw; x++) {
                    const uint8_t *p = row + (size_t)x * 4;
                    uint32_t al = p[3];
                    ar += (uint32_t)p[0] * al;
                    ag += (uint32_t)p[1] * al;
                    ab += (uint32_t)p[2] * al;
                    aa += al;
                    cnt++;
                }
            }
            uint8_t *o = dst + ((size_t)dy * dw + dx) * 4;
            if (cnt == 0 || aa == 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            o[0] = (uint8_t)(ar / aa);
            o[1] = (uint8_t)(ag / aa);
            o[2] = (uint8_t)(ab / aa);
            o[3] = (uint8_t)(aa / cnt);
        }
    }
}

/* Rasterize a color emoji for `cp` into the RGBA color atlas. Returns the new
 * slot index (with .color = true), or -1 if the emoji font lacks it / atlas
 * full. Emoji are square strikes drawn across the glyph's TWO cells at the
 * supersampled cell height; term_render draws head+tail halves from w = 2*cw. */
static int raster_emoji(glyph_atlas *a, uint32_t cp) {
    if (!a->emoji || !a->color_bitmap) return -1;
    if (!cbdt_has(a->emoji, cp)) return -1;
    int sw = 0, sh = 0;
    uint8_t *rgba = cbdt_render(a->emoji, cp, &sw, &sh);
    if (!rgba || sw <= 0 || sh <= 0) { free(rgba); return -1; }

    /* Cell span must match the screen model's char_width: SMP pictographs and
     * the wide-emoji blocks occupy TWO cells; BMP dingbats/symbols occupy one.
     * A mismatch would clip a 2-cell strike to half or overrun a 1-cell one. */
    int cells_wide = (cp >= 0x1F300 && cp <= 0x1FAFF) ? 2 : 1;

    /* Target: fit the strike into a cells_wide-cell box at cell height,
     * preserving aspect. cell_w/cell_h are already the supersampled size. */
    int box_w = a->cell_w * cells_wide;
    int box_h = a->cell_h;
    int dw = box_w, dh = box_h;
    /* preserve aspect within the box */
    if (sw * box_h > sh * box_w) dh = (int)((int64_t)box_w * sh / sw);
    else                          dw = (int)((int64_t)box_h * sw / sh);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    uint16_t ox, oy;
    if (!color_reserve(a, dw + 1, dh + 1, &ox, &oy)) { free(rgba); return -1; }

    /* downscale into a temp, then blit into the atlas at (ox,oy) */
    uint8_t *tmp = malloc((size_t)dw * dh * 4);
    if (!tmp) { free(rgba); return -1; }
    rgba_downscale(rgba, sw, sh, tmp, dw, dh);
    free(rgba);
    for (int y = 0; y < dh; y++)
        memcpy(a->color_bitmap + ((size_t)(oy + y) * ATLAS_W + ox) * 4,
               tmp + (size_t)y * dw * 4, (size_t)dw * 4);
    free(tmp);
    mark_color_dirty_rows(a, oy, oy + dh);

    if (a->slot_count >= a->slot_cap) {
        int ncap = a->slot_cap * 2;
        glyph_slot *ns = realloc(a->slots, (size_t)ncap * sizeof(glyph_slot));
        if (!ns) return -1;
        a->slots = ns;
        a->slot_cap = ncap;
    }
    glyph_slot *s = &a->slots[a->slot_count];
    s->x = ox; s->y = oy; s->w = (uint16_t)dw; s->h = (uint16_t)dh;
    /* centre the (possibly aspect-shrunk) strike inside the 2-cell box */
    s->off_x = (int16_t)((box_w - dw) / 2);
    s->off_y = (int16_t)((box_h - dh) / 2);
    s->valid = true;
    s->color = true;
    return a->slot_count++;
}

glyph_atlas *glyph_atlas_create_ex(const uint8_t *font_data, size_t font_len,
                                   const char *bold_path, const char *italic_path,
                                   int cell_w, int cell_h) {
    if (cell_w <= 0 || cell_h <= 0) return NULL;

    glyph_atlas *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->cell_w = cell_w;
    a->cell_h = cell_h;

    /* --- primary (regular) face --- */
    if (font_data && font_len) {
        uint8_t *copy = malloc(font_len);
        if (!copy) { free(a); return NULL; }
        memcpy(copy, font_data, font_len);
        if (!face_init(&a->regular, copy, font_len, true, cell_h)) { free(a); return NULL; }
    } else {
        size_t len = 0; uint8_t *loaded = NULL;
        for (int i = 0; kDefaultFontPaths[i]; i++) {
            loaded = read_file(kDefaultFontPaths[i], &len);
            if (loaded) break;
        }
        if (!loaded || !face_init(&a->regular, loaded, len, true, cell_h)) {
            free(loaded); free(a); return NULL;
        }
    }

    /* --- optional bold / italic faces (fall back to regular if absent) --- */
    face_init_path(&a->bold, bold_path, cell_h);
    face_init_path(&a->italic, italic_path, cell_h);

    /* --- fallback chain for glyphs the primary lacks (CJK/symbols) --- */
    for (int i = 0; kFallbackFontPaths[i] && a->fallback_count < MAX_FALLBACK; i++) {
        if (face_init_path(&a->fallback[a->fallback_count], kFallbackFontPaths[i], cell_h))
            a->fallback_count++;
    }

    /* --- color emoji font (CBDT/CBLC or sbix), via the self-contained reader
     * since stb_truetype rejects outline-less fonts. First present one wins. */
    for (int i = 0; kColorEmojiFontPaths[i]; i++) {
        size_t elen = 0;
        uint8_t *edata = read_file(kColorEmojiFontPaths[i], &elen);
        if (!edata) continue;
        cbdt_font *cf = cbdt_open(edata, elen);
        if (cf) { a->emoji = cf; a->emoji_data = edata; break; }
        free(edata);
    }

    a->bitmap = calloc((size_t)ATLAS_W * ATLAS_H, 1);
    if (!a->bitmap) { face_free(&a->regular); free(a); return NULL; }
    /* Color atlas is only allocated when an emoji font is actually present. */
    if (a->emoji) {
        a->color_bitmap = calloc((size_t)ATLAS_W * ATLAS_H * 4, 1);
        if (!a->color_bitmap) {   /* degrade gracefully: no color emoji, text still works */
            cbdt_close(a->emoji); a->emoji = NULL;
            free(a->emoji_data);  a->emoji_data = NULL;
        }
    }
    a->slot_cap = 512;
    a->slots = calloc((size_t)a->slot_cap, sizeof(glyph_slot));
    if (!a->slots) { free(a->bitmap); face_free(&a->regular); free(a); return NULL; }

    a->shelf_x = a->shelf_y = a->shelf_h = 0;
    a->cshelf_x = a->cshelf_y = a->cshelf_h = 0;
    a->dirty = true;
    a->dirty_y0 = 0; a->dirty_y1 = ATLAS_H;   /* first upload covers all */
    a->color_dirty = a->emoji ? true : false;
    a->color_dirty_y0 = 0; a->color_dirty_y1 = ATLAS_H;
    return a;
}

glyph_atlas *glyph_atlas_create(const uint8_t *font_data, size_t font_len,
                                int cell_w, int cell_h) {
    return glyph_atlas_create_ex(font_data, font_len, NULL, NULL, cell_w, cell_h);
}

void glyph_atlas_destroy(glyph_atlas *a) {
    if (!a) return;
    free(a->bitmap);
    free(a->color_bitmap);
    if (a->emoji) cbdt_close(a->emoji);
    free(a->emoji_data);
    free(a->slots);
    face_free(&a->regular);
    face_free(&a->bold);
    face_free(&a->italic);
    for (int i = 0; i < a->fallback_count; i++) face_free(&a->fallback[i]);
    free(a);
}

/* Choose the face for a style, falling back to regular when a variant is
 * absent. Bold-without-a-bold-face still uses regular (synthetic emboldening
 * would need a second raster pass; acceptable degrade). */
static face *pick_face(glyph_atlas *a, uint32_t style) {
    if ((style & STYLE_ITALIC) && a->italic.ready) return &a->italic;
    if ((style & STYLE_BOLD)   && a->bold.ready)   return &a->bold;
    return &a->regular;
}

/* Codepoints that should render as COLOR emoji whenever the emoji font has them,
 * even if a text face also carries a dull monochrome outline (e.g. ❤ ☀ ⭐ ✈).
 * These are the Unicode ranges with Emoji_Presentation or common emoji dingbats;
 * plain letters/box-drawing/CJK are deliberately excluded so text stays crisp. */
static bool prefers_color_emoji(uint32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FAFF) ||   /* SMP emoji + symbols + pictographs */
           (cp >= 0x2600  && cp <= 0x27BF)  ||   /* Misc symbols + Dingbats */
           (cp >= 0x2B00  && cp <= 0x2BFF)  ||   /* Misc symbols & arrows (stars) */
           (cp >= 0x2190  && cp <= 0x21FF)  ||   /* arrows (emoji-presentation subset) */
           cp == 0x203C || cp == 0x2049 ||       /* !! ?! */
           (cp >= 0x2122 && cp <= 0x2139) ||     /* TM, info */
           (cp >= 0x1F1E6 && cp <= 0x1F1FF);     /* regional indicators (flags) */
}

/* Rasterize a font glyph from a specific face into the atlas; returns the new
 * slot index, or -1 if the face lacks the glyph / atlas is full. Does NOT touch
 * the map (caller keys it). */
static int raster_face(glyph_atlas *a, face *fc, uint32_t cp) {
    if (!fc || !fc->ready) return -1;
    int gi = stbtt_FindGlyphIndex(&fc->font, (int)cp);
    if (gi == 0) return -1;   /* not in this face */

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&fc->font, gi, fc->scale, fc->scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;

    if (gw <= 0 || gh <= 0) {
        /* whitespace (space, etc.) — valid but no ink */
        if (a->slot_count >= a->slot_cap) return -1;
        glyph_slot *s = &a->slots[a->slot_count];
        memset(s, 0, sizeof(*s));
        s->valid = true;
        return a->slot_count++;
    }

    uint16_t ox, oy;
    if (!atlas_reserve(a, gw + 1, gh + 1, &ox, &oy)) return -1;

    stbtt_MakeGlyphBitmap(&fc->font,
                          a->bitmap + (size_t)oy * ATLAS_W + ox,
                          gw, gh, ATLAS_W, fc->scale, fc->scale, gi);
    mark_dirty_rows(a, oy, oy + gh);

    if (a->slot_count >= a->slot_cap) {
        int ncap = a->slot_cap * 2;
        glyph_slot *ns = realloc(a->slots, (size_t)ncap * sizeof(glyph_slot));
        if (!ns) return -1;
        a->slots = ns;
        a->slot_cap = ncap;
    }
    glyph_slot *s = &a->slots[a->slot_count];
    s->x = ox; s->y = oy; s->w = (uint16_t)gw; s->h = (uint16_t)gh;
    s->off_x = (int16_t)x0;                        /* glyph left bearing */
    s->off_y = (int16_t)(fc->ascent_px + y0);      /* top of ink from cell top */
    s->valid = true;
    return a->slot_count++;
}

/* Rasterize + insert a (codepoint, style) key; returns its slot index. */
static int rasterize(glyph_atlas *a, uint32_t cp, uint32_t style) {
    uint32_t key = cp | style;

    /* TUI drawing ranges (box/block/braille) are synthesized to fill the whole
     * cell edge-to-edge, so meters and sparklines tile with no seams. This runs
     * BEFORE the font path — the font's own versions are inset/bearing-placed
     * and render as small misaligned marks. Synth ignores style. */
    if (glyph_synth_has(cp)) {
        int cw = a->cell_w, ch = a->cell_h;
        uint8_t *tmp = malloc((size_t)cw * ch);
        if (tmp && glyph_synth_render(cp, tmp, cw, ch)) {
            uint16_t ox, oy;
            if (!atlas_reserve(a, cw + 1, ch + 1, &ox, &oy)) { free(tmp); goto blank; }
            for (int y = 0; y < ch; y++)
                memcpy(a->bitmap + (size_t)(oy + y) * ATLAS_W + ox,
                       tmp + (size_t)y * cw, (size_t)cw);
            mark_dirty_rows(a, oy, oy + ch);
            free(tmp);
            if (a->slot_count >= a->slot_cap) {
                int ncap = a->slot_cap * 2;
                glyph_slot *ns = realloc(a->slots, (size_t)ncap * sizeof(glyph_slot));
                if (!ns) goto blank;
                a->slots = ns;
                a->slot_cap = ncap;
            }
            glyph_slot *s = &a->slots[a->slot_count];
            s->x = ox; s->y = oy; s->w = (uint16_t)cw; s->h = (uint16_t)ch;
            s->off_x = 0; s->off_y = 0;   /* fills the cell exactly */
            s->valid = true;
            map_insert(a, key, (uint32_t)a->slot_count);
            return a->slot_count++;
        }
        free(tmp);
        /* synth declined: fall through to the font. */
    }

    /* Emoji-presentation codepoints render as COLOR whenever the emoji font
     * has them — tried BEFORE the text faces so ❤ ⭐ ✈ don't come out as dull
     * monochrome outlines that a text/symbol face happens to carry. */
    if (prefers_color_emoji(cp)) {
        int cidx = raster_emoji(a, cp);
        if (cidx >= 0) { map_insert(a, key, (uint32_t)cidx); return cidx; }
    }

    /* 1) the styled face, 2) regular (if we started styled), 3) fallback chain. */
    int idx = raster_face(a, pick_face(a, style), cp);
    if (idx < 0 && style) idx = raster_face(a, &a->regular, cp);

    /* For non-presentation codepoints, still try a COLOR emoji strike before
     * the monochrome fallback fonts (catches emoji the text face lacks). */
    if (idx < 0) {
        int cidx = raster_emoji(a, cp);
        if (cidx >= 0) { map_insert(a, key, (uint32_t)cidx); return cidx; }
    }

    for (int i = 0; idx < 0 && i < a->fallback_count; i++)
        idx = raster_face(a, &a->fallback[i], cp);

    if (idx >= 0) { map_insert(a, key, (uint32_t)idx); return idx; }

blank:
    if (a->slot_count >= a->slot_cap) return -1;
    {
        glyph_slot *b = &a->slots[a->slot_count];
        memset(b, 0, sizeof(*b));
        b->valid = false;
        map_insert(a, key, (uint32_t)a->slot_count);
        return a->slot_count++;
    }
}

const glyph_slot *glyph_atlas_get_styled(glyph_atlas *a, uint32_t cp,
                                         bool bold, bool italic) {
    static const glyph_slot blank = {0};
    if (!a || cp == 0) return &blank;
    uint32_t style = (bold ? STYLE_BOLD : 0u) | (italic ? STYLE_ITALIC : 0u);
    int idx = map_find(a, cp | style);
    if (idx < 0) idx = rasterize(a, cp, style);
    if (idx < 0 || idx >= a->slot_count) return &blank;
    return &a->slots[idx];
}

const glyph_slot *glyph_atlas_get(glyph_atlas *a, uint32_t cp) {
    return glyph_atlas_get_styled(a, cp, false, false);
}

const uint8_t *glyph_atlas_bitmap(const glyph_atlas *a) { return a ? a->bitmap : NULL; }
int  glyph_atlas_width(const glyph_atlas *a)  { (void)a; return ATLAS_W; }
int  glyph_atlas_height(const glyph_atlas *a) { (void)a; return ATLAS_H; }
int  glyph_atlas_cell_w(const glyph_atlas *a) { return a ? a->cell_w : 0; }
int  glyph_atlas_cell_h(const glyph_atlas *a) { return a ? a->cell_h : 0; }
bool glyph_atlas_dirty(const glyph_atlas *a)  { return a ? a->dirty : false; }
void glyph_atlas_clear_dirty(glyph_atlas *a)  { if (a) { a->dirty = false; a->dirty_y0 = a->dirty_y1 = 0; } }

void glyph_atlas_dirty_rows(const glyph_atlas *a, int *y0, int *y1) {
    if (y0) *y0 = a ? a->dirty_y0 : 0;
    if (y1) *y1 = a ? a->dirty_y1 : 0;
}

const uint8_t *glyph_atlas_color_bitmap(const glyph_atlas *a) { return a ? a->color_bitmap : NULL; }
bool glyph_atlas_color_dirty(const glyph_atlas *a) { return a ? a->color_dirty : false; }
void glyph_atlas_clear_color_dirty(glyph_atlas *a) {
    if (a) { a->color_dirty = false; a->color_dirty_y0 = a->color_dirty_y1 = 0; }
}
void glyph_atlas_color_dirty_rows(const glyph_atlas *a, int *y0, int *y1) {
    if (y0) *y0 = a ? a->color_dirty_y0 : 0;
    if (y1) *y1 = a ? a->color_dirty_y1 : 0;
}
