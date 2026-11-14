/* Decode the next cycle image on a worker thread, upload it on the loop. */

#include <stdio.h>
#include "neowall/compositor/compositor.h"
#include "neowall/egl/egl_core.h"
#include "neowall/render/render.h"  /* Only output.c includes render.h */
#include "output_internal.h"
#include "neowall/core/log.h"
#include "neowall/state/state.h"

/* Background thread function for async image decoding */
struct preload_thread_args {
    struct output_state *output;
    char path[NW_PATH_MAX];
    int32_t width;
    int32_t height;
    enum wallpaper_mode mode;
    uint64_t generation;
};

static void *preload_thread_func(void *arg) {
    struct preload_thread_args *args = (struct preload_thread_args *)arg;
    struct output_state *output = args->output;

    /* Cooperative cancellation only — libpng/libjpeg/etc. are NOT async-cancel-safe.
     * Caller signals shutdown via output->preload_should_stop. */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    log_debug("Background thread: decoding image %s (%dx%d, mode=%d)",
              args->path, args->width, args->height, args->mode);

    /* Bail early if caller already asked us to stop. */
    if (atomic_load(&output->preload_should_stop)) {
        atomic_store(&output->preload_thread_active, false);
        free(args);
        return NULL;
    }

    /* Decode image in background (CPU-bound, no GL context needed) */
    struct image_data *decoded_image = image_load(args->path, args->width, args->height, args->mode);

    if (!decoded_image) {
        log_error("Background thread: failed to decode image: %s", args->path);
        atomic_store(&output->preload_thread_active, false);
        free(args);
        return NULL;
    }

    /* If caller asked us to stop while we were decoding, throw the result away
     * rather than handing it off. */
    if (atomic_load(&output->preload_should_stop) ||
        args->generation != atomic_load(&output->preprocessing_generation)) {
        image_free(decoded_image);
        atomic_store(&output->preload_thread_active, false);
        free(args);
        return NULL;
    }

    log_debug("Background thread: decoded image %s (%ux%u) - ready for GPU upload",
             args->path, decoded_image->width, decoded_image->height);

    /* Hand off decoded image to main thread for GPU upload */
    pthread_mutex_lock(&output->preload_mutex);

    /* Clean up old decoded image if exists */
    if (output->preload_decoded_image) {
        image_free(output->preload_decoded_image);
    }

    output->preload_decoded_image = decoded_image;
    output->preload_generation = args->generation;
    output->preload_width = args->width;
    output->preload_height = args->height;
    output->preload_mode = args->mode;
    snprintf(output->preload_path, sizeof(output->preload_path), "%s", args->path);

    pthread_mutex_unlock(&output->preload_mutex);

    /* Signal main thread that upload is pending */
    atomic_store(&output->preload_upload_pending, true);
    atomic_store(&output->preload_thread_active, false);

    free(args);
    return NULL;
}

/* Start background preload of next wallpaper (non-blocking) */
void output_preload_next_wallpaper(struct output_state *output) {
    if (!output || !output->config) {
        return;
    }

    /* Only preload for cycling image wallpapers */
    if (!output->config->cycle || output->config->cycle_count <= 1 ||
        output->config->type != WALLPAPER_IMAGE) {
        return;
    }

    /* Reap a previously completed joinable thread before reusing its handle. */
    if (atomic_load(&output->preload_thread_join_pending) &&
        !atomic_load(&output->preload_thread_active)) {
        pthread_join(output->preload_thread, NULL);
        atomic_store(&output->preload_thread_join_pending, false);
    }

    /* Don't start new preload if thread is already running */
    if (atomic_load(&output->preload_thread_active)) {
        log_debug("Preload thread already active, skipping");
        return;
    }

    /* Calculate next index */
    size_t next_index = (output->config->current_cycle_index + 1) % output->config->cycle_count;

    /* Get next path - protect with state mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    if (!output->config->cycle_paths || next_index >= output->config->cycle_count) {
        pthread_mutex_unlock(&output->state->state_mutex);
        return;
    }

    const char *next_path = output->config->cycle_paths[next_index];

    /* Check if already preloaded */
    if (atomic_load(&output->preload_ready) && strcmp(output->preload_path, next_path) == 0) {
        pthread_mutex_unlock(&output->state->state_mutex);
        log_debug("Next wallpaper already preloaded: %s", next_path);
        return;
    }

    /* Prepare thread arguments */
    struct preload_thread_args *args = malloc(sizeof(struct preload_thread_args));
    if (!args) {
        pthread_mutex_unlock(&output->state->state_mutex);
        log_error("Failed to allocate preload thread args");
        return;
    }

    args->output = output;
    snprintf(args->path, sizeof(args->path), "%s", next_path);
    args->width = output->width;
    args->height = output->height;
    args->mode = output->config->mode;
    args->generation = atomic_load(&output->preprocessing_generation);

    pthread_mutex_unlock(&output->state->state_mutex);

    char preload_path[NW_PATH_MAX];
    snprintf(preload_path, sizeof(preload_path), "%s", args->path);
    log_debug("Starting background preload for output %s: %s",
              output->model[0] ? output->model : "unknown", preload_path);

    /* Launch background thread. We keep it joinable so output_destroy can wait
     * for it deterministically (we never call pthread_cancel — image codecs
     * aren't async-cancel-safe). */
    atomic_store(&output->preload_should_stop, false);
    atomic_store(&output->preload_thread_active, true);
    atomic_store(&output->preload_thread_join_pending, true);
    if (pthread_create(&output->preload_thread, NULL, preload_thread_func, args) != 0) {
        log_error("Failed to create preload thread");
        atomic_store(&output->preload_thread_active, false);
        atomic_store(&output->preload_thread_join_pending, false);
        free(args);
        return;
    }

    log_debug("Background preload thread started for: %s", preload_path);
}

/* Upload preloaded image to GPU and return texture ID */
GLuint output_upload_preload_texture(struct output_state *output) {
    if (!output || !output->preload_decoded_image) {
        return 0;
    }
    if (output->preload_generation != atomic_load(&output->preprocessing_generation) ||
        output->preload_width != output->width || output->preload_height != output->height ||
        output->preload_mode != output->config->mode) {
        image_free(output->preload_decoded_image);
        output->preload_decoded_image = NULL;
        atomic_store(&output->preload_upload_pending, false);
        log_debug("Discarded stale preloaded image after geometry/config change");
        return 0;
    }

    /* Make EGL context current */
    if (!output->compositor_surface ||
        !egl_make_surface_current(output->state->egl_display, output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for preload upload");
        return 0;
    }

    /* Upload decoded image to GPU */
    GLuint new_texture = render_create_texture(output->preload_decoded_image);
    if (new_texture != 0) {
        /* Invalidate GL state cache after texture creation */
        output->gl_state.bound_texture = 0;

        /* Clean up old preload texture if exists */
        if (output->preload_texture) {
            render_destroy_texture(output->preload_texture);
        }
        if (output->preload_image) {
            image_free(output->preload_image);
        }

        /* Store uploaded texture */
        output->preload_texture = new_texture;
        output->preload_image = output->preload_decoded_image;
        output->preload_decoded_image = NULL;
        atomic_store(&output->preload_ready, true);

        log_info("GPU upload complete: %s (texture=%u) - ZERO-STALL ready!",
                 output->preload_path, new_texture);
    } else {
        log_error("Failed to create preload texture from decoded image");
        image_free(output->preload_decoded_image);
        output->preload_decoded_image = NULL;
    }

    return new_texture;
}


