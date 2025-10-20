/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 *
 * EGL initialization and context management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "staticwall.h"

static const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
};

static const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

bool egl_init(struct staticwall_state *state) {
    if (!state || !state->display) {
        log_error("Invalid state or Wayland display");
        return false;
    }

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
    log_info("EGL vendor: %s", eglQueryString(state->egl_display, EGL_VENDOR));
    log_info("EGL version string: %s", eglQueryString(state->egl_display, EGL_VERSION));

    /* Bind OpenGL ES API */
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_error("Failed to bind OpenGL ES API");
        eglTerminate(state->egl_display);
        return false;
    }

    /* Choose EGL config */
    EGLint num_configs;
    if (!eglChooseConfig(state->egl_display, config_attribs,
                        &state->egl_config, 1, &num_configs)) {
        log_error("Failed to choose EGL config");
        eglTerminate(state->egl_display);
        return false;
    }

    if (num_configs == 0) {
        log_error("No suitable EGL configs found");
        eglTerminate(state->egl_display);
        return false;
    }

    /* Create EGL context */
    state->egl_context = eglCreateContext(state->egl_display,
                                         state->egl_config,
                                         EGL_NO_CONTEXT,
                                         context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT) {
        log_error("Failed to create EGL context");
        eglTerminate(state->egl_display);
        return false;
    }

    log_info("EGL context created successfully");

    /* Create EGL surfaces for all outputs */
    struct output_state *output = state->outputs;
    while (output) {
        if (output->width > 0 && output->height > 0) {
            if (!output_create_egl_surface(output)) {
                log_error("Failed to create EGL surface for output %s", output->model);
            } else {
                /* Create EGL surface from EGL window */
                output->egl_surface = eglCreateWindowSurface(
                    state->egl_display,
                    state->egl_config,
                    (EGLNativeWindowType)output->egl_window,
                    NULL
                );

                if (output->egl_surface == EGL_NO_SURFACE) {
                    log_error("Failed to create EGL window surface for output %s",
                             output->model);
                    wl_egl_window_destroy(output->egl_window);
                    output->egl_window = NULL;
                } else {
                    log_debug("Created EGL surface for output %s", output->model);

                    /* Make context current for this output */
                    if (!eglMakeCurrent(state->egl_display, output->egl_surface,
                                       output->egl_surface, state->egl_context)) {
                        log_error("Failed to make EGL context current: 0x%x", eglGetError());
                    } else {
                        /* Initialize rendering for this output */
                        if (!render_init_output(output)) {
                            log_error("Failed to initialize rendering for output %s",
                                     output->model);
                        }
                    }
                }
            }
        }
        output = output->next;
    }

    return true;
}

void egl_cleanup(struct staticwall_state *state) {
    if (!state) {
        return;
    }

    log_debug("Cleaning up EGL resources");

    /* Clean up output EGL surfaces */
    struct output_state *output = state->outputs;
    while (output) {
        if (output->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(state->egl_display, output->egl_surface);
            output->egl_surface = EGL_NO_SURFACE;
        }
        output = output->next;
    }

    /* Destroy EGL context */
    if (state->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(state->egl_display, state->egl_context);
        state->egl_context = EGL_NO_CONTEXT;
    }

    /* Terminate EGL */
    if (state->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(state->egl_display);
        state->egl_display = EGL_NO_DISPLAY;
    }

    log_debug("EGL cleanup complete");
}

bool egl_make_current(struct staticwall_state *state, struct output_state *output) {
    if (!state || !output) {
        log_error("Invalid state or output for egl_make_current");
        return false;
    }

    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("Output has no EGL surface");
        return false;
    }

    if (!eglMakeCurrent(state->egl_display, output->egl_surface,
                       output->egl_surface, state->egl_context)) {
        log_error("Failed to make EGL context current: 0x%x", eglGetError());
        return false;
    }

    return true;
}

bool egl_swap_buffers(struct staticwall_state *state, struct output_state *output) {
    if (!state || !output) {
        log_error("Invalid state or output for egl_swap_buffers");
        return false;
    }

    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("Output has no EGL surface");
        return false;
    }

    if (!eglSwapBuffers(state->egl_display, output->egl_surface)) {
        EGLint error = eglGetError();
        log_error("Failed to swap EGL buffers: 0x%x", error);
        return false;
    }

    return true;
}

/* Recreate EGL surface for output (e.g., after resize) */
bool egl_recreate_output_surface(struct staticwall_state *state,
                                 struct output_state *output) {
    if (!state || !output) {
        log_error("Invalid parameters for egl_recreate_output_surface");
        return false;
    }

    log_debug("Recreating EGL surface for output %s", output->model);

    /* Destroy old EGL surface */
    if (output->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(state->egl_display, output->egl_surface);
        output->egl_surface = EGL_NO_SURFACE;
    }

    /* Destroy old EGL window */
    if (output->egl_window) {
        wl_egl_window_destroy(output->egl_window);
        output->egl_window = NULL;
    }

    /* Create new EGL window */
    if (!output_create_egl_surface(output)) {
        log_error("Failed to create new EGL window for output %s", output->model);
        return false;
    }

    /* Create new EGL surface */
    output->egl_surface = eglCreateWindowSurface(
        state->egl_display,
        state->egl_config,
        (EGLNativeWindowType)output->egl_window,
        NULL
    );

    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("Failed to create new EGL surface for output %s", output->model);
        wl_egl_window_destroy(output->egl_window);
        output->egl_window = NULL;
        return false;
    }

    log_debug("Successfully recreated EGL surface for output %s", output->model);

    /* Reinitialize rendering resources */
    return render_init_output(output);
}