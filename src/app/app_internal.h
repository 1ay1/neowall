/* Shared by the files of the app program (src/app/). Not a public header.
 *
 *   app.c      the model, posting from other threads, app_run
 *   update.c   update + subscribe: msg -> model + commands. No I/O, no GL
 *   perform.c  runs the commands update asks for
 *   present.c  draw every output that needs it, bring outputs up
 *   host.c     the compositor fd and the shader watch fd -> messages
 *   control.c  the control socket's requests -> messages */
#ifndef NEOWALL_APP_INTERNAL_H
#define NEOWALL_APP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "neowall/neowall.h"
#include "neowall/runtime/ipc.h"
#include "neowall/runtime/rt.h"

/* A terminal's FX settle window: the change-fade plus cursor slide/trail
 * decay, with a margin so the last animated frame is never clipped. */
enum { APP_TERM_FX_SETTLE_MS = 700 };

/* The model. Only the loop thread touches it. */
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

/* Effects update asks for (rt_cmd.kind). perform.c runs them. */
enum {
    APP_CMD_NONE = 0,
    APP_CMD_NOTIFY,             /* v.s: an sd_notify line, static storage */
    APP_CMD_STOP,               /* tell other threads we're stopping */
    APP_CMD_RELOAD,             /* reload config; v.u = ipc conn to answer, or 0 */
    APP_CMD_READ_INDEX_FILE,    /* old CLI: index is in a file -> SET_INDEX */
    APP_CMD_READ_TERMINAL_FILE, /* old CLI: command is in a file -> SET_TERMINAL */
    APP_CMD_SHADER_PAUSE,       /* v.i = 1 freeze / 0 unfreeze shader clocks */
    APP_CMD_REDRAW_ALL,         /* mark every output dirty */
    APP_CMD_FRAME,              /* id = output: a paced frame is due */
    APP_CMD_OCCLUSION,          /* re-check fullscreen windows */
    APP_CMD_WATCH_RELOAD,       /* reapply the watched shader */
};

/* app.c */
void app_send(rt_loop *loop, uint32_t kind, uint32_t id, int64_t v);
void app_arm_output_wake(bool armed);

/* update.c */
void app_update(void *self, const rt_msg *m, rt_cmds *cmds);
void app_subscribe(void *self, rt_subs *subs);

/* perform.c */
void app_perform(void *self, rt_loop *loop, const rt_cmd *cmd);
void app_mark_redraw_all(struct neowall_state *state);

/* control.c */
void app_on_ipc_request(void *ctx, rt_loop *loop, const nw_ipc_request_t *req);

/* present.c */
void app_present(void *self, rt_loop *loop);
int64_t app_wait_hint(void *self);

/* host.c */
bool app_host_prepare(void *ctx, rt_loop *loop);
void app_host_after_wait(void *ctx, rt_loop *loop, bool fd_ready);
void app_on_compositor(void *ctx, rt_loop *loop, int fd, unsigned ev);
void app_on_watch(void *ctx, rt_loop *loop, int fd, unsigned ev);

/* Readers for the command files the CLI writes before signalling (main.c). */
int read_set_index_file(void);
bool read_set_terminal_file(char *buf, size_t size);

#endif /* NEOWALL_APP_INTERNAL_H */
