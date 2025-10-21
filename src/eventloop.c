/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 *
 * Main event loop and rendering dispatch
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include "staticwall.h"

/* Forward declaration of state pointer for EGL operations */
static struct staticwall_state *event_loop_state = NULL;




/* Render all outputs that need redrawing */
static void render_outputs(struct staticwall_state *state) {
    if (!state) {
        return;
    }

    struct output_state *output = state->outputs;
    uint64_t current_time = get_time_ms();

    while (output) {
        /* Handle next wallpaper request(s) - process all queued requests */
        int next_count = atomic_load(&state->next_requested);
        if (next_count > 0 && output->config.cycle && output->config.cycle_count > 0) {
            /* Cycle through all queued next requests for this output */
            for (int i = 0; i < next_count; i++) {
                output_cycle_wallpaper(output);
            }
            current_time = get_time_ms();
        }
        
        /* Check if we should cycle wallpaper (before rendering) */
        if (!state->paused && output->config.cycle && output->config.duration > 0) {
            if (output_should_cycle(output, current_time)) {
                output_cycle_wallpaper(output);
                /* Recalculate current_time after cycling since it may have taken some time */
                current_time = get_time_ms();
            }
        }

        /* Check if this output needs rendering */
        if (output->needs_redraw && output->egl_surface != EGL_NO_SURFACE) {
            /* Make EGL context current for this output */
            if (!eglMakeCurrent(state->egl_display, output->egl_surface,
                               output->egl_surface, state->egl_context)) {
                log_error("Failed to make EGL context current for output %s: 0x%x",
                         output->model, eglGetError());
                output = output->next;
                continue;
            }

            /* Recalculate time for accurate transition timing */
            current_time = get_time_ms();

            /* Handle transitions */
            if (output->transition_start_time > 0 &&
                output->config.transition != TRANSITION_NONE) {
                uint64_t elapsed = current_time - output->transition_start_time;
                float progress = (float)elapsed / (float)output->config.transition_duration;

                if (progress >= 1.0f) {
                    /* Transition complete */
                    output->transition_progress = 1.0f;
                    output->transition_start_time = 0;

                    /* Clean up old texture */
                    if (output->next_texture) {
                        render_destroy_texture(output->next_texture);
                        output->next_texture = 0;
                    }

                    if (output->next_image) {
                        image_free(output->next_image);
                        output->next_image = NULL;
                    }
                } else {
                    /* Apply easing function */
                    output->transition_progress = ease_in_out_cubic(progress);
                }
            }

            /* Render frame */
            if (!render_frame(output)) {
                log_error("Failed to render frame for output %s", output->model);
                state->errors_count++;
            } else {
                /* Swap buffers */
                if (!eglSwapBuffers(state->egl_display, output->egl_surface)) {
                    log_error("Failed to swap buffers for output %s: 0x%x",
                             output->model, eglGetError());
                    state->errors_count++;
                } else {
                    /* Commit Wayland surface */
                    wl_surface_commit(output->surface);
                    output->last_frame_time = current_time;
                    state->frames_rendered++;
                }
            }
        }

        output = output->next;
    }
}

