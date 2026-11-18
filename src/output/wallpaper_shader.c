/* Show a shader wallpaper: build now, or in the background while the
 * current one keeps drawing. */

#include <stdio.h>
#include "neowall/surface/surface.h"
#include "neowall/egl/egl_core.h"
#include "render.h"
#include "output_internal.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/core/util.h"
#include "neowall/output/state_file.h"
#include "neowall/state/state.h"

static void output_install_shader(struct output_state *output, wallshader *fresh,
                                  const char *shader_path);

nw_result output_set_shader(struct output_state *output, const char *shader_path) {
    if (!output || !shader_path) {
        log_error("Invalid parameters for output_set_shader");
        return nw_err(NW_ERR_INVALID_ARG, "NULL output or shader path");
    }

    /* A .neowall manifest or a .glsl with a sidecar: the loader handles both. */

    /* Copy the model string under the state mutex to avoid a data race with
     * the registry/output-destroy paths that can rewrite it concurrently. */
    char model_copy[64];

    /* Acquire state mutex to safely read model string */
    pthread_mutex_lock(&output->state->state_mutex);
    snprintf(model_copy, sizeof(model_copy), "%s", output->model);
    pthread_mutex_unlock(&output->state->state_mutex);

    log_info("Setting shader for output %s: %s", model_copy[0] ? model_copy : "unknown",
             shader_path);

    /* Defensive checks before any EGL/GL operations */
    if (!output->state) {
        log_error("Output state is NULL, cannot set shader");
        return nw_err(NW_ERR_STATE, "output has no state");
    }

    if (output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not initialized, cannot set shader");
        return nw_err(NW_ERR_GL, "EGL display not initialized");
    }

    /* CRITICAL: Ensure EGL context is current before any GL operations */
    if (!output->compositor_surface ||
        !egl_make_surface_current(output->state->egl_display,
                                  output->compositor_surface->egl_surface,
                                  output->state->egl_context)) {
        log_error("Failed to make EGL context current for shader set");
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed for shader set");
    }

    if (output->state->egl_context == EGL_NO_CONTEXT) {
        log_error("EGL context not initialized, cannot set shader");
        return nw_err(NW_ERR_GL, "EGL context not initialized");
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_debug("EGL surface not ready for output %s, deferring shader load: %s",
                  output->model[0] ? output->model : "unknown", shader_path);
        /* Store shader path in config for later application when surface is ready */
        config_str_set(output->config->shader_path, sizeof(output->config->shader_path),
                       shader_path);
        output->config->type = WALLPAPER_SHADER;
        /* Deferred, not failed: the render loop applies this path once the
         * surface comes up, so callers must not treat it as a load failure. */
        return nw_ok();
    }

    /* What rendering needs is an EGL surface; how the backend got one (a
     * wl_egl_window, an X11 window, a pbuffer) doesn't matter here. */
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not created for output %s, cannot set shader",
                  output->model[0] ? output->model : "unknown");
        return nw_err(NW_ERR_GL, "EGL surface not created");
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return nw_err(NW_ERR_GL, "EGL display unavailable");
    }

    /* Make EGL context current before creating shader program */
    if (!egl_make_surface_current(output->state->egl_display,
                                  output->compositor_surface->egl_surface,
                                  output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error(
            "Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
            output->model[0] ? output->model : "unknown", egl_error);
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed");
    }

    log_debug("EGL context made current for output %s",
              output->model[0] ? output->model : "unknown");

    /* Build the new one before dropping the old: a shader that doesn't
     * compile leaves what's on screen alone. */
    wallshader *fresh = NULL;
    nw_result lr      = wallshader_load(shader_path, &fresh);
    if (nw_is_err(lr)) {
        log_error("Failed to load shader: %s", shader_path);
        return lr;
    }
    output_install_shader(output, fresh, shader_path);
    return nw_ok();
}

/* Put a loaded shader on screen in place of whatever was there. The output's
 * context must be current. */
