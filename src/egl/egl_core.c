#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "../../include/neowall.h"
#include "../../include/egl/egl_core.h"
#include "../../include/egl/capability.h"

/**
 * EGL Core Dispatch System - Simplified for compilation
 */

const char *egl_error_string(EGLint error) {
    switch (error) {
        case EGL_SUCCESS: return "Success";
        case EGL_NOT_INITIALIZED: return "Not initialized";
        case EGL_BAD_ACCESS: return "Bad access";
        case EGL_BAD_ALLOC: return "Bad alloc";
        case EGL_BAD_ATTRIBUTE: return "Bad attribute";
        case EGL_BAD_CONFIG: return "Bad config";
        case EGL_BAD_CONTEXT: return "Bad context";
        case EGL_BAD_CURRENT_SURFACE: return "Bad current surface";
        case EGL_BAD_DISPLAY: return "Bad display";
        case EGL_BAD_MATCH: return "Bad match";
        case EGL_BAD_NATIVE_PIXMAP: return "Bad native pixmap";
        case EGL_BAD_NATIVE_WINDOW: return "Bad native window";
        case EGL_BAD_PARAMETER: return "Bad parameter";
        case EGL_BAD_SURFACE: return "Bad surface";
        case EGL_CONTEXT_LOST: return "Context lost";
        default: return "Unknown error";
    }
}

bool egl_check_error(const char *context) {
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        log_error("EGL error in %s: %s (0x%x)", 
                  context ? context : "unknown", 
                  egl_error_string(error), 
                  error);
        return true;
    }
    return false;
}