/* Handle pending Wayland events */
static bool handle_wayland_events(struct staticwall_state *state) {
    if (!state || !state->display) {
        return false;
    }

    /* Dispatch pending events */
    if (wl_display_dispatch_pending(state->display) < 0) {
        log_error("Failed to dispatch pending Wayland events");
        return false;
    }

    /* Flush outgoing requests */
    if (wl_display_flush(state->display) < 0) {
        if (errno != EAGAIN) {
            log_error("Failed to flush Wayland display: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

/* Main event loop */
void event_loop_run(struct staticwall_state *state) {
    if (!state) {
        log_error("Invalid state for event loop");
        return;
    }

    event_loop_state = state;

    log_info("Starting event loop");

    /* Log initial cycling configuration for debugging */
    struct output_state *output = state->outputs;
    while (output) {
        if (output->config.cycle && output->config.duration > 0) {
            log_info("Output %s: cycling enabled with %zu images, duration %us",
                     output->model[0] ? output->model : "unknown",
                     output->config.cycle_count,
                     output->config.duration);
        }
        output = output->next;
    }

    /* Get Wayland display file descriptor */
    int wl_fd = wl_display_get_fd(state->display);
    if (wl_fd < 0) {
        log_error("Failed to get Wayland display file descriptor");
        return;
    }

    struct pollfd fds[1];
    fds[0].fd = wl_fd;
    fds[0].events = POLLIN;

    /* Initial render for all outputs */
    output = state->outputs;
    while (output) {
        output->needs_redraw = true;
        output = output->next;
    }

    uint64_t last_stats_time = get_time_ms();
    uint64_t frame_count = 0;

    while (state->running) {
        /* Handle configuration reload if requested */
        if (state->reload_requested) {
            config_reload(state);
        }

        /* Prepare for reading events */
        while (wl_display_prepare_read(state->display) != 0) {
            if (wl_display_dispatch_pending(state->display) < 0) {
                log_error("Failed to dispatch Wayland events during prepare");
                state->running = false;
                break;
            }
        }

        if (!state->running) {
            wl_display_cancel_read(state->display);
            break;
        }

        /* Flush before polling */
        if (wl_display_flush(state->display) < 0) {
            if (errno != EAGAIN) {
                log_error("Failed to flush Wayland display: %s", strerror(errno));
                wl_display_cancel_read(state->display);
                state->running = false;
                break;
            }
        }

        /* Poll for events with timeout */
        int timeout_ms = 16; /* ~60 FPS */
        int ret = poll(fds, 1, timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(state->display);
                continue;
            }
            log_error("Poll failed: %s", strerror(errno));
            wl_display_cancel_read(state->display);
            state->running = false;
            break;
        }

        if (ret == 0) {
            /* Timeout - no events */
            wl_display_cancel_read(state->display);
        } else {
            /* Events available */
            if (fds[0].revents & POLLIN) {
                if (wl_display_read_events(state->display) < 0) {
                    log_error("Failed to read Wayland events");
                    state->running = false;
                    break;
                }
            } else {
                wl_display_cancel_read(state->display);
            }
        }

        /* Dispatch any events that were read */
        if (!handle_wayland_events(state)) {
            log_error("Failed to handle Wayland events");
            state->running = false;
            break;
        }

        /* Render outputs that need updating */
        render_outputs(state);
        frame_count++;

        /* Print statistics every 10 seconds */
        uint64_t current_time = get_time_ms();
        if (current_time - last_stats_time >= 10000) {
            double elapsed_sec = (current_time - last_stats_time) / 1000.0;
            double fps = frame_count / elapsed_sec;

            log_debug("Stats: %.1f FPS, %lu frames rendered, %lu errors",
                     fps, state->frames_rendered, state->errors_count);

            last_stats_time = current_time;
            frame_count = 0;
        }

        /* Keep outputs with cycling enabled or active transitions marked for redraw */
        output = state->outputs;
        while (output) {
            if (output->config.cycle && output->config.duration > 0) {
                output->needs_redraw = true;
            }
            /* Keep redrawing during transitions */
            if (output->transition_start_time > 0 && 
                output->config.transition != TRANSITION_NONE) {
                output->needs_redraw = true;
            }
            output = output->next;
        }
        
        /* Reset next_requested counter after processing all outputs */
        int next_count = atomic_load(&state->next_requested);
        if (next_count > 0) {
            atomic_fetch_sub(&state->next_requested, next_count);
        }
    }

    log_info("Event loop stopped");
}

/* Stop the event loop */
void event_loop_stop(struct staticwall_state *state) {
    if (state) {
        state->running = false;
        log_info("Event loop stop requested");
    }
}

/* Request a redraw for all outputs */
void event_loop_request_redraw(struct staticwall_state *state) {
    if (!state) {
        return;
    }

    struct output_state *output = state->outputs;
    while (output) {
        output->needs_redraw = true;
        output = output->next;
    }
}

/* Request a redraw for a specific output */
void event_loop_request_output_redraw(struct output_state *output) {
    if (output) {
        output->needs_redraw = true;
    }
}