static void output_install_shader(struct output_state *output, wallshader *fresh,
                                  const char *shader_path) {
    /* a build that was still going belongs to the shader we're replacing,
     * and so does a reload waiting on it */
    wallshader_load_cancel(output->pending_shader);
    output->pending_shader = NULL;
    output_unstage(output);

    /* the old one's last frame fades out over the new one, no pop */
    if (output->config->type == WALLPAPER_SHADER) render_snapshot_for_swap(output);
    wallshader_free(output->shader);
    output->shader = fresh;
    /* the config's `palette` becomes iPalette (built-in one when unset) */
    wallshader_set_palette(fresh, (const float (*)[3])output->config->palette,
                           output->config->palette_count);
    /* the config's `channels` fill the iChannels the shader leaves open */
    (void)render_load_channel_textures(output, output->config);

    output->shader_start_time = get_time_ms();
    /* Fresh shader = fresh frame count. The static-shader idle path keys off
     * frames_rendered>0; without this reset a newly loaded static shader
     * would inherit the old count and never paint. */
    output->frames_rendered = 0;
    /* A freshly loaded shader starts from a clean timeline; clear any frozen
     * baseline so a pending resume doesn't shift start_time into the future
     * (which would underflow the elapsed-time calculation in render.c). */
    output->shader_pause            = (shader_pause_clock){0};
    output->shader_pause_final_draw = false;

    log_info("Loaded shader with %d pass(es): %s", wallshader_pass_count(output->shader),
             shader_path);

    /* Update config with new shader path - protected by state mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    config_str_set(output->config->shader_path, sizeof(output->config->shader_path), shader_path);
    output->config->type = WALLPAPER_SHADER;
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Leaving terminal mode (if we were in it): drop keyboard focus. */
    output_sync_keyboard_focus(output);

    /* Write state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config->mode);
    write_wallpaper_state(output_get_identifier(output), shader_path, mode_str,
                          output->config->current_cycle_index, output->config->cycle_count,
                          "active");

    /* Mark for immediate redraw with new shader */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    /* Initialize frame time for animation */
    uint64_t now            = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Configure vsync based on shader_fps setting */
    if (output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        if (!egl_make_surface_current(output->state->egl_display,
                                      output->compositor_surface->egl_surface,
                                      output->state->egl_context)) {
            log_error("Failed to make EGL context current for vsync config");
        } else {
            /* Configure vsync for shader rendering */
            output_configure_vsync(output);

            /* Configure frame timer for precise pacing when vsync is disabled */
            output_configure_frame_timer(output);
        }
    }

    /* Free any existing image data (shaders don't use images) */
    if (output->current_image) {
        image_free(output->current_image);
        output->current_image = NULL;
    }
    if (output->next_image) {
        image_free(output->next_image);
        output->next_image = NULL;
    }
    if (output->texture) {
        render_destroy_texture(output->texture);
        output->texture = 0;
    }
    if (output->next_texture) {
        render_destroy_texture(output->next_texture);
        output->next_texture = 0;
    }

    log_debug("Shader wallpaper loaded");
}

/* Load a shader without stalling the loop: compiles run on the driver's
 * threads and output_poll_shader_build swaps it in when done, the current
 * shader drawing meanwhile. A file error fails right here; a compile error
 * when the build finishes. A second request replaces the first. */
nw_result output_set_shader_async(struct output_state *output, const char *shader_path) {
    if (!output || !shader_path) return nw_err(NW_ERR_INVALID_ARG, "NULL output or shader path");
    /* nothing on screen yet, or no surface: nothing to keep drawing */
    if (!output->shader || !output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        return output_set_shader(output, shader_path);
    }
    if (!egl_make_surface_current(output->state->egl_display,
                                  output->compositor_surface->egl_surface,
                                  output->state->egl_context)) {
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed");
    }
    wallshader_load_cancel(output->pending_shader);
    output->pending_shader = NULL;
    output_unstage(output); /* a newer request replaces a waiting reload */
    log_info("Setting shader for output %s: %s (building in the background)",
             output->model[0] ? output->model : "unknown", shader_path);
    nw_result r = wallshader_load_start(shader_path, &output->pending_shader);
    if (nw_is_err(r)) {
        log_error("Failed to load shader: %s", shader_path);
        return r;
    }
    snprintf(output->pending_shader_path, sizeof(output->pending_shader_path), "%s", shader_path);
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    return nw_ok();
}

bool output_poll_shader_build(struct output_state *output) {
    if (!output || !output->pending_shader) return false;
    wallshader *fresh = NULL;
    nw_build_state st = wallshader_load_poll(output->pending_shader, &fresh);
    if (st == NW_BUILD_PENDING) return true;
    wallshader_load_cancel(output->pending_shader);
    output->pending_shader = NULL;
    if (st == NW_BUILD_DONE) {
        char path[NW_PATH_MAX];
        snprintf(path, sizeof(path), "%s", output->pending_shader_path);
        /* a reload was waiting on this shader: its config goes live with it */
        struct wallpaper_config *staged = output->staged_config;
        output->staged_config           = NULL;
        output_install_shader(output, fresh, path);
        output->pending_failures = 0;
        if (staged) output_commit_staged(output, staged);
    } else if (output->staged_config) {
        /* a reload's shader didn't compile: the old config and shader stay */
        log_error("Failed to load shader: %s (keeping the current config)",
                  output->pending_shader_path);
        output_unstage(output);
    } else {
        log_error("Failed to load shader: %s (keeping the current one)",
                  output->pending_shader_path);
        /* a broken entry in a cycle: try the next, but give up after a
         * whole lap so an all-broken list stops */
        if (output->config->cycle && output->config->cycle_count > 1 &&
            ++output->pending_failures < output->config->cycle_count) {
            output_cycle_wallpaper(output);
            return output->pending_shader != NULL;
        }
        output->pending_failures = 0;
    }
    return false;
}
