/* The neowall program on the rt runtime. See docs/architecture/RUNTIME.md.
 *
 *   on      signals, the compositor fd, the watch fd, other threads  → msgs
 *   update  folds one msg into the model (struct app); no GL, no I/O
 *   subs    which timers exist: one per cycling output, one per vsync-off
 *           animated output, one while a transition runs
 *   present draws every output that needs it (render_outputs, unchanged)
 *
 * This replaced eventloop.c. The request atomics that used to live on
 * neowall_state (next_requested, set_index_requested, set_terminal_requested,
 * paused, shader_paused, outputs_need_init) are model fields here: only the
 * loop thread touches them, and only through update(). */

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "neowall/neowall.h"
#include "neowall/app.h"
#include "neowall/ipc.h"
#include "neowall/sd_notify.h"
#include "neowall/runtime/rt.h"
#include "neowall/watch.h"
#include "neowall/shader/wallshader.h"
#include "neowall/config/config_access.h"
#include "neowall/config/config.h"
#include "neowall/egl/egl_core.h"
#include "neowall/egl/gpu_health.h"
#include "neowall/output/output.h"
#include "neowall/shader/reactive.h"
#include "neowall/constants.h"
#include "neowall/compositor/compositor.h"
#include "neowall/occlusion/occlusion.h"
#include "neowall/vec.h"

#ifdef HAVE_WAYLAND_BACKEND
bool output_configure_compositor_surface(struct output_state *output);
void wayland_request_present_feedback(struct output_state *output);
#endif

/* Readers for the command files the CLI writes before signalling (main.c). */
int read_set_index_file(void);
bool read_set_terminal_file(char *buf, size_t size);

/* A terminal's FX settle window: the change-fade plus cursor slide/trail
 * decay, with a margin so the last animated frame is never clipped. */
static const unsigned kTermFxSettleMs = 700;

/* ── the model ────────────────────────────────────────────────────────── */

struct app {
    struct neowall_state *state;
    rt_loop *loop;

    /* The GL context or a surface is unrecoverable: stop and exit with
     * NW_EXIT_GPU_LOST so we come back in a fresh process. */
    bool gpu_lost;

    /* Commands waiting for the next frame. */
    int next_pending;
    int set_index;                  /* -1 = none */
    bool terminal_pending;
    char terminal_cmd[512];

    bool cycle_paused;              /* `neowall pause` */
    bool shader_paused;             /* `neowall pause-shader` */
    bool shader_pause_applied;      /* what apply_shader_pause last did */

    /* The compositor told us outputs need bring-up (reconnect, hotplug). */
    bool outputs_need_init;

    /* Host bookkeeping. */
    int compositor_fd;
    uint32_t compositor_watch;
    bool flush_blocked;
    bool read_prepared;
    uint32_t watch_handle;
    bool has_occlusion;

    uint64_t stats_since_ms;
    uint64_t stats_frames;

    nw_ipc_server *ipc;             /* NULL for a preview, or if binding failed */
    char ipc_terminal_cmd[512];     /* set-terminal's command, for its message */
};

/* The loop for app_post / app_request_* from other threads. Set for the
 * lifetime of app_run; NULL otherwise, so a post before or after is a no-op.
 * g_posters counts posts in flight: app_run clears g_loop, then waits for it
 * to drain before freeing the loop. Both are seq_cst, so either a poster sees
 * NULL or app_run sees the poster. */
static _Atomic(rt_loop *) g_loop = NULL;
static atomic_int g_posters = 0;
/* Armed by the loop just before it sleeps with nothing else due to wake it.
 * A terminal's reader only posts when it can claim this, so a chatty child
 * costs at most one wake per sleep instead of one per read. */
static atomic_bool g_output_wake_armed = true;

static void post(rt_loop *l, uint32_t kind, uint32_t id, int64_t v) {
    rt_msg m = {.kind = kind, .id = id};
    m.v.i = v;
    rt_post(l, &m);
}

bool app_post(uint32_t kind, uint32_t id, int64_t value) {
    atomic_fetch_add(&g_posters, 1);
    rt_loop *l = atomic_load(&g_loop);
    bool ok = false;
    if (l) {
        rt_msg m = {.kind = kind, .id = id};
        m.v.i = value;
        ok = rt_post(l, &m);
    }
    atomic_fetch_sub(&g_posters, 1);
    return ok;
}

