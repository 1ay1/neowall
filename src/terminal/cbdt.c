/*
 * cbdt.c — see cbdt.h. Self-contained sfnt + cmap + CBLC/CBDT (+ sbix) reader
 * that yields decoded RGBA emoji pixels. PNG decode is delegated to the vendored
 * stb_image (public-domain, PNG-only build).
 */
#include "cbdt.h"

#include <stdlib.h>
#include <string.h>

/* PNG-only, memory-only, no stdio: keeps the vendored decoder lean and never
 * touches the filesystem behind our back. This TU owns the implementation. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_GIF
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_NO_JPEG
#include "vendor/stb_image.h"

/* ---- big-endian readers over a bounds-checked window ---- */
typedef struct { const uint8_t *p; size_t len; } buf;

static inline uint8_t  rd8 (const uint8_t *p) { return p[0]; }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
/* safe read: returns 0 if [off, off+n) is out of range */
static inline bool in_range(const buf *b, size_t off, size_t n) {
    return off <= b->len && n <= b->len - off;
}

/* ---- cmap: codepoint -> glyph id (formats 12, 4, 6) -------------------- */
typedef struct {
    const uint8_t *sub;   /* start of the chosen cmap subtable */
    size_t         sublen;
    uint16_t       format;
} cmap_t;

struct cbdt_font {
    buf       file;
    cmap_t    cmap;
    /* CBLC/CBDT */
    const uint8_t *cblc; size_t cblc_len;
    const uint8_t *cbdt; size_t cbdt_len;
    /* sbix */
    const uint8_t *sbix; size_t sbix_len;
    uint16_t       num_glyphs;
    int            ppem;
};

/* Find a table by 4-char tag in the sfnt directory. */
static bool find_table(const buf *file, const char *tag,
                       const uint8_t **out, size_t *out_len) {
    if (file->len < 12) return false;
    uint32_t sfnt = rd32(file->p);
    /* 0x00010000 = TrueType, 'OTTO' = CFF, 'ttcf' = collection (unsupported here) */
    if (sfnt == 0x74746366u) return false; /* 'ttcf': collections not handled */
    uint16_t num = rd16(file->p + 4);
    size_t dir = 12;
    for (uint16_t i = 0; i < num; i++) {
        size_t rec = dir + (size_t)i * 16;
        if (!in_range(file, rec, 16)) return false;
        const uint8_t *r = file->p + rec;
        if (memcmp(r, tag, 4) == 0) {
            uint32_t off = rd32(r + 8), len = rd32(r + 12);
            if (!in_range(file, off, len)) return false;
            *out = file->p + off;
            *out_len = len;
            return true;
        }
    }
    return false;
}

/* Pick the best Unicode cmap subtable (prefer format 12 full-repertoire). */
static bool load_cmap(cbdt_font *f) {
    const uint8_t *cmap; size_t clen;
    if (!find_table(&f->file, "cmap", &cmap, &clen)) return false;
    if (clen < 4) return false;
    uint16_t ntab = rd16(cmap + 2);
    const uint8_t *best = NULL; int best_score = -1;
    for (uint16_t i = 0; i < ntab; i++) {
        size_t rec = 4 + (size_t)i * 8;
        if (rec + 8 > clen) break;
        uint16_t pid = rd16(cmap + rec);
        uint16_t eid = rd16(cmap + rec + 2);
        uint32_t off = rd32(cmap + rec + 4);
        if (off + 2 > clen) continue;
        uint16_t fmt = rd16(cmap + off);
        int score = -1;
        /* Prefer Unicode full (pid3/eid10 or pid0/eid4+), format 12. */
        bool unicode = (pid == 0) || (pid == 3 && (eid == 1 || eid == 10));
        if (!unicode) continue;
        if (fmt == 12) score = 3;
        else if (fmt == 4) score = 2;
        else if (fmt == 6) score = 1;
        else continue;
        if (pid == 3 && eid == 10) score += 4;  /* full repertoire beats BMP */
        if (score > best_score) { best_score = score; best = cmap + off; f->cmap.sublen = clen - off; }
    }
    if (!best) return false;
    f->cmap.sub = best;
    f->cmap.format = rd16(best);
    return true;
}

