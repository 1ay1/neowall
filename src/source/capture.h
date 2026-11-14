/* A helper process feeding a reader thread: spawn `argv`, read its stdout on
 * a thread until stopped or EOF, then reap it. Used for parec (audio) and
 * nvidia-smi. No globals.
 *
 * Each start makes a *run*: the wake pipe, the child pid and the exit flag
 * belong to it, shared by the starter and the reader through a refcount.
 * Stopping is bounded and safe:
 *   - the wake pipe breaks the reader out of poll() at once;
 *   - the child is killed under a lock the reader also takes before it
 *     reaps, so a reaped (maybe reused) pid is never signalled;
 *   - a reader that hasn't finished after stop_ms is detached and keeps its
 *     run (pipe open, so it can't poll a recycled fd); a later start gets a
 *     fresh run and the two never share state. */
#ifndef NEOWALL_SOURCE_CAPTURE_H
#define NEOWALL_SOURCE_CAPTURE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct capture capture;
typedef struct capture_run capture_run;

/* Called on the reader thread with the child's stdout. Loop on
 * capture_poll(run, fd); return when it says stop or fd hits EOF. */
typedef void (*capture_read_fn)(capture_run *run, int fd, void *ctx);

struct capture {
    const char *name;            /* for the log */
    char *const *argv;           /* argv[0] is looked up on PATH */
    capture_read_fn read;
    void *ctx;
    capture_run *run;            /* the current run, NULL when stopped */
    atomic_bool live;            /* the current child is producing */
};

/* Spawn and start reading. A no-op if already running. False only if the
 * thread couldn't start (a missing program is logged by the reader). */
bool capture_start(capture *c);
/* Stop, kill the child and reap, waiting at most stop_ms. False if the
 * reader had to be abandoned. Safe when not running. */
bool capture_stop(capture *c, long stop_ms);
static inline bool capture_running(const capture *c) { return c->run != NULL; }
static inline bool capture_live(capture *c) { return atomic_load(&c->live); }

/* For the read callback: wait for data on fd or a stop. True = fd ready. */
bool capture_poll(capture_run *run, int fd);
/* The callback reports when data actually flows. */
void capture_set_live(capture_run *run, bool live);

#endif /* NEOWALL_SOURCE_CAPTURE_H */
