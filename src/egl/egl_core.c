#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "neowall/state/state.h"
#include "neowall/egl/egl_core.h"
#include "neowall/egl/gpu_health.h"
#include "neowall/core/log.h"

/**
 * EGL Core - Simple Desktop OpenGL 3.3 Context Management
 *
 * Uses desktop OpenGL 3.3 Core profile for Shadertoy compatibility.
 */

bool egl_core_init(struct neowall_state *state, void *native_display, EGLenum platform) {
    if (!state) {
        log_error("Invalid state");
        return false;
    }

    log_info("Initializing EGL with desktop OpenGL 3.3...");

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
    state->swap_with_damage      = NULL;
    state->swap_damage_supported = false;
    {
        const char *egl_exts = eglQueryString(state->egl_display, EGL_EXTENSIONS);
        if (egl_exts) {
            /* eglGetProcAddress returns a function pointer; store it as void*
             * through a union to avoid the ISO-C function<->object cast warning. */
            union {
                void (*fn)(void);
                void *obj;
            } u = {.fn = NULL};
            if (strstr(egl_exts, "EGL_EXT_swap_buffers_with_damage")) {
                u.fn = eglGetProcAddress("eglSwapBuffersWithDamageEXT");
            }
            if (!u.fn && strstr(egl_exts, "EGL_KHR_swap_buffers_with_damage")) {
                u.fn = eglGetProcAddress("eglSwapBuffersWithDamageKHR");
            }
            state->swap_with_damage = u.obj;
        }
        state->swap_damage_supported = state->swap_with_damage != NULL;
        log_info("Damage-aware presentation: %s", state->swap_damage_supported
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
    const EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                                     EGL_WINDOW_BIT,
                                     EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_BIT,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_ALPHA_SIZE,
                                     8,
                                     EGL_NONE};

    EGLint num_configs;
    if (!eglChooseConfig(state->egl_display, config_attribs, &state->egl_config, 1, &num_configs) ||
        num_configs == 0) {
        log_error("Failed to choose EGL config for desktop OpenGL");
        eglTerminate(state->egl_display);
        return false;
    }

    /* Create OpenGL 3.3 Core context, robust when the driver can do it so a
     * GPU reset is reported instead of silently leaving us drawing garbage. */
    EGLint context_attribs[16];
    bool robust = gpu_health_context_attribs(state->egl_display, context_attribs, 16);

    state->egl_context =
        eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT && robust) {
        /* Some drivers advertise the extension but reject the attribs. */
        log_info("Robust GL context refused, falling back to a plain one");
        robust             = false;
        context_attribs[6] = EGL_NONE; /* keep version + profile, drop robustness */
        state->egl_context = eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT,
                                              context_attribs);
    }
    if (state->egl_context == EGL_NO_CONTEXT) {
        log_error("Failed to create OpenGL 3.3 context");
        eglTerminate(state->egl_display);
        return false;
    }
    state->gl_robust = robust;

    log_info("Created OpenGL 3.3 Core context%s", robust ? " (robust)" : "");

    /* Context-wide setup can't wait for an output surface: on Wayland outputs
     * usually have no size yet. Make the context current surfaceless for it. */
    if (egl_make_current_raw(state->egl_display, EGL_NO_SURFACE, state->egl_context)) {
        const char *gl_version  = (const char *)glGetString(GL_VERSION);
        const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
        log_info("OpenGL: %s", gl_version ? gl_version : "unknown");
        log_info("Renderer: %s", gl_renderer ? gl_renderer : "unknown");
        gpu_health_init(state->gl_robust, log_get_level() >= LOG_LEVEL_DEBUG);
    } else {
        log_debug("Surfaceless context unsupported; GPU health starts with the first surface");
        state->gpu_health_pending = true;
    }

    return true;
}

/* Outputs drop their EGL surfaces first (the app does it, it owns them). */
void egl_core_cleanup(struct neowall_state *state) {
    if (!state) return;

    if (state->egl_display != EGL_NO_DISPLAY) {
        egl_make_current_raw(state->egl_display, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (state->egl_context != EGL_NO_CONTEXT) {
        egl_forget_current();
        eglDestroyContext(state->egl_display, state->egl_context);
        state->egl_context = EGL_NO_CONTEXT;
    }

    if (state->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(state->egl_display);
        state->egl_display = EGL_NO_DISPLAY;
    }
}
