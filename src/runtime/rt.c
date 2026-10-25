/* rt: the runtime loop. See include/neowall/runtime/rt.h. */

/* ppoll: nanosecond waits. POSIX.1-2024, but glibc still hides it behind
 * _GNU_SOURCE. Defined here only, before any header. */
#define _GNU_SOURCE

#include "neowall/runtime/rt.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>

#include "neowall/neowall.h"   /* log_* */

/* ── time ─────────────────────────────────────────────────────────────── */

int64_t rt_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ── storage ──────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t handle;            /* 0 = free slot */
    int fd;
    unsigned interest;
    rt_fd_fn fn;
    void *ctx;
} rt_watcher;

typedef struct {
    uint32_t kind, id;
    int64_t period_ns;          /* > 0 repeating; 0 one-shot */
    int64_t due_ns;
    bool fired;                 /* one-shot already delivered */
    bool seen;                  /* returned by the latest subscribe() */
} rt_timer;

struct rt_subs {
    rt_loop *loop;
};

/* Mailbox: a growable ring under a mutex, plus an eventfd to wake the wait.
 * Unbounded on purpose: a message from a worker or a signal must never be
 * dropped because the loop was busy for a moment. */
typedef struct {
    pthread_mutex_t mu;
    rt_msg *buf;
    size_t cap, head, len;
    bool closed;
    int efd;
} rt_mailbox;

#define RT_SIGS_MAX 16

struct rt_loop {
    rt_watcher *watch;
    size_t nwatch, capwatch;
    uint32_t next_handle;

    rt_timer *timers;
    size_t ntimers, captimers;

    rt_msg *local;              /* rt_send queue, folded this step */
    size_t nlocal, caplocal;

    rt_mailbox mb;

    int sfd;                    /* signalfd, -1 until first rt_on_signal */
    sigset_t sigmask;
    struct { int signo; uint32_t kind; } sigs[RT_SIGS_MAX];
    int nsigs;

    rt_prepare_fn prepare;
    rt_after_wait_fn after_wait;
    void *host_ctx;

    bool quitting;
    int exit_code;
    uint64_t steps;
};

/* Grow a dynamic array by doubling. Returns false on OOM. */
static bool grow(void **buf, size_t *cap, size_t need, size_t elem) {
    if (need <= *cap) return true;
    size_t n = *cap ? *cap : 8;
    while (n < need) n *= 2;
    void *p = realloc(*buf, n * elem);
    if (!p) return false;
    *buf = p;
    *cap = n;
    return true;
}

/* ── lifecycle ────────────────────────────────────────────────────────── */

rt_loop *rt_loop_new(void) {
    rt_loop *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->next_handle = 1;
    l->sfd = -1;
    sigemptyset(&l->sigmask);
    pthread_mutex_init(&l->mb.mu, NULL);
    l->mb.efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (l->mb.efd < 0) {
        log_error("rt: eventfd: %s", strerror(errno));
        pthread_mutex_destroy(&l->mb.mu);
        free(l);
        return NULL;
    }
    return l;
}

void rt_loop_free(rt_loop *l) {
    if (!l) return;
    pthread_mutex_lock(&l->mb.mu);
    l->mb.closed = true;
    free(l->mb.buf);
    l->mb.buf = NULL;
    pthread_mutex_unlock(&l->mb.mu);
    if (l->mb.efd >= 0) close(l->mb.efd);
    if (l->sfd >= 0) close(l->sfd);
    pthread_mutex_destroy(&l->mb.mu);
    free(l->watch);
    free(l->timers);
    free(l->local);
    free(l);
}

void rt_quit(rt_loop *l, int code) {
    l->quitting = true;
    l->exit_code = code;
}

uint64_t rt_steps(const rt_loop *l) { return l->steps; }

/* ── mailbox ──────────────────────────────────────────────────────────── */

bool rt_post(rt_loop *l, const rt_msg *msg) {
    rt_mailbox *mb = &l->mb;
    pthread_mutex_lock(&mb->mu);
    if (mb->closed) {
        pthread_mutex_unlock(&mb->mu);
        return false;
    }
    if (mb->len == mb->cap) {
        /* Unroll the ring into a bigger linear buffer. */
        size_t ncap = mb->cap ? mb->cap * 2 : 16;
        rt_msg *nb = malloc(ncap * sizeof(*nb));
        if (!nb) {
            pthread_mutex_unlock(&mb->mu);
            return false;
        }
        for (size_t i = 0; i < mb->len; i++) nb[i] = mb->buf[(mb->head + i) % mb->cap];
        free(mb->buf);
        mb->buf = nb;
        mb->cap = ncap;
        mb->head = 0;
    }
    mb->buf[(mb->head + mb->len) % mb->cap] = *msg;
    mb->len++;
    /* Wake under the lock, so the loop can't drain, find the queue empty,
     * and sleep past a message that was just added. */
    uint64_t one = 1;
    ssize_t w = write(mb->efd, &one, sizeof(one));
    (void)w;   /* EAGAIN: the counter is already non-zero, a wake is pending */
    pthread_mutex_unlock(&mb->mu);
    return true;
}

