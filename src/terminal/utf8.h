/*
 * utf8.h — incremental UTF-8 decoder (private to the terminal module)
 *
 * A PTY delivers bytes in arbitrary chunks: a multibyte codepoint (box-drawing,
 * blocks, emoji) can straddle two read()s. So the decoder is a small resumable
 * state machine — you push one byte at a time and it emits a codepoint only
 * when a full sequence has arrived.
 *
 * Correctness (per the Unicode standard, matching the well-known Björn Höhrmann
 * DFA semantics but written out longhand here so it stays dependency-free):
 *   - rejects overlong encodings, surrogate-range (D800..DFFF) and > U+10FFFF
 *   - on ANY malformed byte, emits U+FFFD and RESUMES cleanly (a bad byte never
 *     desynchronises the stream — the offending lead byte is re-fed if it could
 *     itself start a valid sequence, matching WHATWG replacement behaviour)
 */
#ifndef NEOWALL_TERMINAL_UTF8_H
#define NEOWALL_TERMINAL_UTF8_H

#include <stdint.h>
#include <stdbool.h>

#define UTF8_REPLACEMENT 0xFFFDu

typedef struct utf8_decoder {
    uint32_t cp;        /* codepoint accumulated so far */
    uint8_t  need;      /* continuation bytes still expected */
    uint8_t  seen;      /* continuation bytes consumed for current cp */
    uint32_t lo, hi;    /* valid codepoint range for the current lead (overlong guard) */
} utf8_decoder;

static inline void utf8_reset(utf8_decoder *d) {
    d->cp = 0; d->need = 0; d->seen = 0; d->lo = 0; d->hi = 0;
}

/* Push one byte. Returns:
 *   1  → *out_cp holds a finished codepoint (valid scalar, or U+FFFD on error)
 *   0  → sequence continues, need more bytes
 *  -1  → this byte was rejected AND *out_cp = U+FFFD was emitted for the
 *        aborted sequence; the caller should RE-FEED this same byte (it may be
 *        a fresh lead byte). This is how we resync without dropping data.
 */
static inline int utf8_push(utf8_decoder *d, uint8_t b, uint32_t *out_cp) {
    if (d->need == 0) {
        /* Expecting a lead byte. */
        if (b < 0x80u) {                 /* ASCII fast path */
            *out_cp = b;
            return 1;
        } else if ((b & 0xE0u) == 0xC0u) {   /* 110xxxxx → 2-byte */
            d->cp = b & 0x1Fu; d->need = 1; d->seen = 0; d->lo = 0x80; d->hi = 0x7FF;
            return 0;
        } else if ((b & 0xF0u) == 0xE0u) {   /* 1110xxxx → 3-byte */
            d->cp = b & 0x0Fu; d->need = 2; d->seen = 0; d->lo = 0x800; d->hi = 0xFFFF;
            return 0;
        } else if ((b & 0xF8u) == 0xF0u) {   /* 11110xxx → 4-byte */
            d->cp = b & 0x07u; d->need = 3; d->seen = 0; d->lo = 0x10000; d->hi = 0x10FFFF;
            return 0;
        }
        /* 0x80..0xBF stray continuation, or 0xF8..0xFF invalid lead. */
        *out_cp = UTF8_REPLACEMENT;
        return 1;
    }

    /* Expecting a continuation byte 10xxxxxx. */
    if ((b & 0xC0u) != 0x80u) {
        /* Malformed: the sequence is aborted. Emit U+FFFD and ask the caller to
         * re-feed this byte, which may legitimately start a new sequence. */
        utf8_reset(d);
        *out_cp = UTF8_REPLACEMENT;
        return -1;
    }

    d->cp = (d->cp << 6) | (uint32_t)(b & 0x3Fu);
    d->seen++;
    if (d->seen < d->need) {
        return 0;
    }

    /* Full sequence assembled — validate. */
    uint32_t cp = d->cp;
    uint32_t lo = d->lo, hi = d->hi;
    utf8_reset(d);

    if (cp < lo || cp > hi ||                    /* overlong / out of range */
        (cp >= 0xD800u && cp <= 0xDFFFu)) {      /* UTF-16 surrogate half */
        *out_cp = UTF8_REPLACEMENT;
        return 1;
    }
    *out_cp = cp;
    return 1;
}

#endif /* NEOWALL_TERMINAL_UTF8_H */