static uint32_t cmap_lookup(const cmap_t *c, uint32_t cp) {
    const uint8_t *s = c->sub;
    if (c->format == 12) {
        uint32_t ngroups = rd32(s + 12);
        /* groups: startChar, endChar, startGlyph (12 bytes each), sorted */
        uint32_t lo = 0, hi = ngroups;
        while (lo < hi) {
            uint32_t mid = lo + ((hi - lo) >> 1);
            const uint8_t *g = s + 16 + (size_t)mid * 12;
            uint32_t sc = rd32(g), ec = rd32(g + 4);
            if (cp < sc) hi = mid;
            else if (cp > ec) lo = mid + 1;
            else return rd32(g + 8) + (cp - sc);
        }
        return 0;
    } else if (c->format == 4) {
        if (cp > 0xFFFF) return 0;
        uint16_t segX2 = rd16(s + 6);
        uint16_t segcount = segX2 / 2;
        const uint8_t *endc = s + 14;
        const uint8_t *startc = endc + segX2 + 2;
        const uint8_t *iddelta = startc + segX2;
        const uint8_t *idrange = iddelta + segX2;
        for (uint16_t i = 0; i < segcount; i++) {
            uint16_t end = rd16(endc + i * 2);
            if (cp <= end) {
                uint16_t start = rd16(startc + i * 2);
                if (cp < start) return 0;
                int16_t delta = (int16_t)rd16(iddelta + i * 2);
                uint16_t ro = rd16(idrange + i * 2);
                if (ro == 0) return (uint16_t)(cp + delta);
                const uint8_t *gp = idrange + i * 2 + ro + (cp - start) * 2;
                uint16_t g = rd16(gp);
                if (g == 0) return 0;
                return (uint16_t)(g + delta);
            }
        }
        return 0;
    } else if (c->format == 6) {
        uint16_t first = rd16(s + 6);
        uint16_t count = rd16(s + 8);
        if (cp < first || cp >= (uint32_t)first + count) return 0;
        return rd16(s + 10 + (cp - first) * 2);
    }
    return 0;
}