/* Simple implementation - uses new modular system */
bool egl_core_init(struct neowall_state *state) {
    if (!state || !state->display) {
        log_error("Invalid state or Wayland display");
        return false;
    }
    
    log_info("Initializing EGL with multi-version support...");
    
    /* Get EGL display */
    state->egl_display = eglGetDisplay((EGLNativeDisplayType)state->display);
    if (state->egl_display == EGL_NO_DISPLAY) {
        log_error("Failed to get EGL display");
        return false;
    }
    
    /* Initialize EGL */
    EGLint major, minor;
    if (!eglInitialize(state->egl_display, &major, &minor)) {
        log_error("Failed to initialize EGL");
        return false;
    }
    
    log_info("EGL version: %d.%d", major, minor);
    
    /* Bind OpenGL ES API */
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_error("Failed to bind OpenGL ES API");
        eglTerminate(state->egl_display);
        return false;
    }
    
    /* Detect capabilities */
    egl_detect_capabilities(state->egl_display, &state->gl_caps);
    
    /* Try ES 3.0 first, then ES 2.0 */
    const EGLint config_attribs_es3[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    const EGLint config_attribs_es2[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    EGLint num_configs;
    bool using_es3 = false;
    
    /* Try ES 3.0 */
    if (eglChooseConfig(state->egl_display, config_attribs_es3,
                        &state->egl_config, 1, &num_configs) && num_configs > 0) {
        const EGLint context_attribs_es3[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 0,
            EGL_NONE
        };
        
        state->egl_context = eglCreateContext(state->egl_display,
                                             state->egl_config,
                                             EGL_NO_CONTEXT,
                                             context_attribs_es3);
        if (state->egl_context != EGL_NO_CONTEXT) {
            using_es3 = true;
            state->gl_caps.gles_version = GLES_VERSION_3_0;
            log_info("Created OpenGL ES 3.0 context (enhanced Shadertoy support)");
        }
    }
    
    /* Fallback to ES 2.0 */
    if (!using_es3) {
        log_info("OpenGL ES 3.0 not available, falling back to ES 2.0");
        
        if (!eglChooseConfig(state->egl_display, config_attribs_es2,
                            &state->egl_config, 1, &num_configs) || num_configs == 0) {
            log_error("No suitable EGL configs found");
            eglTerminate(state->egl_display);
            return false;
        }
        
        const EGLint context_attribs_es2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        
        state->egl_context = eglCreateContext(state->egl_display,
                                             state->egl_config,
                                             EGL_NO_CONTEXT,
                                             context_attribs_es2);
        if (state->egl_context == EGL_NO_CONTEXT) {
            log_error("Failed to create EGL context");
            eglTerminate(state->egl_display);
            return false;
        }
        
        state->gl_caps.gles_version = GLES_VERSION_2_0;
        log_info("Created OpenGL ES 2.0 context");
    }
    
    /* Create surfaces and detect GL capabilities */
    struct output_state *output = state->outputs;
    while (output) {
        if (output->width > 0 && output->height > 0) {
            if (output_create_egl_surface(output)) {
                output->egl_surface = eglCreateWindowSurface(
                    state->egl_display,
                    state->egl_config,
                    (EGLNativeWindowType)output->egl_window,
                    NULL
                );
                
                if (output->egl_surface != EGL_NO_SURFACE) {
                    log_debug("Created EGL surface for output %s",
                              output->model[0] ? output->model : "unknown");
                } else {
                    log_error("Failed to create EGL surface for output %s: 0x%x",
                              output->model[0] ? output->model : "unknown",
                              eglGetError());
                }
            }
        }
        output = output->next;
    }
    
    /* Make context current to detect GL capabilities */
    output = state->outputs;
    if (output && output->egl_surface != EGL_NO_SURFACE) {
        if (eglMakeCurrent(state->egl_display, output->egl_surface,
                          output->egl_surface, state->egl_context)) {
            gles_detect_capabilities_for_context(state->egl_display, 
                                                 state->egl_context,
                                                 &state->gl_caps);
            
            log_info("Using OpenGL ES %s (%s Shadertoy compatibility)",
                     gles_version_string(state->gl_caps.gles_version),
                     state->gl_caps.gles_version >= GLES_VERSION_3_0 ? "enhanced" : "basic");
        }
    }
    
    /* Initialize rendering resources for each output */
    log_debug("Initializing rendering resources for outputs...");
    output = state->outputs;
    while (output) {
        if (output->egl_surface != EGL_NO_SURFACE) {
            if (!render_init_output(output)) {
                log_error("Failed to initialize rendering for output %s",
                          output->model[0] ? output->model : "unknown");
            } else {
                log_debug("Initialized rendering for output %s",
                          output->model[0] ? output->model : "unknown");
            }
        }
        output = output->next;
    }
    
    /* Apply any deferred configuration now that surfaces are ready */
    log_debug("Applying deferred configuration to outputs...");
    output = state->outputs;
    while (output) {
        output_apply_deferred_config(output);
        output = output->next;
    }
    
    return true;
}

void egl_core_cleanup(struct neowall_state *state) {
    if (!state) return;
    
    struct output_state *output = state->outputs;
    while (output) {
        if (output->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(state->egl_display, output->egl_surface);
            output->egl_surface = EGL_NO_SURFACE;
        }
        output = output->next;
    }
    
    if (state->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->egl_display, EGL_NO_SURFACE,
                      EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    
    if (state->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(state->egl_display, state->egl_context);
        state->egl_context = EGL_NO_CONTEXT;
    }
    
    if (state->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(state->egl_display);
        state->egl_display = EGL_NO_DISPLAY;
    }
}

bool egl_core_make_current(struct neowall_state *state, struct output_state *output) {
    if (!state || state->egl_display == EGL_NO_DISPLAY) return false;
    
    if (!output) {
        return eglMakeCurrent(state->egl_display, EGL_NO_SURFACE,
                             EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    
    if (output->egl_surface == EGL_NO_SURFACE) return false;
    
    return eglMakeCurrent(state->egl_display, output->egl_surface,
                         output->egl_surface, state->egl_context);
}

bool egl_core_swap_buffers(struct neowall_state *state, struct output_state *output) {
    if (!state || !output || state->egl_display == EGL_NO_DISPLAY) return false;
    if (output->egl_surface == EGL_NO_SURFACE) return false;
    return eglSwapBuffers(state->egl_display, output->egl_surface);
}
