/* update and subscribe: fold one message into the model. No GL here. */

#include <stdio.h>
#include <unistd.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/app/watch.h"
#include "neowall/config/config.h"
#include "neowall/core/constants.h"
#include "neowall/occlusion/occlusion.h"
#include "neowall/output/output.h"
#include "neowall/runtime/sd_notify.h"
#include "neowall/shader/term_gpu.h"
#include "neowall/shader/wallshader.h"

/* ── update ───────────────────────────────────────────────────────────── */

static struct output_state *output_by_id(struct neowall_state *state, uint32_t id) {
    for (struct output_state *o = state->outputs; o; o = o->next)
        if (o->name == id) return o;
    return NULL;
}

static void mark_redraw_all(struct neowall_state *state) {
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next)
        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* After the compositor fd was serviced: occlusion, and every per-iteration
 * redraw rule the old loop applied (transitions, vsync animation, terminals).
 * Runs as part of the DISPLAY message, so it's one place, not scattered. */
static void after_display(struct app *a) {
    struct neowall_state *state = a->state;
    if (a->has_occlusion) {
        occlusion_update(state);
    }
}

/* Freeze or unfreeze the shader animation across every shader output.
 * Freezing records the instant it stopped; unfreezing advances each shader's
 * start_time by the frozen duration, so it picks up on the frame it stopped. */
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

/* What the outputs can do right now, for answering commands honestly. */
struct output_summary {
    size_t longest_cycle;   /* largest cycle on any output (0/1 = nothing to cycle) */
    bool has_terminal;
};

static struct output_summary summarize_outputs(struct neowall_state *state) {
    struct output_summary s = {0};
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->cycle && o->config->cycle_count > s.longest_cycle) {
            s.longest_cycle = o->config->cycle_count;
        }
        if (o->config->type == WALLPAPER_TERMINAL) s.has_terminal = true;
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return s;
}

/* `set <index>`: answered for real, since an index no output can take is
 * an error, not an "ok" the daemon then quietly drops. */
static void ipc_set_index(struct app *a, rt_loop *loop, const nw_ipc_request_t *r) {
    char text[NW_IPC_MAX_LINE];
    char *end = NULL;
    long idx = strtol(r->arg, &end, 10);
    if (!r->arg[0] || *end || idx < 0 || idx > 100000) {
        ipc_server_reply(a->ipc, r->conn, false, "usage: set <index>");
        return;
    }
    size_t most = summarize_outputs(a->state).longest_cycle;
    if (most <= 1) {
        ipc_server_reply(a->ipc, r->conn, false,
                         "nothing to set: only one wallpaper or shader is configured");
        return;
    }
    if ((size_t)idx >= most) {
        snprintf(text, sizeof(text), "index %ld is out of range (0-%zu)", idx, most - 1);
        ipc_server_reply(a->ipc, r->conn, false, text);
        return;
    }
    rt_msg m = {.kind = NW_MSG_SET_INDEX};
    m.v.i = idx;
    rt_send(loop, &m);
    snprintf(text, sizeof(text), "setting wallpaper %ld", idx);
    ipc_server_reply(a->ipc, r->conn, true, text);
}

/* `set-terminal <command>` */
static void ipc_set_terminal(struct app *a, rt_loop *loop, const nw_ipc_request_t *r) {
    if (!r->arg[0] || strlen(r->arg) >= sizeof(a->terminal_cmd)) {
        ipc_server_reply(a->ipc, r->conn, false,
                         r->arg[0] ? "command too long" : "usage: set-terminal <command>");
        return;
    }
    if (!summarize_outputs(a->state).has_terminal) {
        ipc_server_reply(a->ipc, r->conn, false, "no output is running a terminal wallpaper");
        return;
    }
    /* The command is too long for a message; it rides in the model and the
     * message says it's there. */
    snprintf(a->ipc_terminal_cmd, sizeof(a->ipc_terminal_cmd), "%s", r->arg);
    rt_msg m = {.kind = NW_MSG_SET_TERMINAL, .id = 1};
    rt_send(loop, &m);
    ipc_server_reply(a->ipc, r->conn, true, "swapping terminal wallpaper");
}

/* A request off the control socket. The command verbs fold through the same
 * messages the signals do, so there is one path for each command. */
