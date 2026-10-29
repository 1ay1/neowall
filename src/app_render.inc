/* Rendering and output bring-up. Included by app.c only: these need its
 * struct app and helpers. */

/* Whether this output draws this step. Pure gating: no GL, no side effects. */
static bool output_wants_frame(const struct app *a, struct output_state *output) {
    if (output->config->pause_on_fullscreen &&
        atomic_load_explicit(&output->occluded, memory_order_acquire)) {
        return false;   /* a fullscreen window covers it */
    }
    /* Shader pause keeps the last frame on screen with the GPU idle; the
     * clock is reconciled on resume (apply_shader_pause). */
    if (output->config->type == WALLPAPER_SHADER && a->shader_paused) return false;
    if (!atomic_load_explicit(&output->needs_redraw, memory_order_acquire) ||
        !output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        return false;
    }
    /* Vsync on Wayland: draw only once this output's frame callback came back.
     * The swap doesn't block (interval 0), so a slow monitor can't stall the
     * others. vsync=false keeps its own timerfd: gating both on one output
     * makes two pacers beat (~23 FPS). */
    bool frame_paced = output->config->vsync &&
        compositor_surface_supports_frame_pacing(output->compositor_surface);
    return !frame_paced || compositor_surface_frame_ready(output->compositor_surface);
}

/* Advance an image cross-fade to `now`. */
static void output_step_transition(struct output_state *output, uint64_t now) {
    if (output->transition_start_time == 0 || output->config->transition == TRANSITION_NONE) return;
    uint64_t elapsed = now - output->transition_start_time;
    uint64_t dur_ms = (uint64_t)(output->config->transition_duration * 1000.0f);
    float progress = dur_ms > 0 ? (float)elapsed / (float)dur_ms : 1.0f;
    output->transition_progress = progress >= 1.0f ? 1.0f : ease_in_out_cubic(progress);
}

/* Count a shader frame and log the measured FPS every two seconds. */
static void output_meter_fps(struct output_state *output, uint64_t frame_start, uint64_t frame_end) {
    output->fps_frame_count++;
    if (output->fps_last_log_time == 0) output->fps_last_log_time = frame_end;
    uint64_t elapsed = frame_end - output->fps_last_log_time;
    if (elapsed < 2000) return;
    float fps = (float)output->fps_frame_count / ((float)elapsed / 1000.0f);
    output->fps_current = fps;
    uint64_t frame_time = frame_end - frame_start;
    if (output->config->vsync) {
        log_info("FPS [%s]: %.1f FPS (vsync: monitor sync, frame_time: %lums)",
                 output->model, fps, frame_time);
    } else {
        log_info("FPS [%s]: %.1f FPS (target: %d, frame_time: %lums)", output->model, fps,
                 shader_fps_resolve(output->config->shader_fps), frame_time);
    }
    output->fps_frame_count = 0;
    output->fps_last_log_time = frame_end;
}

/* Log render failures at most once a second, with a count of the rest. */
static void log_render_failure(struct output_state *output) {
    static uint64_t last = 0;
    static int suppressed = 0;
    if (output->shader_load_failed) return;   /* already explained */
    uint64_t now = get_time_ms();
    if (now - last < 1000) {
        suppressed++;
        return;
    }
    if (suppressed > 0) {
        log_error("Failed to render frame for output %s (%d failures in last second)",
                  output->model, suppressed + 1);
    } else {
        log_error("Failed to render frame for output %s", output->model);
    }
    last = now;
    suppressed = 0;
}

/* Swap the terminal program on an output hosting a terminal wallpaper. The
 * old child and its process group are killed before the new one starts. */
static void output_swap_terminal(struct output_state *output, const char *cmd) {
    if (output->config->type != WALLPAPER_TERMINAL || cmd[0] == '\0') return;
    const char *name = output->model[0] ? output->model : "unknown";
    log_info("Swapping terminal wallpaper on output %s: '%s'", name, cmd);
    /* cols/rows 0: re-derive the grid from the output size, as at spawn. */
    nw_result tr = output_set_terminal(
        output, cmd,
        output->config->shader_path[0] ? output->config->shader_path : NULL,
        output->config->term_font[0] ? output->config->term_font : NULL, 0, 0);
    if (nw_is_err(tr)) {
        log_error("Failed to swap terminal wallpaper on output %s: %s", name,
                  tr.context ? tr.context : nw_status_str(tr.status));
    }
}

/* The rect this output actually changed, for damage-aware present. Only a
 * crisp terminal that moved a few rows reports a band (GL bottom-left
 * origin); everything else is the full surface. Returns whether it's partial. */
