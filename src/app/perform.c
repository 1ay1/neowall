/* perform: run the commands update asks for. This is where the program
 * touches files, outputs and systemd. Results come back as messages. */

#include <stdio.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/app/watch.h"
#include "neowall/proto/proto.h"
#include "neowall/config/config.h"
#include "neowall/occlusion/occlusion.h"
#include "neowall/output/output.h"
#include "neowall/runtime/sd_notify.h"
#include "neowall/shader/term_gpu.h"
#include "neowall/shader/wallshader.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"

void app_mark_redraw_all(struct neowall_state *state) {
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next)
        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
    pthread_rwlock_unlock(&state->output_list_lock);
}

static struct output_state *output_by_id(struct neowall_state *state, uint32_t id) {
    for (struct output_state *o = state->outputs; o; o = o->next)
        if (o->name == id) return o;
    return NULL;
}

/* Freezing records the instant it stopped; unfreezing moves each shader's
 * start_time on by the frozen time, so it resumes on the frame it stopped. */
static void shader_pause(struct neowall_state *state, int64_t duration_ms) {
    uint64_t now = get_time_ms();
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->type != WALLPAPER_SHADER) continue;
        if (duration_ms >= 0) {
            o->shader_pause = shader_pause_begin(now * 1000, (uint32_t)duration_ms);
        } else if (o->shader_pause.start_us != 0) {
            bool final_frame;
            uint64_t time_us  = shader_pause_sample(&o->shader_pause, now * 1000, &final_frame);
            uint64_t shift_ms = now - time_us / 1000;
            o->shader_start_time += shift_ms;
            if (o->span_start_time) o->span_start_time += shift_ms;
            o->shader_pause            = (shader_pause_clock){0};
            o->shader_pause_final_draw = false;
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* A vsync-off animated output's frame. An idle terminal still gets the tick,
 * so fresh child output is seen soon, but only an animating one redraws. */
static void frame_due(struct app *a, uint32_t id) {
    struct neowall_state *state = a->state;
    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *o = output_by_id(state, id);
    if (o && !app_shader_is_parked(&a->model, o)) {
        bool idle = o->config->type == WALLPAPER_TERMINAL && o->shader && o->frames_rendered > 0 &&
                    !term_gpu_animating(wallshader_terminal(o->shader), APP_TERM_FX_SETTLE_MS);
        if (!idle) atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        /* A frame that won't render must still move the pacer on, or its
         * timer fires once and goes quiet. One that will render moves it
         * after the swap; doing it here too halves the frame rate. */
        else
            output_pace_advance(o, (uint64_t)rt_now_ns());
    } else if (o) {
        output_pace_advance(o, (uint64_t)rt_now_ns()); /* paused shader */
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Whether any output still has a reload's shader compiling, and whether one
 * failed (clearing that once read: it's reported to this reload only). */
static void staged_builds(struct neowall_state *state, bool *building, bool *failed) {
    *building = *failed = false;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (output_reload_pending(o)) *building = true;
        if (output_reload_failed(o)) *failed = true;
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

static void staged_forget(struct neowall_state *state) {
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next)
        output_reload_forget(o);
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Last-known-good: the whole file is checked before anything applies, and a
 * failure keeps the old config. A shader swap compiles in the background, so
 * this never waits on the compiler; update hears whether one is building. */
static void reload(struct app *a, rt_loop *loop) {
    struct neowall_state *state = a->state;
    log_info("Reloading configuration from %s", state->config_path);
    staged_forget(state);
    bool ok = app_config_reload(state, state->config_path);
    if (ok) app_mark_redraw_all(state);
    bool building, failed;
    staged_builds(state, &building, &failed);
    app_send(loop, NW_MSG_RELOAD_APPLIED, 0, (ok && !failed ? 1 : 0) | (building ? 2 : 0));
}

void app_perform(void *self, rt_loop *loop, const rt_cmd *c) {
    struct app *a               = self;
    struct neowall_state *state = a->state;

    switch (c->kind) {
    case APP_CMD_NOTIFY: nw_sd_notify(c->v.s); break;

    case APP_CMD_STOP: atomic_store_explicit(&state->running, false, memory_order_release); break;

    case APP_CMD_RELOAD: reload(a, loop); break;

    case APP_CMD_REPLY:
        ipc_server_reply(a->host.ipc, c->id, c->v.i != 0,
                         c->v.i ? "configuration reloaded"
                                : "reload failed; the previous configuration is still in effect");
        break;

    case APP_CMD_WATCH_BUILDS:
        a->view.watch_builds = true; /* present reports when they settle */
        break;

    case APP_CMD_READ_INDEX_FILE: {
        int index = read_set_index_file();
        if (index < 0) {
            log_error("set-index requested but no valid index file found");
            break;
        }
        app_send(loop, NW_MSG_SET_INDEX, 0, index);
        break;
    }

    case APP_CMD_READ_TERMINAL_FILE: {
        char buf[APP_TERMINAL_CMD_MAX];
        if (!read_set_terminal_file(buf, sizeof(buf))) {
            log_error("set-terminal requested but no valid command file found");
            break;
        }
        app_send_terminal(loop, buf);
        break;
    }

    case APP_CMD_SHADER_PAUSE: shader_pause(state, c->v.i); break;

    case APP_CMD_REDRAW_ALL: app_mark_redraw_all(state); break;

    case APP_CMD_FRAME: frame_due(a, c->id); break;

    case APP_CMD_OCCLUSION: occlusion_update(state); break;

    case APP_CMD_WATCH_RELOAD:
        if (state->watch_path[0]) watch_reload(state, state->watch_path);
        break;

    default: log_error("unknown command %u", c->kind); break;
    }
}
