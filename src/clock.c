/* Virtual shader clock. See include/neowall/clock.h for the rationale. */

#define _POSIX_C_SOURCE 200809L

#include "neowall/clock.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Offset applied to real time, in seconds. */
static atomic_long g_offset_sec = 0;

/* Rate multiplier for shader time. Stored as the bit pattern of a double so it
 * can live in an atomic without a lock; only ever written whole. */
static _Atomic double g_scale = 1.0;

/* Real time at which the current scale took effect, and the shader time that
 * corresponded to it. Rebasing on every scale change keeps the clock monotonic:
 * elapsed real time since the anchor is what gets multiplied, so switching from
 * 1x to 100x speeds up the future without rewriting the past. */
static atomic_long g_anchor_real = 0;
static atomic_long g_anchor_virtual = 0;

static time_t real_now(void) {
    return time(NULL);
}

/* Establish the anchor lazily on first use so a daemon that never time-travels
 * pays nothing and returns exactly time(NULL). */
static void ensure_anchor(void) {
    if (atomic_load_explicit(&g_anchor_real, memory_order_relaxed) == 0) {
        time_t now = real_now();
        atomic_store_explicit(&g_anchor_real, (long)now, memory_order_relaxed);
        atomic_store_explicit(&g_anchor_virtual, (long)now, memory_order_relaxed);
    }
}

time_t nw_clock_now(void) {
    double scale = atomic_load_explicit(&g_scale, memory_order_relaxed);
    long offset  = atomic_load_explicit(&g_offset_sec, memory_order_relaxed);

    /* Fast path: an untouched clock is the system clock. */
    if (scale == 1.0 && offset == 0) {
        return real_now();
    }

    ensure_anchor();
    long anchor_real    = atomic_load_explicit(&g_anchor_real, memory_order_relaxed);
    long anchor_virtual = atomic_load_explicit(&g_anchor_virtual, memory_order_relaxed);

    long elapsed = (long)real_now() - anchor_real;
    if (elapsed < 0) elapsed = 0;   /* system clock stepped backwards; hold */

    double virtual_now = (double)anchor_virtual + (double)elapsed * scale;
    return (time_t)(virtual_now + (double)offset);
}

void nw_clock_set_offset(long seconds) {
    atomic_store_explicit(&g_offset_sec, seconds, memory_order_relaxed);
    ensure_anchor();
}

void nw_clock_set_scale(double scale) {
    if (!(scale > 0.0)) return;

    /* Rebase so the jump in rate does not move the current instant. */
    ensure_anchor();
    time_t vnow_before = nw_clock_now();
    long   offset      = atomic_load_explicit(&g_offset_sec, memory_order_relaxed);

    atomic_store_explicit(&g_anchor_real, (long)real_now(), memory_order_relaxed);
    atomic_store_explicit(&g_anchor_virtual, (long)vnow_before - offset, memory_order_relaxed);
    atomic_store_explicit(&g_scale, scale, memory_order_relaxed);
}

long nw_clock_offset(void) {
    return atomic_load_explicit(&g_offset_sec, memory_order_relaxed);
}

double nw_clock_scale(void) {
    return atomic_load_explicit(&g_scale, memory_order_relaxed);
}

bool nw_clock_is_virtual(void) {
    return atomic_load_explicit(&g_offset_sec, memory_order_relaxed) != 0 ||
           atomic_load_explicit(&g_scale, memory_order_relaxed) != 1.0;
}

/* Multiplier for a duration suffix; 0 means "unknown suffix". */
static long suffix_scale(char c) {
    switch (c) {
        case 's': case 'S': return 1;
        case 'm': case 'M': return 60;
        case 'h': case 'H': return 3600;
        case 'd': case 'D': return 86400;
        case 'w': case 'W': return 604800;
        default:            return 0;
    }
}

/* Parse "<number><suffix>" into seconds. A bare number means seconds. */
static bool parse_duration(const char *spec, double *out_seconds) {
    if (!spec || !*spec) return false;

    char *end = NULL;
    double value = strtod(spec, &end);
    if (end == spec) return false;

    while (*end && isspace((unsigned char)*end)) end++;

    long mult = 1;
    if (*end) {
        mult = suffix_scale(*end);
        if (mult == 0) return false;
        end++;
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end) return false;      /* trailing garbage */
    }

    *out_seconds = value * (double)mult;
    return true;
}

bool nw_clock_parse_offset(const char *spec, long *out_seconds) {
    double secs = 0.0;
    if (!parse_duration(spec, &secs)) return false;
    if (out_seconds) *out_seconds = (long)secs;
    return true;
}

bool nw_clock_parse_timelapse(const char *spec, double *out_scale) {
    if (!spec) return false;

    const char *slash = strchr(spec, '/');
    if (!slash || slash == spec || !slash[1]) return false;

    char span_buf[64];
    size_t span_len = (size_t)(slash - spec);
    if (span_len >= sizeof(span_buf)) return false;
    memcpy(span_buf, spec, span_len);
    span_buf[span_len] = '\0';

    double span = 0.0, duration = 0.0;
    if (!parse_duration(span_buf, &span)) return false;
    if (!parse_duration(slash + 1, &duration)) return false;
    if (!(span > 0.0) || !(duration > 0.0)) return false;

    if (out_scale) *out_scale = span / duration;
    return true;
}
