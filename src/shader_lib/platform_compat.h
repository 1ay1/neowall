/* Platform Compatibility Header
 * Provides compatibility layer for shader library
 * Works in both neowall (daemon) and standalone (gleditor) contexts
 */

#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OpenGL ES headers */
#ifdef HAVE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

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
extern void log_info(const char *format, ...);
extern void log_debug(const char *format, ...);
#else
/* Standalone context - use shader_log.h's implementation */
#include "shader_log.h"
#endif

#endif /* PLATFORM_COMPAT_H */