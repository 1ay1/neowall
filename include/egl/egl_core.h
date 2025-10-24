#ifndef EGL_CORE_H
#define EGL_CORE_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "capability.h"

/* Forward declarations */
struct staticwall_state;

/**
 * EGL Core Dispatch System
 * 
 * This module provides a unified interface for EGL/OpenGL ES initialization
 * that automatically detects and uses the best available version.
 */

/* Context creation strategy */
typedef enum {
    CONTEXT_STRATEGY_BEST,      /* Try highest version first, fallback to lower */
    CONTEXT_STRATEGY_PREFER_32, /* Prefer ES 3.2 if available */
    CONTEXT_STRATEGY_PREFER_31, /* Prefer ES 3.1 if available */
    CONTEXT_STRATEGY_PREFER_30, /* Prefer ES 3.0 if available */
    CONTEXT_STRATEGY_FORCE_20,  /* Force ES 2.0 (for testing) */
} egl_context_strategy_t;

/* EGL state management */
typedef struct {
    EGLDisplay display;
    EGLContext context;
    EGLConfig config;
    egl_capabilities_t caps;
    egl_context_strategy_t strategy;
    bool initialized;
} egl_core_state_t;

/**
 * Initialize EGL with automatic version detection
 * 
 * @param state Staticwall global state
 * @return true on success, false on failure
 */
bool egl_core_init(struct staticwall_state *state);

/**
 * Initialize EGL with specific strategy
 * 
 * @param state Staticwall global state
 * @param strategy Context creation strategy
 * @return true on success, false on failure
 */
bool egl_core_init_with_strategy(struct staticwall_state *state, 
                                  egl_context_strategy_t strategy);

/**
 * Cleanup EGL resources
 * 
 * @param state Staticwall global state
 */
void egl_core_cleanup(struct staticwall_state *state);

/**
 * Create best available OpenGL ES context
 * 
 * @param display EGL display
 * @param config EGL config
 * @param caps Capability structure (updated with detected version)
 * @return EGL context or EGL_NO_CONTEXT on failure
 */
EGLContext egl_create_best_context(EGLDisplay display, EGLConfig config,
                                    egl_capabilities_t *caps);

/**
 * Create OpenGL ES context with specific version
 * 
 * @param display EGL display
 * @param config EGL config
 * @param version Desired OpenGL ES version
 * @return EGL context or EGL_NO_CONTEXT on failure
 */
EGLContext egl_create_context_version(EGLDisplay display, EGLConfig config,
                                       gles_version_t version);

/**
 * Make EGL context current for an output
 * 
 * @param state Staticwall global state
 * @param output Output state (can be NULL to unbind)
 * @return true on success, false on failure
 */
bool egl_core_make_current(struct staticwall_state *state, 
                           struct output_state *output);

/**
 * Swap buffers for an output
 * 
 * @param state Staticwall global state
 * @param output Output state
 * @return true on success, false on failure
 */
bool egl_core_swap_buffers(struct staticwall_state *state,
                           struct output_state *output);

/**
 * Get EGL config with best attributes
 * 
 * @param display EGL display
 * @param version Desired OpenGL ES version
 * @param config Output config
 * @return true on success, false on failure
 */
bool egl_get_best_config(EGLDisplay display, gles_version_t version,
                        EGLConfig *config);

/**
 * Validate EGL setup
 * 
 * @param state Staticwall global state
 * @return true if valid, false otherwise
 */
bool egl_core_validate(struct staticwall_state *state);

/**
 * Print EGL configuration info
 * 
 * @param state Staticwall global state
 */
void egl_core_print_info(struct staticwall_state *state);

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