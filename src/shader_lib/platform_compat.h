/* Platform Compatibility Header
 * Provides compatibility layer for shader library
 * Uses desktop OpenGL for Shadertoy compatibility
 */

#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>

/* ============================================
 * Unified OpenGL Header Include
 * ============================================ */

/* Use desktop OpenGL for full Shadertoy shader compatibility.
 * Desktop OpenGL 3.3 Core supports #version 330 core shaders
 * which is what Shadertoy uses. */
#include <GL/gl.h>
#include <GL/glext.h>

/* Platform detection */
#ifdef _WIN32
#define PLATFORM_WINDOWS
#else
#define PLATFORM_UNIX
#endif

/* When building within neowall, NEOWALL_VERSION is defined by meson.
 * In that case, we use neowall's logging functions declared in neowall.h.
 * When building standalone (gleditor), we use shader_log.h's implementation.
 */
#ifdef NEOWALL_VERSION
/* Neowall context - logging functions are declared elsewhere (neowall.h/utils.c) */
/* Just declare them as extern so shader_lib can use them */
extern void log_error(const char *format, ...);
extern void log_warn(const char *format, ...);
extern void log_info(const char *format, ...);
extern void log_debug(const char *format, ...);

/*
 * log_debug_once - logs only the first N times (default 3)
 * Useful for per-frame debugging without spamming logs
 * Usage: log_debug_once(count, "format", args...)
 * where count is a static counter variable
 */
#define LOG_DEBUG_ONCE_MAX 3

#define log_debug_once(counter, ...) do { \
    if ((counter) < LOG_DEBUG_ONCE_MAX) { \
        log_debug(__VA_ARGS__); \
        (counter)++; \
    } \
} while(0)

/*
 * log_debug_frame - logs only during first N frames of shader execution
 * Pass the frame_count from the shader context
 */
#define LOG_DEBUG_FRAME_MAX 3

#define log_debug_frame(frame_count, ...) do { \
    if ((frame_count) < LOG_DEBUG_FRAME_MAX) { \
        log_debug(__VA_ARGS__); \
    } \
} while(0)

#else
/* Standalone context - use shader_log.h's implementation */
#include "shader_log.h"
#endif

/* Cross-platform time function */
static inline double platform_get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

#endif /* PLATFORM_COMPAT_H */