static void ipc_answer(struct app *a, rt_loop *loop, const nw_ipc_request_t *r) {
    static const struct { const char *verb; uint32_t kind; const char *done; } simple[] = {
        {"next",          NW_MSG_NEXT,          "skipping to next wallpaper"},
        {"pause",         NW_MSG_CYCLE_PAUSE,   "cycling paused"},
        {"resume",        NW_MSG_CYCLE_RESUME,  "cycling resumed"},
        {"pause-shader",  NW_MSG_SHADER_PAUSE,  "shader animation paused"},
        {"resume-shader", NW_MSG_SHADER_RESUME, "shader animation resumed"},
        {"kill",          NW_MSG_QUIT,          "stopping"},
    };
    char text[NW_IPC_MAX_LINE];

    if (strcmp(r->verb, "ping") == 0) {
        snprintf(text, sizeof(text), "neowall %s pid %d", NEOWALL_VERSION, (int)getpid());
        ipc_server_reply(a->ipc, r->conn, true, text);
        return;
    }

    if (strcmp(r->verb, "next") == 0 && summarize_outputs(a->state).longest_cycle <= 1) {
        /* Say so instead of pretending: nothing to cycle is an answer. */
        ipc_server_reply(a->ipc, r->conn, false,
                         "nothing to cycle: only one wallpaper or shader is configured");
        return;
    }

    if (strcmp(r->verb, "reload") == 0) {
        /* Reload is answered with its outcome, so it runs here rather than as
         * a queued message: the client wants to know if the new config took. */
        bool ok = config_reload(a->state, a->state->config_path);
        if (ok) mark_redraw_all(a->state);
        ipc_server_reply(a->ipc, r->conn, ok,
                         ok ? "configuration reloaded"
                            : "reload failed; the previous configuration is still in effect");
        return;
    }

    for (size_t i = 0; i < sizeof(simple) / sizeof(simple[0]); i++) {
        if (strcmp(r->verb, simple[i].verb) != 0) continue;
        /* Queue it like a signal would; update() folds it in this same step.
         * Reply now: "kill" stops the loop, and the client is owed an answer
         * before that. */
        ipc_server_reply(a->ipc, r->conn, true, simple[i].done);
        rt_msg m = {.kind = simple[i].kind};
        rt_send(loop, &m);
        return;
    }

    if (strcmp(r->verb, "set") == 0) {
        ipc_set_index(a, loop, r);
        return;
    }
    if (strcmp(r->verb, "set-terminal") == 0) {
        ipc_set_terminal(a, loop, r);
        return;
    }

    snprintf(text, sizeof(text), "unknown command '%s'", r->verb);
    ipc_server_reply(a->ipc, r->conn, false, text);
}

void app_on_ipc_request(void *ctx, rt_loop *loop, const nw_ipc_request_t *req) {
    /* The request lives in the server's buffer only for this call, so answer
     * now. It still runs on the loop thread, in this step, like any message. */
    ipc_answer(ctx, loop, req);
}

