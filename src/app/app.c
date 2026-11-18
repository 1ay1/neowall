/* The neowall program on the rt runtime. See docs/internals/runtime.md.
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

#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/surface/surface.h"
#include "neowall/occlusion/occlusion.h"
#include "neowall/runtime/sd_notify.h"
#include "neowall/source/reactive.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/output.h"
#include "neowall/proto/proto.h"

/* The loop for app_post / app_request_* from other threads. Set for the
 * lifetime of app_run; NULL otherwise, so a post before or after is a no-op.
 * g_posters counts posts in flight: app_run clears g_loop, then waits for it
 * to drain before freeing the loop. Both are seq_cst, so either a poster sees
 * NULL or app_run sees the poster. */
static _Atomic(rt_loop *) g_loop = NULL;
static atomic_int g_posters      = 0;
/* Armed by the loop just before it sleeps with nothing else due to wake it.
 * A terminal's reader only posts when it can claim this, so a chatty child
 * costs at most one wake per sleep instead of one per read. */
static atomic_bool g_output_wake_armed = true;

/* Send from the loop thread itself (host hooks, perform, control). Folded in
 * this same step, before present. */
void app_send(rt_loop *l, uint32_t kind, uint32_t id, int64_t v) {
    rt_msg m = {.kind = kind, .id = id};
    m.v.i    = v;
    rt_send(l, &m);
}

bool app_send_terminal(rt_loop *l, const char *cmd) {
    char *copy = strdup(cmd);
    if (!copy) return false;
    rt_msg m = {.kind = NW_MSG_SET_TERMINAL};
    m.v.p    = copy;
    rt_send(l, &m);
    return true;
}

void app_arm_output_wake(bool armed) {
    atomic_store(&g_output_wake_armed, armed);
}

bool app_post(uint32_t kind, uint32_t id, int64_t value) {
    atomic_fetch_add(&g_posters, 1);
    rt_loop *l = atomic_load(&g_loop);
    bool ok    = false;
    if (l) {
        rt_msg m = {.kind = kind, .id = id};
        m.v.i    = value;
        ok       = rt_post(l, &m);
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
    if (atomic_exchange(&g_output_wake_armed, false)) app_post(NW_MSG_REDRAW, 0, 0);
}

/* ── run ──────────────────────────────────────────────────────────────── */

/* The hooks lower layers fire (state.h). Before app_run they reach no loop
 * and do nothing, like app_post. */
static void hook_output_dirty(struct output_state *o) {
    app_request_output_redraw(o);
}
static void hook_outputs_changed(uint32_t name) {
    app_post(NW_MSG_OUTPUTS_NEW, name, 0);
}

void app_attach(struct neowall_state *state) {
    state->on_output_dirty    = hook_output_dirty;
    state->on_outputs_changed = hook_outputs_changed;
    state->on_output_ready    = app_config_output;
}

int app_run(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state for the runtime");
        return EXIT_FAILURE;
    }

    struct app a = {.state = state, .host.compositor_fd = -1};
    app_model_init(&a.model);
    a.view.stats_since_ms = get_time_ms();
    a.loop                = rt_loop_new();
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
        a.host.compositor_fd = ops->get_fd(bd);
        if (a.host.compositor_fd >= 0) {
            a.host.compositor_watch =
                rt_watch(a.loop, a.host.compositor_fd, RT_READ, app_on_compositor, &a);
            log_info("Using compositor backend '%s' with fd %d", state->compositor_backend->name,
                     a.host.compositor_fd);
        }
    }
    rt_set_host_hooks(a.loop, app_host_prepare, app_host_after_wait, &a);

    if (state->watch_fd >= 0) {
        a.host.watch_handle = rt_watch(a.loop, state->watch_fd, RT_READ, app_on_watch, &a);
    }

    a.model.has_occlusion = occlusion_init(state);
    reactive_init();

    /* The control socket. A preview runs beside the real daemon and must not
     * take it; neither does an instance that finds another one listening. */
    if (!state->is_preview) {
        a.host.ipc = ipc_server_start(a.loop, proto_socket_path(), app_on_ipc_request, &a);
        if (!a.host.ipc) log_warn("Control socket unavailable; CLI commands fall back to signals");
    }

    atomic_store_explicit(&g_loop, a.loop, memory_order_release);

    rt_program prog = {
        .self         = &a,
        .update       = app_update,
        .perform      = app_perform,
        .subscribe    = app_subscribe,
        .present      = app_present,
        .wait_hint_ns = app_wait_hint,
    };
    log_info("Entering main loop");
    nw_sd_notify("READY=1");
    int code = rt_run(a.loop, &prog);
    log_info("Main loop stopped");

    atomic_store(&g_loop, NULL);
    /* A terminal reader may be mid-post; it's joined later, in main(). */
    while (atomic_load(&g_posters) > 0)
        sched_yield();
    atomic_store_explicit(&state->running, false, memory_order_release);

    ipc_server_stop(a.host.ipc);

    if (a.model.has_occlusion) occlusion_cleanup(state);
    reactive_shutdown();
    rt_loop_free(a.loop);
    return code;
}
