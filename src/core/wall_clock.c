/* Virtual shader clock. See include/neowall/core/wall_clock.h for the rationale. */

#define _POSIX_C_SOURCE 200809L

#include "neowall/core/wall_clock.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Offset applied to real time, in seconds. */
static atomic_long g_offset_sec = 0;

/* Rate multiplier for shader time. */
static _Atomic double g_scale = 1.0;

/* When the current scale took effect: the monotonic time then, and the shader
 * time it corresponded to. Elapsed time is measured on the monotonic clock,
 * in nanoseconds, so a fast timelapse moves smoothly every frame instead of
 * jumping once per second, and a wall-clock step (NTP) doesn't jolt it.
 * Written only by nw_clock_set_*, at startup, before any reader thread. */
static _Atomic int64_t g_anchor_mono_ns = 0;
static _Atomic double g_anchor_virtual  = 0.0;

static int64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double real_now_f(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Shader time before the offset, in seconds. */
static double virtual_base(void) {
    double scale = atomic_load_explicit(&g_scale, memory_order_acquire);
    if (scale == 1.0) return real_now_f();
    int64_t since = mono_ns() - atomic_load_explicit(&g_anchor_mono_ns, memory_order_relaxed);
    return atomic_load_explicit(&g_anchor_virtual, memory_order_relaxed) +
           (double)since / 1e9 * scale;
}

time_t nw_clock_now(void) {
    /* Fast path: an untouched clock is the system clock. */
    if (atomic_load_explicit(&g_scale, memory_order_relaxed) == 1.0 &&
        atomic_load_explicit(&g_offset_sec, memory_order_relaxed) == 0) {
        return time(NULL);
    }
    return (time_t)(virtual_base() +
                    (double)atomic_load_explicit(&g_offset_sec, memory_order_relaxed));
}

double nw_clock_now_f(void) {
    return virtual_base() + (double)atomic_load_explicit(&g_offset_sec, memory_order_relaxed);
}

void nw_clock_set_offset(long seconds) {
    atomic_store_explicit(&g_offset_sec, seconds, memory_order_relaxed);
}

void nw_clock_set_scale(double scale) {
    if (!(scale > 0.0)) return;
    /* Rebase so the change in rate doesn't move the current instant: the
     * future runs at the new speed, the past stays. The anchor is published
     * before the scale (release), so a reader that sees the scale sees it. */
    double base = virtual_base();
    atomic_store_explicit(&g_anchor_mono_ns, mono_ns(), memory_order_relaxed);
    atomic_store_explicit(&g_anchor_virtual, base, memory_order_relaxed);
    atomic_store_explicit(&g_scale, scale, memory_order_release);
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
    case 's':
    case 'S': return 1;
    case 'm':
    case 'M': return 60;
    case 'h':
    case 'H': return 3600;
    case 'd':
    case 'D': return 86400;
    case 'w':
    case 'W': return 604800;
    default: return 0;
    }
}

/* Parse "<number><suffix>" into seconds. A bare number means seconds. */
static bool parse_duration(const char *spec, double *out_seconds) {
    if (!spec || !*spec) return false;

    char *end    = NULL;
    double value = strtod(spec, &end);
    if (end == spec) return false;

    while (*end && isspace((unsigned char)*end))
        end++;

    long mult = 1;
    if (*end) {
        mult = suffix_scale(*end);
        if (mult == 0) return false;
        end++;
        while (*end && isspace((unsigned char)*end))
            end++;
        if (*end) return false; /* trailing garbage */
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
