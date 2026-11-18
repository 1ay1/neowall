/* An output: create, destroy, refcount, EGL surface, geometry changes, and
 * the render/input wrappers the app uses. See output_internal.h for the rest. */

#include <errno.h>
#include <unistd.h>
#include "neowall/surface/surface.h"
#include "render.h"
#include "output_internal.h"
#include "neowall/config/config.h"
#include "neowall/core/log.h"
#include "neowall/state/state.h"
#include "neowall/egl/egl_core.h"

/* Helper function to get the preferred output identifier
 * Prefers connector_name (e.g., "HDMI-A-2", "DP-1") over model name
 * for consistent identification across reboots/reconnections */
const char *output_get_identifier(const struct output_state *output) {
    if (output->connector_name[0] != '\0') {
        return output->connector_name;
    }
    return output->model;
}

/* Keep the surface's keyboard interactivity in step with the wallpaper type.
 *
 * The layer surface is created in output_configure_compositor_surface() long
 * before the config that says "this output is a terminal" has been parsed, so
 * the interactivity chosen at creation time is stale for exactly the case that
 * needs it: a terminal wallpaper comes up keyboard-inert and could never be
 * typed into on first paint. Every path that changes output->config->type must
 * call this afterwards.
 *
 * Direction of failure is safe: only WALLPAPER_TERMINAL asks for keyboard
 * focus, and it is ON_DEMAND (never EXCLUSIVE), so an image/shader wallpaper
 * can only ever lose keyboard focus here, never grab it from real windows. */
void output_sync_keyboard_focus(struct output_state *output) {
    if (!output || !output->compositor_surface || !output->config) {
        return;
    }

    bool want_kbd = output->config->type == WALLPAPER_TERMINAL;

    if (!compositor_surface_set_keyboard_interactivity(output->compositor_surface, want_kbd) &&
        want_kbd) {
        /* Backend pins interactivity at creation (X11, GNOME subsurface,
         * fallback). Not fatal — those paths route keys by other means — but
         * worth saying out loud when a terminal wallpaper won't take input. */
        log_debug("Backend cannot enable keyboard focus post-creation for output %s",
                  output->model[0] ? output->model : "unknown");
    }
}

struct output_state *output_new(struct neowall_state *state, void *native_output, uint32_t name) {
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
    out->native_output  = native_output;
    out->xdg_output     = NULL;
    out->name           = name;
    out->width          = 0;
    out->height         = 0;
    out->logical_width  = 0;
    out->logical_height = 0;
    out->pixel_width    = 0;
    out->pixel_height   = 0;
    out->scale          = 1;
    out->transform      = COMPOSITOR_TRANSFORM_NORMAL;
    out->configured     = false;
    atomic_init(&out->needs_redraw, true);
    atomic_init(&out->occluded, false);
    atomic_init(&out->refcount, 1); /* the output list's reference */
    out->state             = state;
    out->connector_name[0] = '\0';

    /* Initialize preload state */
    out->preload_texture = 0;
    out->preload_image   = NULL;
    out->preload_path[0] = '\0';
    atomic_init(&out->preload_ready, false);

    /* Initialize background preload thread state */
    pthread_mutex_init(&out->preload_mutex, NULL);
    out->preload_decoded_image = NULL;
    atomic_init(&out->preload_thread_active, false);
    atomic_init(&out->preload_thread_join_pending, false);
    atomic_init(&out->preload_should_stop, false);
    atomic_init(&out->preprocessing_generation, 1);
    atomic_init(&out->geometry_change_pending, false);
    out->terminal_auto_cols = false;
    out->terminal_auto_rows = false;
    atomic_init(&out->preload_upload_pending, false);

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
    out->config->mode                = MODE_FILL;
    out->config->duration            = 0;
    out->config->transition          = TRANSITION_NONE;
    out->config->transition_duration = 300;
    out->config->cycle               = false;
    out->config->shuffle             = false;
    out->config->cycle_paths         = NULL;
    out->config->cycle_count         = 0;
    out->config->current_cycle_index = 0;
    out->config->type                = WALLPAPER_IMAGE;
    out->config->path[0]             = '\0';
    out->config->shader_path[0]      = '\0';
    out->config->shader_speed        = 1.0f;
    out->config->shader_fps          = 60; /* Default 60 FPS */
    /* Default: vsync ON, matching config.c's parsed default.
     *
     * calloc leaves this false, and an output that never gets a parsed config
     * (preview, `neowall new`, a hotplugged output before config applies)
     * would keep that false and request ASYNC presentation -- the compositor
     * then scans out frames the moment they land instead of on the refresh
     * boundary, so the frame interval is never the shader's dt and motion
     * visibly jerks even while the FPS counter looks healthy. */
    out->config->vsync         = true;
    out->config->show_fps      = false; /* Default: no FPS watermark */
    out->config->channel_paths = NULL;
    out->config->channel_count = 0;

