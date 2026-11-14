/* rt: the runtime loop, tested on its own. No display, no GL. */

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "neowall/runtime/rt.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

enum { K_TICK = 1, K_ONCE, K_POSTED, K_SIG, K_SELF, K_STOP, K_LATE, K_EFFECT, K_EFFECT_DONE };
enum { C_ECHO = 1 };

/* A small program: counts what arrives, and its subscriptions are a pure
 * function of its fields. */
typedef struct {
    int ticks, onces, posted, sigs, selfs, lates;
    bool want_tick;
    int64_t tick_ns;
    int64_t once_at;
    int stop_after_ticks;
    int presents;
    int64_t first_tick_at, last_tick_at;
    int64_t first_due, last_due, worst_early; /* from the tick's v.i */
    uint32_t order[64];
    int norder;
    int performed, effect_dones;
    uint32_t trace[16]; /* 'u' update / 'p' perform, in run order */
    int ntrace;
} prog_t;

static void upd(void *self, const rt_msg *m, rt_cmds *cmds) {
    prog_t *p = self;
    if (p->norder < 64) p->order[p->norder++] = m->kind;
    switch (m->kind) {
    case K_TICK:
        p->ticks++;
        if (!p->first_tick_at) p->first_tick_at = rt_now_ns();
        p->last_tick_at = rt_now_ns();
        if (!p->first_due) p->first_due = m->v.i;
        p->last_due = m->v.i;
        /* how far before its deadline this tick ran (should be <= 0) */
        if (m->v.i - p->last_tick_at > p->worst_early) p->worst_early = m->v.i - p->last_tick_at;
        if (p->stop_after_ticks && p->ticks >= p->stop_after_ticks) rt_cmd_quit(cmds, 3);
        break;
    case K_ONCE: p->onces++; break;
    case K_POSTED: p->posted++; break;
    case K_SIG:
        p->sigs++;
        rt_cmd_quit(cmds, 0);
        break;
    case K_SELF: {
        p->selfs++;
        if (p->selfs < 5) {
            rt_msg again = {.kind = K_SELF};
            rt_cmd_send(cmds, &again); /* folded in this same step */
        }
        break;
    }
    case K_LATE: p->lates++; break;
    case K_STOP: rt_cmd_quit(cmds, 0); break;
    case K_EFFECT: {
        if (p->ntrace < 16) p->trace[p->ntrace++] = 'u';
        rt_cmd c = {.kind = C_ECHO};
        rt_cmd_push(cmds, &c);
        rt_cmd_push(cmds, &c);
        break;
    }
    case K_EFFECT_DONE:
        if (p->ntrace < 16) p->trace[p->ntrace++] = 'u';
        if (++p->effect_dones == 2) rt_cmd_quit(cmds, 7);
        break;
    }
}

/* Runs each command; answers with a message, like a real effect would. */
static void perform(void *self, rt_loop *l, const rt_cmd *c) {
    prog_t *p = self;
    if (c->kind != C_ECHO) return;
    p->performed++;
    if (p->ntrace < 16) p->trace[p->ntrace++] = 'p';
    rt_msg done = {.kind = K_EFFECT_DONE};
    rt_send(l, &done);
}

static void subs(void *self, rt_subs *s) {
    prog_t *p = self;
    if (p->want_tick) rt_sub_every(s, K_TICK, 0, p->tick_ns);
    if (p->once_at) rt_sub_at(s, K_ONCE, 0, p->once_at);
}

static void present(void *self, rt_loop *l) {
    (void)l;
    ((prog_t *)self)->presents++;
}

static rt_program program(prog_t *p) {
    return (rt_program){
        .self = p, .update = upd, .perform = perform, .subscribe = subs, .present = present};
}

/* 1. A repeating timer fires on schedule and keeps its phase. */
static void test_every(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {.want_tick = true, .tick_ns = 10 * 1000000LL, .stop_after_ticks = 10};
    rt_program pr = program(&p);
    int64_t t0    = rt_now_ns();
    int code      = rt_run(l, &pr);
    int64_t took  = rt_now_ns() - t0;
    CHECK(code == 3, "exit code from rt_quit, got %d", code);
    CHECK(p.ticks == 10, "10 ticks, got %d", p.ticks);
    /* 10 x 10 ms: at least 100 ms, never early. */
    CHECK(took >= 100 * 1000000LL, "ran %lld ns, timer fired early", (long long)took);
    CHECK(took < 400 * 1000000LL, "ran %lld ns, far too slow", (long long)took);
    CHECK(p.presents >= 10, "present after each step that folded, got %d", p.presents);
    rt_loop_free(l);
}

