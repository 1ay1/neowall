/* update and subscribe.
 *
 * update folds one message into the model and lists the effects to run. It
 * never does I/O, never touches GL and never sees the loop: everything
 * outside the model goes out as a command (perform.c). So the same message
 * on the same model always does the same thing, and update can be tested
 * without a display. */

#include <stdio.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/core/constants.h"
#include "neowall/egl/gpu_health.h"

static void cmd(rt_cmds *cmds, uint32_t kind, uint32_t id, int64_t v) {
    rt_cmd c = {.kind = kind, .id = id};
    c.v.i = v;
    rt_cmd_push(cmds, &c);
}

static void notify(rt_cmds *cmds, const char *line) {
    rt_cmd c = {.kind = APP_CMD_NOTIFY};
    c.v.s = line;
    rt_cmd_push(cmds, &c);
}

/* Signals carry id = signo. Messages from the socket or from perform carry
 * id 0, with any argument already in hand. */
static bool from_signal(const rt_msg *m) { return m->id != 0; }

void app_update(void *self, const rt_msg *m, rt_cmds *cmds) {
    struct app *a = self;

    switch (m->kind) {
    case NW_MSG_QUIT:
        log_info("Shutting down");
        notify(cmds, "STOPPING=1");
        cmd(cmds, APP_CMD_STOP, 0, 0);
        rt_cmd_quit(cmds, 0);
        return;

    case NW_MSG_RELOAD:
        /* v.u is the socket connection waiting for the outcome, or 0. */
        cmd(cmds, APP_CMD_RELOAD, 0, from_signal(m) ? 0 : m->v.i);
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
        a->shader_paused = want;
        if (want != a->shader_pause_applied) {
            cmd(cmds, APP_CMD_SHADER_PAUSE, 0, want);
            a->shader_pause_applied = want;
            log_info("Shader animation %s", want ? "paused" : "resumed");
        }
        return;
    }

    case NW_MSG_SET_TERMINAL:
        /* Same as SET_INDEX: a signal means the command is in a file. From
         * the socket or perform it is already in ipc_terminal_cmd. */
        if (from_signal(m)) {
            cmd(cmds, APP_CMD_READ_TERMINAL_FILE, 0, 0);
            return;
        }
        snprintf(a->terminal_cmd, sizeof(a->terminal_cmd), "%s", a->ipc_terminal_cmd);
        a->terminal_pending = true;
        log_info("Swapping terminal wallpaper to: %s", a->terminal_cmd);
        return;

    case NW_MSG_SIGNAL:
        log_debug("Received signal: %u", m->id);
        return;

    case NW_MSG_CYCLE_DUE:
        /* present decides which outputs actually cycle (output_should_cycle),
         * so a due timer marks them all for it to look at. */
        cmd(cmds, APP_CMD_REDRAW_ALL, 0, 0);
        return;

    case NW_MSG_FRAME_DUE:
        cmd(cmds, APP_CMD_FRAME, m->id, 0);
        return;

    case NW_MSG_OUTPUTS_NEW:
        /* A new wl_output (hotplug, monitor back from DPMS). present brings
         * it up after this step's dispatch configured it. */
        a->outputs_need_init = true;
        return;

    case NW_MSG_TRANSITION:
    case NW_MSG_REDRAW:
    case NW_MSG_REDRAW_OUTPUT:
        return;

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

    case NW_MSG_WATCH:
        cmd(cmds, APP_CMD_WATCH_RELOAD, 0, 0);
        return;
    }
}
