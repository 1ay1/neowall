#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "neowall/neowall.h"
#include "neowall/output/output.h"
#include "neowall/image/image.h"    /* For struct image_data definition */
#include "neowall/compositor/compositor.h"
#include "neowall/config/config_access.h"
#include "neowall/config/config.h"  /* config_shuffle_cycle_paths() */
#include "neowall/constants.h"
#include "neowall/shader/shader.h"
#include "neowall/shader/shader_multipass.h"
#include "neowall/shader/manifest.h"
#include "neowall/render/render.h"  /* Only output.c includes render.h */

/* Helper function to get the preferred output identifier
 * Prefers connector_name (e.g., "HDMI-A-2", "DP-1") over model name
 * for consistent identification across reboots/reconnections */
static inline const char *output_get_identifier(const struct output_state *output) {
    if (output->connector_name[0] != '\0') {
        return output->connector_name;
    }
    return output->model;
}

/* Helper function to configure vsync (swap interval) for shader rendering
 * DRY principle: Single source of truth for vsync configuration logic */
static void output_configure_vsync(struct output_state *output) {
    if (!output || !output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        return;
    }

    /* Make EGL context current */
    if (!eglMakeCurrent(output->state->egl_display,
                       output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface,
                       output->state->egl_context)) {
        log_error("Failed to make EGL context current for vsync config");
        return;
    }

    /* Configure vsync based on user preference:
     * - vsync=true:  Enable vsync, sync to monitor refresh rate (ignores shader_fps)
     * - vsync=false: Disable vsync, use tearing control for custom FPS (default) */
    int swap_interval = output->config->vsync ? 1 : 0;

    if (!eglSwapInterval(output->state->egl_display, swap_interval)) {
        EGLint err = eglGetError();
        log_error("Failed to set swap interval to %d (error: 0x%x)", swap_interval, err);
        if (!output->config->vsync) {
            log_error("This may prevent achieving target FPS of %d", output->config->shader_fps);
        }
    } else {
        if (output->config->vsync) {
            log_debug("Enabled vsync for output %s (will sync to monitor refresh rate)",
                     output->model[0] ? output->model : "unknown");
        } else {
            log_debug("Disabled vsync for output %s (shader_fps=%d, target frame time: %.1fms)",
                     output->model[0] ? output->model : "unknown",
                     output->config->shader_fps,
                     1000.0f / output->config->shader_fps);
        }
    }
}

/* Helper function to configure high-precision frame timer for vsync-off mode
 * Uses timerfd for kernel-level precision instead of poll() timeout */
static bool output_configure_frame_timer(struct output_state *output) {
    if (!output) {
        return false;
    }

    /* If vsync is enabled, we don't need a frame timer - eglSwapBuffers handles pacing */
    if (output->config->vsync) {
        if (output->frame_timer_fd >= 0) {
            close(output->frame_timer_fd);
            output->frame_timer_fd = -1;
            log_debug("Closed frame timer for output %s (vsync enabled)", output_get_identifier(output));
        }
        return true;
    }

    /* For animated wallpapers (shader or terminal) with vsync disabled, set up
     * a precise frame timer. Static images don't need one. */
    if (!wallpaper_is_animated(output->config->type)) {
        if (output->frame_timer_fd >= 0) {
            close(output->frame_timer_fd);
            output->frame_timer_fd = -1;
        }
        return true;
    }

    /* Create timerfd if not already created */
    if (output->frame_timer_fd < 0) {
        output->frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (output->frame_timer_fd < 0) {
            log_error("Failed to create frame timer for output %s: %s",
                     output_get_identifier(output), strerror(errno));
            return false;
        }
        log_debug("Created frame timer fd=%d for output %s",
                 output->frame_timer_fd, output_get_identifier(output));
    }

    /* Configure timer for target FPS */
    int target_fps = shader_fps_resolve(output->config->shader_fps);
    
    /* Calculate interval in seconds and nanoseconds
     * tv_nsec must be < 1000000000 (less than 1 second) */
    time_t interval_sec = 0;
    long interval_ns = 0;
    
    if (target_fps >= 1) {
        /* For FPS >= 1, interval is less than or equal to 1 second */
        long total_ns = 1000000000L / target_fps;
        interval_sec = total_ns / 1000000000L;
        interval_ns = total_ns % 1000000000L;
        
        /* Handle the edge case where FPS=1 results in exactly 1 second */
        if (interval_ns == 0 && target_fps == 1) {
            interval_sec = 1;
            interval_ns = 0;
        }
    } else {
        /* FPS < 1 means interval > 1 second (e.g., 0.5 FPS = 2 seconds) */
        interval_sec = 1;
        interval_ns = 0;
    }

    struct itimerspec timer_spec = {
        .it_interval = { .tv_sec = interval_sec, .tv_nsec = interval_ns },  /* Recurring */
        .it_value = { .tv_sec = interval_sec, .tv_nsec = interval_ns }      /* Initial expiration */
    };

    /* Record the target period for the phase-locked pacer. The recurring
     * interval above is only the BOOTSTRAP arming; from the first present
     * onward output_pace_advance() re-arms this fd as a one-shot absolute
     * deadline, so the schedule tracks real completion times instead of
     * free-running (see output.h pace_* fields). Seed the deadline to "now +
     * one period" so the very first re-arm has a sane anchor. */
    output->pace_period_ns = (uint64_t)interval_sec * 1000000000ULL + (uint64_t)interval_ns;
    {
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + (uint64_t)now_ts.tv_nsec;
        output->pace_next_deadline_ns = now_ns + output->pace_period_ns;
    }

    if (timerfd_settime(output->frame_timer_fd, 0, &timer_spec, NULL) < 0) {
        log_error("Failed to set frame timer for output %s: %s",
                 output_get_identifier(output), strerror(errno));
        return false;
    }

    log_debug("Configured frame timer for %d FPS (interval: %ld.%09ld s) on output %s",
             target_fps, (long)interval_sec, interval_ns, output_get_identifier(output));

    return true;
}



struct output_state *output_create(struct neowall_state *state,
                                   void *native_output, uint32_t name) {
    if (!state) {
        log_error("Invalid parameters for output_create");
        return NULL;
    }

    struct output_state *out = calloc(1, sizeof(struct output_state));
    if (!out) {
        log_error("Failed to allocate output state: %s", strerror(errno));
        return NULL;
    }

    /* Initialize output state */
    out->native_output = native_output;
    out->xdg_output = NULL;
    out->name = name;
    out->width = 0;
    out->height = 0;
    out->logical_width = 0;
    out->logical_height = 0;
    out->pixel_width = 0;
    out->pixel_height = 0;
    out->scale = 1;
    out->transform = COMPOSITOR_TRANSFORM_NORMAL;
    out->configured = false;
    atomic_store_explicit(&out->needs_redraw, true, memory_order_release);
    atomic_store(&out->occluded, false);
    atomic_store_explicit(&out->refcount, 1, memory_order_release);  /* the list's reference */
    out->state = state;
    out->connector_name[0] = '\0';

    /* Initialize preload state */
    out->preload_texture = 0;
    out->preload_image = NULL;
    out->preload_path[0] = '\0';
    atomic_store(&out->preload_ready, false);

    /* Initialize background preload thread state */
    pthread_mutex_init(&out->preload_mutex, NULL);
    out->preload_decoded_image = NULL;
    atomic_store(&out->preload_thread_active, false);
    atomic_store(&out->preload_should_stop, false);
    atomic_store(&out->preload_upload_pending, false);

    /* Compositor surface will be created later in output_configure_compositor_surface() */
    out->compositor_surface = NULL;

    /* Allocate config structure */
    out->config = calloc(1, sizeof(struct wallpaper_config));
    if (!out->config) {
        log_error("Failed to allocate config for output");
        free(out);
        return NULL;
    }

    /* Initialize config with defaults */
    out->config->mode = MODE_FILL;
    out->config->duration = 0;
    out->config->transition = TRANSITION_NONE;
    out->config->transition_duration = 300;
    out->config->cycle = false;
    out->config->shuffle = false;
    out->config->cycle_paths = NULL;
    out->config->cycle_count = 0;
    out->config->current_cycle_index = 0;
    out->config->type = WALLPAPER_IMAGE;
    out->config->path[0] = '\0';
    out->config->shader_path[0] = '\0';
    out->config->shader_speed = 1.0f;
    out->config->shader_fps = 60;  /* Default 60 FPS */
    out->config->show_fps = false;  /* Default: no FPS watermark */
    out->config->channel_paths = NULL;
    out->config->channel_count = 0;

    out->shader_fade_start_time = 0;
    out->shader_paused_at = 0;
    out->pending_shader_path[0] = '\0';

    /* Initialize FPS tracking */
    out->fps_last_log_time = 0;
    out->fps_frame_count = 0;
    out->fps_current = 0.0f;

    /* Initialize mouse tracking (-1 means use default center position) */
    out->mouse_x = -1.0f;
    out->mouse_y = -1.0f;

    /* Initialize frame timer for precise pacing when vsync is disabled */
    out->frame_timer_fd = -1;  /* Will be created when needed */

    /* Add to linked list - CALLER MUST HOLD WRITE LOCK */
    /* Note: List modification moved to caller (wayland.c) to ensure proper locking */
    out->next = state->outputs;
    state->outputs = out;
    state->output_count++;

    log_debug("Created output state (name=%u)", name);

    return out;
}