static bool output_damage_rect(const struct neowall_state *state, struct output_state *output,
                               EGLint rect[4]) {
    rect[0] = 0; rect[1] = 0; rect[2] = INT32_MAX; rect[3] = INT32_MAX;
    if (!state->swap_damage_supported || output->config->type != WALLPAPER_TERMINAL ||
        !output->multipass_shader || output->config->shader_path[0] != '\0') {
        return false;
    }
    int px, py, pw, ph;
    if (!multipass_last_damage(output->multipass_shader, true, output->width, output->height,
                               &px, &py, &pw, &ph)) {
        return false;
    }
    rect[0] = px; rect[1] = py; rect[2] = pw; rect[3] = ph;
    return true;
}

/* Present one rendered output: damage, present feedback, frame request,
 * swap, commit, pacing, and the post-frame bookkeeping. */
static void output_present(struct app *a, struct output_state *output, uint64_t frame_time) {
    struct neowall_state *state = a->state;
    EGLSurface surf = output->compositor_surface->egl_surface;

    /* On the common single-output path the render phase left this current;
     * skipping a redundant eglMakeCurrent avoids Mesa's implicit flush. */
    if ((eglGetCurrentContext() != state->egl_context || eglGetCurrentSurface(EGL_DRAW) != surf) &&
        !egl_make_surface_current(state->egl_display, surf, state->egl_context)) {
        log_error("Failed to make context current before swap for output %s: 0x%x",
                  output->model, eglGetError());
        return;
    }

    /* wl_surface damage (top-left origin) must be queued before the commit
     * that publishes the buffer; the same rect drives swap-with-damage. */
    EGLint rect[4];
    bool partial = output_damage_rect(state, output, rect);
    if (partial) {
        compositor_surface_damage(output->compositor_surface, rect[0],
                                  output->height - (rect[1] + rect[3]), rect[2], rect[3]);
    } else {
        compositor_surface_damage(output->compositor_surface, 0, 0, INT32_MAX, INT32_MAX);
    }

    /* Present feedback and frame callbacks ride the swap's commit, so they're
     * requested before it. */
#ifdef HAVE_WAYLAND_BACKEND
    if (wallpaper_is_animated(output->config->type)) wayland_request_present_feedback(output);
#endif
    if (output->config->vsync && compositor_surface_supports_frame_pacing(output->compositor_surface)) {
        compositor_surface_frame_request(output->compositor_surface);
    }

    bool swap_ok;
    if (partial && state->swap_with_damage) {
        typedef EGLBoolean (*swap_dmg_fn)(EGLDisplay, EGLSurface, const EGLint *, EGLint);
        /* eglGetProcAddress returns a function address as void*; a union keeps
         * the conversion out of ISO C's object->function cast rule. */
        union { void *obj; swap_dmg_fn fn; } u = { .obj = state->swap_with_damage };
        swap_ok = u.fn(state->egl_display, surf, rect, 1);
    } else {
        swap_ok = eglSwapBuffers(state->egl_display, surf);
    }
    if (!swap_ok) {
        EGLint err = eglGetError();
        log_error("Failed to swap buffers for output %s: 0x%x", output->model, err);
        state->errors_count++;
        /* A lost context, or a surface that keeps failing, won't heal by
         * retrying every frame. Hand it to the restart path. */
        if (err == EGL_CONTEXT_LOST || gpu_health_swap_result(&output->swap_fail_streak, false)) {
            a->gpu_lost = true;
        }
        return;
    }
    gpu_health_swap_result(&output->swap_fail_streak, true);
    compositor_surface_commit(output->compositor_surface);

    /* Phase-lock vsync-off animated outputs to this present (no-op otherwise). */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    output_pace_advance(output, (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec);

    output->last_frame_time = frame_time;
    state->frames_rendered++;

    if (output->transition_start_time > 0 && output->transition_progress >= 1.0f) {
        output->transition_start_time = 0;
        output_cleanup_transition(output);
        if (output->config->cycle && output->config->cycle_count > 1 &&
            output->config->type == WALLPAPER_IMAGE) {
            output_preload_next_wallpaper(output);
        }
    }

    /* Animated wallpapers keep needs_redraw set so the loop draws them every
     * frame, except a settled terminal, which sleeps until its child writes. */
    bool term_settled = output->config->type == WALLPAPER_TERMINAL &&
                        output->multipass_shader && output->frames_rendered > 0 &&
                        !multipass_terminal_animating(output->multipass_shader, kTermFxSettleMs);
    if ((output->transition_start_time == 0 || output->config->transition == TRANSITION_NONE) &&
        (!wallpaper_is_animated(output->config->type) || term_settled)) {
        atomic_store_explicit(&output->needs_redraw, false, memory_order_release);
    }
}

/* `next` asked for but nothing could cycle: say why. */
static void explain_no_cycle(int total_outputs) {
    if (total_outputs == 0) {
        log_info("Cannot cycle wallpaper: no outputs are configured");
    } else if (total_outputs == 1) {
        log_info("Cannot cycle wallpaper: only a single wallpaper is configured. "
                 "Point path/shader at a directory (ending in '/') to cycle.");
    } else {
        log_info("Cannot cycle wallpaper: none of the %d outputs cycle. "
                 "Point path/shader at a directory (ending in '/') to cycle.", total_outputs);
    }
}

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

        /* Handle set-terminal request: swap the live terminal-wallpaper
         * command on every output that hosts a terminal wallpaper. */
        if (terminal_requested && output->config->type == WALLPAPER_TERMINAL &&
            terminal_cmd[0] != '\0') {
            output_swap_terminal(output, terminal_cmd);
            current_time = get_time_ms();
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

        if (!output_wants_frame(a, output)) continue;

        /* Make EGL context current for this output. Note: eglMakeCurrent can
         * synchronize with the GPU — we are NOT holding output_list_lock here. */
        if (!egl_make_surface_current(state->egl_display, output->compositor_surface->egl_surface, state->egl_context)) {
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

        output_step_transition(output, current_time);

        uint64_t frame_start = get_time_ms();
        bool render_success = output_render_frame(output);
        uint64_t frame_end = get_time_ms();

        if (render_success && output->config->type == WALLPAPER_SHADER) {
            output_meter_fps(output, frame_start, frame_end);
        }
        if (!render_success) {
            log_render_failure(output);
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
        if (swaps.data[i].render_success) output_present(a, swaps.data[i].output, current_time);
    }

    /* If we had a next request but couldn't process it, inform the user */
    if (next_count > 0 && !processed_next) {
        if (!has_cycleable_output) explain_no_cycle(total_outputs);
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

    /* One cheap query per rendered step: did the GPU reset under us? */
    if (state->gpu_health_pending) {
        gpu_health_init(state->gl_robust, log_get_level() >= LOG_LEVEL_DEBUG);
        state->gpu_health_pending = false;
    }
    if (gpu_health_lost()) a->gpu_lost = true;
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
static const char *output_display_name(const struct output_state *o) {
    const char *n = o->connector_name[0] ? o->connector_name : o->model;
    return n[0] ? n : "unknown";
}

/* Finish one output whose surface the compositor has sized: EGL surface, GL
 * state, then only this output's config (live outputs are untouched). */
static void bring_up_output(struct neowall_state *state, struct output_state *o) {
    const char *name = output_display_name(o);
    if (!o->compositor_surface) return;   /* surface creation failed */
    if (o->width <= 0 || o->height <= 0) {
        /* The configure that sizes it posts a retry (output_on_configure_callback). */
        log_info("Reconnect: output %s has no size yet, waiting for the compositor", name);
        return;
    }
    if (!output_create_egl_surface(o) || !egl_core_make_current(state, o) || !output_init_render(o)) {
        log_error("Reconnect: GL bring-up failed for output %s", name);
        return;
    }
    if (config_apply_to_output(state, o)) {
        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        log_info("Reconnect: output %s online (%dx%d)", name, o->width, o->height);
    }
}

/* Bring up outputs that appeared (hotplug, DPMS) or are still waiting for a
 * size. Surfaces are created first and one roundtrip collects all their
 * configures, then each is finished. */
static void reinit_reconnected_outputs(struct neowall_state *state) {
    if (!state || !state->compositor_backend) return;
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *backend_data = state->compositor_backend->data;

    /* A ref on each, so a removal can't free it while the lock is dropped
     * for the slow surface/GL work. */
    struct output_state *pending[MAX_OUTPUTS];
    int n = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n < MAX_OUTPUTS; o = o->next) {
        if (!o->compositor_surface || o->compositor_surface->egl_surface == EGL_NO_SURFACE) {
            output_ref(o);
            pending[n++] = o;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    if (n == 0) return;

    for (int i = 0; i < n; i++) {
        log_info("Reconnect: bringing up output %s", output_display_name(pending[i]));
        if (!output_configure_compositor_surface(pending[i])) {
            log_error("Reconnect: failed to create surface for output %s",
                      output_display_name(pending[i]));
        }
    }
    if (ops && ops->sync) ops->sync(backend_data);
    if (ops && ops->dispatch_events) ops->dispatch_events(backend_data);

    for (int i = 0; i < n; i++) {
        bring_up_output(state, pending[i]);
        output_unref(pending[i]);
    }
}
#endif /* HAVE_WAYLAND_BACKEND */
