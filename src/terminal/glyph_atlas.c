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

/* Read an entire font file into a malloc'd buffer. Caller frees. */
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
#define ATLAS_W 1024
#define ATLAS_H 1024

/* ---- hash map: codepoint -> slot index ---- */
#define MAP_CAP 4096   /* power of two; load kept < 0.7 */

typedef struct {
    uint32_t cp;       /* 0 = empty (codepoint 0 is never cached) */
    uint32_t slot;     /* index into slots[] */
} map_entry;

struct glyph_atlas {
    stbtt_fontinfo font;
    const uint8_t *font_data;   /* borrowed (bundled) or owned copy */
    bool           owns_font;

    float  scale;        /* stbtt scale for the requested pixel height */
    int    ascent_px;    /* baseline offset from cell top, in pixels */
    int    cell_w, cell_h;

    uint8_t *bitmap;     /* ATLAS_W * ATLAS_H, 8-bit coverage */
    bool     dirty;

    /* shelf packer cursor */
    int shelf_x, shelf_y, shelf_h;

    glyph_slot *slots;
    int         slot_count, slot_cap;

    map_entry map[MAP_CAP];
};

/* ---- hash ---- */
static inline uint32_t hash_cp(uint32_t cp) {
    cp *= 2654435761u;   /* Knuth multiplicative */
    return cp & (MAP_CAP - 1);
}

static int map_find(glyph_atlas *a, uint32_t cp) {
    uint32_t h = hash_cp(cp);
    for (int probe = 0; probe < MAP_CAP; probe++) {
        map_entry *e = &a->map[h];
        if (e->cp == 0) return -1;         /* empty slot -> not present */
        if (e->cp == cp) return (int)e->slot;
        h = (h + 1) & (MAP_CAP - 1);
    }
    return -1;
}

static void map_insert(glyph_atlas *a, uint32_t cp, uint32_t slot) {
    uint32_t h = hash_cp(cp);
    for (int probe = 0; probe < MAP_CAP; probe++) {
        map_entry *e = &a->map[h];
        if (e->cp == 0 || e->cp == cp) { e->cp = cp; e->slot = slot; return; }
        h = (h + 1) & (MAP_CAP - 1);
    }
    /* map full: silently drop (atlas working set exceeded MAP_CAP, absurd for
     * a terminal). Lookup will just re-rasterize each time — correctness holds. */
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

glyph_atlas *glyph_atlas_create(const uint8_t *font_data, size_t font_len,
                                int cell_w, int cell_h) {
    if (cell_w <= 0 || cell_h <= 0) return NULL;

    glyph_atlas *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->cell_w = cell_w;
    a->cell_h = cell_h;

    if (font_data && font_len) {
        /* Copy so the atlas owns a stable buffer regardless of caller. */
        uint8_t *copy = malloc(font_len);
        if (!copy) { free(a); return NULL; }
        memcpy(copy, font_data, font_len);
        a->font_data = copy;
        a->owns_font = true;
    } else {
        /* No font supplied: search well-known system monospace paths. */
        size_t len = 0;
        uint8_t *loaded = NULL;
        for (int i = 0; kDefaultFontPaths[i]; i++) {
            loaded = read_file(kDefaultFontPaths[i], &len);
            if (loaded) break;
        }
        if (!loaded) { free(a); return NULL; }   /* no usable system font */
        a->font_data = loaded;
        a->owns_font = true;
        font_len = len;
    }

    int off = stbtt_GetFontOffsetForIndex(a->font_data, 0);
    if (off < 0 || !stbtt_InitFont(&a->font, a->font_data, off)) {
        if (a->owns_font) free((void *)a->font_data);
        free(a);
        return NULL;
    }

    /* Scale so the font's cell height maps to cell_h pixels. Terminals use the
     * font's line metrics; we fit ascent+descent into the cell. */
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&a->font, &ascent, &descent, &line_gap);
    /* scale to fit (ascent - descent) into cell_h */
    a->scale = stbtt_ScaleForPixelHeight(&a->font, (float)cell_h);
    a->ascent_px = (int)(ascent * a->scale + 0.5f);

    a->bitmap = calloc((size_t)ATLAS_W * ATLAS_H, 1);
    if (!a->bitmap) {
        if (a->owns_font) free((void *)a->font_data);
        free(a);
        return NULL;
    }
    a->slot_cap = 512;
    a->slots = calloc((size_t)a->slot_cap, sizeof(glyph_slot));
    if (!a->slots) {
        free(a->bitmap);
        if (a->owns_font) free((void *)a->font_data);
        free(a);
        return NULL;
    }

    a->shelf_x = a->shelf_y = a->shelf_h = 0;
    a->dirty = true;   /* empty upload is fine; first glyphs mark it too */
    return a;
}