void output_destroy(struct output_state *output) {
    if (!output) {
        return;
    }

    log_debug("Destroying output %s (name=%u)",
              output->model[0] ? output->model : "unknown", output->name);

    /* Clean up rendering resources */
    render_cleanup_output(output);

    /* Clean up multipass shader */
    if (output->multipass_shader != NULL) {
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
    }

    /* Clean up legacy shader programs */
    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }

    /* Free wallpaper config */
    if (output->config) {
        config_free_wallpaper(output->config);
        free(output->config);
        output->config = NULL;
    }

    /* Free image data */
    if (output->current_image) {
        image_free(output->current_image);
        output->current_image = NULL;
    }

    if (output->next_image) {
        image_free(output->next_image);
        output->next_image = NULL;
    }

    /* Wait for background preload thread to finish. We don't pthread_cancel
     * (libpng/libjpeg are not async-cancel-safe) and we never detached the
     * thread, so a join here is correct. preload_should_stop tells the thread
     * to abandon its work if the result is no longer needed. */
    if (atomic_load(&output->preload_thread_active)) {
        atomic_store(&output->preload_should_stop, true);
        pthread_join(output->preload_thread, NULL);
        atomic_store(&output->preload_thread_active, false);
    }

    /* Close frame timer (independent of preload state; do it outside the
     * preload_mutex which had no business protecting it). The poll loop must
     * have already stopped using this fd by the time we get here — callers
     * of output_destroy must hold the output_list_lock as writer, which
     * serializes against the poll loop's fd-building rdlocked traversal. */
    if (output->frame_timer_fd >= 0) {
        close(output->frame_timer_fd);
        output->frame_timer_fd = -1;
    }

    /* Free preload data */
    pthread_mutex_lock(&output->preload_mutex);
    if (output->preload_texture) {
        render_destroy_texture(output->preload_texture);
        output->preload_texture = 0;
    }

    if (output->preload_image) {
        image_free(output->preload_image);
        output->preload_image = NULL;
    }

    if (output->preload_decoded_image) {
        image_free(output->preload_decoded_image);
        output->preload_decoded_image = NULL;
    }
    pthread_mutex_unlock(&output->preload_mutex);
    pthread_mutex_destroy(&output->preload_mutex);

    log_debug("Destroying output %s (name=%u)",
              output->model[0] ? output->model : "unknown", output->name);

    /* Destroy compositor surface (handles all surface cleanup) */
    if (output->compositor_surface) {
        if (output->compositor_surface->egl_surface != EGL_NO_SURFACE && output->state) {
            compositor_surface_destroy_egl(output->compositor_surface, output->state->egl_display);
        }
        compositor_surface_destroy(output->compositor_surface);
        output->compositor_surface = NULL;
    }

    /* Note: We don't destroy native_output as it's managed by the display server */

    free(output);
}

void output_ref(struct output_state *output) {
    if (!output) {
        return;
    }
    /* relaxed: an existing valid reference already happens-before this call, so
     * we only need atomicity of the increment, not ordering. */
    atomic_fetch_add_explicit(&output->refcount, 1, memory_order_relaxed);
}

void output_unref(struct output_state *output) {
    if (!output) {
        return;
    }
    /* acq_rel so that the thread which observes the 1->0 transition sees all
     * writes made by every other ref-holder before its unref (standard
     * refcount-release pattern), and the destroy below is properly ordered
     * after them. */
    int prev = atomic_fetch_sub_explicit(&output->refcount, 1, memory_order_acq_rel);
    if (prev == 1) {
        /* We dropped the final reference. Safe to tear down. */
        output_destroy(output);
    }
}

bool output_create_egl_surface(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for EGL surface creation (NULL)");
        return false;
    }

    if (!output->compositor_surface) {
        log_error("Invalid compositor surface for output %s (NULL)",
                  output->model[0] ? output->model : "unknown");
        return false;
    }

    if (output->width <= 0 || output->height <= 0) {
        log_debug("Output %s dimensions not ready yet: %dx%d (deferring surface creation)",
                  output->model[0] ? output->model : "unknown",
                  output->width, output->height);
        return false;
    }



    /* Check if EGL surface already exists */
    if (output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        log_debug("EGL surface already exists for output %s, skipping creation",
                  output->model[0] ? output->model : "unknown");
        return true;
    }

    log_debug("Creating EGL surface for output %s: %dx%d",
              output->model[0] ? output->model : "unknown",
              output->width, output->height);

    /* Create EGL surface through compositor abstraction */
    EGLSurface egl_surface = compositor_surface_create_egl(
        output->compositor_surface,
        output->state->egl_display,
        output->state->egl_config,
        output->width,
        output->height
    );

    if (egl_surface == EGL_NO_SURFACE) {
        log_error("Failed to create EGL surface for output %s",
                  output->model[0] ? output->model : "unknown");
        return false;
    }

    log_debug("Created EGL surface for output %s",
              output->model[0] ? output->model : "unknown");

    log_debug("Created EGL surface for output %s: %dx%d",
             output->model[0] ? output->model : "unknown",
             output->width, output->height);

    return true;
}

