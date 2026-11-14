/* perform: run the commands update asks for. This is where the program
 * touches files, outputs and systemd. Results come back as messages. */

#include <stdio.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/app/watch.h"
#include "neowall/config/config.h"
#include "neowall/occlusion/occlusion.h"
#include "neowall/output/output.h"
#include "neowall/runtime/sd_notify.h"
#include "neowall/shader/term_gpu.h"
#include "neowall/shader/wallshader.h"

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
static void shader_pause(struct neowall_state *state, bool paused) {
    uint64_t now = get_time_ms();
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->type != WALLPAPER_SHADER) continue;
        if (paused) {
            if (o->shader_paused_at == 0) o->shader_paused_at = now;
        } else if (o->shader_paused_at != 0) {
            o->shader_start_time += now - o->shader_paused_at;
            o->shader_paused_at = 0;
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
    if (o && !(o->config->type == WALLPAPER_SHADER && a->shader_paused)) {
        bool idle = o->config->type == WALLPAPER_TERMINAL && o->shader &&
                    o->frames_rendered > 0 &&
                    !term_gpu_animating(wallshader_terminal(o->shader), APP_TERM_FX_SETTLE_MS);
        if (!idle) atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        /* A frame that won't render must still move the pacer on, or its
         * timer fires once and goes quiet. One that will render moves it
         * after the swap; doing it here too halves the frame rate. */
        else output_pace_advance(o, (uint64_t)rt_now_ns());
    } else if (o) {
        output_pace_advance(o, (uint64_t)rt_now_ns());   /* paused shader */
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Last-known-good: the whole file is checked before anything applies, and a
 * failure keeps the old config. A socket request gets the outcome. */
static void reload(struct app *a, uint32_t conn) {
    struct neowall_state *state = a->state;
    log_info("Reloading configuration from %s", state->config_path);
    nw_sd_notify("RELOADING=1");
    bool ok = config_reload(state, state->config_path);
    if (ok) {
        log_info("Configuration reloaded");
        app_mark_redraw_all(state);
    } else {
        log_error("Reload failed; previous configuration remains in effect");
    }
    nw_sd_notify("READY=1");
    if (conn)
        ipc_server_reply(a->ipc, conn, ok,
                         ok ? "configuration reloaded"
                            : "reload failed; the previous configuration is still in effect");
}

void app_perform(void *self, rt_loop *loop, const rt_cmd *c) {
    struct app *a = self;
    struct neowall_state *state = a->state;

    switch (c->kind) {
    case APP_CMD_NOTIFY:
        nw_sd_notify(c->v.s);
        break;

    case APP_CMD_STOP:
        atomic_store_explicit(&state->running, false, memory_order_release);
        break;

    case APP_CMD_RELOAD:
        reload(a, (uint32_t)c->v.u);
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

    case APP_CMD_READ_TERMINAL_FILE:
        if (!read_set_terminal_file(a->ipc_terminal_cmd, sizeof(a->ipc_terminal_cmd))) {
            log_error("set-terminal requested but no valid command file found");
            break;
        }
        app_send(loop, NW_MSG_SET_TERMINAL, 0, 0);
        break;

    case APP_CMD_SHADER_PAUSE:
        shader_pause(state, c->v.i != 0);
        break;

    case APP_CMD_REDRAW_ALL:
        app_mark_redraw_all(state);
        break;

    case APP_CMD_FRAME:
        frame_due(a, c->id);
        break;

    case APP_CMD_OCCLUSION:
        occlusion_update(state);
        break;

    case APP_CMD_WATCH_RELOAD:
        if (state->watch_path[0]) watch_reload(state, state->watch_path);
        break;

    default:
        log_error("unknown command %u", c->kind);
        break;
    }
}
