#ifndef EGL_CORE_H
#define EGL_CORE_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* Forward declarations */
struct neowall_state;
struct output_state;

/**
 * EGL Core - Simple Desktop OpenGL 3.3 Context Management
 * 
 * Uses desktop OpenGL 3.3 Core profile for Shadertoy compatibility.
 */

/**
 * Initialize EGL with desktop OpenGL 3.3 context
 * 
 * @param state NeoWall global state
 * @return true on success, false on failure
 */
bool egl_core_init(struct neowall_state *state);

/**
 * Cleanup EGL resources
 * 
 * @param state NeoWall global state
 */
void egl_core_cleanup(struct neowall_state *state);

/**
 * Make EGL context current for an output
 * 
 * @param state NeoWall global state
 * @param output Output state (can be NULL to unbind)
 * @return true on success, false on failure
 */
bool egl_core_make_current(struct neowall_state *state, 
                           struct output_state *output);

/**
 * Swap buffers for an output
 * 
 * @param state NeoWall global state
 * @param output Output state
 * @return true on success, false on failure
 */
bool egl_core_swap_buffers(struct neowall_state *state,
                           struct output_state *output);

/**
 * Get human-readable error string
 * 
 * @param error EGL error code
 * @return Error string
 */
const char *egl_error_string(EGLint error);

/**
 * Check for EGL errors and log them
 * 
 * @param context Context string for error message
 * @return true if error occurred, false otherwise
 */
bool egl_check_error(const char *context);

#endif /* EGL_CORE_H */