/* Background thread function for async image decoding */
struct preload_thread_args {
    struct output_state *output;
    char path[MAX_PATH_LENGTH];
    int32_t width;
    int32_t height;
    enum wallpaper_mode mode;
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
    if (atomic_load(&output->preload_should_stop)) {
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

    pthread_mutex_unlock(&output->state->state_mutex);

    log_debug("Starting background preload for output %s: %s",
              output->model[0] ? output->model : "unknown", args->path);

    /* Launch background thread. We keep it joinable so output_destroy can wait
     * for it deterministically (we never call pthread_cancel — image codecs
     * aren't async-cancel-safe). */
    atomic_store(&output->preload_should_stop, false);
    atomic_store(&output->preload_thread_active, true);
    if (pthread_create(&output->preload_thread, NULL, preload_thread_func, args) != 0) {
        log_error("Failed to create preload thread");
        atomic_store(&output->preload_thread_active, false);
        free(args);
        return;
    }

    log_debug("Background preload thread started for: %s", args->path);
}

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
        if (atomic_load(&output->preload_thread_active)) {
            atomic_store(&output->preload_should_stop, true);
            pthread_join(output->preload_thread, NULL);
            atomic_store(&output->preload_thread_active, false);
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
    if (!output->compositor_surface || !eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
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
    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
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
    snprintf(output->config->path, sizeof(output->config->path), "%s", path);

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

/* Set live shader wallpaper */
nw_result output_set_shader(struct output_state *output, const char *shader_path) {
    if (!output || !shader_path) {
        log_error("Invalid parameters for output_set_shader");
        return nw_err(NW_ERR_INVALID_ARG, "NULL output or shader path");
    }

    /* If asked to load a .neowall manifest directly, resolve the .glsl it names
     * and load that instead (the manifest is re-read below to apply bindings). */
    char resolved_path[MAX_PATH_LENGTH];
    const char *manifest_path = shader_path;  /* used to apply bindings later */
    if (manifest_resolve_shader_path(shader_path, resolved_path, sizeof(resolved_path))) {
        log_info("Manifest %s -> shader %s", shader_path, resolved_path);
        shader_path = resolved_path;
        /* manifest_path stays pointing at the .neowall file */
    } else {
        /* Ordinary .glsl: manifest_apply() will look for a <base>.neowall sidecar. */
        manifest_path = shader_path;
    }

    /* Copy the model string under the state mutex to avoid a data race with
     * the registry/output-destroy paths that can rewrite it concurrently. */
    char model_copy[64];

    /* Acquire state mutex to safely read model string */
    pthread_mutex_lock(&output->state->state_mutex);
    snprintf(model_copy, sizeof(model_copy), "%s", output->model);
    pthread_mutex_unlock(&output->state->state_mutex);

    log_info("Setting shader for output %s: %s",
             model_copy[0] ? model_copy : "unknown", shader_path);

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
    if (!output->compositor_surface || !eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
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
        snprintf(output->config->shader_path, sizeof(output->config->shader_path), "%s", shader_path);
        output->config->type = WALLPAPER_SHADER;
        /* Deferred, not failed: the render loop applies this path once the
         * surface comes up, so callers must not treat it as a load failure. */
        return nw_ok();
    }

    /* X11 backend uses backend_data instead of egl_window */
    if (!output->compositor_surface ||
        (!output->compositor_surface->egl_window && !output->compositor_surface->backend_data)) {
        log_error("EGL window not created for output %s, cannot set shader",
                  output->model[0] ? output->model : "unknown");
        return nw_err(NW_ERR_GL, "EGL window not created");
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return nw_err(NW_ERR_GL, "EGL display unavailable");
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return nw_err(NW_ERR_GL, "EGL surface unavailable");
    }

    /* Make EGL context current before creating shader program */
    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error("Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
                  output->model[0] ? output->model : "unknown", egl_error);
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed");
    }

    log_debug("EGL context made current for output %s",
              output->model[0] ? output->model : "unknown");

    /* If there's an existing multipass shader, destroy it first */
    if (output->multipass_shader != NULL) {
        /* Prevent re-entrant shader changes */
        if (output->shader_fade_start_time > 0 && output->pending_shader_path[0] != '\0') {
            log_debug("Shader change already in progress, ignoring new request for: %s", shader_path);
            return nw_err(NW_ERR_AGAIN, "shader change already in progress");
        }

        log_debug("Destroying existing multipass shader before loading: %s", shader_path);
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
    }

    /* Also clean up legacy single-pass shader if present */
    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }

    /* Load shader source from file */
    char *shader_source = shader_load_file(shader_path);
    if (!shader_source) {
        log_error("Failed to load shader source from: %s", shader_path);
        return nw_err(NW_ERR_IO, "cannot read shader source");
    }

    log_info("Loaded shader source: %zu bytes from %s", strlen(shader_source), shader_path);

    /* Create multipass shader from source */
    output->multipass_shader = multipass_create(shader_source);
    free(shader_source);

    if (!output->multipass_shader) {
        log_error("Failed to create multipass shader from: %s", shader_path);
        return nw_err(NW_ERR_PARSE, "multipass_create failed");
    }

    /* Apply a .neowall manifest if present: explicit channel bindings + custom
     * reactive uniforms. Must run before compile (uniforms are injected into the
     * wrapper) and before GL init (so buffer-channel indices are correct). */
    manifest_apply(output->multipass_shader, manifest_path);

    /* Initialize GL resources for multipass rendering */
    if (!multipass_init_gl(output->multipass_shader, output->width, output->height)) {
        log_error("Failed to initialize multipass GL resources for: %s", shader_path);
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
        return nw_err(NW_ERR_GL, "multipass_init_gl failed");
    }

    /* Compile all passes */
    if (!multipass_compile_all(output->multipass_shader)) {
        char *errors = multipass_get_all_errors(output->multipass_shader);
        log_error("Failed to compile multipass shader: %s", shader_path);
        if (errors) {
            log_error("Compilation errors:\n%s", errors);
            free(errors);
        }
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
        return nw_err(NW_ERR_PARSE, "shader failed to compile");
    }

    /* Configure adaptive resolution scaling to target the config's FPS */
    int target_fps = shader_fps_resolve(output->config->shader_fps);
    multipass_set_adaptive_resolution(output->multipass_shader, 
                                      true,           /* enabled */
                                      (float)target_fps,
                                      0.25f,          /* min_scale */
                                      1.0f);          /* max_scale */
    log_info("Adaptive resolution targeting %d FPS for shader: %s", target_fps, shader_path);

    output->shader_start_time = get_time_ms();
    /* Fresh shader = fresh frame count. The static-shader idle path keys off
     * frames_rendered>0; without this reset a newly loaded static shader
     * would inherit the old count and never paint. */
    output->frames_rendered = 0;
    /* A freshly loaded shader starts from a clean timeline; clear any frozen
     * baseline so a pending resume doesn't shift start_time into the future
     * (which would underflow the elapsed-time calculation in render.c). */
    output->shader_paused_at = 0;

    log_info("Successfully loaded multipass shader with %d pass(es): %s",
             output->multipass_shader->pass_count, shader_path);

    /* Debug dump shader structure */
    multipass_debug_dump(output->multipass_shader);

    /* Update config with new shader path - protected by state mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    snprintf(output->config->shader_path, sizeof(output->config->shader_path), "%s", shader_path);
    output->config->type = WALLPAPER_SHADER;
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Write state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config->mode);
    write_wallpaper_state(output_get_identifier(output), shader_path, mode_str,
                         output->config->current_cycle_index,
                         output->config->cycle_count,
                         "active");

    /* Mark for immediate redraw with new shader */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    
    /* Initialize frame time for animation */
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Configure vsync based on shader_fps setting */
    if (output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                           output->compositor_surface->egl_surface, output->state->egl_context)) {
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

    log_debug("Multipass shader wallpaper loaded successfully");
    return nw_ok();
}

/* Built-in crisp terminal pass-through: sample the whole grid across the frame.
 * Used when the terminal wallpaper names no styling shader. */
static const char *kTermCrispShader =
    "void mainImage(out vec4 fragColor, in vec2 fragCoord){\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "    fragColor = vec4(nwTerm(uv), 1.0);\n"
    "}\n";

nw_result output_set_terminal(struct output_state *output, const char *cmd,
                              const char *shader_path, const char *font_path,
                              int cols, int rows) {
    if (!output || !cmd || !cmd[0]) {
        return nw_err(NW_ERR_INVALID_ARG, "output_set_terminal: null cmd");
    }
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        return nw_err(NW_ERR_GL, "EGL display not initialized");
    }
    if (!output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        /* Surface not up yet: stash it on the config and let the render loop
         * re-apply once the surface arrives (mirrors output_set_shader). */
        pthread_mutex_lock(&output->state->state_mutex);
        output->config->type = WALLPAPER_TERMINAL;
        snprintf(output->config->term_cmd, sizeof(output->config->term_cmd), "%s", cmd);
        if (shader_path) snprintf(output->config->shader_path,
                                  sizeof(output->config->shader_path), "%s", shader_path);
        if (font_path) snprintf(output->config->term_font,
                                sizeof(output->config->term_font), "%s", font_path);
        output->config->term_cols = cols;
        output->config->term_rows = rows;
        pthread_mutex_unlock(&output->state->state_mutex);
        return nw_ok();
    }

    if (!eglMakeCurrent(output->state->egl_display,
                        output->compositor_surface->egl_surface,
                        output->compositor_surface->egl_surface,
                        output->state->egl_context)) {
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed for terminal set");
    }