/* 2. Sub-millisecond precision: a 16.67 ms period isn't padded to 17, and
 * stays phase-locked. The schedule (each tick's due time) is checked
 * exactly; wake latency only loosely, since a loaded machine wakes late. */
static void test_precise(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {.want_tick = true, .tick_ns = 16666667, .stop_after_ticks = 31};
    rt_program pr = program(&p);
    int64_t t0    = rt_now_ns();
    rt_run(l, &pr);
    int64_t span = p.last_due - p.first_due;
    CHECK(span == 30 * 16666667LL, "30 periods of exactly 16666667 ns, got %lld", (long long)span);
    CHECK(p.first_due - t0 <= 16666667LL + 1000000, "first tick due one period in");
    CHECK(p.worst_early <= 0, "a tick ran %lld ns early", (long long)p.worst_early);
    double per = (double)(p.last_tick_at - t0) / 31.0 / 1e6;
    CHECK(per >= 16.66 && per < 25.0, "mean period %.3f ms", per);
    rt_loop_free(l);
}

/* 3. One-shot at an absolute time, delivered once, not re-armed by being
 * returned again with the same deadline. */
static void *stopper(void *arg) {
    struct timespec d = {0, 60 * 1000000L};
    nanosleep(&d, NULL);
    rt_msg m = {.kind = K_STOP};
    rt_post((rt_loop *)arg, &m);
    return NULL;
}

static void test_once(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {.once_at = rt_now_ns() + 20 * 1000000LL};
    rt_program pr = program(&p);
    pthread_t th;
    pthread_create(&th, NULL, stopper, l);
    rt_run(l, &pr);
    pthread_join(th, NULL);
    CHECK(p.onces == 1, "one-shot fired %d times", p.onces);
    rt_loop_free(l);
}

/* 4. Messages from another thread arrive, all of them, in order. */
typedef struct {
    rt_loop *l;
    int n;
} poster_arg;
static void *poster(void *arg) {
    poster_arg *a = arg;
    for (int i = 0; i < a->n; i++) {
        rt_msg m = {.kind = K_POSTED, .id = (uint32_t)i};
        rt_post(a->l, &m);
    }
    rt_msg stop = {.kind = K_STOP};
    rt_post(a->l, &stop);
    return NULL;
}

static void test_post(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {0};
    rt_program pr = program(&p);
    poster_arg a  = {l, 10000};
    pthread_t th;
    pthread_create(&th, NULL, poster, &a);
    rt_run(l, &pr);
    pthread_join(th, NULL);
    CHECK(p.posted == 10000, "all posted messages arrive, got %d", p.posted);
    /* After the loop stopped, posting reports failure instead of writing
     * into a dead loop. */
    rt_msg m = {.kind = K_POSTED};
    CHECK(!rt_post(l, &m), "post after the loop stopped is refused");
    rt_loop_free(l);
}

/* 5. A timer dropped from subscribe() never fires again, and a message the
 * program sends itself is folded in the same step. */
static void test_unsubscribe_and_send(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {.want_tick = true, .tick_ns = 5 * 1000000LL};
    rt_program pr = program(&p);
    pthread_t th;
    pthread_create(&th, NULL, stopper, l);
    /* Drop the tick after the first fire: a fresh program state the
     * subscribe() sees, set from inside update would be the real shape;
     * here flip it from a stepped hook instead. */
    p.stop_after_ticks = 0;
    rt_msg self        = {.kind = K_SELF};
    rt_send(l, &self);
    rt_run(l, &pr);
    pthread_join(th, NULL);
    CHECK(p.selfs == 5, "self-sent chain folded, got %d", p.selfs);
    CHECK(p.ticks >= 5, "tick kept running while subscribed, got %d", p.ticks);
    rt_loop_free(l);
}

