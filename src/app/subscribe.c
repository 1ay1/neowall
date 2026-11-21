/* subscribe: which timers exist, as a pure function of the model and the
 * outputs. rt diffs the result against what's running. */

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/output/output.h"
#include "neowall/shader/wallshader.h"

void app_subscribe(void *self, rt_subs *subs) {
    const struct app *a         = self;
    struct neowall_state *state = a->state;
    bool transition             = false;
    bool building               = false;
    bool watch_occlusion        = false;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        bool occluded = o->config->pause_on_fullscreen &&
                        atomic_load_explicit(&o->occluded, memory_order_acquire);

        /* Occlusion has to be re-checked on a clock, not only when the
         * compositor sends something: when the window over an output closes,
         * nothing is guaranteed to arrive (the paused output draws nothing,
         * so no frame callbacks; Hyprland's IPC is a poll; X11's root doesn't
         * hear a client's state change). A covered output is checked until it
         * comes back (raw bit: one whose option was just turned off still
         * gets the check that clears it). An animated one is checked so it
         * gets covered at all: it already wakes the loop every frame, so the
         * tick costs nothing. A static uncovered output has nothing to pause
         * and isn't. */
        if (a->model.has_occlusion &&
            (atomic_load_explicit(&o->occluded, memory_order_acquire) ||
             (o->config->pause_on_fullscreen && wallpaper_is_animated(o->config->type)))) {
            watch_occlusion = true;
        }

        /* Cycling: one timer per output, due when its duration is up. */
        if (!a->model.cycle_paused && o->config->cycle && o->config->duration > 0.0f &&
            o->config->cycle_count > 1) {
            uint64_t dur_ms = (uint64_t)(o->config->duration * 1000.0f);
            int64_t due     = (int64_t)(o->last_cycle_time + dur_ms) * 1000000LL;
            rt_sub_at(subs, NW_MSG_CYCLE_DUE, o->name, due);
        }

        if (o->transition_start_time > 0 && o->config->transition != TRANSITION_NONE)
            transition = true;
        /* a background shader build lands on the next step that looks */
        if (o->pending_shader) building = true;

        /* vsync-off animated output: the phase-locked pacer's next deadline.
         * output_pace_advance() moves it after each present. */
        if (!occluded && o->pace_period_ns > 0 && output_get_frame_timer_fd(o) >= 0 &&
            wallpaper_is_animated(o->config->type) && !app_shader_is_parked(&a->model, o) &&
            !(o->config->type == WALLPAPER_SHADER && o->shader && !wallshader_animated(o->shader) &&
              o->frames_rendered > 0)) {
            rt_sub_at(subs, NW_MSG_FRAME_DUE, o->name, (int64_t)o->pace_next_deadline_ns);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (transition || building) rt_sub_every(subs, NW_MSG_TRANSITION, 0, 16666667);
    if (watch_occlusion) rt_sub_every(subs, NW_MSG_OCCLUSION_TICK, 0, APP_OCCLUSION_TICK_NS);
}