    if (output->multipass_shader) {
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
    }
    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }

    /* Styling shader (optional) or the built-in crisp pass-through. */
    char *shader_source = NULL;
    if (shader_path && shader_path[0]) {
        shader_source = shader_load_file(shader_path);
        if (!shader_source) {
            log_warn("Terminal styling shader '%s' unreadable; using crisp pass-through",
                     shader_path);
        }
    }
    const char *src = shader_source ? shader_source : kTermCrispShader;
    output->multipass_shader = multipass_create(src);
    free(shader_source);
    if (!output->multipass_shader) {
        return nw_err(NW_ERR_PARSE, "terminal multipass_create failed");
    }

    /* Derive a grid from the output size if not specified. Cell size is fixed at
     * a legible 9x18 by default; term_font_size overrides the cell HEIGHT (width
     * scales ~0.5x to keep a monospace aspect). */
    int cell_h = output->config->term_font_size > 0 ? output->config->term_font_size : 18;
    if (cell_h < 6)  cell_h = 6;
    if (cell_h > 96) cell_h = 96;
    int cell_w = (cell_h + 1) / 2;
    if (cell_w < 3) cell_w = 3;
    int gc = cols > 0 ? cols : output->width / cell_w;
    int gr = rows > 0 ? rows : output->height / cell_h;
    if (gc < 1) gc = 80;
    if (gr < 1) gr = 24;

    /* Carry optional terminal config onto the shader so attach_terminal can
     * pass it through to the emulator (cwd/env/font faces/default colours). */
    if (output->config->term_cwd[0])
        output->multipass_shader->term_cwd = strdup(output->config->term_cwd);
    if (output->config->term_env[0])
        output->multipass_shader->term_env = strdup(output->config->term_env);
    if (output->config->term_font_bold[0])
        output->multipass_shader->term_font_bold = strdup(output->config->term_font_bold);
    if (output->config->term_font_italic[0])
        output->multipass_shader->term_font_italic = strdup(output->config->term_font_italic);
    if (output->config->term_fg >= 0) {
        output->multipass_shader->term_fg = output->config->term_fg;
        output->multipass_shader->term_has_fg = true;
    }
    if (output->config->term_bg >= 0) {
        output->multipass_shader->term_bg = output->config->term_bg;
        output->multipass_shader->term_has_bg = true;
    }

    /* Attach the terminal BEFORE init_gl (init creates the cell/atlas textures
     * sized to the grid) and before compile (nwTerm uniforms must resolve). */
    nw_result tr = multipass_attach_terminal(output->multipass_shader, cmd,
                                             gc, gr, cell_w, cell_h,
                                             (font_path && font_path[0]) ? font_path : NULL);
    if (nw_is_err(tr)) {
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
        return tr;
    }

    if (!multipass_init_gl(output->multipass_shader, output->width, output->height)) {
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
        return nw_err(NW_ERR_GL, "multipass_init_gl failed (terminal)");
    }
    if (!multipass_compile_all(output->multipass_shader)) {
        char *errors = multipass_get_all_errors(output->multipass_shader);
        log_error("Terminal shader failed to compile:\n%s", errors ? errors : "(none)");
        free(errors);
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
        return nw_err(NW_ERR_PARSE, "terminal shader failed to compile");
    }

    int target_fps = shader_fps_resolve(output->config->shader_fps);
    multipass_set_adaptive_resolution(output->multipass_shader, true,
                                      (float)target_fps, 0.25f, 1.0f);

    output->shader_start_time = get_time_ms();
    output->frames_rendered = 0;
    output->shader_paused_at = 0;

    pthread_mutex_lock(&output->state->state_mutex);
    output->config->type = WALLPAPER_TERMINAL;
    snprintf(output->config->term_cmd, sizeof(output->config->term_cmd), "%s", cmd);
    if (shader_path) snprintf(output->config->shader_path,
                              sizeof(output->config->shader_path), "%s", shader_path);
    else output->config->shader_path[0] = '\0';
    output->config->term_cols = gc;
    output->config->term_rows = gr;
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Terminal wallpapers don't use images; free any lingering texture. */
    if (output->current_image) { image_free(output->current_image); output->current_image = NULL; }
    if (output->texture) { render_destroy_texture(output->texture); output->texture = 0; }

    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;
    output_configure_vsync(output);
    output_configure_frame_timer(output);

    log_info("Terminal wallpaper running: '%s' (%dx%d cells)", cmd, gc, gr);
    return nw_ok();
}

/* ============================================
 * Span groups — one scene across several monitors
 * ============================================ */

/* The wallpaper an output is showing right now, "" if none. */
static const char *output_live_path(const struct output_state *o) {
    if (o->config->type == WALLPAPER_SHADER) {
        return o->config->shader_path;
    }
    return o->config->path;
}

/* Order-independent fingerprint of a cycle list.
 *
 * Two outputs pointed at the same directory hold equal lists, but not
 * necessarily in equal ORDER: shuffle randomises each output's copy separately
 * when it is loaded. Comparing slot by slot would therefore split a shuffled
 * pair into two groups of one and quietly disable spanning, so the entries are
 * summed rather than sequenced — the sum is unchanged by any permutation. */
static uint64_t cycle_list_digest(const struct wallpaper_config *c) {
    uint64_t total = 0;
    for (size_t i = 0; i < c->cycle_count; i++) {
        const char *p = c->cycle_paths[i];
        uint64_t h = 1469598103934665603ull;  /* FNV-1a 64 */
        for (; p && *p; p++) {
            h ^= (unsigned char)*p;
            h *= 1099511628211ull;
        }
        total += h;
    }
    return total;
}

bool output_same_span_group(const struct output_state *a, const struct output_state *b) {
    if (!a || !b || !a->config || !b->config) {
        return false;
    }
    const struct wallpaper_config *ca = a->config;
    const struct wallpaper_config *cb = b->config;

    if (ca->type != cb->type || ca->cycle != cb->cycle) {
        return false;
    }
    if (ca->cycle) {
        if (ca->cycle_count != cb->cycle_count || !ca->cycle_paths || !cb->cycle_paths) {
            return false;
        }
        return cycle_list_digest(ca) == cycle_list_digest(cb);
    }
    return strcmp(output_live_path(a), output_live_path(b)) == 0;
}

void outputs_update_spans(struct output_state **outs, size_t count) {
    if (!outs || count == 0) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        struct output_state *me = outs[i];
        if (!me) {
            continue;
        }

        struct span_rect rects[MAX_OUTPUTS];
        size_t n = 0;
        size_t self = 0;
        uint64_t start = 0;

        for (size_t j = 0; j < count && n < MAX_OUTPUTS; j++) {
            struct output_state *o = outs[j];
            if (!o || !output_same_span_group(me, o)) {
                continue;
            }
            if (o == me) {
                self = n;
            }

            /* THE UNIT SEAM. x_offset/y_offset are LOGICAL on Wayland (they come
             * from xdg-output's logical_position) while width/height are the
             * PHYSICAL framebuffer (logical * scale). span_rect wants all four
             * logical.
             *
             * Prefer the compositor's own logical_size when we have it: that is
             * the exact box it laid the output out in. Reconstructing it as
             * width/scale is exact only under INTEGER scale, where the physical
             * size is a whole multiple of the logical one. Under fractional
             * scale the compositor rounds the framebuffer independently, so
             * width/scale is off by up to a pixel — enough to make two heads'
             * logical boxes disagree and seam the shared scene. Fall back to
             * width/scale when logical_size has not arrived (or the compositor
             * lacks zxdg_output_manager_v1). On X11 scale is 1, there is no
             * xdg logical size, and width/scale is the identity. */
            int32_t scale = output_normalized_scale(o);
            int32_t logical_w =
                o->xdg_logical_width > 0 ? o->xdg_logical_width : o->width / scale;
            int32_t logical_h =
                o->xdg_logical_height > 0 ? o->xdg_logical_height : o->height / scale;
            rects[n++] = (struct span_rect){
                .logical_x = o->x_offset,
                .logical_y = o->y_offset,
                .logical_w = logical_w,
                .logical_h = logical_h,
                .scale     = scale,
                /* The real framebuffer, so span_compute maps the logical box
                 * into exactly these pixels under fractional scale. */
                .device_w  = o->width,
                .device_h  = o->height,
            };

            /* Earliest start in the group, so every member's iTime agrees. A
             * member that loaded a moment later must not run a moment behind:
             * the seam between two halves of one scene shows any phase gap. */
            if (o->shader_start_time > 0 && (start == 0 || o->shader_start_time < start)) {
                start = o->shader_start_time;
            }
        }

        me->span_start_time = start;

        struct span_view v;
        if (n < 2 || !span_compute(rects, n, self, &v)) {
            me->spanned = false;
            continue;
        }
        /* Degenerate group: the box came out exactly this output's own size at
         * zero offset, which means every peer's rect lies inside this one's. The
         * spanned path would hand the shader this output's own resolution and no
         * offset — precisely what the standalone path already does — so short-
         * circuit it. Reached by a cloned/mirrored group (several outputs on one
         * logical region, equal or nested, as `xrandr --same-as` produces), where
         * each peer is separately either degenerate itself or given the sub-rect
         * of the box it actually covers.
         *
         * It is ALSO how a group whose rects were built from mismatched units
         * presents, because a swallowed neighbour nests exactly like a smaller
         * clone. The two are indistinguishable from the rects alone, so this is
         * logged rather than rejected: a wrongly-nested group is a bug at the
         * seam above, not something to detect down here. */
        if (v.virt_w == me->width && v.virt_h == me->height &&
            v.off_x == 0 && v.off_y == 0) {
            if (me->spanned) {
                log_debug("Output %s: no longer spanning, its logical rect "
                          "%dx%d+%d+%d (scale %d) contains every peer's in the group",
                          output_get_identifier(me),
                          rects[self].logical_w, rects[self].logical_h,
                          rects[self].logical_x, rects[self].logical_y, rects[self].scale);
            }
            me->spanned = false;
            continue;
        }
        if (!me->spanned) {
            log_debug("Output %s: spanning, drawing %dx%d+%d+%d of a %dx%d virtual screen",
                      output_get_identifier(me),
                      me->width, me->height, v.off_x, v.off_y, v.virt_w, v.virt_h);
        }
        me->span = v;
        me->spanned = true;
    }
}