void app_request_redraw(struct neowall_state *state) {
    if (!state) return;
    /* Keep the dirty bits as the source of truth for present(), then wake the
     * loop so it runs a step and notices them. */
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next)
        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
    pthread_rwlock_unlock(&state->output_list_lock);
    app_post(NW_MSG_REDRAW, 0, 0);
}

void app_request_output_redraw(struct output_state *output) {
    if (!output) return;
    /* seq_cst pairs with app_wait_hint: it arms, then reads needs_redraw. */
    atomic_store(&output->needs_redraw, true);
    if (atomic_exchange(&g_output_wake_armed, false))
        app_post(NW_MSG_REDRAW, 0, 0);
}

/* ── rendering ────────────────────────────────────────────────────────── */

struct swap_info {
    struct output_state *output;
    bool render_success;
};

NW_VEC_DEFINE_STATIC(output_ptr_vec, struct output_state *)
NW_VEC_DEFINE_STATIC(swap_vec, struct swap_info)

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

#include "app_render.inc"

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

/* The redraw rules for an output that isn't frame-timed. A vsync output is
 * paced by its own frame callback (the swap doesn't block), so "redraw it
 * every step" is what drives it. */
static void mark_animated(struct app *a) {
    struct neowall_state *state = a->state;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->pause_on_fullscreen &&
            atomic_load_explicit(&o->occluded, memory_order_acquire)) {
            continue;
        }
        if (o->transition_start_time > 0 && o->config->transition != TRANSITION_NONE) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        }
        if (!wallpaper_is_animated(o->config->type)) continue;
        if (o->shader_load_failed) continue;
        if (o->shader == NULL) continue;
        if (o->config->type == WALLPAPER_SHADER && a->shader_paused) continue;

        /* A static shader painted its one frame. */
        bool is_static = o->config->type == WALLPAPER_SHADER &&
                         !wallshader_animated(o->shader) && o->frames_rendered > 0;
        /* A settled terminal only redraws when its child writes. */
        bool term_idle = o->config->type == WALLPAPER_TERMINAL &&
                         o->frames_rendered > 0 &&
                         !term_gpu_animating(wallshader_terminal(o->shader), kTermFxSettleMs);
        /* An output with a frame timer (vsync off, or vsync without frame
         * callbacks) is driven by FRAME_DUE. Anything else is paced by its
         * frame callback, so mark it every step. */
        if (!is_static && !term_idle && output_get_frame_timer_fd(o) < 0) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

static bool app_update(void *self, rt_loop *loop, const rt_msg *m);

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

static void on_ipc_request(void *ctx, rt_loop *loop, const nw_ipc_request_t *req) {
    /* The request lives in the server's buffer only for this call, so answer
     * now. It still runs on the loop thread, in this step, like any message. */
    ipc_answer(ctx, loop, req);
}

static bool app_update(void *self, rt_loop *loop, const rt_msg *m) {
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
                        !term_gpu_animating(wallshader_terminal(o->shader), kTermFxSettleMs);
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

static void app_subscribe(void *self, rt_subs *subs) {
    struct app *a = self;
    struct neowall_state *state = a->state;
    bool transition = false;

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

    if (transition) rt_sub_every(subs, NW_MSG_TRANSITION, 0, 16666667);
}

/* ── present ──────────────────────────────────────────────────────────── */

static void app_present(void *self, rt_loop *loop) {
    struct app *a = self;
    struct neowall_state *state = a->state;

#ifdef HAVE_WAYLAND_BACKEND
    if (a->outputs_need_init) {
        a->outputs_need_init = false;
        reinit_reconnected_outputs(state);
    }
#endif

    mark_animated(a);

    bool any = a->next_pending > 0 || a->set_index >= 0 || a->terminal_pending;
    if (!any) {
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            if (atomic_load_explicit(&o->needs_redraw, memory_order_acquire)) {
                any = true;
                break;
            }
        }
        pthread_rwlock_unlock(&state->output_list_lock);
    }
    if (any) render_outputs(a);
    if (a->gpu_lost) {
        log_error("Graphics can't recover in this process; exiting to restart cleanly");
        nw_sd_notify("STOPPING=1");
        rt_quit(loop, NW_EXIT_GPU_LOST);
        return;
    }

    a->stats_frames++;
    uint64_t now = get_time_ms();
    if (now - a->stats_since_ms >= STATS_INTERVAL_MS) {
        double secs = (double)(now - a->stats_since_ms) / (double)MS_PER_SECOND;
        log_debug("Stats: %.1f steps/s, %lu frames rendered, %lu errors",
                  (double)a->stats_frames / secs, state->frames_rendered, state->errors_count);
        a->stats_since_ms = now;
        a->stats_frames = 0;
    }
}

