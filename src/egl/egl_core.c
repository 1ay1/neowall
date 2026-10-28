#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "neowall/neowall.h"
#include "neowall/compositor/compositor.h"
#include "neowall/egl/egl_core.h"
#include "neowall/egl/gpu_health.h"

/**
 * EGL Core - Simple Desktop OpenGL 3.3 Context Management
 * 
 * Uses desktop OpenGL 3.3 Core profile for Shadertoy compatibility.
 */

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

    /* Damage-aware presentation. eglSwapBuffersWithDamageEXT/KHR lets us hand the
     * compositor the exact changed rectangle(s) so it recomposites just that
     * region (and, on a frei-buffer path, re-scans only those scanlines) instead
     * of the whole surface. The terminal wallpaper feeds its dirty cell-row band;
     * other wallpaper types damage the full surface. Falls back to plain
     * eglSwapBuffers when neither extension is present. */
    state->swap_with_damage = NULL;
    state->swap_damage_supported = false;
    {
        const char *egl_exts = eglQueryString(state->egl_display, EGL_EXTENSIONS);
        if (egl_exts) {
            /* eglGetProcAddress returns a function pointer; store it as void*
             * through a union to avoid the ISO-C function<->object cast warning. */
            union { void (*fn)(void); void *obj; } u = { .fn = NULL };
            if (strstr(egl_exts, "EGL_EXT_swap_buffers_with_damage")) {
                u.fn = eglGetProcAddress("eglSwapBuffersWithDamageEXT");
            }
            if (!u.fn && strstr(egl_exts, "EGL_KHR_swap_buffers_with_damage")) {
                u.fn = eglGetProcAddress("eglSwapBuffersWithDamageKHR");
            }
            state->swap_with_damage = u.obj;
        }
        state->swap_damage_supported = state->swap_with_damage != NULL;
        log_info("Damage-aware presentation: %s",
                 state->swap_damage_supported
                     ? "enabled (eglSwapBuffersWithDamage)"
                     : "unavailable (full-surface swaps)");
    }

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

    /* Create OpenGL 3.3 Core context, robust when the driver can do it so a
     * GPU reset is reported instead of silently leaving us drawing garbage. */
    EGLint context_attribs[16];
    bool robust = gpu_health_context_attribs(state->egl_display, context_attribs, 16);

    state->egl_context = eglCreateContext(state->egl_display,
                                          state->egl_config,
                                          EGL_NO_CONTEXT,
                                          context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT && robust) {
        /* Some drivers advertise the extension but reject the attribs. */
        log_info("Robust GL context refused, falling back to a plain one");
        robust = false;
        context_attribs[6] = EGL_NONE;   /* keep version + profile, drop robustness */
        state->egl_context = eglCreateContext(state->egl_display, state->egl_config,
                                              EGL_NO_CONTEXT, context_attribs);
    }
    if (state->egl_context == EGL_NO_CONTEXT) {
        log_error("Failed to create OpenGL 3.3 context");
        eglTerminate(state->egl_display);
        return false;
    }
    state->gl_robust = robust;

    log_info("Created OpenGL 3.3 Core context%s", robust ? " (robust)" : "");

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

    /* Context-wide setup can't wait for an output surface: on Wayland outputs
     * usually have no size yet. Make the context current surfaceless for it. */
    if (eglMakeCurrent(state->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, state->egl_context)) {
        const char *gl_version = (const char *)glGetString(GL_VERSION);
        const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
        log_info("OpenGL: %s", gl_version ? gl_version : "unknown");
        log_info("Renderer: %s", gl_renderer ? gl_renderer : "unknown");
        gpu_health_init(state->gl_robust, log_get_level() >= LOG_LEVEL_DEBUG);
    } else {
        log_debug("Surfaceless context unsupported; GPU health starts with the first surface");
        state->gpu_health_pending = true;
    }

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
                          output->compositor_surface->egl_surface, state->egl_context) &&
            state->gpu_health_pending) {
            gpu_health_init(state->gl_robust, log_get_level() >= LOG_LEVEL_DEBUG);
            state->gpu_health_pending = false;
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

