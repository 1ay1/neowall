#ifndef NEOWALL_CHILD_SIGNALS_H
#define NEOWALL_CHILD_SIGNALS_H

/* Children must not inherit the daemon's signal state. main() blocks
 * SIGTERM/SIGINT/SIGHUP/... in every thread (they arrive via signalfd) and
 * ignores SIGPIPE, and both survive exec. A child that inherits them can't be
 * stopped with kill, and outlives neowall writing into a dead pipe forever. */

#include <signal.h>
#include <spawn.h>

/* Call in a fork() child before exec. Only async-signal-safe calls. */
static inline void nw_child_reset_signals(void) {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    signal(SIGPIPE, SIG_DFL);
}

/* Same for posix_spawn: empty mask, default SIGPIPE. Returns 0 on success. */
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

#endif /* NEOWALL_CHILD_SIGNALS_H */