/* A vsync output waits for its frame callback, which arrives on the
 * compositor fd. If the compositor never sends it (hidden surface), the
 * frame-ready timeout must still fire, so don't sleep past it. */
static int64_t app_wait_hint(void *self) {
    struct app *a = self;
    struct neowall_state *state = a->state;
    int64_t hint = -1;
    /* Arm the output wake only if some output has no frame callback in
     * flight; otherwise the callback wakes us and sees needs_redraw. */
    bool want_wake = false;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->compositor_surface || !o->compositor_surface->frame_cb) {
            want_wake = true;
            break;
        }
    }
    atomic_store(&g_output_wake_armed, want_wake);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->config->vsync || output_get_frame_timer_fd(o) >= 0) continue;
        if (o->compositor_surface && o->compositor_surface->frame_cb) {
            hint = 50 * 1000000LL;
            break;
        }
        /* An animated vsync output with no callback pending wants the next
         * step at once: present() will draw it and request one. Not if it
         * can't draw (paused shader, hidden output): needs_redraw stays set
         * for those, and treating it as owed work would spin the loop. */
        if (!wallpaper_is_animated(o->config->type)) continue;
        if (o->config->type == WALLPAPER_SHADER && a->shader_paused) continue;
        if (o->config->pause_on_fullscreen &&
            atomic_load_explicit(&o->occluded, memory_order_acquire)) continue;
        if (atomic_load(&o->needs_redraw)) {
            hint = 0;
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return hint;
}

/* ── host: the compositor fd ──────────────────────────────────────────── */

/* Before waiting: dispatch what's queued, flush, and tell rt whether the
 * backend already holds events poll() can't see (Xlib's queue). */
static bool host_prepare(void *ctx, rt_loop *loop) {
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend ?
                                          state->compositor_backend->ops : NULL;
    void *bd = state->compositor_backend ? state->compositor_backend->data : NULL;
    if (!ops) return false;

    a->read_prepared = false;
    if (ops->prepare_events) {
        if (!ops->prepare_events(bd)) {
            log_error("Failed to prepare compositor events");
            post(loop, NW_MSG_QUIT, 0, 0);
            return true;
        }
        a->read_prepared = true;
    }
    if (ops->flush) {
        compositor_flush_result_t fr = ops->flush(bd);
        if (fr == COMPOSITOR_FLUSH_FATAL) {
            log_error("Failed to flush compositor requests");
            post(loop, NW_MSG_QUIT, 0, 0);
            return true;
        }
        a->flush_blocked = fr == COMPOSITOR_FLUSH_BLOCKED;
        if (a->compositor_watch)
            rt_watch_modify(loop, a->compositor_watch,
                            RT_READ | (a->flush_blocked ? RT_WRITE : 0));
    }
    return ops->has_pending_events && ops->has_pending_events(bd);
}

