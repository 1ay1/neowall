/* Show an image wallpaper (with a transition from the last one). */

#include "neowall/compositor/compositor.h"
#include "neowall/config/config_access.h"
#include "neowall/egl/egl_core.h"
#include "neowall/render/render.h"  /* Only output.c includes render.h */
#include "output_internal.h"

void output_set_wallpaper(struct output_state *output, const char *path) {
    if (!output || !path) {
        log_error("Invalid parameters for output_set_wallpaper");
        return;
    }

    log_info("Setting wallpaper for output %s: %s",
             output->model[0] ? output->model : "unknown", path);

    /* Check if we have a preloaded texture for this path */
    struct image_data *new_image = NULL;
    GLuint new_texture = 0;
    bool used_preload = false;

    if (atomic_load(&output->preload_ready) && strcmp(output->preload_path, path) == 0) {
        /* Use preloaded texture - no blocking I/O! */
        log_info("Using preloaded texture for %s (ZERO-STALL transition!)", path);
        new_image = output->preload_image;
        new_texture = output->preload_texture;
        used_preload = true;

        /* Clear preload state (we're taking ownership) */
        output->preload_image = NULL;
        output->preload_texture = 0;
        atomic_store(&output->preload_ready, false);
        output->preload_path[0] = '\0';
    } else {
        /* No preload available, load synchronously (may cause jitter) */
        if (atomic_load(&output->preload_ready)) {
            log_debug("Preloaded texture mismatch: wanted '%s', have '%s'", path, output->preload_path);
        }

        /* If a background decode is sitting in the handoff slot for a different
         * path, drop it on the floor now — otherwise render_outputs() will
         * happily upload it on the next frame and overwrite the texture we're
         * about to install. Also wait for any still-running decode to finish
         * so it can't race with us. */
        if (atomic_load(&output->preload_thread_join_pending)) {
            atomic_store(&output->preload_should_stop, true);
            pthread_join(output->preload_thread, NULL);
            atomic_store(&output->preload_thread_active, false);
            atomic_store(&output->preload_thread_join_pending, false);
        }
        pthread_mutex_lock(&output->preload_mutex);
        if (output->preload_decoded_image) {
            image_free(output->preload_decoded_image);
            output->preload_decoded_image = NULL;
        }
        atomic_store(&output->preload_upload_pending, false);
        output->preload_path[0] = '\0';
        pthread_mutex_unlock(&output->preload_mutex);

        /* Load new image with display-aware scaling */
        new_image = image_load(path, output->width, output->height, output->config->mode);
        if (!new_image) {
            log_error("Failed to load wallpaper image: %s", path);
            return;
        }
    }

    /* Defensive checks before any EGL/GL operations */
    if (!output->state) {
        log_error("Output state is NULL, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    if (output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not initialized, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    /* CRITICAL: Ensure EGL context is current for this thread before any GL operations */
    if (!output->compositor_surface || !egl_make_surface_current(output->state->egl_display, output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for wallpaper set");
        image_free(new_image);
        return;
    }

    if (output->state->egl_context == EGL_NO_CONTEXT) {
        log_error("EGL context not initialized, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_debug("EGL surface not ready for output %s, deferring wallpaper load",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }

    /* Make EGL context current before creating textures */
    if (!egl_make_surface_current(output->state->egl_display, output->compositor_surface->egl_surface, output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error("Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
                  output->model[0] ? output->model : "unknown", egl_error);
        image_free(new_image);
        return;
    }

    log_debug("EGL context made current for wallpaper load on output %s",
              output->model[0] ? output->model : "unknown");

    /* Handle transition */
    if (output->config->transition != TRANSITION_NONE && output->current_image && output->texture) {
        /* Store current image as "next_image" for transition */
        if (output->next_image) {
            image_free(output->next_image);
        }
        output->next_image = output->current_image;
        output->current_image = new_image;

        /* Start transition - NOW with preloaded texture already in GPU! */
        output->transition_start_time = get_time_ms();
        output->transition_progress = 0.0f;

        /* Destroy and recreate next texture */
        if (output->next_texture) {
            render_destroy_texture(output->next_texture);
        }
        output->next_texture = output->texture;

        /* Use preloaded texture if available, otherwise create it now */
        if (used_preload) {
            output->texture = new_texture;
        } else {
            output->texture = render_create_texture(new_image);
        }

        log_info("Transition started: %s -> %s (type=%d '%s', duration=%.2fs)%s",
                  output->config->path, path,
                  output->config->transition,
                  transition_type_to_string(output->config->transition),
                  output->config->transition_duration,
                  used_preload ? " [ZERO-STALL PRELOAD]" : "");
    } else {
        /* No transition, just replace */
        if (output->current_image) {
            image_free(output->current_image);
        }
        output->current_image = new_image;

        /* Use preloaded texture if available, otherwise create it now */
        if (output->texture) {
            render_destroy_texture(output->texture);
        }

        if (used_preload) {
            output->texture = new_texture;
        } else {
            output->texture = render_create_texture(new_image);
        }

        log_debug("Wallpaper texture created successfully (texture=%u) for output %s%s",
                 output->texture, output->model[0] ? output->model : "unknown",
                 used_preload ? " [ZERO-STALL]" : "");
    }

    /* Update config path */
    config_str_set(output->config->path, sizeof(output->config->path), path);

    /* An image wallpaper never takes keyboard input; if this output was a
     * terminal a moment ago, hand focus back to real windows. */
    output_sync_keyboard_focus(output);

    /* Initialize frame time for cycling */
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Write current state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config->mode);
    write_wallpaper_state(output_get_identifier(output), path, mode_str,
                         output->config->current_cycle_index,
                         output->config->cycle_count,
                         "active");

    /* Mark for redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    /* Preload next wallpaper if cycling is enabled */
    if (output->config->cycle && output->config->cycle_count > 1) {
        output_preload_next_wallpaper(output);
    }
}