bool app_update(void *self, rt_loop *loop, const rt_msg *m) {
    struct app *a = self;
    struct neowall_state *state = a->state;

    switch (m->kind) {
    case NW_MSG_QUIT:
        log_info("Shutting down");
        nw_sd_notify("STOPPING=1");
        atomic_store_explicit(&state->running, false, memory_order_release);
        rt_quit(loop, 0);
        return true;

    case NW_MSG_RELOAD:
        log_info("Reloading configuration from %s", state->config_path);
        nw_sd_notify("RELOADING=1");
        /* Last-known-good: the whole file is validated before anything is
         * applied, and a failure never falls back to the built-in default. */
        if (!config_reload(state, state->config_path)) {
            log_error("Reload failed; previous configuration remains in effect");
        } else {
            log_info("Configuration reloaded");
            mark_redraw_all(state);
        }
        nw_sd_notify("READY=1");
        return true;

    case NW_MSG_NEXT:
        if (a->next_pending >= MAX_NEXT_REQUESTS) {
            log_warn("Skip-next queue is full (%d), ignoring", a->next_pending);
        } else {
            a->next_pending++;
            log_info("Skipping to next wallpaper (queue: %d)", a->next_pending);
        }
        return true;

    case NW_MSG_CYCLE_PAUSE:
        log_info("Pausing wallpaper cycling");
        a->cycle_paused = true;
        return true;

    case NW_MSG_CYCLE_RESUME:
        log_info("Resuming wallpaper cycling");
        a->cycle_paused = false;
        return true;

    case NW_MSG_SET_INDEX: {
        /* A signal message has id = signo and no argument: an older CLI wrote
         * the index to a file first. From the socket, id is 0 and the index
         * is in the message. */
        int index = m->id != 0 ? read_set_index_file() : (int)m->v.i;
        if (index >= 0) {
            log_info("Setting wallpaper to index %d", index);
            a->set_index = index;
        } else {
            log_error("set-index requested but no valid index file found");
        }
        return true;
    }

    case NW_MSG_SHADER_PAUSE:
    case NW_MSG_SHADER_RESUME: {
        bool want = m->kind == NW_MSG_SHADER_PAUSE;
        a->shader_paused = want;
        if (want != a->shader_pause_applied) {
            apply_shader_pause(state, want);
            a->shader_pause_applied = want;
            log_info("Shader animation %s", want ? "paused" : "resumed");
        }
        return true;
    }

    case NW_MSG_SET_TERMINAL: {
        /* id = signo from a signal (read the file), 1 from the socket (the
         * command is already in ipc_terminal_cmd). */
        char buf[512];
        bool have = m->id == 1 ? (snprintf(buf, sizeof(buf), "%s", a->ipc_terminal_cmd), true)
                               : read_set_terminal_file(buf, sizeof(buf));
        if (have) {
            snprintf(a->terminal_cmd, sizeof(a->terminal_cmd), "%s", buf);
            a->terminal_pending = true;
            log_info("Swapping terminal wallpaper to: %s", buf);
        } else {
            log_error("set-terminal requested but no valid command file found");
        }
        return true;
    }

    case NW_MSG_SIGNAL:
        log_debug("Received signal: %u", m->id);
        return true;

    case NW_MSG_CYCLE_DUE:
        /* render_outputs() decides which outputs actually cycle
         * (output_should_cycle), so a due timer marks every output: that's what
         * gets render_outputs() to run and look. */
        mark_redraw_all(state);
        return true;

    case NW_MSG_FRAME_DUE: {
        /* A vsync-off animated output's frame. A terminal is idle-gated: the
         * tick still comes so fresh child output is seen promptly, but only an
         * animating terminal is re-rendered. */
        pthread_rwlock_rdlock(&state->output_list_lock);
        struct output_state *o = output_by_id(state, m->id);
        if (o && !(o->config->type == WALLPAPER_SHADER && a->shader_paused)) {
            bool idle = o->config->type == WALLPAPER_TERMINAL && o->shader &&
                        o->frames_rendered > 0 &&
                        !term_gpu_animating(wallshader_terminal(o->shader), APP_TERM_FX_SETTLE_MS);
            if (!idle) atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            /* A frame that won't render (idle terminal) must still move the
             * pacer on, or its timer fires once and goes silent. A frame that
             * will render moves it after the swap (render_outputs); doing it
             * here too skips a period every frame: 30 fps instead of 60. */
            else output_pace_advance(o, (uint64_t)rt_now_ns());
        } else if (o) {
            output_pace_advance(o, (uint64_t)rt_now_ns());   /* paused shader */
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        return true;
    }

    case NW_MSG_OUTPUTS_NEW:
        /* The registry saw a new wl_output (hotplug, monitor back from DPMS).
         * present() brings it up after this step's dispatch configured it. */
        a->outputs_need_init = true;
        return true;

    case NW_MSG_TRANSITION:
    case NW_MSG_REDRAW:
    case NW_MSG_REDRAW_OUTPUT:
        return true;

    case NW_MSG_DISPLAY:
        after_display(a);
        return true;

    case NW_MSG_WATCH:
        if (state->watch_path[0]) watch_reload(state, state->watch_path);
        return true;
    }
    return true;
}

/* ── subscriptions ────────────────────────────────────────────────────── */

void app_subscribe(void *self, rt_subs *subs) {
    struct app *a = self;
    struct neowall_state *state = a->state;
    bool transition = false;
    bool building = false;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        bool occluded = o->config->pause_on_fullscreen &&
                        atomic_load_explicit(&o->occluded, memory_order_acquire);

        /* Cycling: one timer per output, due when its duration is up. */
        if (!a->cycle_paused && o->config->cycle && o->config->duration > 0.0f &&
            o->config->cycle_count > 1) {
            uint64_t dur_ms = (uint64_t)(o->config->duration * 1000.0f);
            int64_t due = (int64_t)(o->last_cycle_time + dur_ms) * 1000000LL;
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
            !(o->config->type == WALLPAPER_SHADER && a->shader_paused) &&
            !(o->config->type == WALLPAPER_SHADER && o->shader &&
              !wallshader_animated(o->shader) && o->frames_rendered > 0)) {
            rt_sub_at(subs, NW_MSG_FRAME_DUE, o->name, (int64_t)o->pace_next_deadline_ns);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (transition || building) rt_sub_every(subs, NW_MSG_TRANSITION, 0, 16666667);
}
