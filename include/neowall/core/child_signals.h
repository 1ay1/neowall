#ifndef NEOWALL_CHILD_SIGNALS_H
#define NEOWALL_CHILD_SIGNALS_H

/* Children must not inherit the daemon's state.
 *
 * Signals: main() blocks SIGTERM/SIGINT/SIGHUP/... in every thread (they
 * arrive via signalfd) and ignores SIGPIPE, and both survive exec. A child
 * that inherits them can't be stopped with kill.
 *
 * Fds: anything not marked close-on-exec leaks into the child (another
 * terminal's pty master, a wake pipe, an exec source's pipe). A leaked write
 * end keeps a reader from ever seeing EOF, and the child can touch fds it has
 * no business with. So every child starts with only 0, 1 and 2. */

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <spawn.h>
#include <stdlib.h>
#include <unistd.h>

/* Make a pipe whose both ends are close-on-exec. pipe2() is not in POSIX
 * 2008, so set the flag right after; the window only matters for a fork from
 * another thread, and every child also closes all fds (below). */
static inline int nw_pipe_cloexec(int fds[2]) {
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

/* Close every fd above stderr. For a fork() child before exec: close() is
 * async-signal-safe, and fd numbers are dense, so stop after a long run of
 * fds that were never open. */
static inline void nw_child_close_fds(void) {
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 1 << 20) max = 1 << 20;
    int misses = 0;
    for (int fd = 3; fd < (int)max && misses < 4096; fd++) {
        if (close(fd) == 0)
            misses = 0;
        else
            misses++;
    }
}

/* Call in a fork() child before exec. Only async-signal-safe calls. */
static inline void nw_child_reset_signals(void) {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    signal(SIGPIPE, SIG_DFL);
}

/* Same for posix_spawn: empty mask, default SIGPIPE. Fds: use
 * nw_spawn_close_fds on the file actions. Returns 0 on success. */
static inline int nw_spawnattr_reset_signals(posix_spawnattr_t *attr) {
    sigset_t none, dfl;
    sigemptyset(&none);
    sigemptyset(&dfl);
    sigaddset(&dfl, SIGPIPE);
    int rc = posix_spawnattr_init(attr);
    if (rc) return rc;
    rc = posix_spawnattr_setsigmask(attr, &none);
    if (!rc) rc = posix_spawnattr_setsigdefault(attr, &dfl);
    if (!rc) rc = posix_spawnattr_setflags(attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (rc) posix_spawnattr_destroy(attr);
    return rc;
}

/* For posix_spawn: add a close for every open fd above stderr except
 * `keep` (the pipe end the child writes to, already dup2'd or not). Call
 * after the dup2 actions. Reads /proc/self/fd in the parent, so it's not for
 * a fork child. */
static inline void nw_spawn_close_fds(posix_spawn_file_actions_t *fa, int keep) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) return;
    int self = dirfd(d);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        int fd = atoi(e->d_name);
        if (fd > 2 && fd != self && fd != keep) posix_spawn_file_actions_addclose(fa, fd);
    }
    closedir(d);
}

#endif /* NEOWALL_CHILD_SIGNALS_H */
