/* update and subscribe.
 *
 * update folds one message into the model and lists the effects to run. It
 * never does I/O, never touches GL and never sees the loop: everything
 * outside the model goes out as a command (perform.c). So the same message
 * on the same model always does the same thing, and update can be tested
 * without a display. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/core/constants.h"
#include "neowall/egl/gpu_health.h"
#include "neowall/core/log.h"

static void cmd(rt_cmds *cmds, uint32_t kind, uint32_t id, int64_t v) {
    rt_cmd c = {.kind = kind, .id = id};
    c.v.i    = v;
    rt_cmd_push(cmds, &c);
}

static void notify(rt_cmds *cmds, const char *line) {
    rt_cmd c = {.kind = APP_CMD_NOTIFY};
    c.v.s    = line;
    rt_cmd_push(cmds, &c);
}

/* Signals carry id = signo. Messages from the socket or from perform carry
 * id 0, with any argument already in hand. */
static bool from_signal(const rt_msg *m) {
    return m->id != 0;
}

void app_model_init(struct app_model *a) {
    *a = (struct app_model){.set_index = -1};
}

/* present used these up; clear each only if it's still the same request. */
static void applied(struct app_model *a, const struct app_applied *done) {
    if ((done->flags & APP_APPLIED_NEXT) && a->next_pending > 0) a->next_pending--;
    if ((done->flags & APP_APPLIED_INDEX) && a->set_index == done->index) a->set_index = -1;
    if ((done->flags & APP_APPLIED_TERMINAL) && a->terminal_gen == done->terminal_gen)
        a->terminal_pending = false;
    if ((done->flags & APP_APPLIED_OUTPUTS) && a->outputs_gen == done->outputs_gen)
        a->outputs_need_init = false;
}

/* Answer every client waiting on the reload, and tell systemd it's done. */
static void reload_finish(struct app_model *a, bool ok, rt_cmds *cmds) {
    if (ok)
        log_info("Configuration reloaded");
    else
        log_error("Reload failed; previous configuration remains in effect");
    for (int i = 0; i < a->reload_nwaiters; i++) {
        rt_cmd c = {.kind = APP_CMD_REPLY, .id = a->reload_waiters[i]};
        c.v.i    = ok;
        rt_cmd_push(cmds, &c);
    }
    a->reload_nwaiters = 0;
    a->reload_building = false;
    notify(cmds, "READY=1");
}

/* A reload arrives. While one is still building, a new one restarts it:
 * the waiters stay and get the newest outcome, which is what's on screen. */
static void reload_start(struct app_model *a, uint32_t conn, rt_cmds *cmds) {
    if (conn) {
        if (a->reload_nwaiters < APP_RELOAD_WAITERS) {
            a->reload_waiters[a->reload_nwaiters++] = conn;
        } else {
            rt_cmd c = {.kind = APP_CMD_REPLY, .id = conn}; /* too many: refuse */
            rt_cmd_push(cmds, &c);
        }
    }
    notify(cmds, "RELOADING=1");
    cmd(cmds, APP_CMD_RELOAD, 0, 0);
}