void glyph_atlas_destroy(glyph_atlas *a) {
    if (!a) return;
    free(a->bitmap);
    free(a->slots);
    if (a->owns_font) free((void *)a->font_data);
    free(a);
}

/* Rasterize + insert a codepoint; returns its slot index (or a blank slot). */
static int rasterize(glyph_atlas *a, uint32_t cp) {
    /* TUI drawing ranges (box/block/braille) are synthesized to fill the whole
     * cell edge-to-edge, so meters and sparklines tile with no seams. This runs
     * BEFORE the font path — the font's own versions are inset/bearing-placed
     * and render as small misaligned marks (the exact sparkline breakage a data
     * TUI shows otherwise). */
    if (glyph_synth_has(cp)) {
        int cw = a->cell_w, ch = a->cell_h;
        uint8_t *tmp = malloc((size_t)cw * ch);
        if (tmp && glyph_synth_render(cp, tmp, cw, ch)) {
            uint16_t ox, oy;
            if (!atlas_reserve(a, cw + 1, ch + 1, &ox, &oy)) { free(tmp); goto blank; }
            for (int y = 0; y < ch; y++)
                memcpy(a->bitmap + (size_t)(oy + y) * ATLAS_W + ox,
                       tmp + (size_t)y * cw, (size_t)cw);
            a->dirty = true;
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
            map_insert(a, cp, (uint32_t)a->slot_count);
            return a->slot_count++;
        }
        free(tmp);
        /* synth declined (e.g. a box glyph outside our subset): fall through to
         * the font. */
    }

    int gi = stbtt_FindGlyphIndex(&a->font, (int)cp);
    if (gi == 0) {
        /* no glyph in font (e.g. .notdef) — cache a blank so we don't retry */
        goto blank;
    }

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&a->font, gi, a->scale, a->scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;

    if (gw <= 0 || gh <= 0) {
        /* whitespace (space, etc.) — valid but no ink */
        if (a->slot_count >= a->slot_cap) goto blank;
        glyph_slot *s = &a->slots[a->slot_count];
        memset(s, 0, sizeof(*s));
        s->valid = true;
        map_insert(a, cp, (uint32_t)a->slot_count);
        return a->slot_count++;
    }

    uint16_t ox, oy;
    if (!atlas_reserve(a, gw + 1, gh + 1, &ox, &oy)) goto blank;  /* +1 gutter */

    /* Rasterize directly into the atlas at (ox,oy), respecting atlas stride. */
    stbtt_MakeGlyphBitmap(&a->font,
                          a->bitmap + (size_t)oy * ATLAS_W + ox,
                          gw, gh, ATLAS_W,       /* out_stride = atlas width */
                          a->scale, a->scale, gi);
    a->dirty = true;

    if (a->slot_count >= a->slot_cap) {
        int ncap = a->slot_cap * 2;
        glyph_slot *ns = realloc(a->slots, (size_t)ncap * sizeof(glyph_slot));
        if (!ns) goto blank;
        a->slots = ns;
        a->slot_cap = ncap;
    }
    glyph_slot *s = &a->slots[a->slot_count];
    s->x = ox; s->y = oy; s->w = (uint16_t)gw; s->h = (uint16_t)gh;
    s->off_x = (int16_t)x0;                       /* glyph left bearing */
    s->off_y = (int16_t)(a->ascent_px + y0);      /* top of ink from cell top */
    s->valid = true;
    map_insert(a, cp, (uint32_t)a->slot_count);
    return a->slot_count++;

blank:
    if (a->slot_count >= a->slot_cap) return -1;
    {
        glyph_slot *b = &a->slots[a->slot_count];
        memset(b, 0, sizeof(*b));
        b->valid = false;
        map_insert(a, cp, (uint32_t)a->slot_count);
        return a->slot_count++;
    }
}

const glyph_slot *glyph_atlas_get(glyph_atlas *a, uint32_t cp) {
    static const glyph_slot blank = {0};
    if (!a || cp == 0) return &blank;
    int idx = map_find(a, cp);
    if (idx < 0) idx = rasterize(a, cp);
    if (idx < 0 || idx >= a->slot_count) return &blank;
    return &a->slots[idx];
}

const uint8_t *glyph_atlas_bitmap(const glyph_atlas *a) { return a ? a->bitmap : NULL; }
int  glyph_atlas_width(const glyph_atlas *a)  { (void)a; return ATLAS_W; }
int  glyph_atlas_height(const glyph_atlas *a) { (void)a; return ATLAS_H; }
int  glyph_atlas_cell_w(const glyph_atlas *a) { return a ? a->cell_w : 0; }
int  glyph_atlas_cell_h(const glyph_atlas *a) { return a ? a->cell_h : 0; }
bool glyph_atlas_dirty(const glyph_atlas *a)  { return a ? a->dirty : false; }
void glyph_atlas_clear_dirty(glyph_atlas *a)  { if (a) a->dirty = false; }