static void on_compositor(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    (void)fd;
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *bd = state->compositor_backend->data;

    if (ev & RT_HANGUP) {
        log_error("Compositor disconnected, display server shut down");
        if (a->read_prepared && ops->cancel_read) ops->cancel_read(bd);
        a->read_prepared = false;
        post(loop, NW_MSG_QUIT, 0, 0);
        return;
    }
    if (ev & RT_READ) {
        if (ops->read_events && !ops->read_events(bd)) {
            log_error("Failed to read compositor events");
            post(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
        a->read_prepared = false;
    }
    if ((ev & RT_WRITE) && a->flush_blocked && ops->flush) {
        compositor_flush_result_t fr = ops->flush(bd);
        if (fr == COMPOSITOR_FLUSH_FATAL) {
            log_error("Failed to flush compositor requests after POLLOUT");
            post(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
        a->flush_blocked = fr == COMPOSITOR_FLUSH_BLOCKED;
    }
}

/* After every wait, whatever woke it: balance a prepared read (the fd
 * callback above already did, if the fd was readable), dispatch what's
 * queued (frame callbacks), and check for errors. */
static void host_after_wait(void *ctx, rt_loop *loop, bool fd_ready) {
    (void)fd_ready;
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *bd = state->compositor_backend->data;

    if (a->read_prepared && ops->cancel_read) ops->cancel_read(bd);
    a->read_prepared = false;

    if (ops->dispatch_events && !ops->dispatch_events(bd)) {
        log_error("Failed to dispatch compositor events, display server may have disconnected");
        post(loop, NW_MSG_QUIT, 0, 0);
        return;
    }
    if (ops->get_error) {
        int e = ops->get_error(bd);
        if (e != 0) {
            log_error("Compositor error (code %d), exiting", e);
            post(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
    }
    rt_msg m = {.kind = NW_MSG_DISPLAY};
    rt_send(loop, &m);
}

static void on_watch(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    struct app *a = ctx;
    if (!(ev & RT_READ)) return;
    /* Always drain, even if the name doesn't match, or the fd stays ready. */
    if (watch_consume(fd) && a->state->watch_path[0]) {
        rt_msg m = {.kind = NW_MSG_WATCH};
        rt_send(loop, &m);
    }
}

/* ── run ──────────────────────────────────────────────────────────────── */

int app_run(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state for the runtime");
        return EXIT_FAILURE;
    }

    struct app a = {.state = state, .set_index = -1, .compositor_fd = -1};
    a.stats_since_ms = get_time_ms();
    a.loop = rt_loop_new();
    if (!a.loop) {
        log_error("Failed to create the runtime loop");
        return EXIT_FAILURE;
    }

    /* Signals: the old signalfd's set, each now a message. They were blocked
     * in every thread by main() before any thread started. */
    rt_on_signal(a.loop, SIGINT, NW_MSG_QUIT);
    rt_on_signal(a.loop, SIGTERM, NW_MSG_QUIT);
    rt_on_signal(a.loop, SIGHUP, NW_MSG_RELOAD);
    rt_on_signal(a.loop, SIGUSR1, NW_MSG_NEXT);
    rt_on_signal(a.loop, SIGUSR2, NW_MSG_CYCLE_PAUSE);
    rt_on_signal(a.loop, SIGCONT, NW_MSG_CYCLE_RESUME);
    rt_on_signal(a.loop, SIGRTMIN, NW_MSG_SET_INDEX);
    rt_on_signal(a.loop, SIGRTMIN + 1, NW_MSG_SHADER_PAUSE);
    rt_on_signal(a.loop, SIGRTMIN + 2, NW_MSG_SHADER_RESUME);
    rt_on_signal(a.loop, SIGRTMIN + 3, NW_MSG_SET_TERMINAL);

    /* The compositor fd. */
    const compositor_backend_ops_t *ops =
        state->compositor_backend ? state->compositor_backend->ops : NULL;
    void *bd = state->compositor_backend ? state->compositor_backend->data : NULL;
    if (ops && ops->get_fd) {
        a.compositor_fd = ops->get_fd(bd);
        if (a.compositor_fd >= 0) {
            a.compositor_watch = rt_watch(a.loop, a.compositor_fd, RT_READ, on_compositor, &a);
            log_info("Using compositor backend '%s' with fd %d",
                     state->compositor_backend->name, a.compositor_fd);
        }
    }
    rt_set_host_hooks(a.loop, host_prepare, host_after_wait, &a);

    if (state->watch_fd >= 0) {
        a.watch_handle = rt_watch(a.loop, state->watch_fd, RT_READ, on_watch, &a);
    }

    a.has_occlusion = occlusion_init(state);
    reactive_init();

    /* The control socket. A preview runs beside the real daemon and must not
     * take it; neither does an instance that finds another one listening. */
    if (!state->is_preview) {
        a.ipc = ipc_server_start(a.loop, on_ipc_request, &a);
        if (!a.ipc) log_warn("Control socket unavailable; CLI commands fall back to signals");
    }

    atomic_store_explicit(&g_loop, a.loop, memory_order_release);

    rt_program prog = {
        .self = &a,
        .update = app_update,
        .subscribe = app_subscribe,
        .present = app_present,
        .wait_hint_ns = app_wait_hint,
    };
    log_info("Entering main loop");
    nw_sd_notify("READY=1");
    int code = rt_run(a.loop, &prog);
    log_info("Main loop stopped");

    atomic_store(&g_loop, NULL);
    /* A terminal reader may be mid-post; it's joined later, in main(). */
    while (atomic_load(&g_posters) > 0) sched_yield();
    atomic_store_explicit(&state->running, false, memory_order_release);

    ipc_server_stop(a.ipc);

    if (a.has_occlusion) occlusion_cleanup(state);
    reactive_shutdown();
    rt_loop_free(a.loop);
    return code;
}
