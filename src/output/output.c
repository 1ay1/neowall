#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "neowall.h"
#include "output.h"
#include "../image/image.h"    /* For struct image_data definition */
#include "compositor.h"
#include "config_access.h"
#include "constants.h"
#include "shader.h"
#include "../render/render.h"  /* Only output.c includes render.h */

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

    /* For shaders with vsync disabled, set up precise frame timer */
    if (output->config->type != WALLPAPER_SHADER) {
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
    int target_fps = output->config->shader_fps > 0 ? output->config->shader_fps : 60;
    long interval_ns = 1000000000L / target_fps;

    struct itimerspec timer_spec = {
        .it_interval = { .tv_sec = 0, .tv_nsec = interval_ns },  /* Recurring */
        .it_value = { .tv_sec = 0, .tv_nsec = interval_ns }      /* Initial expiration */
    };

    if (timerfd_settime(output->frame_timer_fd, 0, &timer_spec, NULL) < 0) {
        log_error("Failed to set frame timer for output %s: %s",
                 output_get_identifier(output), strerror(errno));
        return false;
    }

    log_debug("Configured frame timer for %d FPS (interval: %ld ns) on output %s",
             target_fps, interval_ns, output_get_identifier(output));

    return true;
}



struct output_state *output_create(struct neowall_state *state,
                                   struct wl_output *output, uint32_t name) {
    if (!state || !output) {
        log_error("Invalid parameters for output_create");
        return NULL;
    }

    struct output_state *out = calloc(1, sizeof(struct output_state));
    if (!out) {
        log_error("Failed to allocate output state: %s", strerror(errno));
        return NULL;
    }

    /* Initialize output state */
    out->output = output;
    out->xdg_output = NULL;
    out->name = name;
    out->width = 0;
    out->height = 0;
    out->logical_width = 0;
    out->logical_height = 0;
    out->pixel_width = 0;
    out->pixel_height = 0;
    out->scale = 1;
    out->transform = WL_OUTPUT_TRANSFORM_NORMAL;
    out->configured = false;
    out->needs_redraw = true;
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

    /* Clean up shader programs */
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

    /* Cancel and wait for background preload thread if active */
    if (atomic_load(&output->preload_thread_active)) {
        pthread_cancel(output->preload_thread);
        pthread_join(output->preload_thread, NULL);
        atomic_store(&output->preload_thread_active, false);
    }

    /* Free preload data */
    pthread_mutex_lock(&output->preload_mutex);
    if (output->preload_texture) {
        render_destroy_texture(output->preload_texture);
        output->preload_texture = 0;
    }

    /* Close frame timer */
    if (output->frame_timer_fd >= 0) {
        close(output->frame_timer_fd);
        output->frame_timer_fd = -1;
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

    /* Note: We don't destroy output->output as it's managed by Wayland */

    free(output);
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

    /* Enable thread cancellation at any time */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    log_debug("Background thread: decoding image %s (%dx%d, mode=%d)",
              args->path, args->width, args->height, args->mode);

    /* Decode image in background (CPU-bound, no GL context needed) */
    struct image_data *decoded_image = image_load(args->path, args->width, args->height, args->mode);

    if (!decoded_image) {
        log_error("Background thread: failed to decode image: %s", args->path);
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
    strncpy(output->preload_path, args->path, sizeof(output->preload_path) - 1);
    output->preload_path[sizeof(output->preload_path) - 1] = '\0';

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
    strncpy(args->path, next_path, sizeof(args->path) - 1);
    args->path[sizeof(args->path) - 1] = '\0';
    args->width = output->width;
    args->height = output->height;
    args->mode = output->config->mode;

    pthread_mutex_unlock(&output->state->state_mutex);

    log_debug("Starting background preload for output %s: %s",
              output->model[0] ? output->model : "unknown", args->path);

    /* Launch background thread */
    atomic_store(&output->preload_thread_active, true);
    if (pthread_create(&output->preload_thread, NULL, preload_thread_func, args) != 0) {
        log_error("Failed to create preload thread");
        atomic_store(&output->preload_thread_active, false);
        free(args);
        return;
    }

    /* Detach thread so it cleans up automatically when done */
    pthread_detach(output->preload_thread);

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
    strncpy(output->config->path, path, sizeof(output->config->path) - 1);
    output->config->path[sizeof(output->config->path) - 1] = '\0';

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
    output->needs_redraw = true;

    /* Preload next wallpaper if cycling is enabled */
    if (output->config->cycle && output->config->cycle_count > 1) {
        output_preload_next_wallpaper(output);
    }
}

/* Set live shader wallpaper */
void output_set_shader(struct output_state *output, const char *shader_path) {
    if (!output || !shader_path) {
        log_error("Invalid parameters for output_set_shader");
        return;
    }

    /* BUG FIX #8: Protect model string access with mutex to avoid data race */
    char model_copy[64];

    /* Acquire state mutex to safely read model string */
    pthread_mutex_lock(&output->state->state_mutex);
    strncpy(model_copy, output->model, sizeof(model_copy) - 1);
    model_copy[sizeof(model_copy) - 1] = '\0';
    pthread_mutex_unlock(&output->state->state_mutex);

    log_info("Setting shader for output %s: %s",
             model_copy[0] ? model_copy : "unknown", shader_path);

    /* Defensive checks before any EGL/GL operations */
    if (!output->state) {
        log_error("Output state is NULL, cannot set shader");
        return;
    }

    if (output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not initialized, cannot set shader");
        return;
    }

    /* CRITICAL: Ensure EGL context is current before any GL operations */
    if (!output->compositor_surface || !eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for shader set");
        return;
    }

    if (output->state->egl_context == EGL_NO_CONTEXT) {
        log_error("EGL context not initialized, cannot set shader");
        return;
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_debug("EGL surface not ready for output %s, deferring shader load: %s",
                  output->model[0] ? output->model : "unknown", shader_path);
        /* Store shader path in config for later application when surface is ready */
        strncpy(output->config->shader_path, shader_path, sizeof(output->config->shader_path) - 1);
        output->config->shader_path[sizeof(output->config->shader_path) - 1] = '\0';
        output->config->type = WALLPAPER_SHADER;
        return;
    }

    /* X11 backend uses backend_data instead of egl_window */
    if (!output->compositor_surface || 
        (!output->compositor_surface->egl_window && !output->compositor_surface->backend_data)) {
        log_error("EGL window not created for output %s, cannot set shader",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Make EGL context current before creating shader program */
    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error("Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
                  output->model[0] ? output->model : "unknown", egl_error);
        return;
    }

    log_debug("EGL context made current for output %s",
              output->model[0] ? output->model : "unknown");

    /* If there's an existing shader, compile new shader and switch immediately */
    if (output->live_shader_program != 0) {
        /* Prevent re-entrant shader changes */
        if (output->shader_fade_start_time > 0 && output->pending_shader_path[0] != '\0') {
            log_debug("Shader change already in progress, ignoring new request for: %s", shader_path);
            return;
        }

        log_debug("Compiling new shader: %s", shader_path);

        /* Compile new shader immediately (before switching) to avoid stutter */
        GLuint new_shader_program = 0;
        if (!shader_create_live_program(shader_path, &new_shader_program, output->channel_count)) {
            log_error("Failed to create shader program from: %s", shader_path);
            return;
        }

        if (new_shader_program == 0) {
            log_error("Invalid shader program created for: %s", shader_path);
            return;
        }

        /* Successfully compiled - now switch immediately */
        log_debug("Switching to new shader: %s", shader_path);

        /* Destroy old shader */
        shader_destroy_program(output->live_shader_program);

        /* Switch to new shader */
        output->live_shader_program = new_shader_program;
        output->shader_start_time = get_time_ms();

        /* Reset shader uniform cache for new program */
        output->shader_uniforms.position = -2;
        output->shader_uniforms.texcoord = -2;
        output->shader_uniforms.tex_sampler = -2;
        output->shader_uniforms.u_resolution = -2;
        output->shader_uniforms.u_time = -2;
        output->shader_uniforms.u_speed = -2;

        /* Reset iChannel uniform locations */
        if (output->shader_uniforms.iChannel && output->channel_count > 0) {
            for (size_t i = 0; i < output->channel_count; i++) {
                output->shader_uniforms.iChannel[i] = -2;
            }
        }

        /* Update config with new shader path - protected by state mutex */
        pthread_mutex_lock(&output->state->state_mutex);
        strncpy(output->config->shader_path, shader_path, sizeof(output->config->shader_path) - 1);
        output->config->shader_path[sizeof(output->config->shader_path) - 1] = '\0';
        pthread_mutex_unlock(&output->state->state_mutex);

        /* Write state to file */
        const char *mode_str = wallpaper_mode_to_string(output->config->mode);
        write_wallpaper_state(output_get_identifier(output), shader_path, mode_str,
                             output->config->current_cycle_index,
                             output->config->cycle_count,
                             "active");

        /* Mark for immediate redraw with new shader */
        output->needs_redraw = true;
        output->last_cycle_time = get_time_ms();

        /* Configure vsync for shader rendering */
        output_configure_vsync(output);

        /* Configure frame timer for precise pacing when vsync is disabled */
        output_configure_frame_timer(output);

        log_debug("Shader switched successfully: %s", shader_path);
        return;
    }

    /* First shader load - no fade needed, load and compile immediately */

    /* Load iChannel textures based on config */
    if (!render_load_channel_textures(output, output->config)) {
        log_error("Failed to load iChannel textures for shader: %s", shader_path);
        /* Continue anyway - shader may work without textures */
    }

    GLuint new_shader_program = 0;
    if (!shader_create_live_program(shader_path, &new_shader_program, output->channel_count)) {
        log_error("Failed to create shader program from: %s", shader_path);

        /* Clean up iChannel textures that were loaded but can't be used */
        if (output->channel_textures) {
            for (size_t i = 0; i < output->channel_count; i++) {
                if (output->channel_textures[i] != 0) {
                    render_destroy_texture(output->channel_textures[i]);
                    output->channel_textures[i] = 0;
                }
            }
            free(output->channel_textures);
            output->channel_textures = NULL;
        }
        if (output->shader_uniforms.iChannel) {
            free(output->shader_uniforms.iChannel);
            output->shader_uniforms.iChannel = NULL;
        }
        output->channel_count = 0;

        return;
    }

    output->live_shader_program = new_shader_program;
    output->shader_start_time = get_time_ms();

    /* Reset shader uniform cache for new program */
    output->shader_uniforms.position = -2;
    output->shader_uniforms.texcoord = -2;
    output->shader_uniforms.tex_sampler = -2;
    output->shader_uniforms.u_resolution = -2;
    output->shader_uniforms.u_time = -2;
    output->shader_uniforms.u_speed = -2;

    /* Reset iChannel uniform locations to uninitialized */
    if (output->shader_uniforms.iChannel && output->channel_count > 0) {
        for (size_t i = 0; i < output->channel_count; i++) {
            output->shader_uniforms.iChannel[i] = -2;
        }
    }

    log_debug("Shader loaded (first): %s", shader_path);

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

    /* Update config - protected by state mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    strncpy(output->config->shader_path, shader_path, sizeof(output->config->shader_path) - 1);
    output->config->shader_path[sizeof(output->config->shader_path) - 1] = '\0';
    output->config->type = WALLPAPER_SHADER;
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Initialize frame time for animation */
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Mark for redraw */
    output->needs_redraw = true;

    /* Write current state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config->mode);
    write_wallpaper_state(output_get_identifier(output), shader_path, mode_str,
                         output->config->current_cycle_index,
                         output->config->cycle_count,
                         "active");

    log_debug("Live shader wallpaper loaded successfully");
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
        strncpy(model_copy, output->model, sizeof(model_copy) - 1);
        model_copy[sizeof(model_copy) - 1] = '\0';
        const char *output_name = model_copy[0] ? model_copy : "unknown";
        log_info("Shader transition in progress on output '%s', deferring cycle request",
                 output_name);
        return;
    }

    /* Move to next wallpaper/shader - protect cycle_paths access with mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    size_t old_index = output->config->current_cycle_index;
    output->config->current_cycle_index =
        (output->config->current_cycle_index + 1) % output->config->cycle_count;

    /* Copy path before releasing lock to avoid use-after-free if config reloads */
    char next_path_copy[MAX_PATH_LENGTH];
    const char *next_path = output->config->cycle_paths[output->config->current_cycle_index];
    strncpy(next_path_copy, next_path, sizeof(next_path_copy) - 1);
    next_path_copy[sizeof(next_path_copy) - 1] = '\0';
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
            output_set_shader(output, next_path);
        } else {
            output_set_wallpaper(output, next_path);
        }

        /* Mark the output for redraw to ensure change is visible */
        output->needs_redraw = true;
    }

    log_info("Wallpaper cycle completed successfully");
}

/* Check if output needs to cycle wallpaper based on duration */
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

    if (output->config->type == WALLPAPER_SHADER && output->live_shader_program == 0) {
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

        /* Restore cycle index from state file for this specific output */
        const char *output_id = output_get_identifier(output);
        int saved_index = restore_cycle_index_from_state(output_id);
        if (saved_index >= 0 && saved_index < (int)output->config->cycle_count) {
            output->config->current_cycle_index = saved_index;
            log_info("Restored cycle position for %s: %d/%zu",
                    output_id, saved_index, output->config->cycle_count);

            /* Update the initial path to use the restored index */
            if (output->config->type == WALLPAPER_SHADER) {
                strncpy(output->config->shader_path,
                       output->config->cycle_paths[saved_index],
                       sizeof(output->config->shader_path) - 1);
                output->config->shader_path[sizeof(output->config->shader_path) - 1] = '\0';
            } else {
                strncpy(output->config->path,
                       output->config->cycle_paths[saved_index],
                       sizeof(output->config->path) - 1);
                output->config->path[sizeof(output->config->path) - 1] = '\0';
            }
        }
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
    output->needs_redraw = true;

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
        if (output->live_shader_program == 0) {
            log_info("Applying deferred shader config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->shader_path);
            output_set_shader(output, output->config->shader_path);
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
        if (!output->current_image && output->texture == 0 && output->live_shader_program == 0) {
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