void app_model_update(struct app_model *a, const rt_msg *m, rt_cmds *cmds) {
    switch (m->kind) {
    case NW_MSG_QUIT:
        log_info("Shutting down");
        notify(cmds, "STOPPING=1");
        cmd(cmds, APP_CMD_STOP, 0, 0);
        rt_cmd_quit(cmds, 0);
        return;

    case NW_MSG_RELOAD:
        /* v.i is the socket connection waiting for the outcome, or 0. */
        reload_start(a, from_signal(m) ? 0 : (uint32_t)m->v.i, cmds);
        return;

    case NW_MSG_RELOAD_APPLIED: {
        /* The file was applied. If a shader is still compiling, the answer
         * waits for it: "reloaded" must mean it's on screen. */
        bool ok = m->v.i & 1, building = m->v.i & 2;
        if (ok && building) {
            a->reload_building = true;
            cmd(cmds, APP_CMD_WATCH_BUILDS, 0, 0);
            return;
        }
        reload_finish(a, ok, cmds);
        return;
    }

    case NW_MSG_RELOAD_SETTLED:
        if (a->reload_building) reload_finish(a, m->v.i != 0, cmds);
        return;

    case NW_MSG_NEXT:
        if (a->next_pending >= MAX_NEXT_REQUESTS) {
            log_warn("Skip-next queue is full (%d), ignoring", a->next_pending);
        } else {
            a->next_pending++;
            log_info("Skipping to next wallpaper (queue: %d)", a->next_pending);
        }
        return;

    case NW_MSG_CYCLE_PAUSE:
        log_info("Pausing wallpaper cycling");
        a->cycle_paused = true;
        return;

    case NW_MSG_CYCLE_RESUME:
        log_info("Resuming wallpaper cycling");
        a->cycle_paused = false;
        return;

    case NW_MSG_SET_INDEX:
        /* An older CLI wrote the index to a file and sent a signal. Reading
         * it is an effect; the result comes back as this message with id 0. */
        if (from_signal(m)) {
            cmd(cmds, APP_CMD_READ_INDEX_FILE, 0, 0);
            return;
        }
        log_info("Setting wallpaper to index %d", (int)m->v.i);
        a->set_index = (int)m->v.i;
        return;

    case NW_MSG_SHADER_PAUSE:
    case NW_MSG_SHADER_RESUME: {
        bool want = m->kind == NW_MSG_SHADER_PAUSE;
        if (want == a->shader_paused) return;
        a->shader_paused = want;
        int64_t duration = from_signal(m) ? 0 : m->v.i;
        cmd(cmds, APP_CMD_SHADER_PAUSE, 0, want ? duration : -1);
        log_info("Shader animation %s", want ? "pausing" : "resumed");
        return;
    }

    case NW_MSG_SET_TERMINAL:
        /* Same as SET_INDEX: a signal means the command is in a file. From
         * the socket or perform, v.p is a malloc'd copy and it's ours now. */
        if (from_signal(m)) {
            cmd(cmds, APP_CMD_READ_TERMINAL_FILE, 0, 0);
            return;
        }
        if (!m->v.p) return;
        snprintf(a->terminal_cmd, sizeof(a->terminal_cmd), "%s", (const char *)m->v.p);
        free(m->v.p);
        a->terminal_pending = true;
        a->terminal_gen++;
        log_info("Swapping terminal wallpaper to: %s", a->terminal_cmd);
        return;

    case NW_MSG_SIGNAL: log_debug("Received signal: %u", m->id); return;

    case NW_MSG_CYCLE_DUE:
        /* present decides which outputs actually cycle (output_should_cycle),
         * so a due timer marks them all for it to look at. */
        cmd(cmds, APP_CMD_REDRAW_ALL, 0, 0);
        return;

    case NW_MSG_FRAME_DUE: cmd(cmds, APP_CMD_FRAME, m->id, 0); return;

    case NW_MSG_OUTPUTS_NEW:
        /* A new wl_output (hotplug, monitor back from DPMS). present brings
         * it up after this step's dispatch configured it. */
        a->outputs_need_init = true;
        a->outputs_gen++;
        return;

    case NW_MSG_APPLIED: {
        struct app_applied done;
        memcpy(&done, m->v.bytes, sizeof(done));
        applied(a, &done);
        return;
    }

    case NW_MSG_TRANSITION:
    case NW_MSG_REDRAW:
    case NW_MSG_REDRAW_OUTPUT: return;

    case NW_MSG_DISPLAY:
        if (a->has_occlusion) cmd(cmds, APP_CMD_OCCLUSION, 0, 0);
        return;

    case NW_MSG_GPU_LOST:
        /* Come back in a fresh process: systemd or the re-exec in main(). */
        log_error("Graphics can't recover in this process; exiting to restart cleanly");
        notify(cmds, "STOPPING=1");
        cmd(cmds, APP_CMD_STOP, 0, 0);
        rt_cmd_quit(cmds, NW_EXIT_GPU_LOST);
        return;

    case NW_MSG_WATCH: cmd(cmds, APP_CMD_WATCH_RELOAD, 0, 0); return;
    }
}

void app_update(void *self, const rt_msg *m, rt_cmds *cmds) {
    app_model_update(&((struct app *)self)->model, m, cmds);
}
