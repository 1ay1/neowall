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
#include "constants.h"

/* Forward declaration for signal handler from main.c */
extern void handle_signal_from_fd(struct neowall_state *state, int signum);

static struct neowall_state *event_loop_state = NULL;

/* Update timerfd to wake up at next cycle time */
static void update_cycle_timer(struct neowall_state *state) {
    if (!state || state->timer_fd < 0) {
        return;
    }

    uint64_t now = get_time_ms();
    uint64_t next_wake_ms = UINT64_MAX;
    
    /* Find the earliest cycle time across all outputs - use read lock */
    pthread_rwlock_rdlock(&state->output_list_lock);
    
    bool paused = atomic_load_explicit(&state->paused, memory_order_acquire);
    struct output_state *output = state->outputs;
    while (output) {
        if (!paused && output->config.cycle && output->config.duration > 0.0f &&
            output->config.cycle_count > 1 && output->current_image) {
            uint64_t elapsed_ms = now - output->last_cycle_time;
            uint64_t duration_ms = (uint64_t)(output->config.duration * 1000.0f);  /* Convert seconds to milliseconds */
            
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
    
    pthread_rwlock_unlock(&state->output_list_lock);
    
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
static void render_outputs(struct neowall_state *state) {
    if (!state) {
        return;
    }

    uint64_t current_time = get_time_ms();
    
    /* BUG FIX #3: Check if config reload is in progress */
    /* If reload is active, skip rendering to avoid use-after-free of GL resources */
    bool reload_in_progress = atomic_load_explicit(&state->reload_requested, memory_order_acquire);
    if (reload_in_progress) {
        log_debug("Config reload in progress, skipping render frame to avoid resource race");
        /* Don't process next requests either during reload */
        return;
    }
    
    /* Check if there are any pending next requests - process ONE globally per frame to ensure rendering */
    int next_count = atomic_load_explicit(&state->next_requested, memory_order_acquire);
    bool processed_next = false;
    bool has_cycleable_output = false;
    int total_outputs = 0;
    
    /* Acquire read lock for output list traversal */
    pthread_rwlock_rdlock(&state->output_list_lock);
    
    if (next_count > 0) {
        log_debug("Processing next request: %d pending in queue", next_count);
        
        /* First pass: check if any output can actually cycle */
        struct output_state *check_output = state->outputs;
        while (check_output) {
            total_outputs++;
            if (check_output->config.cycle && check_output->config.cycle_count > 0) {
                has_cycleable_output = true;
            }
            check_output = check_output->next;
        }
    }

    struct output_state *output = state->outputs;
    while (output) {
        /* Handle next wallpaper request - ONE total per frame (ensures each change is actually rendered) */
        if (next_count > 0 && !processed_next && output->config.cycle && output->config.cycle_count > 0) {
            log_debug("Cycling to next wallpaper for output %s (%d requests remaining)",
                     output->model[0] ? output->model : "unknown", next_count - 1);
            output_cycle_wallpaper(output);
            current_time = get_time_ms();
            processed_next = true;
            /* Decrement counter immediately after processing */
            atomic_fetch_sub_explicit(&state->next_requested, 1, memory_order_acq_rel);
        }
        
        /* Check if we should cycle wallpaper (timer-driven) */
        if (!state->paused && output->config.cycle && output->config.duration > 0.0f) {
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
                uint64_t transition_duration_ms = (uint64_t)(output->config.transition_duration * 1000.0f);
                float progress = (float)elapsed / (float)transition_duration_ms;

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
                /* Only log if shader hasn't permanently failed (after 3 attempts, be silent) */
                if (!output->shader_load_failed) {
                    /* Throttle error logging to prevent spam when shader fails to load */
                    static uint64_t last_render_error_time = 0;
                    static int render_error_count = 0;
                    uint64_t current_time = get_time_ms();
                    
                    if (current_time - last_render_error_time >= 1000) {
                        /* Log once per second */
                        if (render_error_count > 0) {
                            log_error("Failed to render frame for output %s (%d failures in last second)", 
                                     output->model, render_error_count + 1);
                        } else {
                            log_error("Failed to render frame for output %s", output->model);
                        }
                        last_render_error_time = current_time;
                        render_error_count = 0;
                    } else {
                        render_error_count++;
                    }
                }
                state->errors_count++;
            } else {
                /* Swap buffers */
                if (!eglSwapBuffers(state->egl_display, output->egl_surface)) {
                    log_error("Failed to swap buffers for output %s: 0x%x",
                             output->model, eglGetError());
                    state->errors_count++;
                } else {
                    /* Damage the entire surface to tell compositor it needs repainting */
                    wl_surface_damage(output->surface, 0, 0, INT32_MAX, INT32_MAX);
                    
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
    
    /* Release read lock after output traversal */
    pthread_rwlock_unlock(&state->output_list_lock);
    
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
        
        /* Decrement the counter since we can't fulfill the request */
        atomic_fetch_sub(&state->next_requested, 1);
    }
    
    /* Update timer after rendering changes */
    update_cycle_timer(state);
}

/* Handle pending Wayland events */
static bool handle_wayland_events(struct neowall_state *state) {
    if (!state || !state->display) {
        return false;
    }

    /* Check running flag atomically */
    if (!atomic_load_explicit(&state->running, memory_order_acquire)) {
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
        if (output->config.cycle && output->config.duration > 0.0f) {
            log_info("Output %s: cycling enabled with %zu images, duration %.2fs",
                     output->model[0] ? output->model : "unknown",
                     output->config.cycle_count,
                     output->config.duration);
        }
        output = output->next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    /* Get Wayland display file descriptor */
    int wl_fd = wl_display_get_fd(state->display);
    if (wl_fd < 0) {
        log_error("Failed to get Wayland display file descriptor");
        return;
    }

    struct pollfd fds[4];
    fds[0].fd = wl_fd;
    fds[0].events = POLLIN;
    fds[1].fd = state->timer_fd;
    fds[1].events = POLLIN;
    fds[2].fd = state->wakeup_fd;
    fds[2].events = POLLIN;
    fds[3].fd = state->signal_fd;
    fds[3].events = POLLIN;

    /* Initial render for all outputs - use read lock */
    pthread_rwlock_rdlock(&state->output_list_lock);
    output = state->outputs;
    while (output) {
        output->needs_redraw = true;
        output = output->next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

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
    
    /* Track shader state to avoid log spam */
    static bool shader_mode_logged = false;
    uint64_t log_throttle_counter = 0;
    
    while (atomic_load_explicit(&state->running, memory_order_acquire)) {
        
        /* Handle new outputs that need initialization (reconnected displays) */
        if (atomic_load_explicit(&state->outputs_need_init, memory_order_acquire)) {
            log_info("New outputs detected, will be initialized by normal config load");
            atomic_store_explicit(&state->outputs_need_init, false, memory_order_release);
            /* Don't call config_reload here - it causes deadlock on startup */
            /* Outputs will be initialized through the normal config_load path */
        }

        /* Prepare for reading events */
        while (wl_display_prepare_read(state->display) != 0) {
            if (wl_display_dispatch_pending(state->display) < 0) {
                log_error("Failed to dispatch Wayland events during prepare");
                wl_display_cancel_read(state->display);
                break;
            }

            if (!atomic_load_explicit(&state->running, memory_order_acquire)) {
                wl_display_cancel_read(state->display);
                break;
            }
        }

        /* Flush before polling */
        if (wl_display_flush(state->display) < 0) {
            if (errno != EAGAIN) {
                log_error("Failed to flush Wayland display: %s", strerror(errno));
                wl_display_cancel_read(state->display);
                break;
            }
        }

        /* Calculate poll timeout - use 1 second max to ensure signals are processed promptly
         * BUG FIX: Previously used POLL_TIMEOUT_INFINITE (-1) which caused slow signal response
         * (Ctrl+C, neowall kill, etc.) because poll wouldn't return until a Wayland event arrived */
        int timeout_ms = 1000; /* 1 second max - ensures signals checked regularly */
        
        /* Check if any output has active transitions or shader wallpapers - use read lock */
        pthread_rwlock_rdlock(&state->output_list_lock);
        output = state->outputs;
        int shader_count = 0;
        while (output) {
            /* Only count shader as active if it loaded successfully and hasn't failed */
            if (output->config.type == WALLPAPER_SHADER && 
                !output->shader_load_failed && 
                output->live_shader_program != 0) {
                shader_count++;
                timeout_ms = FRAME_TIME_MS; /* ~60 FPS for smooth transitions/animations */
                /* Only log shader detection once, not every frame */
                if (!shader_mode_logged) {
                    log_info("Shader active on %s, rendering at 60 FPS", output->model);
                    shader_mode_logged = true;
                }
            }
            if ((output->transition_start_time > 0 && 
                 output->config.transition != TRANSITION_NONE) ||
                (output->config.type == WALLPAPER_SHADER && 
                 !output->shader_load_failed && 
                 output->live_shader_program != 0)) {
                timeout_ms = FRAME_TIME_MS;
                break;
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
        int ret = poll(fds, 4, timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) {
                log_info("Poll interrupted by signal (EINTR), checking running flag");
                wl_display_cancel_read(state->display);
                if (!atomic_load_explicit(&state->running, memory_order_acquire)) {
                    log_info("Running flag is false, exiting event loop");
                    break;
                }
                continue;
            }
            log_error("Poll failed: %s", strerror(errno));
            wl_display_cancel_read(state->display);
            atomic_store_explicit(&state->running, false, memory_order_release);
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
                    atomic_store_explicit(&state->running, false, memory_order_release);
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
        }

        /* Config reload - exactly like startup */
        static atomic_bool reload_in_progress = ATOMIC_VAR_INIT(false);
        bool reload_flag = atomic_load_explicit(&state->reload_requested, memory_order_acquire);
        
        if (reload_flag) {
            /* Prevent concurrent reloads */
            bool expected = false;
            if (!atomic_compare_exchange_strong(&reload_in_progress, &expected, true)) {
                log_debug("Reload already in progress, skipping duplicate request");
                atomic_store_explicit(&state->reload_requested, false, memory_order_release);
            } else {
                log_info("Config changed, reloading...");
                atomic_store_explicit(&state->reload_requested, false, memory_order_release);
                
                /* Step 1: Clear old resources for all outputs */
                pthread_rwlock_wrlock(&state->output_list_lock);
                struct output_state *output = state->outputs;
                while (output) {
                    /* Destroy old shader program before loading new one */
                    if (output->live_shader_program != 0) {
                        glDeleteProgram(output->live_shader_program);
                        output->live_shader_program = 0;
                    }
                    /* Reset failure flags to allow retry */
                    output->shader_load_failed = false;
                    output = output->next;
                }
                pthread_rwlock_unlock(&state->output_list_lock);
                
                /* Step 2: Reload config file (this updates output configs) */
                bool reload_ok = config_load(state, state->config_path);
                
                /* Step 3: Apply to all outputs and trigger re-initialization */
                pthread_rwlock_wrlock(&state->output_list_lock);
                output = state->outputs;
                bool all_outputs_ok = true;
                while (output) {
                    /* Apply deferred config (loads shaders/images) */
                    output_apply_deferred_config(output);
                    
                    /* Check if shader failed to load for SHADER type outputs */
                    if (output->config.type == WALLPAPER_SHADER && output->live_shader_program == 0) {
                        log_error("Output %s: Shader failed to load from '%s'", 
                                 output->model, output->config.shader_path);
                        all_outputs_ok = false;
                    }
                    
                    /* Force redraw */
                    output->needs_redraw = true;
                    
                    output = output->next;
                }
                pthread_rwlock_unlock(&state->output_list_lock);
                
                if (reload_ok && all_outputs_ok) {
                    log_info("╔═══════════════════════════════════════════════════════════════╗");
                    log_info("║ ✓ Config reloaded successfully!                              ║");
                    log_info("║   Wallpapers/shaders have been updated                        ║");
                    log_info("╚═══════════════════════════════════════════════════════════════╝");
                } else {
                    log_error("╔═══════════════════════════════════════════════════════════════╗");
                    log_error("║ ✗ Config reload failed - check errors above                  ║");
                    log_error("║   Fix config and save - hot-reload will retry automatically  ║");
                    log_error("╚═══════════════════════════════════════════════════════════════╝");
                }
                
                /* Release reload lock */
                atomic_store(&reload_in_progress, false);
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
        while (output) {
            /* Keep redrawing during transitions */
            if (output->transition_start_time > 0 && 
                output->config.transition != TRANSITION_NONE) {
                output->needs_redraw = true;
            }
            /* Keep redrawing for shader wallpapers (continuous animation) 
             * but only if shader loaded successfully and hasn't failed */
            if (output->config.type == WALLPAPER_SHADER && 
                !output->shader_load_failed && 
                output->live_shader_program != 0) {
                output->needs_redraw = true;
            }
            output = output->next;
        }
        
        /* Throttle debug logging - only every 300 frames (~5 seconds at 60fps) */
        log_throttle_counter++;
        if (log_throttle_counter >= 300 && shader_count > 0) {
            log_debug("Shader animation active: %d outputs rendering at ~60 FPS", shader_count);
            log_throttle_counter = 0;
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
void event_loop_stop(struct neowall_state *state) {
    if (state) {
        state->running = false;
        log_info("Event loop stop requested");
    }
}

/* Request a redraw for all outputs */
void event_loop_request_redraw(struct neowall_state *state) {
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