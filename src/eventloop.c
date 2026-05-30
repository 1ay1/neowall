#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include "neowall.h"
#include "config_access.h"
#include "constants.h"
#include "compositor.h"
#include "occlusion/occlusion.h"

/* Forward declarations */
extern void handle_signal_from_fd(struct neowall_state *state, int signum);

static struct neowall_state *event_loop_state = NULL;

/* Poll-set sizing. File-scope (was a function-scoped #define which leaked
 * into following translation units — see audit fix #36). */
enum {
    BASE_FD_COUNT = 4,
    MAX_POLL_FDS  = BASE_FD_COUNT + MAX_OUTPUTS,
};

/* Internal: compute & arm the cycle timer. Caller must NOT hold output_list_lock
 * (we take it ourselves). The _locked variant skips the lock for callers that
 * already hold it as reader. */
static void update_cycle_timer_locked(struct neowall_state *state);
static void update_cycle_timer(struct neowall_state *state) {
    if (!state) return;
    pthread_rwlock_rdlock(&state->output_list_lock);
    update_cycle_timer_locked(state);
    pthread_rwlock_unlock(&state->output_list_lock);
}

static void update_cycle_timer_locked(struct neowall_state *state) {
    if (!state || state->timer_fd < 0) {
        return;
    }

    uint64_t now = get_time_ms();
    uint64_t next_wake_ms = UINT64_MAX;

    bool paused = atomic_load_explicit(&state->paused, memory_order_acquire);
    struct output_state *output = state->outputs;
    while (output) {
        if (!paused && output->config->cycle && output->config->duration > 0.0f &&
            output->config->cycle_count > 1 && output->current_image) {
            uint64_t elapsed_ms = now - output->last_cycle_time;
            uint64_t duration_ms = (uint64_t)(output->config->duration * 1000.0f);  /* Convert seconds to milliseconds */

            if (elapsed_ms >= duration_ms) {
                /* Should cycle now */
                next_wake_ms = 0;
                break;
            } else {
                uint64_t time_until_cycle = duration_ms - elapsed_ms;
                if (time_until_cycle < next_wake_ms) {
                    next_wake_ms = time_until_cycle;
                }
            }
        }
        output = output->next;
    }

    /* Set the timer */
    struct itimerspec timer_spec;
    memset(&timer_spec, 0, sizeof(timer_spec));

    if (next_wake_ms == UINT64_MAX) {
        /* No cycling needed, disarm timer */
        timer_spec.it_value.tv_sec = 0;
        timer_spec.it_value.tv_nsec = 0;
    } else if (next_wake_ms == 0) {
        /* Fire immediately. timerfd treats {0,0} as DISARM, so use 1ns instead
         * (audit fix #35). */
        timer_spec.it_value.tv_sec = 0;
        timer_spec.it_value.tv_nsec = 1;
    } else {
        /* Set timer to wake at next cycle time */
        timer_spec.it_value.tv_sec = next_wake_ms / MS_PER_SECOND;
        timer_spec.it_value.tv_nsec = (next_wake_ms % MS_PER_SECOND) * NS_PER_MS;
    }

    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = 0;

    if (timerfd_settime(state->timer_fd, 0, &timer_spec, NULL) < 0) {
        log_error("Failed to set timerfd: %s", strerror(errno));
    } else if (next_wake_ms != UINT64_MAX) {
        log_debug("Cycle timer set to wake in %lums", next_wake_ms);
    }
}

/* Render all outputs that need redrawing */
struct swap_info {
    struct output_state *output;
    bool render_success;
};

/* Freeze or unfreeze the shader animation across every shader output.
 *
 * Freezing records the wall-clock instant the animation stopped. Unfreezing
 * advances each shader's start_time by the frozen duration, so the animation
 * picks up from the exact frame it stopped on rather than jumping forward by
 * however long it was paused. Runs on the main loop thread, the only writer of
 * shader_start_time / shader_paused_at, so those fields need no atomics; the
 * read lock guards the list walk against output add/remove. */
