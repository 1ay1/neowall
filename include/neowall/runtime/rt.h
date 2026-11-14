#ifndef NEOWALL_RUNTIME_RT_H
#define NEOWALL_RUNTIME_RT_H

/* rt: a small Elm-style runtime loop (see docs/architecture/RUNTIME.md).
 *
 * The loop owns the thread and waits on three kinds of thing:
 *   - fds a host watches (the Wayland/X11 socket, an inotify fd)
 *   - timers, which only exist as SUBSCRIPTIONS keyed by (kind, id)
 *   - a mailbox that other threads and signal delivery post messages into
 *
 * It knows nothing about wallpapers. A program supplies:
 *   update(prog, msg, cmds)  fold one message into the model and say which
 *                         effects to run. It gets no rt_loop: it can't post,
 *                         quit or touch the world, only push commands.
 *   perform(prog, cmd)    run one command (file I/O, a reload, a syscall).
 *                         Results come back as messages via rt_send.
 *   subscribe(prog, sub)  declare which timers should exist right now
 *   present(prog)         draw, after a step that changed something
 *
 * Messages and commands are opaque to rt: small fixed-size PODs whose `kind`
 * and payload belong to the program. A command runs right after the update
 * that pushed it, before the next message, so the order is the order the
 * program wrote. Two commands are rt's own: rt_cmd_send (fold another
 * message) and rt_cmd_quit.
 *
 * Threading: everything runs on the loop thread except rt_post(), which any
 * thread may call while the loop exists. After rt_run returns, rt_post
 * returns false. rt_loop_free must not race a post: the caller makes sure
 * no thread is inside rt_post (app.c counts posters in flight for that).
 * rt_post takes a mutex, so it isn't async-signal-safe; signals come in
 * through a signalfd instead (rt_on_signal). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── messages ─────────────────────────────────────────────────────────── */

/* Big enough for every neowall message or command (a path is carried by
 * reference to the program's own storage, never inline). */
#define RT_MSG_PAYLOAD 48

typedef union rt_payload {
    int64_t i;
    uint64_t u;
    double d;
    void *p;
    const char *s; /* static storage only */
    unsigned char bytes[RT_MSG_PAYLOAD];
} rt_payload;

typedef struct rt_msg {
    uint32_t kind; /* program-defined; 0 is reserved */
    uint32_t id;   /* program-defined target (an output) */
    rt_payload v;
} rt_msg;

/* An effect update asks for. Same shape as a message, separate type. */
typedef struct rt_cmd {
    uint32_t kind; /* program-defined; 0 is reserved */
    uint32_t id;
    rt_payload v;
} rt_cmd;

/* ── the loop ─────────────────────────────────────────────────────────── */

typedef struct rt_loop rt_loop;

/* A subscription set under construction. Passed to the program's subscribe(). */
typedef struct rt_subs rt_subs;

/* The commands one update call pushes. Passed to the program's update(). */
typedef struct rt_cmds rt_cmds;

typedef struct rt_program {
    void *self;
    /* Fold one message into the model. Effects go in `cmds`. */
    void (*update)(void *self, const rt_msg *msg, rt_cmds *cmds);
    /* Run one program command. Loop thread; may rt_send results. Optional
     * if update never pushes a program command. */
    void (*perform)(void *self, rt_loop *loop, const rt_cmd *cmd);
    /* Declare the timers that should exist now (rt_sub_every / rt_sub_at).
     * Called after every step that ran update. Optional. */
    void (*subscribe)(void *self, rt_subs *subs);
    /* Draw. Called after a step that ran update. Optional. */
    void (*present)(void *self, rt_loop *loop);
    /* Upper bound on the next wait, in ns, or -1 for none: work the program
     * owes that no fd or timer announces (a frame callback that may never
     * come). Optional. */
    int64_t (*wait_hint_ns)(void *self);
} rt_program;

rt_loop *rt_loop_new(void);
void rt_loop_free(rt_loop *loop);

/* Run until a quit command, rt_quit(), or a fatal error. Returns the exit
 * code given to the quit (0 by default). */
int rt_run(rt_loop *loop, const rt_program *prog);

/* Ask the loop to stop after the current step. Loop thread only, from
 * host code or present; update uses rt_cmd_quit. */
void rt_quit(rt_loop *loop, int code);

/* Post a message from ANY thread. Folded in order on the loop thread.
 * Returns false only if the mailbox is closed (the loop has stopped). */
bool rt_post(rt_loop *loop, const rt_msg *msg);

/* Post from the loop thread itself (host code, perform): folded in this
 * same step. */
void rt_send(rt_loop *loop, const rt_msg *msg);

/* ── commands (inside update) ─────────────────────────────────────────── */