/* Slot of `path` in a `count`-long cycle list, or `fallback` if it is absent.
 *
 * A shuffled cycle pass permutes the list in place when it wraps, so an index
 * captured before a pass no longer names the same entry after it. The entry that
 * is actually live is known by path, so its slot is re-found by string compare
 * rather than carried as an index. */
static size_t cycle_restore_index(char *const *paths, size_t count, const char *path,
                                  size_t fallback) {
    if (!paths || !path || path[0] == '\0') return fallback;

    for (size_t i = 0; i < count; i++) {
        if (paths[i] && strcmp(paths[i], path) == 0) return i;
    }

    return fallback;
}

/* Put `o` on exactly the wallpaper `leader` is showing, and point o's cycle
 * index at it, so the pair stays in step from here on.
 *
 * Only the leader ever steps a cycle; members are pushed its result. Their lists
 * hold the same entries but need not hold them in the same ORDER (shuffle
 * randomises each output's copy separately), so the leader's index means nothing
 * here and the entry is located in o's own list by path. */
static void output_adopt_group_wallpaper(struct output_state *o, struct output_state *leader) {
    char entry[OUTPUT_MAX_PATH_LENGTH];
    char live[OUTPUT_MAX_PATH_LENGTH];

    pthread_mutex_lock(&o->state->state_mutex);
    size_t index = leader->config->current_cycle_index;
    if (leader->config->cycle_paths && index < leader->config->cycle_count) {
        snprintf(entry, sizeof(entry), "%s", leader->config->cycle_paths[index]);
    } else {
        entry[0] = '\0';
    }
    snprintf(live, sizeof(live), "%s", output_live_path(leader));

    if (entry[0] != '\0' && o->config->cycle_paths) {
        o->config->current_cycle_index =
            cycle_restore_index(o->config->cycle_paths, o->config->cycle_count, entry,
                                o->config->current_cycle_index);
    }
    pthread_mutex_unlock(&o->state->state_mutex);

    if (live[0] == '\0') {
        return;
    }

    /* Remember the attempt before making it: if this shader compiles on the
     * leader's GPU context but not on this one, we must not queue it again on
     * every pass of the main loop. */
    snprintf(o->span_synced_path, sizeof(o->span_synced_path), "%s", live);

    if (o->config->type == WALLPAPER_SHADER) {
        if (strcmp(output_live_path(o), live) != 0 &&
            nw_is_err(output_set_shader(o, live))) {
            log_error("Failed to sync output %s onto its span group's shader: %s",
                      o->model[0] ? o->model : "unknown", live);
            return;
        }
        /* Shader + image cycling: the shader is fixed and the cycle list feeds
         * iChannel0, so the group is in step only once the image matches too. */
        if (entry[0] != '\0' && strcmp(entry, live) != 0 &&
            !render_update_channel_texture(o, 0, entry)) {
            log_error("Failed to sync iChannel0 on output %s: %s",
                      o->model[0] ? o->model : "unknown", entry);
            return;
        }
    } else {
        output_set_wallpaper(o, live);
    }

    atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
}

void outputs_sync_span_group(struct output_state **outs, size_t count, size_t index) {
    if (!outs || index >= count || !outs[index]) {
        return;
    }
    struct output_state *me = outs[index];

    /* The first member in list order leads; everyone else follows it. */
    struct output_state *leader = NULL;
    for (size_t i = 0; i < count; i++) {
        if (outs[i] && output_same_span_group(me, outs[i])) {
            leader = outs[i];
            break;
        }
    }
    if (!leader || leader == me) {
        return;
    }

    const char *live = output_live_path(leader);
    if (live[0] == '\0' || strcmp(output_live_path(me), live) == 0) {
        return;
    }
    if (strcmp(me->span_synced_path, live) == 0) {
        return;  /* already tried this one and it did not take */
    }

    log_info("Syncing output %s onto its span group's wallpaper: %s",
             me->model[0] ? me->model : "unknown", live);
    output_adopt_group_wallpaper(me, leader);
}

size_t output_cycle_group(struct output_state **outs, size_t count, struct output_state *leader) {
    if (!leader) {
        return 0;
    }

    /* Membership is settled BEFORE the leader moves: a shuffled wrap permutes
     * the leader's list in place, and the members have not been given the new
     * order yet, so asking again afterwards could fail to recognise them. */
    struct output_state *members[MAX_OUTPUTS];
    size_t n = 0;
    for (size_t i = 0; i < count && n < MAX_OUTPUTS; i++) {
        if (outs && outs[i] && outs[i] != leader && output_same_span_group(outs[i], leader)) {
            members[n++] = outs[i];
        }
    }

    output_cycle_wallpaper(leader);

    for (size_t i = 0; i < n; i++) {
        members[i]->span_synced_path[0] = '\0';
        output_adopt_group_wallpaper(members[i], leader);
        /* The group moved together, so its members' cycle timers must restart
         * together too — otherwise each one fires again on its own schedule. */
        members[i]->last_cycle_time = leader->last_cycle_time;
    }

    return n + 1;
}

