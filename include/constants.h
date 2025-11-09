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
#define POLL_TIMEOUT_INFINITE   -1
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
#define GLSL_VERSION_STRING     "#version 100\n"        /* OpenGL ES 2.0 */
#define GLSL_VERSION_LINE       "#version 100\\n"       /* For concatenated strings */

/* ============================================================================
 * Default Values
 * ============================================================================ */
#define DEFAULT_SHADER_SPEED    1.0f
#define MIN_SHADER_SPEED        0.1f
#define SHADER_SPEED_INCREMENT  1.0f

#endif /* NEOWALL_CONSTANTS_H */