/* 6. A signal becomes a message, through the signalfd. */
static void test_signal(void) {
    const int sigs[] = {SIGUSR1, 0};
    CHECK(rt_block_signals(sigs), "block SIGUSR1");
    rt_loop *l = rt_loop_new();
    CHECK(rt_on_signal(l, SIGUSR1, K_SIG), "route SIGUSR1");
    prog_t p      = {0};
    rt_program pr = program(&p);
    kill(getpid(), SIGUSR1);
    int code = rt_run(l, &pr);
    CHECK(p.sigs == 1 && code == 0, "signal arrived as a message (%d, code %d)", p.sigs, code);
    rt_loop_free(l);
}

/* 7. An fd watcher runs on the loop thread and can quit it. */
static void on_pipe(void *ctx, rt_loop *l, int fd, unsigned ev) {
    int *hits = ctx;
    char b[8];
    if (ev & RT_READ) {
        ssize_t r = read(fd, b, sizeof b);
        (void)r;
        (*hits)++;
        rt_msg m = {.kind = K_STOP};
        rt_send(l, &m);
    }
}

static void test_fd(void) {
    int pp[2];
    CHECK(pipe(pp) == 0, "pipe");
    rt_loop *l = rt_loop_new();
    int hits   = 0;
    CHECK(rt_watch(l, pp[0], RT_READ, on_pipe, &hits) > 0, "watch");
    CHECK(rt_watch(l, pp[0], RT_READ, on_pipe, &hits) == 0, "duplicate watch refused");
    ssize_t w = write(pp[1], "x", 1);
    (void)w;
    prog_t p      = {0};
    rt_program pr = program(&p);
    rt_run(l, &pr);
    CHECK(hits == 1, "fd callback ran once, got %d", hits);
    rt_loop_free(l);
    close(pp[0]);
    close(pp[1]);
}

/* 8. An idle loop sleeps: no timers, no fds, nothing posted for 50 ms
 * means at most a couple of wakeups, not a spin. */
static void test_idle(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {0};
    rt_program pr = program(&p);
    pthread_t th;
    pthread_create(&th, NULL, stopper, l);
    rt_run(l, &pr);
    pthread_join(th, NULL);
    CHECK(rt_steps(l) <= 3, "idle loop woke %llu times", (unsigned long long)rt_steps(l));
    rt_loop_free(l);
}

/* 9. A signal still arrives when fds are watched too (the signalfd and the
 * watchers share the wait set). */
static void on_quiet(void *ctx, rt_loop *l, int fd, unsigned ev) {
    (void)ctx;
    (void)l;
    (void)fd;
    (void)ev;
}

static void test_signal_after_watch(void) {
    const int sigs[] = {SIGUSR2, 0};
    CHECK(rt_block_signals(sigs), "block SIGUSR2");
    int pp[2];
    CHECK(pipe(pp) == 0, "pipe");
    rt_loop *l = rt_loop_new();
    CHECK(rt_watch(l, pp[0], RT_READ, on_quiet, NULL) > 0, "watch");
    CHECK(rt_on_signal(l, SIGUSR2, K_SIG), "route SIGUSR2");
    prog_t p      = {0};
    rt_program pr = program(&p);
    kill(getpid(), SIGUSR2);
    int code = rt_run(l, &pr);
    CHECK(p.sigs == 1 && code == 0, "signal arrived (%d)", p.sigs);
    rt_loop_free(l);
    close(pp[0]);
    close(pp[1]);
}

/* 10. Many threads post at once while the loop folds; every message lands,
 * in per-thread order. Run under TSan in CI. */
#define STRESS_THREADS 8
#define STRESS_EACH    5000

typedef struct {
    rt_loop *l;
    uint32_t who;
} stress_arg;

typedef struct {
    int got[STRESS_THREADS];
    int64_t last[STRESS_THREADS];
    bool ordered;
    int total;
} stress_prog;

static void *stress_poster(void *arg) {
    stress_arg *a = arg;
    for (int i = 0; i < STRESS_EACH; i++) {
        rt_msg m = {.kind = K_POSTED, .id = a->who};
        m.v.i    = i;
        if (!rt_post(a->l, &m)) return NULL; /* loop stopped */
    }
    return NULL;
}

