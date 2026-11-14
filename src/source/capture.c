/* See capture.h. */

#define _DEFAULT_SOURCE

#include "capture.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "neowall/core/child_signals.h"
#include "neowall/core/log.h"

extern char **environ;

/* One start..stop. The starter and the reader each hold a reference. */
struct capture_run {
    atomic_int refs;
    char *const *argv;
    const char *name;
    capture_read_fn read;
    void *ctx;
    atomic_bool *live;

    pthread_t thread;
    atomic_bool stop;
    int wake[2];                 /* starter writes, reader polls */
    pthread_mutex_t pid_lock;    /* kill vs reap */
    pid_t pid;                   /* under pid_lock; -1 when none */
    pthread_mutex_t exit_lock;
    pthread_cond_t exit_cond;    /* CLOCK_MONOTONIC */
    bool exited;                 /* under exit_lock */
};

static void run_unref(capture_run *r) {
    if (atomic_fetch_sub(&r->refs, 1) != 1) return;
    close(r->wake[0]);
    close(r->wake[1]);
    pthread_mutex_destroy(&r->pid_lock);
    pthread_mutex_destroy(&r->exit_lock);
    pthread_cond_destroy(&r->exit_cond);
    free(r);
}

static capture_run *run_new(capture *c) {
    capture_run *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    if (nw_pipe_cloexec(r->wake) != 0) {
        free(r);
        return NULL;
    }
    fcntl(r->wake[1], F_SETFL, O_NONBLOCK);   /* a wake never blocks */
    atomic_init(&r->refs, 2);                 /* starter + reader */
    atomic_init(&r->stop, false);
    r->argv = c->argv;
    r->name = c->name;
    r->read = c->read;
    r->ctx = c->ctx;
    r->live = &c->live;
    r->pid = -1;
    pthread_mutex_init(&r->pid_lock, NULL);
    pthread_mutex_init(&r->exit_lock, NULL);
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&r->exit_cond, &a);
    pthread_condattr_destroy(&a);
    return r;
}

static struct timespec mono_in(long ms) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return t;
}

/* Spawn with stdout on a pipe; the child gets only 0-2 and default signals. */
static int spawn(capture_run *r, pid_t *out) {
    int p[2];
    if (nw_pipe_cloexec(p) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_addclose(&fa, p[1]);
    nw_spawn_close_fds(&fa, -1);
    posix_spawnattr_t attr;
    bool have_attr = nw_spawnattr_reset_signals(&attr) == 0;
    int rc = posix_spawnp(out, r->argv[0], &fa, have_attr ? &attr : NULL, r->argv, environ);
    if (have_attr) posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    if (rc != 0) {
        close(p[0]);
        return -1;
    }
    return p[0];
}

bool capture_poll(capture_run *r, int fd) {
    for (;;) {
        struct pollfd pf[2] = {{.fd = fd, .events = POLLIN}, {.fd = r->wake[0], .events = POLLIN}};
        int n = poll(pf, 2, -1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || pf[1].revents || atomic_load(&r->stop)) return false;
        return (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    }
}

void capture_set_live(capture_run *r, bool live) {
    if (!atomic_load(&r->stop)) atomic_store(r->live, live);
}

static void *reader(void *arg) {
    capture_run *r = arg;
    signal(SIGPIPE, SIG_IGN);
    pid_t pid = -1;
    int fd = spawn(r, &pid);
    if (fd < 0) {
        log_info("Reactive: %s unavailable (%s not found)", r->name, r->argv[0]);
    } else {
        pthread_mutex_lock(&r->pid_lock);
        r->pid = pid;
        bool stopping = atomic_load(&r->stop);
        pthread_mutex_unlock(&r->pid_lock);
        if (!stopping) r->read(r, fd, r->ctx);
        close(fd);
        /* take the pid under the lock, so a stop never signals a reaped one */
        pthread_mutex_lock(&r->pid_lock);
        r->pid = -1;
        pthread_mutex_unlock(&r->pid_lock);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
    capture_set_live(r, false);
    pthread_mutex_lock(&r->exit_lock);
    r->exited = true;
    pthread_cond_broadcast(&r->exit_cond);
    pthread_mutex_unlock(&r->exit_lock);
    run_unref(r);
    return NULL;
}

bool capture_start(capture *c) {
    if (c->run) return true;
    capture_run *r = run_new(c);
    if (!r) return false;
    if (pthread_create(&r->thread, NULL, reader, r) != 0) {
        log_info("Reactive: could not start the %s thread", c->name);
        atomic_store(&r->refs, 1);
        run_unref(r);
        return false;
    }
    c->run = r;
    return true;
}

static void kill_child(capture_run *r) {
    pthread_mutex_lock(&r->pid_lock);
    if (r->pid > 0) kill(r->pid, SIGKILL);
    pthread_mutex_unlock(&r->pid_lock);
}

bool capture_stop(capture *c, long stop_ms) {
    capture_run *r = c->run;
    if (!r) return true;
    c->run = NULL;
    atomic_store(&r->stop, true);
    atomic_store(&c->live, false);
    char b = 'x';
    ssize_t n = write(r->wake[1], &b, 1);
    (void)n;
    kill_child(r);
    struct timespec until = mono_in(stop_ms);
    pthread_mutex_lock(&r->exit_lock);
    while (!r->exited && pthread_cond_timedwait(&r->exit_cond, &r->exit_lock, &until) != ETIMEDOUT) {}
    bool done = r->exited;
    pthread_mutex_unlock(&r->exit_lock);
    if (done) {
        pthread_join(r->thread, NULL);
    } else {
        /* the reader keeps its reference and its pipe; it frees the run */
        pthread_detach(r->thread);
        kill_child(r);
        log_warn("Reactive: %s capture thread did not stop in time; abandoning", c->name);
    }
    run_unref(r);
    return done;
}
