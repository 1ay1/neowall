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

/* Longest set-terminal command. */
enum { APP_TERMINAL_CMD_MAX = 512 };

/* Clients that can wait on one reload; more are answered at once. */
enum { APP_RELOAD_WAITERS = 8 };

/* The model. Only update writes it; everything else gets it const. */
struct app_model {
    /* Requests waiting for present. present reports what it used with
     * NW_MSG_APPLIED, and update clears them. */
    int next_pending;
    int set_index;                  /* -1 = none */
    bool terminal_pending;
    uint32_t terminal_gen;          /* bumps per set-terminal */
    char terminal_cmd[APP_TERMINAL_CMD_MAX];
    bool outputs_need_init;         /* reconnect / hotplug: bring outputs up */
    uint32_t outputs_gen;

    bool cycle_paused;              /* `neowall pause` */
    bool shader_paused;             /* `neowall pause-shader` */
    bool shader_pause_applied;      /* what the last SHADER_PAUSE command did */

    /* A reload in flight: applied, but a shader is still compiling in the
     * background. Clients that asked wait here for the real outcome, and
     * any number can pile on; they're all answered when it settles. */
    bool reload_building;
    uint32_t reload_waiters[APP_RELOAD_WAITERS];
    int reload_nwaiters;

    bool has_occlusion;             /* fixed at start */
};

/* What present used up this frame (NW_MSG_APPLIED's payload). The index and
 * generations say which request it was, so a newer one isn't cleared. */
enum {
    APP_APPLIED_NEXT = 1u << 0,
    APP_APPLIED_INDEX = 1u << 1,
    APP_APPLIED_TERMINAL = 1u << 2,
    APP_APPLIED_OUTPUTS = 1u << 3,
};
struct app_applied {
    uint32_t flags;
    int32_t index;
    uint32_t terminal_gen;
    uint32_t outputs_gen;
};
_Static_assert(sizeof(struct app_applied) <= RT_MSG_PAYLOAD, "app_applied fits a message");

/* present's own bookkeeping. */
struct app_view {
    uint64_t stats_since_ms;
    uint64_t stats_frames;
    bool watch_builds;      /* report when staged reload builds settle */
};

/* The host side's: the compositor fd, the watch fd, the socket. */
struct app_host {
    int compositor_fd;
    uint32_t compositor_watch;
    bool flush_blocked;
    bool read_prepared;
    uint32_t watch_handle;
    nw_ipc_server *ipc;             /* NULL for a preview, or if binding failed */
};

/* Everything the program holds. `state` is the world update doesn't own:
 * outputs, GL, config. */
struct app {
    struct neowall_state *state;
    rt_loop *loop;
    struct app_model model;
    struct app_view view;
    struct app_host host;
};

/* Effects update asks for (rt_cmd.kind). perform.c runs them. */
enum {
    APP_CMD_NONE = 0,
    APP_CMD_NOTIFY,             /* v.s: an sd_notify line, static storage */
    APP_CMD_STOP,               /* tell other threads we're stopping */
    APP_CMD_RELOAD,             /* apply the config file -> NW_MSG_RELOAD_APPLIED */
    APP_CMD_REPLY,              /* answer socket conn `id`; v.i = ok */
    APP_CMD_WATCH_BUILDS,       /* start reporting when staged builds settle */
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
/* Send SET_TERMINAL carrying a copy of `cmd` (update takes ownership). */
bool app_send_terminal(rt_loop *loop, const char *cmd);

/* update.c */
void app_model_init(struct app_model *m);
/* The pure part: fold `msg` into `m`, push effects into `cmds`. */
void app_model_update(struct app_model *m, const rt_msg *msg, rt_cmds *cmds);
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


#endif /* NEOWALL_APP_INTERNAL_H */