    nw_frame_clock_init(&out->frame_clock);

    /* Initialize FPS tracking */
    out->fps_last_log_time = 0;
    out->fps_frame_count   = 0;
    out->fps_current       = 0.0f;

    /* Initialize mouse tracking (-1 means use default center position) */
    out->mouse_x = -1.0f;
    out->mouse_y = -1.0f;

    /* Initialize frame timer for precise pacing when vsync is disabled */
    out->frame_timer_fd = -1; /* Will be created when needed */

    log_debug("Created output state (name=%u)", name);
    return out;
}

/* ── the output list ─────────────────────────────────────────────────────
 * Each takes output_list_lock as writer itself. The list holds one
 * reference per output. */

void output_list_add(struct neowall_state *state, struct output_state *out) {
    pthread_rwlock_wrlock(&state->output_list_lock);
    out->next      = state->outputs;
    state->outputs = out;
    state->output_count++;
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Unlink; the list's reference becomes the caller's. False if it wasn't in
 * the list (someone else already removed it). */
bool output_list_take(struct neowall_state *state, struct output_state *out) {
    bool found = false;
    pthread_rwlock_wrlock(&state->output_list_lock);
    for (struct output_state **pp = &state->outputs; *pp; pp = &(*pp)->next) {
        if (*pp == out) {
            *pp       = out->next;
            out->next = NULL;
            state->output_count--;
            found = true;
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return found;
}

/* Unlink the output with this registry name; NULL if none. */
struct output_state *output_list_take_name(struct neowall_state *state, uint32_t name) {
    struct output_state *found = NULL;
    pthread_rwlock_wrlock(&state->output_list_lock);
    for (struct output_state **pp = &state->outputs; *pp; pp = &(*pp)->next) {
        if ((*pp)->name == name) {
            found       = *pp;
            *pp         = found->next;
            found->next = NULL;
            state->output_count--;
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return found;
}

/* Empty the list, handing each output to `drop` (which must unref it). */
void output_list_clear(struct neowall_state *state, void (*drop)(struct output_state *)) {
    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *o = state->outputs;
    state->outputs         = NULL;
    state->output_count    = 0;
    pthread_rwlock_unlock(&state->output_list_lock);
    while (o) {
        struct output_state *next = o->next;
        o->next                   = NULL;
        drop(o);
        o = next;
    }
}

void output_destroy(struct output_state *output) {
    if (!output) {
        return;
    }

    log_debug("Destroying output %s (name=%u)", output->model[0] ? output->model : "unknown",
              output->name);

    /* Clean up rendering resources */
    render_cleanup_output(output);

    wallshader_free(output->shader);
    output->shader = NULL;

    /* Free wallpaper config, and a reload still waiting on its shader */
    output_unstage(output);
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
    if (atomic_load(&output->preload_thread_join_pending)) {
        atomic_store(&output->preload_should_stop, true);
        pthread_join(output->preload_thread, NULL);
        atomic_store(&output->preload_thread_active, false);
        atomic_store(&output->preload_thread_join_pending, false);
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

    log_debug("Destroying output %s (name=%u)", output->model[0] ? output->model : "unknown",
              output->name);

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
                  output->model[0] ? output->model : "unknown", output->width, output->height);
        return false;
    }

    /* Check if EGL surface already exists */
    if (output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        log_debug("EGL surface already exists for output %s, skipping creation",
                  output->model[0] ? output->model : "unknown");
        return true;
    }

    log_debug("Creating EGL surface for output %s: %dx%d",
              output->model[0] ? output->model : "unknown", output->width, output->height);

    /* Create EGL surface through compositor abstraction */
    EGLSurface egl_surface =
        compositor_surface_create_egl(output->compositor_surface, output->state->egl_display,
                                      output->state->egl_config, output->width, output->height);

    if (egl_surface == EGL_NO_SURFACE) {
        log_error("Failed to create EGL surface for output %s",
                  output->model[0] ? output->model : "unknown");
        return false;
    }

    log_debug("Created EGL surface for output %s", output->model[0] ? output->model : "unknown");

    log_debug("Created EGL surface for output %s: %dx%d",
              output->model[0] ? output->model : "unknown", output->width, output->height);

    return true;
}

/* Callback-safe half of geometry handling. Bumping the generation immediately
 * makes any in-flight decoder discard its result; teardown and GL replacement
 * are deliberately left to the event-loop/render thread. */
void output_notify_geometry_change(struct output_state *output) {
    if (!output) return;
    atomic_fetch_add_explicit(&output->preprocessing_generation, 1, memory_order_acq_rel);
    atomic_store_explicit(&output->preload_should_stop, true, memory_order_release);
    atomic_store_explicit(&output->geometry_change_pending, true, memory_order_release);
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
}

void output_process_geometry_change(struct output_state *output) {
    if (!output ||
        !atomic_exchange_explicit(&output->geometry_change_pending, false, memory_order_acq_rel)) {
        return;
    }

    /* Reap/cancel CPU preloads before clearing their handoff slots. */
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
    pthread_mutex_unlock(&output->preload_mutex);

    /* This runs only after the event loop made this output's context current. */
    if (output->preload_texture) {
        render_destroy_texture(output->preload_texture);
        output->preload_texture = 0;
    }
    if (output->preload_image) {
        image_free(output->preload_image);
        output->preload_image = NULL;
    }
    atomic_store(&output->preload_ready, false);
    output->preload_path[0] = '\0';

    if (output->config && output->config->type == WALLPAPER_IMAGE && output->config->path[0] &&
        output->width > 0 && output->height > 0) {
        /* Build both CPU image and GPU texture before touching the visible pair.
         * A decode/upload failure therefore leaves the old wallpaper intact. */
        struct image_data *replacement =
            image_load(output->config->path, output->width, output->height, output->config->mode);
        if (replacement) {
            GLuint texture = render_create_texture(replacement);
            if (texture) {
                struct image_data *old_image   = output->current_image;
                GLuint old_texture             = output->texture;
                output->current_image          = replacement;
                output->texture                = texture;
                output->gl_state.bound_texture = 0;
                if (old_texture) render_destroy_texture(old_texture);
                if (old_image) image_free(old_image);
                log_info("Reprocessed wallpaper for output %s at %dx%d",
                         output->model[0] ? output->model : "unknown", output->width,
                         output->height);
            } else {
                image_free(replacement);
                log_error("Keeping old wallpaper after resize texture upload failed");
            }
        } else {
            log_error("Keeping old wallpaper after resize preprocessing failed: %s",
                      output->config->path);
        }
        output_preload_next_wallpaper(output);
    } else if (output->config && output->config->type == WALLPAPER_TERMINAL && output->shader) {
        int cell_h = output->config->term_font_size > 0 ? output->config->term_font_size : 18;
        if (cell_h < 6) cell_h = 6;
        if (cell_h > 96) cell_h = 96;
        int cell_w = (cell_h + 1) / 2;
        if (cell_w < 3) cell_w = 3;
        int cols = output->terminal_auto_cols ? output->width / cell_w : output->config->term_cols;
        int rows = output->terminal_auto_rows ? output->height / cell_h : output->config->term_rows;
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
        term_gpu_resize(wallshader_terminal(output->shader), cols, rows);
        if (output->terminal_auto_cols) output->config->term_cols = cols;
        if (output->terminal_auto_rows) output->config->term_rows = rows;
    }
}

/* Get output count */

/* Iterate through all outputs and apply a function */

/* ============================================================================
 * Rendering Wrappers - Hide render module from the runtime
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
bool output_terminal_mouse(struct output_state *output, int px, int py, int button, bool pressed,
                           bool motion) {
    if (!output || output->config->type != WALLPAPER_TERMINAL || !output->shader) {
        return false;
    }
    return term_gpu_mouse(wallshader_terminal(output->shader), px, py, button, pressed, motion);
}

/* Forward already-encoded key bytes to this output's terminal wallpaper child.
 * Returns true if written. No-op unless this output hosts a terminal. */
bool output_terminal_key(struct output_state *output, const void *bytes, size_t len) {
    if (!output || output->config->type != WALLPAPER_TERMINAL || !output->shader) {
        return false;
    }
    return term_gpu_write(wallshader_terminal(output->shader), bytes, len);
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

bool output_make_current(struct output_state *output) {
    if (!output || !output->state || !output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        return false;
    }
    return egl_make_surface_current(output->state->egl_display,
                                    output->compositor_surface->egl_surface,
                                    output->state->egl_context);
}

void output_release_shared_gl(void) {
    render_release_shared();
}