static void apply_shader_pause(struct neowall_state *state, bool paused) {
    uint64_t now = get_time_ms();
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->type != WALLPAPER_SHADER) {
            continue;
        }
        if (paused) {
            if (o->shader_paused_at == 0) {
                o->shader_paused_at = now;
            }
        } else if (o->shader_paused_at != 0) {
            o->shader_start_time += now - o->shader_paused_at;
            o->shader_paused_at = 0;
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

static void render_outputs(struct neowall_state *state) {
    if (!state) {
        return;
    }

    uint64_t current_time = get_time_ms();

    /* Check if there are any pending next requests - cycle ALL outputs with matching configs */
    int next_count = atomic_load_explicit(&state->next_requested, memory_order_acquire);
    int set_index = atomic_load_explicit(&state->set_index_requested, memory_order_acquire);
    bool processed_next = false;
    bool processed_set_index = false;
    bool has_cycleable_output = false;
    int total_outputs = 0;

    /* === PHASE 1: snapshot the output list under the read lock ============
     * We copy the pointer list to a stack array and then drop the lock so
     * that all GL/EGL work below (which may BLOCK on vsync inside
     * eglMakeCurrent / eglSwapBuffers) runs without holding output_list_lock.
     * The outputs themselves stay alive as long as removal requires the
     * write lock, so the pointers remain valid for the duration of this
     * call as long as no writer can complete during it. */
    struct output_state *outputs_snapshot[MAX_OUTPUTS];
    size_t output_n = 0;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && output_n < MAX_OUTPUTS; o = o->next) {
        outputs_snapshot[output_n++] = o;
        total_outputs++;
        if (o->config->cycle && o->config->cycle_count > 0) {
            has_cycleable_output = true;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (next_count > 0) {
        log_debug("Processing next request: %d pending in queue", next_count);
    }

    /* === PHASE 2: handle set-index / next requests and per-output cycling ===
     * These call back into output_set_* and output_cycle_* which take their
     * own locks; we must NOT be holding output_list_lock here. */
    struct swap_info swap_list[MAX_OUTPUTS];
    size_t swap_n = 0;

    for (size_t idx = 0; idx < output_n; idx++) {
        struct output_state *output = outputs_snapshot[idx];

        /* Handle set-index request - set ALL outputs with same config to the specified index */
        if (set_index >= 0 && !processed_set_index && output->config->cycle && output->config->cycle_count > 0) {
            int set_count = 0;
            for (size_t j = idx; j < output_n; j++) {
                struct output_state *sync_output = outputs_snapshot[j];
                bool same_config = (sync_output->config->cycle &&
                                   sync_output->config->cycle_count == output->config->cycle_count);
                if (same_config && sync_output->config->cycle_paths && output->config->cycle_paths) {
                    for (size_t i = 0; i < output->config->cycle_count && same_config; i++) {
                        if (strcmp(sync_output->config->cycle_paths[i], output->config->cycle_paths[i]) != 0) {
                            same_config = false;
                        }
                    }
                }
                if (same_config) {
                    if ((size_t)set_index < sync_output->config->cycle_count) {
                        log_debug("Setting wallpaper index %d for output %s (synchronized)",
                                 set_index, sync_output->model[0] ? sync_output->model : "unknown");
                        output_set_cycle_index(sync_output, (size_t)set_index);
                        set_count++;
                    } else {
                        log_error("Index %d out of bounds for output %s (max: %zu)",
                                 set_index, sync_output->model[0] ? sync_output->model : "unknown",
                                 sync_output->config->cycle_count - 1);
                    }
                }
            }
            current_time = get_time_ms();
            processed_set_index = true;
            log_info("Set wallpaper index %d on %d output(s) with matching configuration",
                     set_index, set_count);
            atomic_store_explicit(&state->set_index_requested, -1, memory_order_release);
        }

        /* Handle next wallpaper request - cycle ALL outputs with same config for synchronization */
        if (next_count > 0 && !processed_next && output->config->cycle && output->config->cycle_count > 0) {
            int cycled_count = 0;
            for (size_t j = idx; j < output_n; j++) {
                struct output_state *sync_output = outputs_snapshot[j];
                bool same_config = (sync_output->config->cycle &&
                                   sync_output->config->cycle_count == output->config->cycle_count);
                if (same_config && sync_output->config->cycle_paths && output->config->cycle_paths) {
                    for (size_t i = 0; i < output->config->cycle_count && same_config; i++) {
                        if (strcmp(sync_output->config->cycle_paths[i], output->config->cycle_paths[i]) != 0) {
                            same_config = false;
                        }
                    }
                }
                if (same_config) {
                    log_debug("Cycling to next wallpaper for output %s (synchronized)",
                             sync_output->model[0] ? sync_output->model : "unknown");
                    output_cycle_wallpaper(sync_output);
                    cycled_count++;
                }
            }
            current_time = get_time_ms();
            processed_next = true;
            log_info("Cycled %d output(s) with matching configuration (%d requests remaining)",
                     cycled_count, next_count - 1);
            atomic_fetch_sub_explicit(&state->next_requested, 1, memory_order_acq_rel);
        }

        /* Check if we should cycle wallpaper (timer-driven) */
        if (!atomic_load_explicit(&state->paused, memory_order_acquire) &&
            output->config->cycle && output->config->duration > 0.0f) {
            if (output_should_cycle(output, current_time)) {
                output_cycle_wallpaper(output);
                current_time = get_time_ms();
            }
        }

        /* Skip rendering if output is occluded by a fullscreen window */
        if (output->config->pause_on_fullscreen &&
            atomic_load_explicit(&output->occluded, memory_order_acquire)) {
            continue;
        }

        /* Freeze shader animation while shader-pause is active: skip the draw so
         * the last rendered frame stays on screen and the GPU stays idle. The
         * time offset is reconciled on resume in the main loop (apply_shader_pause). */
        if (output->config->type == WALLPAPER_SHADER &&
            atomic_load_explicit(&state->shader_paused, memory_order_acquire)) {
            continue;
        }

        /* Check if this output needs rendering */
        if (!atomic_load_explicit(&output->needs_redraw, memory_order_acquire) ||
            !output->compositor_surface ||
            output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
            continue;
        }

        /* Make EGL context current for this output. Note: eglMakeCurrent can
         * synchronize with the GPU — we are NOT holding output_list_lock here. */
        if (!eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                           output->compositor_surface->egl_surface, state->egl_context)) {
            log_error("Failed to make EGL context current for output %s: 0x%x",
                     output->model, eglGetError());
            continue;
        }

        /* Recalculate time for accurate transition timing */
        current_time = get_time_ms();

        /* Check if background thread finished decoding - upload to GPU now.
         * The EGL context is already current from the call above. */
        if (atomic_load(&output->preload_upload_pending)) {
            pthread_mutex_lock(&output->preload_mutex);
            if (output->preload_decoded_image) {
                GLuint new_texture = output_upload_preload_texture(output);
                if (new_texture == 0) {
                    log_error("Failed to upload preload texture");
                }
            }
            atomic_store(&output->preload_upload_pending, false);
            pthread_mutex_unlock(&output->preload_mutex);
        }

        /* Handle image transitions */
        if (output->transition_start_time > 0 &&
            output->config->transition != TRANSITION_NONE) {
            uint64_t elapsed = current_time - output->transition_start_time;
            uint64_t transition_duration_ms = (uint64_t)(output->config->transition_duration * 1000.0f);
            float progress = transition_duration_ms > 0
                ? (float)elapsed / (float)transition_duration_ms
                : 1.0f;
            if (progress >= 1.0f) {
                output->transition_progress = 1.0f;
            } else {
                output->transition_progress = ease_in_out_cubic(progress);
            }
        }

        /* Render frame */
        uint64_t frame_start = get_time_ms();
        bool render_success = output_render_frame(output);
        uint64_t frame_end = get_time_ms();

        /* FPS measurement for shaders */
        if (render_success && output->config->type == WALLPAPER_SHADER) {
            output->fps_frame_count++;
            if (output->fps_last_log_time == 0) {
                output->fps_last_log_time = frame_end;
            }
            uint64_t elapsed = frame_end - output->fps_last_log_time;
            if (elapsed >= 2000) {
                float actual_fps = (float)output->fps_frame_count / ((float)elapsed / 1000.0f);
                output->fps_current = actual_fps;
                uint64_t frame_time = frame_end - frame_start;
                int target_fps = shader_fps_resolve(output->config->shader_fps);
                if (output->config->vsync) {
                    log_info("FPS [%s]: %.1f FPS (vsync: monitor sync, frame_time: %lums)",
                             output->model, actual_fps, frame_time);
                } else {
                    log_info("FPS [%s]: %.1f FPS (target: %d, frame_time: %lums)",
                             output->model, actual_fps, target_fps, frame_time);
                }
                output->fps_frame_count = 0;
                output->fps_last_log_time = frame_end;
            }
        }

        if (!render_success) {
            if (!output->shader_load_failed) {
                static uint64_t last_render_error_time = 0;
                static int render_error_count = 0;
                uint64_t err_now = get_time_ms();
                if (err_now - last_render_error_time >= 1000) {
                    if (render_error_count > 0) {
                        log_error("Failed to render frame for output %s (%d failures in last second)",
                                 output->model, render_error_count + 1);
                    } else {
                        log_error("Failed to render frame for output %s", output->model);
                    }
                    last_render_error_time = err_now;
                    render_error_count = 0;
                } else {
                    render_error_count++;
                }
            }
            state->errors_count++;
        }

        /* Queue for swap. swap_n cannot exceed output_n which is bounded by MAX_OUTPUTS. */
        swap_list[swap_n].output = output;
        swap_list[swap_n].render_success = render_success;
        swap_n++;
    }

    /* === PHASE 3: damage + swap + commit (still no locks held) ============= */
    for (size_t i = 0; i < swap_n; i++) {
        struct output_state *output = swap_list[i].output;
        if (!swap_list[i].render_success) {
            continue;
        }

        if (!eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                           output->compositor_surface->egl_surface, state->egl_context)) {
            log_error("Failed to make context current before swap for output %s: 0x%x",
                     output->model, eglGetError());
            continue;
        }

        /* Damage BEFORE swap+commit — wl_surface damage must be queued before
         * the commit that publishes the new buffer (audit fix #17). */
        compositor_surface_damage(output->compositor_surface, 0, 0, INT32_MAX, INT32_MAX);

        /* Swap can block waiting for vsync; we hold no locks here. */
        if (!eglSwapBuffers(state->egl_display, output->compositor_surface->egl_surface)) {
            log_error("Failed to swap buffers for output %s: 0x%x",
                     output->model, eglGetError());
            state->errors_count++;
            continue;
        }

        static uint64_t swap_counter = 0;
        swap_counter++;
        if (swap_counter % 60 == 0) {
            log_info("Buffer swap #%lu successful for output %s", swap_counter, output->model);
        }

        compositor_surface_commit(output->compositor_surface);

        output->last_frame_time = current_time;
        state->frames_rendered++;

        /* Clean up transition after final frame is rendered */
        if (output->transition_start_time > 0 &&
            output->transition_progress >= 1.0f) {
            output->transition_start_time = 0;
            output_cleanup_transition(output);
            if (output->config->cycle && output->config->cycle_count > 1 &&
                output->config->type == WALLPAPER_IMAGE) {
                output_preload_next_wallpaper(output);
            }
        }

        /* Reset needs_redraw unless we're in a transition or using a shader wallpaper */
        if ((output->transition_start_time == 0 ||
             output->config->transition == TRANSITION_NONE) &&
            output->config->type != WALLPAPER_SHADER) {
            atomic_store_explicit(&output->needs_redraw, false, memory_order_release);
        }
    }

    /* If we had a next request but couldn't process it, inform the user */
    if (next_count > 0 && !processed_next) {
        if (!has_cycleable_output) {
            if (total_outputs == 0) {
                log_info("Cannot cycle wallpaper: No outputs are configured");
            } else if (total_outputs == 1) {
                log_info("Cannot cycle wallpaper: Current configuration has only a single wallpaper (no cycling enabled)");
                log_info("To enable cycling:");
                log_info("  - Use a directory path ending with '/' (e.g., path ~/Pictures/Wallpapers/)");
                log_info("  - Or configure a 'duration' to cycle through multiple wallpapers");
                log_info("  - Or specify multiple 'shader' files in a directory");
            } else {
                log_info("Cannot cycle wallpaper: None of the %d outputs have cycling enabled", total_outputs);
                log_info("Hint: Configure cycling with directory paths or duration settings");
            }
        }
        atomic_fetch_sub(&state->next_requested, 1);
    }

    /* Update timer after rendering changes */
    update_cycle_timer(state);
}