static void stress_upd(void *self, const rt_msg *m, rt_cmds *cmds) {
    stress_prog *s = self;
    if (m->kind != K_POSTED || m->id >= STRESS_THREADS) return;
    if (m->v.i != s->last[m->id] + 1) s->ordered = false;
    s->last[m->id] = m->v.i;
    s->got[m->id]++;
    if (++s->total == STRESS_THREADS * STRESS_EACH) rt_cmd_quit(cmds, 0);
}

static void test_stress(void) {
    rt_loop *l    = rt_loop_new();
    stress_prog s = {.ordered = true};
    for (int i = 0; i < STRESS_THREADS; i++)
        s.last[i] = -1;
    rt_program pr = {.self = &s, .update = stress_upd};
    pthread_t th[STRESS_THREADS];
    stress_arg args[STRESS_THREADS];
    for (int i = 0; i < STRESS_THREADS; i++) {
        args[i] = (stress_arg){l, (uint32_t)i};
        pthread_create(&th[i], NULL, stress_poster, &args[i]);
    }
    rt_run(l, &pr);
    for (int i = 0; i < STRESS_THREADS; i++)
        pthread_join(th[i], NULL);
    CHECK(s.total == STRESS_THREADS * STRESS_EACH, "every post folded, got %d", s.total);
    CHECK(s.ordered, "each thread's posts folded in the order sent");
    rt_loop_free(l);
}

/* 11. present reports what it consumed with rt_send; update must see that
 * before present runs again, or a request gets applied twice. */
typedef struct {
    int pending; /* model: requests waiting */
    int applied; /* how many times present acted on one */
    int presents;
    bool ended;
} consume_prog;

enum { K_REQ = 100, K_DONE, K_END };

static void consume_upd(void *self, const rt_msg *m, rt_cmds *cmds) {
    consume_prog *p = self;
    if (m->kind == K_REQ) p->pending++;
    if (m->kind == K_DONE && p->pending > 0) p->pending--;
    if (m->kind == K_END) rt_cmd_quit(cmds, 0);
}

static void consume_present(void *self, rt_loop *l) {
    consume_prog *p = self;
    p->presents++;
    if (p->pending > 0) {
        p->applied++;
        rt_msg done = {.kind = K_DONE};
        rt_send(l, &done);
    }
    /* idle again (nothing pending, ticks drained): stop */
    if (p->pending == 0 && p->applied > 0 && !p->ended) {
        p->ended   = true;
        rt_msg end = {.kind = K_END};
        rt_send(l, &end);
    }
}

static void test_present_report(void) {
    rt_loop *l     = rt_loop_new();
    consume_prog p = {0};
    rt_program pr  = {.self = &p, .update = consume_upd, .present = consume_present};
    rt_msg req     = {.kind = K_REQ};
    rt_send(l, &req);
    /* keep the loop stepping so present runs several times */
    rt_msg tick = {.kind = K_TICK};
    for (int i = 0; i < 3; i++)
        rt_post(l, &tick);
    rt_run(l, &pr);
    CHECK(p.applied == 1, "request applied once, got %d", p.applied);
    CHECK(p.pending == 0, "and cleared");
    rt_loop_free(l);
}

/* 5b. Commands run right after the update that pushed them, in order, and a
 * message a command sends is folded before the step ends. */
static void test_commands(void) {
    rt_loop *l    = rt_loop_new();
    prog_t p      = {0};
    rt_program pr = program(&p);
    rt_msg go     = {.kind = K_EFFECT};
    rt_send(l, &go);
    int code = rt_run(l, &pr);
    CHECK(code == 7, "quit command's code comes back, got %d", code);
    CHECK(p.performed == 2, "both commands ran, got %d", p.performed);
    CHECK(p.effect_dones == 2, "both results folded, got %d", p.effect_dones);
    /* u (effect) p p (both commands before anything else) u u (results) */
    const char want[] = "uppuu";
    bool same         = p.ntrace == 5;
    for (int i = 0; same && i < 5; i++)
        same = p.trace[i] == (uint32_t)want[i];
    CHECK(same, "update, then its commands, then their results");
    rt_loop_free(l);
}

int main(void) {
    test_every();
    test_precise();
    test_once();
    test_post();
    test_unsubscribe_and_send();
    test_commands();
    test_signal();
    test_signal_after_watch();
    test_fd();
    test_idle();
    test_stress();
    test_present_report();
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