/* Cycle to next wallpaper in the cycle list */
void output_cycle_wallpaper(struct output_state *output) {
    if (!output) {
        log_error("Cannot cycle wallpaper: output is NULL");
        return;
    }

    if (!output->config->cycle || output->config->cycle_count == 0) {
        /* Provide clear feedback about why cycling is not possible */
        const char *output_name = output->model[0] ? output->model : "unknown";

        if (output->config->cycle_count == 0) {
            log_info("Cannot cycle wallpaper on output '%s': No wallpapers configured for cycling",
                     output_name);
            log_info("Hint: Configure multiple wallpapers using a directory path or duration setting");
        } else if (!output->config->cycle) {
            log_info("Cannot cycle wallpaper on output '%s': Cycling is disabled",
                     output_name);
            log_info("Current wallpaper: %s",
                     output->config->type == WALLPAPER_SHADER ?
                     output->config->shader_path : output->config->path);
        }

        /* Write state file to indicate cycling is not available */
        const char *current_path = output->config->type == WALLPAPER_SHADER ?
                                   output->config->shader_path : output->config->path;
        const char *mode_str = wallpaper_mode_to_string(output->config->mode);
        write_wallpaper_state(output_get_identifier(output), current_path, mode_str, 0, 0,
                             "cycling not enabled");

        return;
    }

    /* Don't cycle if a shader cross-fade is in progress */
    if (output->config->type == WALLPAPER_SHADER &&
        output->shader_fade_start_time > 0 &&
        output->pending_shader_path[0] != '\0') {
        /* Use local copy of model to avoid race if output is being modified */
        char model_copy[64];
        snprintf(model_copy, sizeof(model_copy), "%s", output->model);
        const char *output_name = model_copy[0] ? model_copy : "unknown";
        log_info("Shader transition in progress on output '%s', deferring cycle request",
                 output_name);
        return;
    }

    /* Move to next wallpaper/shader - protect cycle_paths access with mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    size_t old_index = output->config->current_cycle_index;
    size_t next_index =
        (output->config->current_cycle_index + 1) % output->config->cycle_count;

    /* End of the sequence — if shuffle is on, re-randomise so the next pass
     * is a fresh order (issue #47). keep_first_at_zero=true pins the
     * just-shown item at slot 0 so we don't immediately repeat it; we then
     * advance past it by leaving next_index at the wrap (0) and bumping it. */
    if (next_index == 0 && output->config->shuffle &&
        output->config->cycle_paths && output->config->cycle_count > 1) {
        /* Pin the currently-displayed item at index 0 so the shuffle keeps
         * it there — swap it in, then shuffle [1, n). After the shuffle we
         * step to index 1 (the first item of the freshly-randomised tail). */
        size_t cur = output->config->current_cycle_index;
        if (cur != 0) {
            char *tmp = output->config->cycle_paths[0];
            output->config->cycle_paths[0] = output->config->cycle_paths[cur];
            output->config->cycle_paths[cur] = tmp;
        }
        config_shuffle_cycle_paths(output->config->cycle_paths,
                                   output->config->cycle_count, true);
        next_index = 1;
        log_debug("Cycle wrap on output %s: re-shuffled %zu-entry list",
                 output->model[0] ? output->model : "unknown",
                 output->config->cycle_count);
    }

    output->config->current_cycle_index = next_index;

    /* Copy path before releasing lock to avoid use-after-free if config reloads */
    char next_path_copy[MAX_PATH_LENGTH];
    const char *next_path = output->config->cycle_paths[output->config->current_cycle_index];
    snprintf(next_path_copy, sizeof(next_path_copy), "%s", next_path);
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Use the copied path from here onwards */
    next_path = next_path_copy;

    /* Detect if we're in "shader + image cycling" mode:
     * - Type is WALLPAPER_SHADER (we have a shader)
     * - shader_path is set (the main shader to keep)
     * - cycle_paths contains images (not shaders)
     *
     * In this mode, we keep the same shader but cycle images through iChannel0
     */
    bool is_shader_with_image_cycling = false;
    if (output->config->type == WALLPAPER_SHADER &&
        output->config->shader_path[0] != '\0') {
        /* Check if the first cycle path looks like an image (not a .glsl shader) */
        const char *ext = strrchr(next_path, '.');
        if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                   strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".PNG") == 0 ||
                   strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0)) {
            is_shader_with_image_cycling = true;
        }
    }

    if (is_shader_with_image_cycling) {
        /* Shader + Image Cycling mode: Update iChannel0 with the next image */
        log_debug("Cycling image for shader on output %s: index %zu->%zu (%zu/%zu): %s",
                 output->model[0] ? output->model : "unknown",
                 old_index,
                 output->config->current_cycle_index,
                 output->config->current_cycle_index + 1,
                 output->config->cycle_count,
                 next_path);

        /* Update iChannel0 with the new image */
        if (!render_update_channel_texture(output, 0, next_path)) {
            log_error("Failed to update iChannel0 with: %s", next_path);
            return;
        }

        /* Write state to file */
        const char *mode_str = wallpaper_mode_to_string(output->config->mode);
        write_wallpaper_state(output_get_identifier(output), output->config->shader_path, mode_str,
                             output->config->current_cycle_index,
                             output->config->cycle_count,
                             "active");

        log_debug("Image cycled through shader successfully");
    } else {
        /* Normal cycling mode: change the wallpaper or shader entirely */
        const char *type_str = (output->config->type == WALLPAPER_SHADER) ? "shader" : "wallpaper";
        log_info("Cycling %s for output %s: index %zu->%zu (%zu/%zu): %s",
                 type_str,
                 output->model[0] ? output->model : "unknown",
                 old_index,
                 output->config->current_cycle_index,
                 output->config->current_cycle_index + 1,
                 output->config->cycle_count,
                 next_path);

        /* Apply the next item based on type */
        if (output->config->type == WALLPAPER_SHADER) {
            /* A shader that fails to load leaves the output with no program
             * bound, so skip past it to the next entry that does load. Only
             * content errors (unreadable file, bad GLSL) are worth retrying:
             * a GL/state error means this output cannot load any shader right
             * now, and every remaining candidate would fail the same way.
             * Bounded by cycle_count so an all-broken list terminates. */
            nw_result r = output_set_shader(output, next_path);

            for (size_t tried = 1;
                 tried < output->config->cycle_count &&
                 (r.status == NW_ERR_IO || r.status == NW_ERR_PARSE);
                 tried++) {
                log_warn("Shader '%s' failed to load on output %s (%s: %s); "
                         "skipping to next entry in the cycle",
                         next_path, output->model[0] ? output->model : "unknown",
                         nw_status_str(r.status), r.context ? r.context : "");

                pthread_mutex_lock(&output->state->state_mutex);
                output->config->current_cycle_index =
                    (output->config->current_cycle_index + 1) % output->config->cycle_count;
                snprintf(next_path_copy, sizeof(next_path_copy), "%s",
                         output->config->cycle_paths[output->config->current_cycle_index]);
                pthread_mutex_unlock(&output->state->state_mutex);
                next_path = next_path_copy;

                log_info("Cycling shader for output %s: index %zu (%zu/%zu): %s",
                         output->model[0] ? output->model : "unknown",
                         output->config->current_cycle_index,
                         output->config->current_cycle_index + 1,
                         output->config->cycle_count,
                         next_path);

                r = output_set_shader(output, next_path);
            }

            if (nw_is_err(r)) {
                log_error("No shader in the cycle list loaded on output %s (last error: %s: %s)",
                          output->model[0] ? output->model : "unknown",
                          nw_status_str(r.status), r.context ? r.context : "");
            }
        } else {
            output_set_wallpaper(output, next_path);
        }

        /* Mark the output for redraw to ensure change is visible */
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    }

    /* Update cycle list file for 'neowall list' command */
    if (output->config->cycle_paths && output->config->cycle_count > 0) {
        write_cycle_list(output_get_identifier(output),
                        output->config->cycle_paths,
                        output->config->cycle_count,
                        output->config->current_cycle_index);
    }

    log_info("Wallpaper cycle completed successfully");
}

/* Check if output needs to cycle wallpaper based on duration */
/* Set wallpaper to a specific index in the cycle */
void output_set_cycle_index(struct output_state *output, size_t index) {
    if (!output) {
        log_error("Cannot set cycle index: output is NULL");
        return;
    }

    if (!output->config->cycle || output->config->cycle_count == 0) {
        const char *output_name = output->model[0] ? output->model : "unknown";
        log_error("Cannot set cycle index on output '%s': Cycling is not enabled or no wallpapers configured",
                  output_name);
        return;
    }

    /* Validate index is within bounds */
    if (index >= output->config->cycle_count) {
        log_error("Invalid cycle index %zu: must be between 0 and %zu",
                  index, output->config->cycle_count - 1);
        return;
    }

    /* Don't do anything if already at the requested index */
    if (output->config->current_cycle_index == index) {
        log_info("Already at wallpaper index %zu", index);
        return;
    }

    /* Set the index */
    pthread_mutex_lock(&output->state->state_mutex);
    size_t old_index = output->config->current_cycle_index;
    output->config->current_cycle_index = index;

    /* Copy path before releasing lock */
    char path_copy[MAX_PATH_LENGTH];
    const char *path = output->config->cycle_paths[index];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    pthread_mutex_unlock(&output->state->state_mutex);

    const char *type_str = (output->config->type == WALLPAPER_SHADER) ? "shader" : "wallpaper";
    log_info("Setting %s index for output %s: %zu -> %zu (%zu/%zu): %s",
             type_str,
             output->model[0] ? output->model : "unknown",
             old_index, index,
             index + 1, output->config->cycle_count,
             path_copy);

    /* Apply the wallpaper/shader at the new index */
    if (output->config->type == WALLPAPER_SHADER) {
        output_set_shader(output, path_copy);
    } else {
        output_set_wallpaper(output, path_copy);
    }

    /* Mark for redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    /* Update cycle list file for 'neowall list' command */
    if (output->config->cycle_paths && output->config->cycle_count > 0) {
        write_cycle_list(output_get_identifier(output),
                        output->config->cycle_paths,
                        output->config->cycle_count,
                        output->config->current_cycle_index);
    }

    log_info("Wallpaper index set successfully");
}