/* Take everything posted so far. Messages posted while we fold arrive next
 * step, so a busy producer can't starve the wait. */
static size_t mailbox_take(rt_loop *l, rt_msg **out, size_t *outcap) {
    rt_mailbox *mb = &l->mb;
    uint64_t drain;
    ssize_t r = read(mb->efd, &drain, sizeof(drain));
    (void)r;
    pthread_mutex_lock(&mb->mu);
    size_t n = mb->len;
    if (n && grow((void **)out, outcap, n, sizeof(rt_msg))) {
        for (size_t i = 0; i < n; i++) (*out)[i] = mb->buf[(mb->head + i) % mb->cap];
        mb->head = 0;
        mb->len = 0;
    } else {
        n = 0;
    }
    pthread_mutex_unlock(&mb->mu);
    return n;
}

void rt_send(rt_loop *l, const rt_msg *msg) {
    if (!grow((void **)&l->local, &l->caplocal, l->nlocal + 1, sizeof(rt_msg))) {
        log_error("rt: out of memory queueing a message (kind %u)", msg->kind);
        return;
    }
    l->local[l->nlocal++] = *msg;
}

/* ── fds ──────────────────────────────────────────────────────────────── */

uint32_t rt_watch(rt_loop *l, int fd, unsigned interest, rt_fd_fn fn, void *ctx) {
    if (fd < 0 || !fn) return 0;
    for (size_t i = 0; i < l->nwatch; i++) {
        if (l->watch[i].handle && l->watch[i].fd == fd) {
            log_error("rt: fd %d is already watched", fd);
            return 0;
        }
    }
    size_t slot = l->nwatch;
    for (size_t i = 0; i < l->nwatch; i++) {
        if (!l->watch[i].handle) { slot = i; break; }
    }
    if (slot == l->nwatch) {
        if (!grow((void **)&l->watch, &l->capwatch, l->nwatch + 1, sizeof(rt_watcher))) return 0;
        l->nwatch++;
    }
    l->watch[slot] = (rt_watcher){l->next_handle++, fd, interest, fn, ctx};
    return l->watch[slot].handle;
}

static rt_watcher *find_watch(rt_loop *l, uint32_t handle) {
    if (!handle) return NULL;
    for (size_t i = 0; i < l->nwatch; i++)
        if (l->watch[i].handle == handle) return &l->watch[i];
    return NULL;
}

bool rt_watch_modify(rt_loop *l, uint32_t handle, unsigned interest) {
    rt_watcher *w = find_watch(l, handle);
    if (!w) return false;
    w->interest = interest;
    return true;
}

void rt_unwatch(rt_loop *l, uint32_t handle) {
    rt_watcher *w = find_watch(l, handle);
    if (w) memset(w, 0, sizeof(*w));
}

void rt_set_host_hooks(rt_loop *l, rt_prepare_fn prepare, rt_after_wait_fn after, void *ctx) {
    l->prepare = prepare;
    l->after_wait = after;
    l->host_ctx = ctx;
}

/* ── signals ──────────────────────────────────────────────────────────── */

bool rt_block_signals(const int *signos) {
    sigset_t set;
    sigemptyset(&set);
    for (const int *s = signos; *s; s++) sigaddset(&set, *s);
    return pthread_sigmask(SIG_BLOCK, &set, NULL) == 0;
}

bool rt_on_signal(rt_loop *l, int signo, uint32_t kind) {
    if (l->nsigs >= RT_SIGS_MAX) return false;
    sigaddset(&l->sigmask, signo);
    int fd = signalfd(l->sfd, &l->sigmask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0) {
        log_error("rt: signalfd: %s", strerror(errno));
        return false;
    }
    l->sfd = fd;
    l->sigs[l->nsigs].signo = signo;
    l->sigs[l->nsigs].kind = kind;
    l->nsigs++;
    return true;
}

