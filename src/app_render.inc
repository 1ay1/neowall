/* Rendering and output bring-up. Included by app.c only: these need its
 * struct app and helpers. */

static void render_outputs(struct app *a) {
    struct neowall_state *state = a->state;

    uint64_t current_time = get_time_ms();

    /* Take the commands update() queued since the last frame. They are model
     * fields now, not atomics another thread can change mid-frame. */
    int next_count = a->next_pending;
    int set_index = a->set_index;
    a->set_index = -1;
    bool terminal_requested = a->terminal_pending;
    char terminal_cmd[512] = {0};
    if (terminal_requested) {
        snprintf(terminal_cmd, sizeof(terminal_cmd), "%s", a->terminal_cmd);
        a->terminal_pending = false;
    }
    bool processed_next = false;
    bool processed_set_index = false;
    bool has_cycleable_output = false;
    int total_outputs = 0;

    /* === PHASE 1: snapshot the output list under the read lock ============
     * We copy the pointer list to a stack array and take a REFERENCE on each
     * output, then drop the lock so that all GL/EGL work below (which may
     * BLOCK on vsync inside eglMakeCurrent / eglSwapBuffers) runs without
     * holding output_list_lock.
     *
     * The ref is what makes this sound: a concurrent hotplug-removal unlinks
     * the output under the write lock and then output_unref()s the list's
     * reference. Because we hold our own ref, the object cannot be freed while
     * we use it here — it is freed when we drop our ref in the cleanup sweep at
     * the end of this function. Without the ref, an unplug mid-frame would be a
     * use-after-free. */
    struct output_ptr_vec snapshot;
    output_ptr_vec_init(&snapshot);

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        output_ref(o);
        if (!output_ptr_vec_push(&snapshot, o)) {
            /* OOM while snapshotting: drop the ref we just took and stop
             * growing. We still render everything captured so far rather than
             * aborting the frame. */
            output_unref(o);
            break;
        }
        total_outputs++;
        if (o->config->cycle && o->config->cycle_count > 0) {
            has_cycleable_output = true;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    size_t output_n = snapshot.len;
    struct output_state **outputs_snapshot = snapshot.data;

    if (next_count > 0) {
        log_debug("Processing next request: %d pending in queue", next_count);
    }

    /* === PHASE 2: handle set-index / next requests and per-output cycling ===
     * These call back into output_set_* and output_cycle_* which take their
     * own locks; we must NOT be holding output_list_lock here. */
    struct swap_vec swaps;
    swap_vec_init(&swaps);

    /* Refresh each output's slice of the scene it shares with its span group.
     * Cheap, and the layout can change under us at any time (hotplug, RandR). */
    outputs_update_spans(outputs_snapshot, output_n);

    for (size_t idx = 0; idx < output_n; idx++) {
        struct output_state *output = outputs_snapshot[idx];

        /* A spanned scene only reads as one scene if every monitor is drawing
         * the SAME shader. Outputs can start out disagreeing — a restored cycle
         * index, a hotplug part-way through a cycle — so converge them here. */
        outputs_sync_span_group(outputs_snapshot, output_n, idx);

        /* Handle set-index on every eligible output. Distinct cycle lists are
         * independent; limiting the operation to the first matching list made
         * multi-monitor configurations silently ignore later outputs. */
        if (set_index >= 0 && output->config->cycle && output->config->cycle_count > 0) {
            if ((size_t)set_index < output->config->cycle_count) {
                log_debug("Setting wallpaper index %d for output %s",
                          set_index, output->model[0] ? output->model : "unknown");
                output_set_cycle_index(output, (size_t)set_index);
                processed_set_index = true;
                current_time = get_time_ms();
            } else {
                log_debug("Index %d is not eligible for output %s (count: %zu)",
                          set_index, output->model[0] ? output->model : "unknown",
                          output->config->cycle_count);
            }
        }

        /* Handle set-terminal request — swap the live terminal-wallpaper
         * command. output_set_terminal destroys the current multipass shader
         * (killing the old terminal child and its whole process group before
         * spawning the new one), so no orphan is possible. We apply it to every
         * output that currently hosts a terminal wallpaper. */
        if (terminal_requested && output->config->type == WALLPAPER_TERMINAL) {
            const char *cmd = terminal_cmd;

            if (cmd[0] != '\0') {
                log_info("Swapping terminal wallpaper on output %s: '%s'",
                         output->model[0] ? output->model : "unknown", cmd);
                /* Reuse the output's existing grid + styling shader; passing
                 * cols/rows 0 lets output_set_terminal re-derive the grid from
                 * the output size, matching the original spawn. */
                nw_result tr = output_set_terminal(
                    output, cmd,
                    output->config->shader_path[0] ? output->config->shader_path : NULL,
                    output->config->term_font[0] ? output->config->term_font : NULL,
                    0, 0);
                if (nw_is_err(tr)) {
                    log_error("Failed to swap terminal wallpaper on output %s: %s",
                              output->model[0] ? output->model : "unknown",
                              tr.context ? tr.context : nw_status_str(tr.status));
                }
                current_time = get_time_ms();
            }
        }

        /* Handle one next request for every distinct span/cycle group. */
        if (next_count > 0 && output->config->cycle && output->config->cycle_count > 0) {
            bool group_already_cycled = false;
            for (size_t previous = 0; previous < idx; previous++) {
                if (outputs_snapshot[previous]->config->cycle &&
                    output_same_span_group(outputs_snapshot[previous], output)) {
                    group_already_cycled = true;
                    break;
                }
            }
            if (!group_already_cycled) {
                size_t cycled_count = output_cycle_group(outputs_snapshot, output_n, output);
                current_time = get_time_ms();
                processed_next = true;
                log_info("Cycled %zu output(s) in cycle group", cycled_count);
            }
        }

        /* Check if we should cycle wallpaper (timer-driven) */
        if (!a->cycle_paused &&
            output->config->cycle && output->config->duration > 0.0f) {
            if (output_should_cycle(output, current_time)) {
                /* Take the whole span group across together: half a scene
                 * advancing to the next shader on its own is worse than either
                 * shader on its own. */
                output_cycle_group(outputs_snapshot, output_n, output);
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
        if (output->config->type == WALLPAPER_SHADER && a->shader_paused) {
            continue;
        }

        /* Check if this output needs rendering */
        if (!atomic_load_explicit(&output->needs_redraw, memory_order_acquire) ||
            !output->compositor_surface ||
            output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
            continue;
        }

        /* Vsync on Wayland: render only once this output's frame callback came
         * back. The swap doesn't block (interval 0), so a slow monitor can't
         * stall the others. vsync=false keeps its timerfd and isn't gated:
         * gating both on one output makes two pacers beat (~23 FPS). */
        bool frame_paced = output->config->vsync &&
            compositor_surface_supports_frame_pacing(output->compositor_surface);
        if (frame_paced && !compositor_surface_frame_ready(output->compositor_surface)) {
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

        /* Geometry callbacks only publish atomics. Once this output's EGL
         * context is current, reconcile all geometry-dependent CPU/GL state
         * before accepting a preload or drawing the next frame. */
        output_process_geometry_change(output);

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

        /* Queue for swap. The swap vec grows as needed; on OOM we simply skip
         * presenting this output's frame rather than aborting. */
        struct swap_info si = { .output = output, .render_success = render_success };
        swap_vec_push(&swaps, si);
    }

    /* One queued `next` advances every distinct group exactly once. */
    if (processed_next) {
        a->next_pending--;
    }
    if (set_index >= 0) {
        log_info("Set wallpaper index %d on all eligible outputs%s", set_index,
                 processed_set_index ? "" : " (none eligible)");
    }
    for (size_t i = 0; i < swaps.len; i++) {
        struct output_state *output = swaps.data[i].output;
        if (!swaps.data[i].render_success) {
            continue;
        }

        /* Make current before swap. On the common single-output path the
         * render phase already left this context/surface current; skipping
         * the redundant eglMakeCurrent avoids Mesa's implicit flush. */
        if (eglGetCurrentContext() != state->egl_context ||
            eglGetCurrentSurface(EGL_DRAW) != output->compositor_surface->egl_surface) {
            if (!eglMakeCurrent(state->egl_display, output->compositor_surface->egl_surface,
                               output->compositor_surface->egl_surface, state->egl_context)) {
                log_error("Failed to make context current before swap for output %s: 0x%x",
                         output->model, eglGetError());
                continue;
            }
        }

        /* Compute the changed screen region for damage-aware presentation. A
         * crisp terminal that only moved a few rows reports a pixel band (GL
         * bottom-left origin); everything else damages the full surface. The
         * same rect drives BOTH the wl_surface damage (what the compositor
         * recomposites) and eglSwapBuffersWithDamage (the buffer sub-region). */
        int dmg_x = 0, dmg_y = 0, dmg_w = INT32_MAX, dmg_h = INT32_MAX;
        bool partial = false;
        if (state->swap_damage_supported &&
            output->config->type == WALLPAPER_TERMINAL &&
            output->multipass_shader &&
            output->config->shader_path[0] == '\0' /* crisp built-in, no post-shader */) {
            int px, py, pw, ph;
            if (multipass_last_damage(output->multipass_shader, true,
                                      output->width, output->height,
                                      &px, &py, &pw, &ph)) {
                dmg_x = px; dmg_y = py; dmg_w = pw; dmg_h = ph;
                partial = true;
            }
        }

        /* Damage BEFORE swap+commit — wl_surface damage must be queued before
         * the commit that publishes the new buffer (audit fix #17). wl_surface
         * damage is in SURFACE (top-left origin) coords; convert the GL
         * bottom-left band back for the partial case. */
        if (partial) {
            int surf_y = output->height - (dmg_y + dmg_h);   /* flip to top-left */
            compositor_surface_damage(output->compositor_surface,
                                      dmg_x, surf_y, dmg_w, dmg_h);
        } else {
            compositor_surface_damage(output->compositor_surface, 0, 0, INT32_MAX, INT32_MAX);
        }

        /* Ask the compositor for a real present timestamp for the frame we are
         * about to publish (wp_presentation). The feedback attaches to the next
         * commit on this surface — and eglSwapBuffers itself performs that
         * commit on Wayland — so it must be requested BEFORE the swap. Only for
         * animated outputs the pacer actually drives; no-op without the
         * protocol. Wayland-only. */
#ifdef HAVE_WAYLAND_BACKEND
        if (wallpaper_is_animated(output->config->type) &&
            output_get_frame_timer_fd(output) >= 0) {
            wayland_request_present_feedback(output);
        }
#endif

        /* Paced vsync: the callback rides the swap's commit. */
        if (output->config->vsync &&
            compositor_surface_supports_frame_pacing(output->compositor_surface)) {
            compositor_surface_frame_request(output->compositor_surface);
        }

        /* Present. With EGL_EXT/KHR_swap_buffers_with_damage we hand the driver
         * the exact changed rect (GL bottom-left origin) so it re-scans/
         * recomposites only those scanlines; otherwise a plain full swap. Swap
         * can block waiting for vsync; we hold no locks here. */
        bool swap_ok;
        if (partial && state->swap_with_damage) {
            typedef EGLBoolean (*swap_dmg_fn)(EGLDisplay, EGLSurface, const EGLint *, EGLint);
            /* Launder the void* through a union: a direct object->function
             * pointer cast is ISO-C-undefined (-Wpedantic), but eglGetProcAddress
             * hands back a function address, so this is safe on every real ABI. */
            union { void *obj; swap_dmg_fn fn; } u = { .obj = state->swap_with_damage };
            EGLint rect[4] = { dmg_x, dmg_y, dmg_w, dmg_h };
            swap_ok = u.fn(state->egl_display,
                           output->compositor_surface->egl_surface, rect, 1);
        } else {
            swap_ok = eglSwapBuffers(state->egl_display,
                                     output->compositor_surface->egl_surface);
        }
        if (!swap_ok) {
            log_error("Failed to swap buffers for output %s: 0x%x",
                     output->model, eglGetError());
            state->errors_count++;
            continue;
        }

        static uint64_t swap_counter = 0;
        swap_counter++;
        if (swap_counter % 600 == 0) {
            log_debug("Buffer swap #%lu successful for output %s", swap_counter, output->model);
        }

        compositor_surface_commit(output->compositor_surface);

        /* Phase-lock the next frame to this present. Re-arms the per-output
         * frame timer as a one-shot absolute deadline one period ahead, anchored
         * to the instant the swap completed, so vsync-off animated wallpapers
         * hold a stable cadence instead of drifting against the display's vblank
         * (which a fixed recurring interval does, producing periodic judder).
         * No-op for vsync / static / non-timed outputs. */
        {
            struct timespec pace_now;
            clock_gettime(CLOCK_MONOTONIC, &pace_now);
            uint64_t pace_now_ns = (uint64_t)pace_now.tv_sec * 1000000000ULL +
                                   (uint64_t)pace_now.tv_nsec;
            output_pace_advance(output, pace_now_ns);
        }

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

        /* Reset needs_redraw unless we're in a transition or using an animated
         * (shader / terminal) wallpaper. Animated wallpapers keep needs_redraw
         * set so the loop re-renders them every frame. A terminal that has
         * settled is the exception: it clears the bit so the loop can sleep
         * until the child writes (mark_animated sets it again then). Leaving it
         * set redrew an idle terminal every frame-callback timeout, forever. */
        bool term_settled = output->config->type == WALLPAPER_TERMINAL &&
                            output->multipass_shader && output->frames_rendered > 0 &&
                            !multipass_terminal_animating(output->multipass_shader,
                                                          kTermFxSettleMs);
        if ((output->transition_start_time == 0 ||
             output->config->transition == TRANSITION_NONE) &&
            (!wallpaper_is_animated(output->config->type) || term_settled)) {
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
        a->next_pending--;
    }

    /* Drop the references taken in PHASE 1. For any output that was removed
     * from the list while we worked (and therefore had the list's reference
     * dropped), this is the unref that finally frees it. */
    for (size_t i = 0; i < output_n; i++) {
        output_unref(outputs_snapshot[i]);
    }
    output_ptr_vec_free(&snapshot);
    swap_vec_free(&swaps);

}

/* Bring freshly-appeared outputs (reconnected monitors / DPMS-on / hotplug)
 * fully online.
 *
 * On a monitor power-off the compositor removes the wl_output global, which
 * destroys the output entirely. When the monitor returns a new output object
 * is created by the registry handler but it has no compositor surface, no EGL
 * surface, no GL render state and no wallpaper config applied. Without this it
 * just sits there forever and nothing renders — the long-standing "rendering
 * never restarts after the monitor wakes" bug.
 *
 * Handles any number of outputs in one pass and never disturbs outputs that
 * are already live (their config is left exactly as-is). The bring-up mirrors
 * the startup sequence (wayland_init_full + egl_core):
 *   1. create every missing compositor surface, then ONE roundtrip so the
 *      compositor delivers all the configure events (dimensions + configured)
 *   2. per output: create the EGL surface, init GL render state
 *   3. per output: apply just that output's wallpaper/shader config
 */
#ifdef HAVE_WAYLAND_BACKEND
static void reinit_reconnected_outputs(struct neowall_state *state) {
    if (!state || !state->compositor_backend) {
        return;
    }
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *backend_data = state->compositor_backend->data;

    /* Snapshot surface-less outputs under the read lock, taking a transient ref
     * on each so a concurrent hotplug-removal can't free them while we work
     * with the lock dropped (surface roundtrips + GL init are slow). */
    struct output_state *pending[MAX_OUTPUTS];
    int n = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n < MAX_OUTPUTS; o = o->next) {
        if (!o->compositor_surface) {
            output_ref(o);
            pending[n++] = o;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (n == 0) {
        return;
    }

    /* Phase 1: create every missing surface, then a single roundtrip so all
     * configure events (which set width/height + configured) arrive at once. */
    for (int i = 0; i < n; i++) {
        struct output_state *o = pending[i];
        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        log_info("Reconnect: bringing up output %s", name[0] ? name : "unknown");
        if (!output_configure_compositor_surface(o)) {
            log_error("Reconnect: failed to create surface for output %s",
                      name[0] ? name : "unknown");
        }
    }
    if (ops && ops->sync) {
        ops->sync(backend_data);
    }
    if (ops && ops->dispatch_events) {
        ops->dispatch_events(backend_data);
    }

    /* Phase 2 + 3: per output, finish GL bring-up and apply its config. */
    for (int i = 0; i < n; i++) {
        struct output_state *o = pending[i];
        const char *name = o->connector_name[0] ? o->connector_name : o->model;

        if (!o->compositor_surface) {
            continue; /* surface creation failed above */
        }
        if (o->width <= 0 || o->height <= 0) {
            log_error("Reconnect: output %s has no dimensions (%dx%d), skipping",
                      name[0] ? name : "unknown", o->width, o->height);
            continue;
        }
        if (!output_create_egl_surface(o) || !egl_core_make_current(state, o) ||
            !output_init_render(o)) {
            log_error("Reconnect: GL bring-up failed for output %s",
                      name[0] ? name : "unknown");
            continue;
        }

        /* Apply ONLY this output's config — live outputs are untouched. */
        if (config_apply_to_output(state, o)) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            log_info("Reconnect: output %s online (%dx%d)",
                     name[0] ? name : "unknown", o->width, o->height);
        }
    }

    for (int i = 0; i < n; i++) {
        output_unref(pending[i]);
    }
}
#endif /* HAVE_WAYLAND_BACKEND */