bool output_should_cycle(struct output_state *output, uint64_t current_time) {
    if (!output) {
        return false;
    }

    if (!output->config->cycle) {
        return false;
    }

    if (output->config->duration == 0.0f) {
        return false;
    }

    /* For images, check if current_image exists. For shaders, check if shader program exists */
    if (output->config->type == WALLPAPER_IMAGE && !output->current_image) {
        return false;
    }

    if (output->config->type == WALLPAPER_SHADER && output->multipass_shader == NULL && output->live_shader_program == 0) {
        return false;
    }

    if (output->config->cycle_count <= 1) {
        return false;
    }

    uint64_t elapsed_ms = current_time - output->last_cycle_time;
    uint64_t duration_ms = (uint64_t)(output->config->duration * 1000.0f);  /* Convert seconds to milliseconds */

    bool should_cycle = elapsed_ms >= duration_ms;

    if (should_cycle) {
        log_debug("Output %s should cycle: elapsed=%lums >= duration=%lums (current_index=%zu/%zu)",
                  output->model[0] ? output->model : "unknown",
                  elapsed_ms, duration_ms,
                  output->config->current_cycle_index,
                  output->config->cycle_count);
    }

    return should_cycle;
}

/* Find output by name */
struct output_state *output_find_by_name(struct neowall_state *state, uint32_t name) {
    if (!state) {
        return NULL;
    }

    struct output_state *output = state->outputs;
    while (output) {
        if (output->name == name) {
            return output;
        }
        output = output->next;
    }

    return NULL;
}

/* Find output by model string */
struct output_state *output_find_by_model(struct neowall_state *state, const char *model) {
    if (!state || !model) {
        return NULL;
    }

    struct output_state *output = state->outputs;
    while (output) {
        if (strcmp(output->model, model) == 0) {
            return output;
        }
        output = output->next;
    }

    return NULL;
}

