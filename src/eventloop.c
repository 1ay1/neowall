#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include "staticwall.h"
#include "constants.h"

static struct staticwall_state *event_loop_state = NULL;

/* Update timerfd to wake up at next cycle time */
static void update_cycle_timer(struct staticwall_state *state) {
    if (!state || state->timer_fd < 0) {
        return;
    }

    uint64_t now = get_time_ms();
    uint64_t next_wake_ms = UINT64_MAX;
    
    /* Find the earliest cycle time across all outputs */
    struct output_state *output = state->outputs;
    while (output) {
        if (!state->paused && output->config.cycle && output->config.duration > 0 &&
            output->config.cycle_count > 1 && output->current_image) {
            uint64_t elapsed_ms = now - output->last_cycle_time;
            uint64_t duration_ms = output->config.duration * MS_PER_SECOND;
            
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
static void render_outputs(struct staticwall_state *state) {
    if (!state) {
        return;
    }

    struct output_state *output = state->outputs;
    uint64_t current_time = get_time_ms();
    
    /* Check if there are any pending next requests - process ONE globally per frame to ensure rendering */
    int next_count = atomic_load(&state->next_requested);
    bool processed_next = false;
    
    if (next_count > 0) {
        log_debug("Processing next request: %d pending in queue", next_count);
    }

    while (output) {
        /* Handle next wallpaper request - ONE total per frame (ensures each change is actually rendered) */
        if (next_count > 0 && !processed_next && output->config.cycle && output->config.cycle_count > 0) {
            log_debug("Cycling to next wallpaper for output %s (%d requests remaining)",
                     output->model[0] ? output->model : "unknown", next_count - 1);
            output_cycle_wallpaper(output);
            current_time = get_time_ms();
            processed_next = true;
            /* Decrement counter immediately after processing */
            atomic_fetch_sub(&state->next_requested, 1);
        }
        
        /* Check if we should cycle wallpaper (timer-driven) */
        if (!state->paused && output->config.cycle && output->config.duration > 0) {
            if (output_should_cycle(output, current_time)) {
                output_cycle_wallpaper(output);
                current_time = get_time_ms();
                /* Update timer for next cycle */
                update_cycle_timer(state);
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

            /* Handle image transitions */
            if (output->transition_start_time > 0 &&
                output->config.transition != TRANSITION_NONE) {
                uint64_t elapsed = current_time - output->transition_start_time;
                float progress = (float)elapsed / (float)output->config.transition_duration;

                if (progress >= 1.0f) {
                    /* Clamp progress to 1.0 for final frame */
                    output->transition_progress = 1.0f;
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
                    
                    /* Clean up transition after final frame is rendered */
                    if (output->transition_start_time > 0 && 
                        output->transition_progress >= 1.0f) {
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
                    }
                    
                    /* Reset needs_redraw unless we're in a transition or using a shader wallpaper */
                    if ((output->transition_start_time == 0 || 
                         output->config.transition == TRANSITION_NONE) &&
                        output->config.type != WALLPAPER_SHADER) {
                        output->needs_redraw = false;
                    }
                }
            }
        }

        output = output->next;
    }
    
    /* Update timer after rendering changes */
    update_cycle_timer(state);
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

    struct pollfd fds[3];
    fds[0].fd = wl_fd;
    fds[0].events = POLLIN;
    fds[1].fd = state->timer_fd;
    fds[1].events = POLLIN;
    fds[2].fd = state->wakeup_fd;
    fds[2].events = POLLIN;

    /* Initial render for all outputs */
    output = state->outputs;
    while (output) {
        output->needs_redraw = true;
        output = output->next;
    }

    uint64_t last_stats_time = get_time_ms();
    uint64_t frame_count = 0;
    
    /* Perform initial render BEFORE entering event loop */
    log_info("Performing initial wallpaper render");
    render_outputs(state);
    handle_wayland_events(state);
    wl_display_flush(state->display);
    
    /* Set initial timer for cycling */
    update_cycle_timer(state);

    log_info("Entering main event loop");
    
    while (state->running) {
        log_debug("Event loop iteration start, running=%d", state->running);
        
        /* Handle new outputs that need initialization (reconnected displays) */
        if (state->outputs_need_init) {
            log_info("New outputs detected, will be initialized by normal config load");
            state->outputs_need_init = false;
            /* Don't call config_reload here - it causes deadlock on startup */
            /* Outputs will be initialized through the normal config_load path */
        }
        
        /* Handle configuration reload if requested */
        if (state->reload_requested) {
            log_info("Config reload requested, reloading...");
            config_reload(state);
            /* Update cycle timer with new configuration */
            update_cycle_timer(state);
            /* Trigger render to apply new config */
            render_outputs(state);
            wl_display_flush(state->display);
        }

        /* Prepare for reading events */
        while (wl_display_prepare_read(state->display) != 0) {
            if (wl_display_dispatch_pending(state->display) < 0) {
                log_error("Failed to dispatch Wayland events during prepare");
                wl_display_cancel_read(state->display);
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
                break;
            }
        }

        /* Calculate poll timeout - only for transitions/shaders, otherwise wait indefinitely */
        int timeout_ms = POLL_TIMEOUT_INFINITE; /* Pure event-driven by default */
        
        /* Check if any output has active transitions or shader wallpapers */
        output = state->outputs;
        int shader_count = 0;
        while (output) {
            if (output->config.type == WALLPAPER_SHADER) {
                shader_count++;
                timeout_ms = FRAME_TIME_MS; /* ~60 FPS for smooth transitions/animations */
                log_info("Shader detected on %s, setting poll timeout to %dms", output->model, FRAME_TIME_MS);
            }
            if ((output->transition_start_time > 0 && 
                 output->config.transition != TRANSITION_NONE) ||
                output->config.type == WALLPAPER_SHADER) {
                timeout_ms = FRAME_TIME_MS;
                break;
            }
            output = output->next;
        }
        if (shader_count == 0) {
            log_debug("No shaders active, using infinite timeout");
        }
        
        /* If next requests pending, wake immediately */
        if (atomic_load(&state->next_requested) > 0) {
            timeout_ms = 0;
        }
        
        /* Poll debug messages removed to reduce log spam during shader rendering */
        
        log_debug("Polling with timeout=%dms, running=%d", timeout_ms, state->running);
        int ret = poll(fds, 3, timeout_ms);
        log_debug("Poll returned: %d (errno=%d)", ret, errno);

        if (ret < 0) {
            if (errno == EINTR) {
                log_info("Poll interrupted by signal (EINTR), checking running flag");
                wl_display_cancel_read(state->display);
                if (!state->running) {
                    log_info("Running flag is false, exiting event loop");
                    break;
                }
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
            
            /* Check timerfd - time to cycle wallpaper */
            if (fds[1].revents & POLLIN) {
                uint64_t expirations;
                ssize_t s = read(state->timer_fd, &expirations, sizeof(expirations));
                if (s == sizeof(expirations)) {
                    log_debug("Cycle timer expired (%lu expirations), checking outputs", expirations);
                }
            }
            
            /* Check eventfd - internal wakeup (config reload, etc) */
            if (fds[2].revents & POLLIN) {
                uint64_t value;
                ssize_t s = read(state->wakeup_fd, &value, sizeof(value));
                if (s == sizeof(value)) {
                    log_debug("Wakeup event received (value=%lu)", value);
                }
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

        /* Keep redrawing during active transitions and for shader wallpapers */
        output = state->outputs;
        int shaders_marked = 0;
        while (output) {
            /* Keep redrawing during transitions */
            if (output->transition_start_time > 0 && 
                output->config.transition != TRANSITION_NONE) {
                output->needs_redraw = true;
            }
            /* Keep redrawing for shader wallpapers (continuous animation) */
            if (output->config.type == WALLPAPER_SHADER) {
                output->needs_redraw = true;
                shaders_marked++;
                log_debug("Marked shader on %s for continuous redraw (live_program=%u)", 
                         output->model, output->live_shader_program);
            }
            output = output->next;
        }
        if (shaders_marked > 0) {
            log_debug("Marked %d shader outputs for continuous animation", shaders_marked);
        }
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