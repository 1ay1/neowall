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

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        bool occluded = o->config->pause_on_fullscreen &&
                        atomic_load_explicit(&o->occluded, memory_order_acquire);

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
            wallpaper_is_animated(o->config->type) &&
            !(o->config->type == WALLPAPER_SHADER && a->model.shader_paused) &&
            !(o->config->type == WALLPAPER_SHADER && o->shader && !wallshader_animated(o->shader) &&
              o->frames_rendered > 0)) {
            rt_sub_at(subs, NW_MSG_FRAME_DUE, o->name, (int64_t)o->pace_next_deadline_ns);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (transition || building) rt_sub_every(subs, NW_MSG_TRANSITION, 0, 16666667);
}