static void signals_take(rt_loop *l) {
    struct signalfd_siginfo si;
    while (read(l->sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
        for (int i = 0; i < l->nsigs; i++) {
            if ((int)si.ssi_signo == l->sigs[i].signo) {
                rt_msg m = {.kind = l->sigs[i].kind, .id = si.ssi_signo};
                m.v.i = si.ssi_int;   /* sigqueue() payload, 0 for kill() */
                rt_send(l, &m);
                break;
            }
        }
    }
}

/* ── subscriptions ────────────────────────────────────────────────────── */

static rt_timer *find_timer(rt_loop *l, uint32_t kind, uint32_t id) {
    for (size_t i = 0; i < l->ntimers; i++)
        if (l->timers[i].kind == kind && l->timers[i].id == id) return &l->timers[i];
    return NULL;
}

static rt_timer *upsert_timer(rt_loop *l, uint32_t kind, uint32_t id, bool *created) {
    rt_timer *t = find_timer(l, kind, id);
    *created = false;
    if (t) return t;
    if (!grow((void **)&l->timers, &l->captimers, l->ntimers + 1, sizeof(rt_timer))) {
        log_error("rt: out of memory adding a timer (kind %u id %u)", kind, id);
        return NULL;
    }
    t = &l->timers[l->ntimers++];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->id = id;
    *created = true;
    return t;
}

void rt_sub_every(rt_subs *s, uint32_t kind, uint32_t id, int64_t period_ns) {
    if (period_ns <= 0) period_ns = 1;
    bool created;
    rt_timer *t = upsert_timer(s->loop, kind, id, &created);
    if (!t) return;
    t->seen = true;
    if (created || t->period_ns != period_ns) {
        /* New, or the period changed: restart the phase from now. */
        t->period_ns = period_ns;
        t->due_ns = rt_now_ns() + period_ns;
        t->fired = false;
    }
}

void rt_sub_at(rt_subs *s, uint32_t kind, uint32_t id, int64_t at_ns) {
    bool created;
    rt_timer *t = upsert_timer(s->loop, kind, id, &created);
    if (!t) return;
    t->seen = true;
    if (created || t->period_ns != 0 || t->due_ns != at_ns) {
        t->period_ns = 0;
        t->due_ns = at_ns;
        t->fired = false;
    }
}

/* Re-run subscribe() and drop every timer it didn't mention. */
static void reconcile(rt_loop *l, const rt_program *p) {
    if (!p->subscribe) return;
    for (size_t i = 0; i < l->ntimers; i++) l->timers[i].seen = false;
    rt_subs s = {l};
    p->subscribe(p->self, &s);
    size_t w = 0;
    for (size_t i = 0; i < l->ntimers; i++)
        if (l->timers[i].seen) l->timers[w++] = l->timers[i];
    l->ntimers = w;
}

/* Fire every due timer as a local message. A repeating timer that fell far
 * behind (a stall, a suspend) fires ONCE and re-phases from now: a burst of
 * catch-up frames is never what a wallpaper wants. */
static void timers_fire(rt_loop *l, int64_t now) {
    for (size_t i = 0; i < l->ntimers; i++) {
        rt_timer *t = &l->timers[i];
        if (t->fired || t->due_ns > now) continue;
        rt_msg m = {.kind = t->kind, .id = t->id};
        m.v.i = t->due_ns;
        rt_send(l, &m);
        if (t->period_ns > 0) {
            t->due_ns += t->period_ns;
            if (t->due_ns <= now) t->due_ns = now + t->period_ns;
        } else {
            t->fired = true;
        }
    }
}

static int64_t next_deadline(const rt_loop *l) {
    int64_t best = -1;
    for (size_t i = 0; i < l->ntimers; i++) {
        const rt_timer *t = &l->timers[i];
        if (t->fired) continue;
        if (best < 0 || t->due_ns < best) best = t->due_ns;
    }
    return best;
}

/* ── the loop ─────────────────────────────────────────────────────────── */

/* Fold the local queue. update() may rt_send() more, folded in this same
 * step; the cap bounds a program that keeps sending itself messages. */
static bool fold(rt_loop *l, const rt_program *p, size_t *folded) {
    size_t i = 0;
    const size_t cap = 4096;
    while (i < l->nlocal && !l->quitting) {
        rt_msg m = l->local[i++];
        if (!p->update(p->self, l, &m)) {
            l->quitting = true;
            break;
        }
        if (++(*folded) >= cap) break;
    }
    /* Keep anything left over (cap reached) for the next step. */
    memmove(l->local, l->local + i, (l->nlocal - i) * sizeof(rt_msg));
    l->nlocal -= i;
    return !l->quitting;
}

int rt_run(rt_loop *l, const rt_program *p) {
    struct pollfd *pfd = NULL;
    uint32_t *pfd_handle = NULL;
    size_t cappfd = 0, cappfh = 0;
    rt_msg *inbox = NULL;
    size_t capinbox = 0;

    reconcile(l, p);
    if (p->present) p->present(p->self, l);   /* first frame */

    while (!l->quitting) {
        /* 1. Let the host flush and report work it already holds. */
        bool host_pending = l->prepare ? l->prepare(l->host_ctx, l) : false;

        /* 2. Build the wait set: the mailbox, the signalfd, the watchers. */
        size_t n = 0, need = l->nwatch + 2;
        if (!grow((void **)&pfd, &cappfd, need, sizeof(*pfd)) ||
            !grow((void **)&pfd_handle, &cappfh, need, sizeof(*pfd_handle))) {
            log_error("rt: out of memory building the wait set");
            l->exit_code = 70;
            break;
        }
        pfd[n] = (struct pollfd){.fd = l->mb.efd, .events = POLLIN};
        pfd_handle[n++] = 0;
        if (l->sfd >= 0) {
            pfd[n] = (struct pollfd){.fd = l->sfd, .events = POLLIN};
            pfd_handle[n++] = 0;
        }
        const size_t first_watch = n;
        for (size_t i = 0; i < l->nwatch; i++) {
            const rt_watcher *w = &l->watch[i];
            if (!w->handle) continue;
            /* Interest 0 = parked: poll() would still report POLLHUP for it, and
             * a parked fd that hung up would wake every step. */
            if (!(w->interest & (RT_READ | RT_WRITE))) continue;
            short ev = 0;
            if (w->interest & RT_READ) ev |= POLLIN;
            if (w->interest & RT_WRITE) ev |= POLLOUT;
            pfd[n] = (struct pollfd){.fd = w->fd, .events = ev};
            pfd_handle[n++] = w->handle;
        }

        /* 3. Wait until the next deadline, exactly. A deadline in the future
         * never becomes 0 (that spins); one in the past doesn't wait. */
        int64_t now = rt_now_ns();
        int64_t until = next_deadline(l);
        if (p->wait_hint_ns) {
            const int64_t h = p->wait_hint_ns(p->self);
            if (h >= 0 && (until < 0 || now + h < until)) until = now + h;
        }
        struct timespec ts, *tp = NULL;
        if (host_pending || l->nlocal) {
            ts = (struct timespec){0, 0};
            tp = &ts;
        } else if (until >= 0) {
            int64_t left = until - now;
            if (left < 0) left = 0;
            ts.tv_sec = (time_t)(left / 1000000000LL);
            ts.tv_nsec = (long)(left % 1000000000LL);
            tp = &ts;
        }
        int r = ppoll(pfd, (nfds_t)n, tp, NULL);
        if (r < 0 && errno != EINTR) {
            log_error("rt: ppoll: %s", strerror(errno));
            l->exit_code = 70;
            if (l->after_wait) l->after_wait(l->host_ctx, l, false);
            break;
        }
        l->steps++;

        bool fd_ready = false;
        if (r > 0) {
            for (size_t i = first_watch; i < n; i++)
                if (pfd[i].revents) { fd_ready = true; break; }
        }

        /* 4. Turn what happened into messages, in a fixed order: signals,
         * fds (the host emits), then the host's after-wait (its dispatch, which
         * must see what the fd callbacks just read), the mailbox, timers. */
        if (r > 0 && l->sfd >= 0 && (pfd[1].revents & POLLIN)) signals_take(l);
        if (r > 0) {
            for (size_t i = first_watch; i < n; i++) {
                if (!pfd[i].revents) continue;
                rt_watcher *w = find_watch(l, pfd_handle[i]);   /* may be gone */
                if (!w) continue;
                unsigned ev = 0;
                if (pfd[i].revents & POLLIN) ev |= RT_READ;
                if (pfd[i].revents & POLLOUT) ev |= RT_WRITE;
                if (pfd[i].revents & (POLLHUP | POLLERR | POLLNVAL)) ev |= RT_HANGUP;
                w->fn(w->ctx, l, w->fd, ev);
                if (l->quitting) break;
            }
        }
        if (l->after_wait) l->after_wait(l->host_ctx, l, fd_ready);
        size_t got = mailbox_take(l, &inbox, &capinbox);
        for (size_t i = 0; i < got; i++) rt_send(l, &inbox[i]);
        timers_fire(l, rt_now_ns());

        /* 5. Fold, re-subscribe, draw. */
        size_t folded = 0;
        if (l->nlocal && !l->quitting) fold(l, p, &folded);
        if (l->quitting) break;
        if (folded) {
            reconcile(l, p);
            if (p->present) p->present(p->self, l);
        }
    }

    /* Close the mailbox: a worker posting after this gets false. */
    pthread_mutex_lock(&l->mb.mu);
    l->mb.closed = true;
    pthread_mutex_unlock(&l->mb.mu);

    free(pfd);
    free(pfd_handle);
    free(inbox);
    return l->exit_code;
}
