#ifndef EGL_V14_H
#define EGL_V14_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/**
 * EGL 1.4 Implementation Header
 * 
 * EGL 1.4 (2011) introduces:
 * - Multi-context support with resource sharing
 * - Improved error handling
 * - Better Wayland integration
 * - Separate draw and read surfaces
 * - Context priority hints
 */

/* ============================================================================
 * Availability and Initialization
 * ============================================================================ */

/**
 * Check if EGL 1.4 is available on the system
 * 
 * @return true if EGL 1.4 or higher is supported
 */
bool egl_v14_available(void);

/**
 * Initialize EGL 1.4 function pointers
 * 
 * @return true on success, false on failure
 */
bool egl_v14_init_functions(void);

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * Create a shared EGL context for multi-context rendering
 * 
 * @param display EGL display
 * @param config EGL configuration
 * @param share_context Context to share resources with (or EGL_NO_CONTEXT)
 * @param gles_version OpenGL ES version (2 or 3)
 * @return New EGL context, or EGL_NO_CONTEXT on failure
 */
EGLContext egl_v14_create_shared_context(EGLDisplay display,
                                         EGLConfig config,
                                         EGLContext share_context,
                                         int gles_version);

/**
 * Make context current with separate draw and read surfaces
 * 
 * @param display EGL display
 * @param draw_surface Surface to draw to
 * @param read_surface Surface to read from (or EGL_NO_SURFACE to use draw_surface)
 * @param context EGL context to make current
 * @return true on success, false on failure
 */
bool egl_v14_make_current_multi(EGLDisplay display,
                                EGLSurface draw_surface,
                                EGLSurface read_surface,
                                EGLContext context);

/**
 * Get the current EGL context
 * 
 * @return Current context, or EGL_NO_CONTEXT if none is current
 */
EGLContext egl_v14_get_current_context(void);

/**
 * Get current draw or read surface
 * 
 * @param readdraw EGL_DRAW or EGL_READ
 * @return Current surface, or EGL_NO_SURFACE if none
 */
EGLSurface egl_v14_get_current_surface(EGLint readdraw);

/**
 * Query context attributes
 * 
 * @param display EGL display
 * @param context EGL context to query
 * @param attribute Attribute to query (e.g., EGL_CONTEXT_CLIENT_VERSION)
 * @param value Pointer to store the attribute value
 * @return true on success, false on failure
 */
bool egl_v14_query_context(EGLDisplay display, EGLContext context,
                           EGLint attribute, EGLint *value);

/**
 * Create context with automatic GLES version fallback
 * Tries ES 3.2, 3.1, 3.0, 2.0 in order until one succeeds
 * 
 * @param display EGL display
 * @param config EGL configuration
 * @param gles_version Pointer to store the actual GLES version created
 * @return New EGL context, or EGL_NO_CONTEXT on failure
 */
EGLContext egl_v14_create_context_with_fallback(EGLDisplay display,
                                                 EGLConfig config,
                                                 int *gles_version);

/**
 * Destroy EGL context safely
 * Automatically unbinds context if it's current
 * 
 * @param display EGL display
 * @param context EGL context to destroy
 */
void egl_v14_destroy_context(EGLDisplay display, EGLContext context);

/* ============================================================================
 * Context Validation and Information
 * ============================================================================ */

/**
 * Verify that a context is valid and current
 * 
 * @param display EGL display
 * @param context EGL context to verify
 * @return true if context is valid and current, false otherwise
 */
bool egl_v14_verify_context(EGLDisplay display, EGLContext context);

/**
 * Get human-readable context information string
 * 
 * @param display EGL display
 * @param context EGL context
 * @return Static string with context info (ES version, config, etc.)
 */
const char *egl_v14_get_context_info(EGLDisplay display, EGLContext context);

/**
 * Print detailed EGL 1.4 information to log
 * 
 * @param display EGL display (or EGL_NO_DISPLAY to query default)
 */
void egl_v14_print_info(EGLDisplay display);

#endif /* EGL_V14_H */