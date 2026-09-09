#ifndef NEOWALL_CONSTANTS_H
#define NEOWALL_CONSTANTS_H

/* ============================================================================
 * NeoWall Constants - Single Source of Truth
 * ============================================================================
 * Centralized constants to eliminate magic numbers and improve maintainability
 * ============================================================================ */

/* ============================================================================
 * Time Constants (in milliseconds)
 * ============================================================================ */
#define MS_PER_SECOND           1000ULL
#define NS_PER_MS               1000000ULL
#define MS_PER_NANOSECOND       1000000ULL

/* Animation and transition timings */
#define FPS_TARGET              60
#define FRAME_TIME_MS           16        /* ~60 FPS (1000/60) - Smooth animations */
#define DEFAULT_TRANSITION_MS   300
#define SHADER_FADE_IN_MS       600
#define SHADER_FADE_OUT_MS      400

/* Polling and sleep intervals */
#define POLL_TIMEOUT_INFINITE   (-1)
#define SLEEP_100MS_NS          100000000  /* 100ms in nanoseconds */
#define STATS_INTERVAL_MS       10000      /* Print stats every 10 seconds */

/* ============================================================================
 * Limits and Thresholds
 * ============================================================================ */
#define MAX_NEXT_REQUESTS       100       /* Maximum queued 'next' wallpaper requests */
#define DAEMON_SHUTDOWN_TIMEOUT 50        /* Max attempts to wait for daemon shutdown */
#define ALPHA_OPAQUE            255       /* Fully opaque alpha value */

/* ============================================================================
 * OpenGL/Shader Version
 * ============================================================================ */
#define GLSL_VERSION_STRING     "#version 330 core\n"   /* OpenGL 3.3 Core */
#define GLSL_VERSION_LINE       "#version 330 core\\n"  /* For concatenated strings */

/* ============================================================================
 * Default Values
 * ============================================================================ */
#define DEFAULT_SHADER_SPEED    1.0f
#define MIN_SHADER_SPEED        0.1f
#define SHADER_SPEED_INCREMENT  1.0f

/* Interval at which the shader clock epoch is rebased, in seconds.
 *
 * iTime reaches GL as a float32, which loses absolute precision as it grows:
 * the gap between representable values is ~0.24 ms after an hour of uptime and
 * ~7.8 ms after a day. A frame at 60 FPS is 16.7 ms, so a day-old wallpaper can
 * express only ~2 distinct time values per frame and smooth motion visibly
 * stairsteps. A wallpaper runs for days, so unlike Shadertoy this is a real
 * failure mode rather than a theoretical one.
 *
 * Naively wrapping iTime does NOT work: shaders use fractional frequencies
 * (sin(0.9*iTime)), so any fixed period leaves a phase discontinuity for all
 * but exact-integer rates -- a visible jolt every wrap. Instead the renderer
 * rebases only while the shader is NOT visible (occluded or paused), where a
 * discontinuity cannot be seen, and otherwise lets iTime run. That bounds the
 * magnitude in practice without ever introducing a visible jump.
 *
 * Shaders needing genuinely long-scale time should read the calendar uniforms
 * (iDate/iTimeOfDay/iDayFraction), which are unwrapped at the source. */
#define SHADER_TIME_REBASE_SECONDS 3600.0

/* Resolve a config shader_fps value with the project default fallback.
 * Centralized so we don't have four copies of `? FPS_TARGET` / `? 60`. */
static inline int shader_fps_resolve(int configured) {
    return configured > 0 ? configured : FPS_TARGET;
}

#endif /* NEOWALL_CONSTANTS_H */
