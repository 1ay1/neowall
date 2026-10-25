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
 *   update(prog, msg)     fold one message; the only place state changes
 *   subscribe(prog, sub)  declare which timers should exist right now
 *   present(prog)         draw, after a step that changed something
 *
 * Messages are opaque to rt: a small fixed-size POD (rt_msg) whose `kind`
 * and payload belong to the program.
 *
 * Threading: everything runs on the loop thread except rt_post(), which any
 * thread may call. It is also async-signal-safe EXCEPT that it may take a
 * mutex; signals are delivered through a signalfd, not a handler, so that
 * never matters in practice. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── messages ─────────────────────────────────────────────────────────── */

/* Big enough for every neowall message (a path is carried by reference to
 * the program's own storage, never inline). */
#define RT_MSG_PAYLOAD 48

typedef struct rt_msg {
    uint32_t kind;                      /* program-defined; 0 is reserved */
    uint32_t id;                        /* program-defined target (an output) */
    union {
        int64_t  i;
        uint64_t u;
        double   d;
        void    *p;
        unsigned char bytes[RT_MSG_PAYLOAD];
    } v;
} rt_msg;

/* ── the loop ─────────────────────────────────────────────────────────── */

typedef struct rt_loop rt_loop;

/* A subscription set under construction. Passed to the program's subscribe(). */
typedef struct rt_subs rt_subs;

typedef struct rt_program {
    void *self;
    /* Fold one message. Return false to ask the loop to stop. */
    bool (*update)(void *self, rt_loop *loop, const rt_msg *msg);
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

/* Run until update returns false, rt_quit() is called, or a fatal error.
 * Returns the exit code given to rt_quit (0 by default). */
int rt_run(rt_loop *loop, const rt_program *prog);

/* Ask the loop to stop after the current step. Loop thread only. */
void rt_quit(rt_loop *loop, int code);

/* Post a message from ANY thread. Folded in order on the loop thread.
 * Returns false only if the mailbox is closed (the loop has stopped). */
bool rt_post(rt_loop *loop, const rt_msg *msg);

/* Post from the loop thread itself: folded next, in this same step. */
void rt_send(rt_loop *loop, const rt_msg *msg);

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
void rt_set_host_hooks(rt_loop *loop, rt_prepare_fn prepare,
                       rt_after_wait_fn after, void *ctx);

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