/* Apply wallpaper configuration to an output */
/* Apply wallpaper configuration to an output */
bool output_apply_config(struct output_state *output, struct wallpaper_config *config) {
    if (!output || !config) {
        log_error("Invalid parameters for output_apply_config");
        return false;
    }

    log_debug("Applying config to output %s (compositor_surface=%p, configured=%d)",
              output->model[0] ? output->model : "unknown",
              (void*)output->compositor_surface,
              output->configured);

    log_info("Config for output %s: type=%s, mode=%s, transition=%d, duration=%.2fs",
             output->model[0] ? output->model : "unknown",
             config->type == WALLPAPER_SHADER ? "shader" : "image",
             wallpaper_mode_to_string(config->mode),
             config->transition,
             config->duration);

    /* Free old config data */
    config_free_wallpaper(output->config);

    /* Copy new config (simple memcpy since no hot-reload) */
    memcpy(output->config, config, sizeof(struct wallpaper_config));

    /* Deep copy channel_paths array if present */
    output->config->channel_paths = NULL;
    output->config->channel_count = 0;
    if (config->channel_paths && config->channel_count > 0) {
        output->config->channel_paths = calloc(config->channel_count, sizeof(char *));
        if (output->config->channel_paths) {
            output->config->channel_count = config->channel_count;
            for (size_t i = 0; i < config->channel_count; i++) {
                if (config->channel_paths[i]) {
                    output->config->channel_paths[i] = strdup(config->channel_paths[i]);
                }
            }
        }
    }

    /* Deep copy cycle_paths array if present */
    output->config->cycle_paths = NULL;
    output->config->cycle_count = 0;
    if (config->cycle && config->cycle_paths && config->cycle_count > 0) {
        output->config->cycle_paths = calloc(config->cycle_count, sizeof(char *));
        if (output->config->cycle_paths) {
            output->config->cycle_count = config->cycle_count;
            for (size_t i = 0; i < config->cycle_count; i++) {
                if (config->cycle_paths[i]) {
                    output->config->cycle_paths[i] = strdup(config->cycle_paths[i]);
                }
            }
        }

        /* Restore cycle index from state file for this specific output.
         * Skipped under shuffle: the saved index refers to a different
         * (previous) random permutation of the directory, so resuming at
         * that slot would just point to an arbitrary file (issue #47). */
        if (!output->config->shuffle) {
            const char *output_id = output_get_identifier(output);
            int saved_index = restore_cycle_index_from_state(output_id);
            if (saved_index >= 0 && saved_index < (int)output->config->cycle_count) {
                output->config->current_cycle_index = saved_index;
                log_info("Restored cycle position for %s: %d/%zu",
                        output_id, saved_index, output->config->cycle_count);

                /* Update the initial path to use the restored index */
                if (output->config->type == WALLPAPER_SHADER) {
                    snprintf(output->config->shader_path,
                             sizeof(output->config->shader_path), "%s",
                             output->config->cycle_paths[saved_index]);
                } else {
                    snprintf(output->config->path,
                             sizeof(output->config->path), "%s",
                             output->config->cycle_paths[saved_index]);
                }
            }
        }

        /* Write cycle list file for 'neowall list' command */
        write_cycle_list(output_get_identifier(output),
                        output->config->cycle_paths,
                        output->config->cycle_count,
                        output->config->current_cycle_index);
    }

    log_debug("Config applied - type=%d, cycle=%d, cycle_count=%zu, cycle_index=%zu",
              output->config->type, output->config->cycle, output->config->cycle_count,
              output->config->current_cycle_index);

    /* If we don't have a compositor surface yet, defer actual wallpaper loading */
    if (!output->compositor_surface || !output->configured) {
        log_debug("Output %s not yet configured, deferring wallpaper load",
                  output->model[0] ? output->model : "unknown");
        return true;
    }

    /* Configure vsync based on config */
    output_configure_vsync(output);
    output_configure_frame_timer(output);

    /* Load initial wallpaper based on type */
    if (output->config->type == WALLPAPER_SHADER) {
        /* Shader mode */
        const char *initial_shader = NULL;

        if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
            /* Load first shader from cycle list */
            initial_shader = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Loading initial shader from cycle: %s (index %zu/%zu)",
                     initial_shader, output->config->current_cycle_index, output->config->cycle_count);
        } else if (output->config->shader_path[0] != '\0') {
            /* Load single shader */
            initial_shader = output->config->shader_path;
            log_info("Loading single shader: %s", initial_shader);
        }

        if (initial_shader) {
            output_set_shader(output, initial_shader);
        } else {
            log_error("No shader configured for output %s", output->model);
            return false;
        }
    } else if (output->config->type == WALLPAPER_TERMINAL) {
        /* Terminal mode: spawn the configured command as the wallpaper. */
        if (output->config->term_cmd[0] != '\0') {
            log_info("Loading terminal wallpaper: %s", output->config->term_cmd);
            output_set_terminal(output, output->config->term_cmd,
                                output->config->shader_path[0] ? output->config->shader_path : NULL,
                                output->config->term_font[0] ? output->config->term_font : NULL,
                                output->config->term_cols, output->config->term_rows);
        } else {
            log_error("No terminal command configured for output %s", output->model);
            return false;
        }
    } else {
        /* Image mode */
        const char *initial_path = NULL;

        if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
            /* Load first image from cycle list */
            initial_path = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Loading initial image from cycle: %s (index %zu/%zu)",
                     initial_path, output->config->current_cycle_index, output->config->cycle_count);
        } else if (output->config->path[0] != '\0') {
            /* Load single image */
            initial_path = output->config->path;
            log_info("Loading single image: %s", initial_path);
        }

        if (initial_path) {
            output_set_wallpaper(output, initial_path);
        } else {
            log_error("No image path configured for output %s", output->model);
            return false;
        }
    }

    /* Initialize cycle time */
    output->last_cycle_time = get_time_ms();

    /* Request immediate redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    log_info("Successfully applied config to output %s", output->model);
    return true;
}
void output_apply_deferred_config(struct output_state *output) {
    if (!output) {
        return;
    }

    /* Check if output is ready for rendering */
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE || !output->compositor_surface->egl_window) {
        log_debug("Output %s not ready for deferred config application",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Check if there's a deferred config to apply */
    if (output->config->type == WALLPAPER_SHADER && output->config->shader_path[0] != '\0') {
        /* Check if shader is not yet loaded */
        if (output->multipass_shader == NULL && output->live_shader_program == 0) {
            log_info("Applying deferred shader config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->shader_path);
            output_set_shader(output, output->config->shader_path);
        }
    } else if (output->config->type == WALLPAPER_TERMINAL && output->config->term_cmd[0] != '\0') {
        if (output->multipass_shader == NULL && output->live_shader_program == 0) {
            log_info("Applying deferred terminal config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->term_cmd);
            output_set_terminal(output, output->config->term_cmd,
                                output->config->shader_path[0] ? output->config->shader_path : NULL,
                                output->config->term_font[0] ? output->config->term_font : NULL,
                                output->config->term_cols, output->config->term_rows);
        }
    } else if (output->config->type == WALLPAPER_IMAGE && output->config->path[0] != '\0') {
        /* Check if wallpaper is not yet loaded */
        if (!output->current_image && output->texture == 0) {
            log_info("Applying deferred wallpaper config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->path);
            output_set_wallpaper(output, output->config->path);
        }
    } else if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
        /* Handle cycling mode */
        if (!output->current_image && output->texture == 0 && output->multipass_shader == NULL && output->live_shader_program == 0) {
            const char *initial_path = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Applying deferred cycle config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     initial_path);

            /* Determine if it's a shader or image */
            const char *ext = strrchr(initial_path, '.');
            if (ext && (strcmp(ext, ".glsl") == 0 || strcmp(ext, ".frag") == 0)) {
                output_set_shader(output, initial_path);
            } else {
                output_set_wallpaper(output, initial_path);
            }
        }
    }
}

/* Get output count */
uint32_t output_get_count(struct neowall_state *state) {
    if (!state) {
        return 0;
    }
    return state->output_count;
}

/* Iterate through all outputs and apply a function */
void output_foreach(struct neowall_state *state,
                   void (*callback)(struct output_state *, void *),
                   void *userdata) {
    if (!state || !callback) {
        return;
    }

    struct output_state *output = state->outputs;
    while (output) {
        struct output_state *next = output->next;
        callback(output, userdata);
        output = next;
    }
}

/* ============================================================================
 * Rendering Wrappers - Hide render module from eventloop
 * ============================================================================ */

/* Render a frame for this output */
bool output_render_frame(struct output_state *output) {
    if (!output) {
        return false;
    }
    return render_frame(output);
}

/* Forward a pointer event to this output's terminal wallpaper, if it has one.
 * px/py are pixel coordinates relative to the output's top-left. Returns true
 * if a mouse report was sent to the child (i.e. the app wanted mouse input). */
bool output_terminal_mouse(struct output_state *output, int px, int py,
                           int button, bool pressed, bool motion) {
    if (!output || output->config->type != WALLPAPER_TERMINAL ||
        !output->multipass_shader) {
        return false;
    }
    return multipass_terminal_mouse(output->multipass_shader, px, py,
                                    button, pressed, motion);
}

/* Forward already-encoded key bytes to this output's terminal wallpaper child.
 * Returns true if written. No-op unless this output hosts a terminal. */
bool output_terminal_key(struct output_state *output, const void *bytes, size_t len) {
    if (!output || output->config->type != WALLPAPER_TERMINAL ||
        !output->multipass_shader) {
        return false;
    }
    return multipass_terminal_write(output->multipass_shader, bytes, len);
}

/* Upload preloaded image to GPU and return texture ID */
GLuint output_upload_preload_texture(struct output_state *output) {
    if (!output || !output->preload_decoded_image) {
        return 0;
    }

    /* Make EGL context current */
    if (!eglMakeCurrent(output->state->egl_display,
                       output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface,
                       output->state->egl_context)) {
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

/* Clean up transition resources after transition completes */
void output_cleanup_transition(struct output_state *output) {
    if (!output) {
        return;
    }

    /* Clean up old texture */
    if (output->next_texture) {
        render_destroy_texture(output->next_texture);
        output->next_texture = 0;
    }

    if (output->next_image) {
        image_free(output->next_image);
        output->next_image = NULL;
    }
}

/**
 * Initialize rendering resources for an output
 * Wrapper around render_init_output()
 */
bool output_init_render(struct output_state *output) {
    if (!output) {
        log_error("output_init_render: Invalid output parameter");
        return false;
    }

    return render_init_output(output);
}

/**
 * Destroy a texture
 * Wrapper around render_destroy_texture()
 */
void output_destroy_texture(GLuint texture) {
    render_destroy_texture(texture);
}

/**
 * Get frame timer file descriptor for precise frame pacing
 * Returns -1 if timer not active (vsync enabled or not a shader)
 */
int output_get_frame_timer_fd(struct output_state *output) {
    if (!output) {
        return -1;
    }
    return output->frame_timer_fd;
}

/* Phase-locked frame pacer: re-arm the per-output frame timer as a ONE-SHOT
 * ABSOLUTE deadline exactly one period after the previous target, instead of
 * letting a free-running recurring interval drift out of phase with the
 * display.
 *
 * Called once per output right after its buffer swap completes. `now_ns` is a
 * CLOCK_MONOTONIC timestamp captured just after the swap (the best available
 * phase anchor for when the frame actually reached the compositor without the
 * full wp_presentation feedback path).
 *
 * Behaviour:
 *   - advance the deadline by exactly one period (preserves average FPS with
 *     zero long-term drift — errors don't accumulate);
 *   - if we've fallen more than one period behind (a slow frame, a stall, or
 *     the app was just unpaused), SNAP the deadline forward to now+period so we
 *     drop the backlog instead of firing a burst of catch-up frames;
 *   - arm with TFD_TIMER_ABSTIME so the kernel wakes us at the true wall-clock
 *     instant, absorbing poll()/scheduler latency that a relative timer bakes
 *     into every subsequent frame.
 *
 * No-op (returns false) if the pacer is inactive (vsync on, static wallpaper,
 * or no frame timer) — those paths keep their existing scheduling. */
bool output_pace_advance(struct output_state *output, uint64_t now_ns) {
    if (!output || output->frame_timer_fd < 0 || output->pace_period_ns == 0) {
        return false;
    }

    uint64_t period = output->pace_period_ns;
    uint64_t deadline = output->pace_next_deadline_ns + period;

    /* Overrun recovery: if the next scheduled deadline is already in the past
     * (we missed one or more slots), resync the phase to the present rather
     * than trying to "catch up" with rapid-fire frames the display can't show. */
    if (deadline <= now_ns) {
        deadline = now_ns + period;
    }
    output->pace_next_deadline_ns = deadline;

    struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },   /* one-shot: we re-arm each present */
        .it_value = {
            .tv_sec  = (time_t)(deadline / 1000000000ULL),
            .tv_nsec = (long)(deadline % 1000000000ULL),
        },
    };
    if (timerfd_settime(output->frame_timer_fd, TFD_TIMER_ABSTIME, &ts, NULL) < 0) {
        /* Fall back to the free-running arming already in place; not fatal. */
        return false;
    }
    return true;
}