/* ---- CBLC: glyph id -> (CBDT offset, length, image format) ------------- */
/* Returns true and fills *img_off (into CBDT), *img_len, *img_fmt. */
static bool cblc_locate(const cbdt_font *f, uint32_t gid,
                        uint32_t *img_off, uint32_t *img_len, uint16_t *img_fmt,
                        uint8_t *ppem_out) {
    const uint8_t *t = f->cblc;
    size_t tlen = f->cblc_len;
    if (tlen < 8) return false;
    uint32_t numSizes = rd32(t + 4);
    for (uint32_t si = 0; si < numSizes; si++) {
        size_t st = 8 + (size_t)si * 48;       /* bitmapSizeTable = 48 bytes */
        if (st + 48 > tlen) break;
        uint32_t idxArrayOff = rd32(t + st + 0);   /* indexSubTableArrayOffset (rel to CBLC) */
        uint32_t numIdxSub   = rd32(t + st + 8);
        uint16_t startGID    = rd16(t + st + 40);
        uint16_t endGID      = rd16(t + st + 42);
        uint8_t  ppemx       = t[st + 44];
        if (gid < startGID || gid > endGID) continue;
        /* walk indexSubTableArray records: {firstGlyph, lastGlyph, additionalOffset} */
        for (uint32_t k = 0; k < numIdxSub; k++) {
            size_t ar = idxArrayOff + (size_t)k * 8;
            if (ar + 8 > tlen) break;
            uint16_t first = rd16(t + ar);
            uint16_t last  = rd16(t + ar + 2);
            uint32_t addl  = rd32(t + ar + 4);
            if (gid < first || gid > last) continue;
            size_t ist = idxArrayOff + addl;       /* indexSubTable (rel to CBLC) */
            if (ist + 8 > tlen) return false;
            uint16_t idxFormat = rd16(t + ist);
            uint16_t imgFormat = rd16(t + ist + 2);
            uint32_t imageDataOff = rd32(t + ist + 4);  /* into CBDT */
            uint32_t gi = gid - first;
            if (idxFormat == 1) {
                /* uint32 offsets[last-first+2] follow */
                size_t oo = ist + 8 + (size_t)gi * 4;
                if (oo + 8 > tlen) return false;
                uint32_t o0 = rd32(t + oo);
                uint32_t o1 = rd32(t + oo + 4);
                if (o1 <= o0) return false;
                *img_off = imageDataOff + o0;
                *img_len = o1 - o0;
            } else if (idxFormat == 2) {
                /* constant-size images: imageSize(u32), bigMetrics(8) */
                uint32_t imageSize = rd32(t + ist + 8);
                *img_off = imageDataOff + gi * imageSize;
                *img_len = imageSize;
            } else if (idxFormat == 3) {
                /* uint16 offsets[last-first+2] */
                size_t oo = ist + 8 + (size_t)gi * 2;
                if (oo + 4 > tlen) return false;
                uint16_t o0 = rd16(t + oo);
                uint16_t o1 = rd16(t + oo + 2);
                if (o1 <= o0) return false;
                *img_off = imageDataOff + o0;
                *img_len = (uint32_t)(o1 - o0);
            } else if (idxFormat == 4) {
                /* glyphArray: header {indexFormat(2) imageFormat(2)
                 * imageDataOffset(4) numGlyphs(4)} then {glyphID(u16),
                 * offset(u16)} pairs (numGlyphs+1 of them, last is a sentinel).
                 * Rare for emoji (Noto uses format 1); guarded conservatively. */
                uint32_t ndat = rd32(t + ist + 8);
                for (uint32_t g = 0; g < ndat; g++) {
                    size_t pr = ist + 12 + (size_t)g * 4;
                    if (pr + 8 > tlen) return false;   /* need this pair + next offset */
                    uint16_t gid_e = rd16(t + pr);
                    if (gid_e == gid) {
                        uint16_t o0 = rd16(t + pr + 2);
                        uint16_t o1 = rd16(t + pr + 6);
                        if (o1 <= o0) return false;
                        *img_off = imageDataOff + o0;
                        *img_len = (uint32_t)(o1 - o0);
                        *img_fmt = imgFormat;
                        if (ppem_out) *ppem_out = ppemx;
                        return true;
                    }
                }
                return false;
            } else {
                return false;
            }
            *img_fmt = imgFormat;
            if (ppem_out) *ppem_out = ppemx;
            return true;
        }
    }
    return false;
}

/* CBDT glyph image formats 17/18/19 carry small/big glyph metrics then a
 * uint32 dataLen + a PNG. Return the PNG window. */
static bool cbdt_png_window(const cbdt_font *f, uint32_t img_off, uint32_t img_len,
                            uint16_t img_fmt, const uint8_t **png, uint32_t *png_len) {
    if ((size_t)img_off + img_len > f->cbdt_len) return false;
    const uint8_t *d = f->cbdt + img_off;
    size_t hdr;
    if (img_fmt == 17) hdr = 5;        /* smallGlyphMetrics(5) */
    else if (img_fmt == 18) hdr = 8;   /* bigGlyphMetrics(8) */
    else if (img_fmt == 19) hdr = 0;   /* metrics carried in CBLC */
    else return false;
    if (hdr + 4 > img_len) return false;
    uint32_t dlen = rd32(d + hdr);
    if (hdr + 4 + dlen > img_len) return false;
    *png = d + hdr + 4;
    *png_len = dlen;
    return true;
}

