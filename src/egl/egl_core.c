#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "../../include/neowall.h"
#include "../../include/compositor.h"
#include "../../include/egl/egl_core.h"

/**
 * EGL Core - Simple Desktop OpenGL 3.3 Context Management
 * 
 * Uses desktop OpenGL 3.3 Core profile for Shadertoy compatibility.
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

bool egl_core_init(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state");
        return false;
    }

    log_info("Initializing EGL with desktop OpenGL 3.3...");

    /* Get native display from compositor backend */
    if (!state->compositor_backend || !state->compositor_backend->ops) {
        log_error("No compositor backend available for EGL initialization");
        return false;
    }

    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *backend_data = state->compositor_backend->data;

    void *native_display = NULL;
    if (ops->get_native_display) {
        native_display = ops->get_native_display(backend_data);
    }

    EGLenum platform = EGL_PLATFORM_WAYLAND_KHR;
    if (ops->get_egl_platform) {
        platform = ops->get_egl_platform(backend_data);
    }

    /* Get EGL display */
    if (native_display) {
        PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

        if (eglGetPlatformDisplayEXT) {
            state->egl_display = eglGetPlatformDisplayEXT(platform, native_display, NULL);
        } else {
            state->egl_display = eglGetDisplay((EGLNativeDisplayType)native_display);
        }
    } else {
        state->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

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

    /* Use desktop OpenGL for Shadertoy compatibility */
    if (!eglBindAPI(EGL_OPENGL_API)) {
        log_error("Failed to bind desktop OpenGL API");
        eglTerminate(state->egl_display);
        return false;
    }

    /* Desktop OpenGL 3.3 Core Profile config */
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(state->egl_display, config_attribs,
                         &state->egl_config, 1, &num_configs) || num_configs == 0) {
        log_error("Failed to choose EGL config for desktop OpenGL");
        eglTerminate(state->egl_display);
        return false;
    }

    /* Create OpenGL 3.3 Core context */
    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    state->egl_context = eglCreateContext(state->egl_display,
                                          state->egl_config,
                                          EGL_NO_CONTEXT,
                                          context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT) {
        log_error("Failed to create OpenGL 3.3 context");
        eglTerminate(state->egl_display);
        return false;
    }

    log_info("Created OpenGL 3.3 Core context (full Shadertoy compatibility)");

    /* Create surfaces for outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    while (output) {
        if (output->width > 0 && output->height > 0) {
            if (output_create_egl_surface(output)) {
                log_debug("Created EGL surface for output %s",
                          output->model[0] ? output->model : "unknown");
            } else {
                log_error("Failed to create EGL surface for output %s",
                          output->model[0] ? output->model : "unknown");
            }
        }
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    /* Make context current and enable vsync */
    pthread_rwlock_rdlock(&state->output_list_lock);

    output = state->outputs;
    if (output && output->compositor_surface && 
        output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        
        if (!eglSwapInterval(state->egl_display, 1)) {
            log_debug("Could not set vsync - continuing without");
        } else {
            log_info("Enabled vsync");
        }

        if (eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                          output->compositor_surface->egl_surface, state->egl_context)) {
            /* Log OpenGL info */
            const char *gl_version = (const char *)glGetString(GL_VERSION);
            const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
            log_info("OpenGL: %s", gl_version ? gl_version : "unknown");
            log_info("Renderer: %s", gl_renderer ? gl_renderer : "unknown");
        }
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    /* Initialize rendering for outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    output = state->outputs;
    while (output) {
        if (output->compositor_surface && 
            output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
            
            if (!eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                               output->compositor_surface->egl_surface, state->egl_context)) {
                log_error("Failed to make context current for output %s",
                         output->model[0] ? output->model : "unknown");
                output = output->next;
                continue;
            }

            if (!output_init_render(output)) {
                log_error("Failed to initialize rendering for output %s",
                          output->model[0] ? output->model : "unknown");
            }
        }
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    /* Apply deferred configuration */
    pthread_rwlock_rdlock(&state->output_list_lock);

    output = state->outputs;
    while (output) {
        output_apply_deferred_config(output);
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    return true;
}

void egl_core_cleanup(struct neowall_state *state) {
    if (!state) return;

    struct output_state *output = state->outputs;
    while (output) {
        if (output->compositor_surface && 
            output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
            compositor_surface_destroy_egl(output->compositor_surface, state->egl_display);
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

    if (!output->compositor_surface || 
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) return false;

    return eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                         output->compositor_surface->egl_surface, state->egl_context);
}

bool egl_core_swap_buffers(struct neowall_state *state, struct output_state *output) {
    if (!state || !output || state->egl_display == EGL_NO_DISPLAY) return false;
    if (!output->compositor_surface || 
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) return false;
    return eglSwapBuffers(state->egl_display, output->compositor_surface->egl_surface);
}