/* A program command, handed to perform() right after this update. */
void rt_cmd_push(rt_cmds *cmds, const rt_cmd *cmd);
/* Fold `msg` after this one, in this same step. */
void rt_cmd_send(rt_cmds *cmds, const rt_msg *msg);
/* Stop the loop after this step with `code`. Later commands still run. */
void rt_cmd_quit(rt_cmds *cmds, int code);

/* What update asked for, one entry per push. */
typedef enum { RT_EFFECT_CMD, RT_EFFECT_SEND, RT_EFFECT_QUIT } rt_effect_tag;
typedef struct rt_effect {
    rt_effect_tag tag;
    union {
        rt_cmd cmd; /* RT_EFFECT_CMD */
        rt_msg msg; /* RT_EFFECT_SEND */
        int code;   /* RT_EFFECT_QUIT */
    } u;
} rt_effect;

/* Run update by hand, with no loop, and look at what it asked for. This is
 * how a program's update is tested: message in, model + effects out. */
rt_cmds *rt_cmds_new(void);
void rt_cmds_free(rt_cmds *cmds);
void rt_cmds_clear(rt_cmds *cmds);
size_t rt_cmds_len(const rt_cmds *cmds);
const rt_effect *rt_cmds_at(const rt_cmds *cmds, size_t i); /* NULL past the end */

/* ── fds ──────────────────────────────────────────────────────────────── */

enum { RT_READ = 1, RT_WRITE = 2 };

/* Called on the loop thread when `fd` is ready. `events` is RT_READ |
 * RT_WRITE, plus RT_HANGUP when the peer is gone. */
enum { RT_HANGUP = 4 };
typedef void (*rt_fd_fn)(void *ctx, rt_loop *loop, int fd, unsigned events);

/* Watch an fd. Returns a handle > 0, or 0 on failure. Level-triggered:
 * the callback runs every step while the fd stays ready. Interest 0 (via
 * rt_watch_modify) parks it: not polled at all, so not even a hangup wakes. */
uint32_t rt_watch(rt_loop *loop, int fd, unsigned interest, rt_fd_fn fn, void *ctx);
bool rt_watch_modify(rt_loop *loop, uint32_t handle, unsigned interest);
void rt_unwatch(rt_loop *loop, uint32_t handle);

/* Called once per step before waiting: a host can flush output buffers and
 * report work it already holds (Xlib's queue) so the loop doesn't sleep on
 * it. Return true when there is pending work: the wait then doesn't block. */
typedef bool (*rt_prepare_fn)(void *ctx, rt_loop *loop);
/* Called once per step after waiting, whatever woke it, and AFTER the fd
 * callbacks ran. For a host that must balance prepare
 * (wl_display_prepare_read / cancel_read) and then dispatch what was read. */
typedef void (*rt_after_wait_fn)(void *ctx, rt_loop *loop, bool fd_was_ready);
void rt_set_host_hooks(rt_loop *loop, rt_prepare_fn prepare, rt_after_wait_fn after, void *ctx);

/* ── signals ──────────────────────────────────────────────────────────── */

/* Deliver `signo` as a message of `kind` (id = signo). Block the signal in
 * every thread first (rt_block_signals) so it reaches the signalfd. */
bool rt_on_signal(rt_loop *loop, int signo, uint32_t kind);

/* Block `signos` (a 0-terminated list) in the calling thread. Call before
 * any thread is created, so every thread inherits the mask. */
bool rt_block_signals(const int *signos);

/* ── subscriptions (timers) ───────────────────────────────────────────── */

/* A timer key: the program's (kind, id). Keyed timers are RECONCILED: a key
 * present before and after a step keeps running with its phase; a key that
 * disappears is cancelled; a new key starts. Messages from a cancelled timer
 * are never delivered. */

/* Every `period_ns`, deliver {kind, id}. The first fire is one period from
 * when the key first appeared. */
void rt_sub_every(rt_subs *subs, uint32_t kind, uint32_t id, int64_t period_ns);

/* Once, at absolute CLOCK_MONOTONIC time `at_ns`. Changing `at_ns` for an
 * existing key re-arms it. A one-shot that fired is not re-armed by being
 * returned again with the same `at_ns`. */
void rt_sub_at(rt_subs *subs, uint32_t kind, uint32_t id, int64_t at_ns);

/* ── time ─────────────────────────────────────────────────────────────── */

/* CLOCK_MONOTONIC, ns. The clock every rt deadline is on. */
int64_t rt_now_ns(void);

/* The number of steps folded so far (a step = one wait + its messages). */
uint64_t rt_steps(const rt_loop *loop);

#ifdef __cplusplus
}
#endif

#endif /* NEOWALL_RUNTIME_RT_H */