/* ---- sbix (Apple color bitmaps): glyph id -> PNG ----------------------- */
static bool sbix_png_window(const cbdt_font *f, uint32_t gid,
                            const uint8_t **png, uint32_t *png_len) {
    const uint8_t *t = f->sbix;
    size_t tlen = f->sbix_len;
    if (tlen < 8) return false;
    uint32_t numStrikes = rd32(t + 4);
    /* choose the largest strike (last-ish with biggest ppem) */
    uint32_t bestOff = 0, bestPpem = 0;
    for (uint32_t i = 0; i < numStrikes; i++) {
        size_t so = 8 + (size_t)i * 4;
        if (so + 4 > tlen) break;
        uint32_t strikeOff = rd32(t + so);
        if (strikeOff + 4 > tlen) continue;
        uint16_t ppem = rd16(t + strikeOff);
        if (ppem >= bestPpem) { bestPpem = ppem; bestOff = strikeOff; }
    }
    if (!bestOff) return false;
    /* glyphDataOffsets: uint32[numGlyphs+1] after ppem(2)+resolution(2) */
    size_t base = bestOff + 4;
    size_t oo = base + (size_t)gid * 4;
    if (oo + 8 > tlen) return false;
    uint32_t o0 = rd32(t + oo), o1 = rd32(t + oo + 4);
    if (o1 <= o0) return false;
    size_t rec = bestOff + o0;
    /* glyph record: originOffsetX(2) originOffsetY(2) graphicType(4) then data */
    if (rec + 8 > tlen) return false;
    *png = t + rec + 8;
    *png_len = (o1 - o0) - 8;
    return true;
}

/* ---- public API -------------------------------------------------------- */
cbdt_font *cbdt_open(const uint8_t *data, size_t len) {
    if (!data || len < 12) return NULL;
    cbdt_font *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->file.p = data; f->file.len = len;

    if (!load_cmap(f)) { free(f); return NULL; }

    size_t l;
    bool has_cbdt = find_table(&f->file, "CBLC", &f->cblc, &f->cblc_len) &&
                    find_table(&f->file, "CBDT", &f->cbdt, &f->cbdt_len);
    bool has_sbix = find_table(&f->file, "sbix", &f->sbix, &f->sbix_len);
    if (!has_cbdt && !has_sbix) { free(f); return NULL; }

    const uint8_t *maxp; size_t maxlen;
    if (find_table(&f->file, "maxp", &maxp, &maxlen) && maxlen >= 6)
        f->num_glyphs = rd16(maxp + 4);

    /* record native strike ppem for scale planning */
    f->ppem = 128; /* NotoColorEmoji default; refined below if CBLC present */
    if (has_cbdt && f->cblc_len >= 8) {
        uint32_t numSizes = rd32(f->cblc + 4);
        if (numSizes >= 1 && f->cblc_len >= 8 + 48) {
            uint8_t ppemx = f->cblc[8 + 44];
            if (ppemx) f->ppem = ppemx;
        }
    }
    (void)l;
    return f;
}

void cbdt_close(cbdt_font *f) { free(f); }

int cbdt_ppem(const cbdt_font *f) { return f ? f->ppem : 0; }

bool cbdt_has(const cbdt_font *f, uint32_t cp) {
    if (!f) return false;
    uint32_t gid = cmap_lookup(&f->cmap, cp);
    if (gid == 0) return false;
    if (f->cbdt) {
        uint32_t io, il; uint16_t fmt; uint8_t pp;
        if (cblc_locate(f, gid, &io, &il, &fmt, &pp)) return true;
    }
    if (f->sbix) {
        const uint8_t *png; uint32_t pl;
        if (sbix_png_window(f, gid, &png, &pl)) return true;
    }
    return false;
}

uint8_t *cbdt_render(const cbdt_font *f, uint32_t cp, int *out_w, int *out_h) {
    if (!f) return NULL;
    uint32_t gid = cmap_lookup(&f->cmap, cp);
    if (gid == 0) return NULL;

    const uint8_t *png = NULL; uint32_t png_len = 0;
    if (f->cbdt) {
        uint32_t io, il; uint16_t fmt; uint8_t pp;
        if (cblc_locate(f, gid, &io, &il, &fmt, &pp)) {
            if (fmt == 17 || fmt == 18 || fmt == 19)
                cbdt_png_window(f, io, il, fmt, &png, &png_len);
        }
    }
    if (!png && f->sbix)
        sbix_png_window(f, gid, &png, &png_len);
    if (!png || png_len < 8) return NULL;

    int w = 0, h = 0, ch = 0;
    /* force 4 channels (RGBA), straight alpha */
    uint8_t *rgba = stbi_load_from_memory(png, (int)png_len, &w, &h, &ch, 4);
    if (!rgba) return NULL;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return rgba;   /* caller frees with free() (stbi uses malloc by default) */
}