/* Note: Wayland-specific event handling has been moved to compositor backend operations.
 * All compositor event handling is now done via the compositor abstraction layer. */

/* Main event loop */
void event_loop_run(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state for event loop");
        return;
    }

    event_loop_state = state;

    log_info("Starting event loop");

    /* Create timerfd for event-driven wallpaper cycling */
    state->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (state->timer_fd < 0) {
        log_error("Failed to create timerfd: %s", strerror(errno));
        return;
    }
    log_info("Created timerfd for event-driven cycling");

    /* Create eventfd for waking poll on internal events (config reload, etc) */
    state->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (state->wakeup_fd < 0) {
        log_error("Failed to create eventfd: %s", strerror(errno));
        close(state->timer_fd);
        return;
    }
    log_info("Created eventfd for internal event notifications");

    /* Log initial cycling configuration for debugging - use read lock */
    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    while (output) {
        if (output->config->cycle && output->config->duration > 0.0f) {
            log_info("Output %s: cycling enabled with %zu images, duration %.2fs",
                     output->model[0] ? output->model : "unknown",
                     output->config->cycle_count,
                     output->config->duration);
        }
        output = output->next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    /* Get compositor file descriptor via backend abstraction */
    int compositor_fd = -1;
    const compositor_backend_ops_t *ops = NULL;
    void *backend_data = NULL;

    if (state->compositor_backend) {
        ops = state->compositor_backend->ops;
        backend_data = state->compositor_backend->data;

        if (ops && ops->get_fd) {
            compositor_fd = ops->get_fd(backend_data);
            if (compositor_fd < 0) {
                log_debug("Compositor backend does not provide pollable file descriptor");
            } else {
                log_info("Using compositor backend '%s' with fd %d",
                        state->compositor_backend->name, compositor_fd);
            }
        }
    } else {
        log_error("No compositor backend available");
        return;
    }

    /* Initialize occlusion detection (pause rendering when fullscreen windows cover outputs) */
    bool has_occlusion = occlusion_init(state);

    /* Base file descriptors - always polled (BASE_FD_COUNT/MAX_POLL_FDS are
     * declared at file scope as an enum). */
    struct pollfd fds[MAX_POLL_FDS];
    fds[0].fd = compositor_fd;  /* -1 if no compositor, valid fd for Wayland/X11 */
    fds[0].events = compositor_fd >= 0 ? POLLIN : 0;  /* Don't poll invalid fd */
    fds[1].fd = state->timer_fd;
    fds[1].events = POLLIN;
    fds[2].fd = state->wakeup_fd;
    fds[2].events = POLLIN;
    fds[3].fd = state->signal_fd;
    fds[3].events = POLLIN;

    int num_fds = BASE_FD_COUNT;  /* Will be increased dynamically for frame timers */

    /* Initial render for all outputs - use read lock */
    pthread_rwlock_rdlock(&state->output_list_lock);
    output = state->outputs;
    while (output) {
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
        output = output->next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    uint64_t last_stats_time = get_time_ms();
    uint64_t frame_count = 0;

    /* Perform initial render BEFORE entering event loop */
    log_info("Performing initial wallpaper render");
    render_outputs(state);

    /* Dispatch any pending compositor events */
    if (ops && ops->dispatch_events) {
        ops->dispatch_events(backend_data);
    }

    /* Flush compositor requests */
    if (ops && ops->flush) {
        ops->flush(backend_data);
    }

    /* Set initial timer for cycling */
    update_cycle_timer(state);

    log_info("Entering main event loop");

    /* Track shader state to avoid log spam */
    static bool shader_mode_logged = false;
    uint64_t log_throttle_counter = 0;

    /* Tracks the freeze state we've already applied to outputs, so the
     * (locked) reconcile only runs on an actual pause<->resume transition. */
    bool shader_pause_applied = false;

    while (atomic_load_explicit(&state->running, memory_order_acquire)) {

        /* Handle new outputs that need initialization (reconnected displays) */
        if (atomic_load_explicit(&state->outputs_need_init, memory_order_acquire)) {
            log_info("New outputs detected, will be initialized by normal config load");
            atomic_store_explicit(&state->outputs_need_init, false, memory_order_release);
            /* Don't call config_reload here - it causes deadlock on startup */
            /* Outputs will be initialized through the normal config_load path */
        }

        /* Prepare for reading events via compositor backend */
        if (ops && ops->prepare_events) {
            if (!ops->prepare_events(backend_data)) {
                log_error("Failed to prepare compositor events");
                if (ops->cancel_read) {
                    ops->cancel_read(backend_data);
                }
                break;
            }
        }

        /* Flush compositor requests before polling */
        if (ops && ops->flush) {
            if (!ops->flush(backend_data)) {
                log_error("Failed to flush compositor requests");
                if (ops->cancel_read) {
                    ops->cancel_read(backend_data);
                }
                break;
            }
        }

        /* Calculate poll timeout - use 1 second max to ensure signals are processed promptly
         * BUG FIX: Previously used POLL_TIMEOUT_INFINITE (-1) which caused slow signal response
         * (Ctrl+C, neowall kill, etc.) because poll wouldn't return until a Wayland event arrived */
        int timeout_ms = 1000; /* 1 second max - ensures signals checked regularly */

        /* Collect frame timer fds for outputs using vsync-off shaders - use read lock */
        pthread_rwlock_rdlock(&state->output_list_lock);
        output = state->outputs;
        int shader_count = 0;
        num_fds = BASE_FD_COUNT;  /* Reset to base fds */

        /* Map frame timer fd indices to outputs for targeted redraw */
        struct output_state *frame_timer_outputs[MAX_POLL_FDS];
        memset(frame_timer_outputs, 0, sizeof(frame_timer_outputs));

        while (output) {
            /* Skip frame timer for occluded outputs */
            if (output->config->pause_on_fullscreen &&
                atomic_load_explicit(&output->occluded, memory_order_acquire)) {
                output = output->next;
                continue;
            }

            /* Skip frame timer for shader outputs whose animation is frozen —
             * leaving the fd out of the poll set stops vsync-off shaders from
             * being woken to redraw. Pending expirations are read back on resume. */
            if (output->config->type == WALLPAPER_SHADER &&
                atomic_load_explicit(&state->shader_paused, memory_order_acquire)) {
                output = output->next;
                continue;
            }

            /* Check for active frame timer (vsync disabled shaders) */
            int frame_fd = output_get_frame_timer_fd(output);
            if (frame_fd >= 0) {
                /* Add frame timer to poll set */
                if (num_fds < MAX_POLL_FDS) {
                    fds[num_fds].fd = frame_fd;
                    fds[num_fds].events = POLLIN;
                    frame_timer_outputs[num_fds] = output;  /* Map index to output */
                    num_fds++;
                }
                /* With frame timers, use long timeout - timers will wake us */
                timeout_ms = 1000;
            }

            /* Only count shader as active if it loaded successfully and hasn't failed */
            if (output->config->type == WALLPAPER_SHADER &&
                !output->shader_load_failed &&
                (output->live_shader_program != 0 || output->multipass_shader != NULL)) {
                shader_count++;
                /* Only log shader detection once, not every frame */
                if (!shader_mode_logged) {
                    int target_fps = shader_fps_resolve(output->config->shader_fps);
                    if (output->config->vsync) {
                        log_info("Shader active on %s with vsync (monitor refresh rate)", output->model);
                    } else {
                        log_info("Shader active on %s, rendering at %d FPS (shader_fps=%d, high-precision frame timer)",
                                 output->model, target_fps, output->config->shader_fps);
                    }
                    shader_mode_logged = true;
                }
            }

            /* Check for transitions */
            if (output->transition_start_time > 0 &&
                output->config->transition != TRANSITION_NONE) {
                timeout_ms = FRAME_TIME_MS;  /* Fast polling during transitions */
            }

            output = output->next;
        }
        pthread_rwlock_unlock(&state->output_list_lock);

        if (shader_count == 0 && shader_mode_logged) {
            log_info("No active shaders, reverting to event-driven mode");
            shader_mode_logged = false;
        }

        /* If next requests pending, wake immediately */
        if (atomic_load_explicit(&state->next_requested, memory_order_acquire) > 0) {
            timeout_ms = 0;
        }

        /* Poll for events */
        int ret = poll(fds, num_fds, timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) {
                log_info("Poll interrupted by signal (EINTR), checking running flag");
                if (ops && ops->cancel_read) {
                    ops->cancel_read(backend_data);
                }
                if (!atomic_load_explicit(&state->running, memory_order_acquire)) {
                    log_info("Running flag is false, exiting event loop");
                    break;
                }
                continue;
            }
            log_error("Poll failed: %s", strerror(errno));
            if (ops && ops->cancel_read) {
                ops->cancel_read(backend_data);
            }
            atomic_store_explicit(&state->running, false, memory_order_release);
            break;
        }

        if (ret == 0) {
            /* Timeout - no events */
            if (ops && ops->cancel_read) {
                ops->cancel_read(backend_data);
            }
        } else {
            /* Check for compositor disconnect (POLLHUP/POLLERR) */
            if (fds[0].revents & (POLLHUP | POLLERR)) {
                log_error("Compositor disconnected (POLLHUP/POLLERR), display server shut down");
                if (ops && ops->cancel_read) {
                    ops->cancel_read(backend_data);
                }
                atomic_store_explicit(&state->running, false, memory_order_release);
                break;
            }

            /* Events available */
            if (fds[0].revents & POLLIN) {
                /* Read events via compositor backend */
                if (ops && ops->read_events) {
                    if (!ops->read_events(backend_data)) {
                        log_error("Failed to read compositor events");
                        atomic_store_explicit(&state->running, false, memory_order_release);
                        break;
                    }
                }

                /* Check for compositor errors after reading events */
                if (ops && ops->get_error) {
                    int display_error = ops->get_error(backend_data);
                    if (display_error != 0) {
                        log_error("Compositor error (code %d), display server disconnected", display_error);
                        atomic_store_explicit(&state->running, false, memory_order_release);
                        break;
                    }
                }
            } else {
                /* No events on compositor fd - cancel prepared read */
                if (ops && ops->cancel_read) {
                    ops->cancel_read(backend_data);
                }
            }

            /* Check timerfd - time to cycle wallpaper */
            if (fds[1].revents & POLLIN) {
                uint64_t expirations;
                ssize_t s = read(state->timer_fd, &expirations, sizeof(expirations));
                if (s == sizeof(expirations)) {
                    log_debug("Cycle timer expired (%lu expirations), checking outputs", expirations);
                    /* Mark all outputs for redraw so render_outputs() is called */
                    pthread_rwlock_rdlock(&state->output_list_lock);
                    for (struct output_state *timer_output = state->outputs;
                         timer_output; timer_output = timer_output->next) {
                        atomic_store_explicit(&timer_output->needs_redraw, true, memory_order_release);
                    }
                    pthread_rwlock_unlock(&state->output_list_lock);
                }
            }

            /* Check wakeup fd - internal events (config reload, etc) */
            if (fds[2].revents & POLLIN) {
                uint64_t value;
                ssize_t s = read(state->wakeup_fd, &value, sizeof(value));
                (void)s; /* Silence unused variable warning */
            }

            /* Check signal fd - signals delivered as file descriptor events (race-free) */
            if (fds[3].revents & POLLIN) {
                struct signalfd_siginfo fdsi;
                ssize_t s = read(state->signal_fd, &fdsi, sizeof(fdsi));
                if (s == sizeof(fdsi)) {
                    log_debug("Received signal %d via signalfd", fdsi.ssi_signo);
                    handle_signal_from_fd(state, fdsi.ssi_signo);
                }
            }

            /* Check frame timer fds - high-precision frame pacing for vsync-off shaders */
            for (int i = BASE_FD_COUNT; i < num_fds; i++) {
                if (fds[i].revents & POLLIN) {
                    uint64_t expirations;
                    ssize_t s = read(fds[i].fd, &expirations, sizeof(expirations));
                    if (s == sizeof(expirations)) {
                        if (expirations > 1) {
                            log_debug("Frame timer expired %lu times (frame overrun)", expirations);
                        }
                        /* Mark the specific output for redraw */
                        if (frame_timer_outputs[i]) {
                            atomic_store_explicit(&frame_timer_outputs[i]->needs_redraw, true, memory_order_release);
                        }
                    }
                }
            }
        }

        /* Config reload removed - restart daemon to change config */

        /* Dispatch any events that were read */
        if (ops && ops->dispatch_events) {
            if (!ops->dispatch_events(backend_data)) {
                log_error("Failed to dispatch compositor events, display server may have disconnected");
                atomic_store_explicit(&state->running, false, memory_order_release);
                break;
            }
        }

        /* Update occlusion state after processing compositor events */
        if (has_occlusion) {
            occlusion_update(state);
        }

        /* Reconcile shader-pause requests (from pause-shader/resume-shader).
         * Only touches the output list on an actual state transition. Runs
         * before the redraw decision below so a resume renders this iteration. */
        bool shader_paused_req = atomic_load_explicit(&state->shader_paused, memory_order_acquire);
        if (shader_paused_req != shader_pause_applied) {
            apply_shader_pause(state, shader_paused_req);
            shader_pause_applied = shader_paused_req;
            log_info("Shader animation %s", shader_paused_req ? "paused" : "resumed");
        }

        /* Additional check for compositor errors after dispatching events */
        if (ops && ops->get_error) {
            int display_error = ops->get_error(backend_data);
            if (display_error != 0) {
                log_error("Compositor error detected (code %d), exiting", display_error);
                atomic_store_explicit(&state->running, false, memory_order_release);
                break;
            }
        }

        /* Skip rendering if no output needs a redraw — avoids spinning CPU needlessly.
         * Walking the output list requires holding the read lock. */
        bool any_needs_redraw = false;
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            if (atomic_load_explicit(&o->needs_redraw, memory_order_acquire)) {
                any_needs_redraw = true;
                break;
            }
        }
        pthread_rwlock_unlock(&state->output_list_lock);

        if (any_needs_redraw ||
            atomic_load_explicit(&state->next_requested, memory_order_acquire) > 0 ||
            atomic_load_explicit(&state->set_index_requested, memory_order_acquire) >= 0) {
            render_outputs(state);
        }
        frame_count++;

        /* Print statistics periodically */
        uint64_t current_time = get_time_ms();
        if (current_time - last_stats_time >= STATS_INTERVAL_MS) {
            double elapsed_sec = (current_time - last_stats_time) / (double)MS_PER_SECOND;
            double fps = frame_count / elapsed_sec;

            log_debug("Stats: %.1f FPS, %lu frames rendered, %lu errors",
                     fps, state->frames_rendered, state->errors_count);

            last_stats_time = current_time;
            frame_count = 0;
        }

        /* Keep redrawing during active transitions and for shader wallpapers.
         * Traversal requires the rwlock. */
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            /* Don't schedule redraws for occluded outputs */
            if (o->config->pause_on_fullscreen &&
                atomic_load_explicit(&o->occluded, memory_order_acquire)) {
                continue;
            }

            /* Keep redrawing during transitions */
            if (o->transition_start_time > 0 &&
                o->config->transition != TRANSITION_NONE) {
                atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            }
            /* For shader wallpapers with vsync enabled, always redraw (vsync paces us)
             * For shaders with vsync disabled, only redraw when frame timer fires (handled above) */
            if (o->config->type == WALLPAPER_SHADER &&
                !o->shader_load_failed &&
                (o->live_shader_program != 0 || o->multipass_shader != NULL) &&
                !atomic_load_explicit(&state->shader_paused, memory_order_acquire)) {
                if (o->config->vsync || output_get_frame_timer_fd(o) < 0) {
                    atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
                }
            }
        }
        pthread_rwlock_unlock(&state->output_list_lock);

        /* Throttle debug logging - only every 300 frames (~5 seconds at 60fps) */
        log_throttle_counter++;
        if (log_throttle_counter >= 300 && shader_count > 0) {
            log_debug("Shader animation active: %d output(s) rendering", shader_count);
            log_throttle_counter = 0;
        }
    }

    /* Clean up occlusion detection */
    if (has_occlusion) {
        occlusion_cleanup(state);
    }

    /* Clean up file descriptors */
    if (state->timer_fd >= 0) {
        close(state->timer_fd);
        state->timer_fd = -1;
    }
    if (state->wakeup_fd >= 0) {
        close(state->wakeup_fd);
        state->wakeup_fd = -1;
    }

    log_info("Event loop stopped");
}

/* Stop the event loop */
void event_loop_stop(struct neowall_state *state) {
    if (state) {
        atomic_store_explicit(&state->running, false, memory_order_release);
        log_info("Event loop stop requested");
    }
}

/* Request a redraw for all outputs */
void event_loop_request_redraw(struct neowall_state *state) {
    if (!state) {
        return;
    }

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *output = state->outputs; output; output = output->next) {
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Request a redraw for a specific output */
void event_loop_request_output_redraw(struct output_state *output) {
    if (output) {
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    }
}
