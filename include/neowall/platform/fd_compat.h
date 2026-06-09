#ifndef NEOWALL_PLATFORM_FD_COMPAT_H
#define NEOWALL_PLATFORM_FD_COMPAT_H

/*
 * Linux fd-primitive compatibility for macOS.
 *
 * The engine's event loop is built on poll() + three Linux fd primitives:
 *   timerfd  — timers readable as uint64 expiration counts
 *   eventfd  — wakeup counter readable as uint64
 *   signalfd — signals readable as siginfo records
 *
 * macOS lacks all three, but kqueue + pipes reproduce the exact read/poll
 * semantics the loop expects, so eventloop.c/main.c/output.c stay byte-for
 * byte identical in their poll() handling:
 *
 *   timerfd  -> kqueue fd with EVFILT_TIMER; nw_timerfd_read() drains the
 *               kevent and returns the expiration count. kqueue fds are
 *               poll()able on macOS (readable when events pending).
 *   eventfd  -> non-blocking pipe; write side stores the fd in a registry so
 *               nw_eventfd_write() can find it from the read fd the caller
 *               holds.
 *   signalfd -> kqueue fd with EVFILT_SIGNAL per signal; signals must ALSO be
 *               blocked via sigprocmask (caller already does) — EVFILT_SIGNAL
 *               fires even for blocked signals, same model as signalfd.
 *
 * On Linux this header is a thin pass-through to the real syscalls so shared
 * code can use the nw_* names unconditionally.
 */

#include <stdint.h>
#include <signal.h>
#include <time.h>

/*
 * Command signals. Linux uses real-time signals (SIGRTMIN+N). macOS has no
 * RT signals, so we repurpose three BSD signals the daemon doesn't otherwise
 * use. Both the daemon's handler and the CLI sender use these names.
 */
#ifdef __APPLE__
#define NW_SIG_SETINDEX      SIGWINCH
#define NW_SIG_PAUSE_SHADER  SIGINFO
#define NW_SIG_RESUME_SHADER SIGVTALRM
#else
#define NW_SIG_SETINDEX      (SIGRTMIN)
#define NW_SIG_PAUSE_SHADER  (SIGRTMIN + 1)
#define NW_SIG_RESUME_SHADER (SIGRTMIN + 2)
#endif

#ifdef __APPLE__

#ifdef __cplusplus
extern "C" {
#endif

struct nw_itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

/* timerfd */
int nw_timerfd_create(void);
int nw_timerfd_settime(int fd, const struct nw_itimerspec *spec);
/* Reads expiration count (0 if none). Mirrors read(fd, &u64, 8). */
int64_t nw_timerfd_read(int fd);
void nw_timerfd_close(int fd);

/* eventfd */
int nw_eventfd_create(void);
void nw_eventfd_write(int read_fd); /* wake the poller */
void nw_eventfd_drain(int read_fd);
void nw_eventfd_close(int read_fd);

/* signalfd */
struct nw_signalfd_info {
    uint32_t ssi_signo;
};
int nw_signalfd_create(const sigset_t *mask);
/* Returns 1 and fills info if a signal was pending, 0 otherwise. */
int nw_signalfd_read(int fd, struct nw_signalfd_info *info);
void nw_signalfd_close(int fd);

#ifdef __cplusplus
}
#endif

#else /* Linux: map nw_* onto the native primitives */

#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#define nw_itimerspec itimerspec

static inline int nw_timerfd_create(void) {
    return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}
static inline int nw_timerfd_settime(int fd, const struct itimerspec *spec) {
    return timerfd_settime(fd, 0, spec, NULL);
}
static inline int64_t nw_timerfd_read(int fd) {
    uint64_t v = 0;
    ssize_t s = read(fd, &v, sizeof(v));
    return s == (ssize_t)sizeof(v) ? (int64_t)v : -1;
}
static inline void nw_timerfd_close(int fd) { close(fd); }

static inline int nw_eventfd_create(void) {
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}
static inline void nw_eventfd_write(int fd) {
    uint64_t one = 1;
    ssize_t s = write(fd, &one, sizeof(one));
    (void)s;
}
static inline void nw_eventfd_drain(int fd) {
    uint64_t v;
    ssize_t s = read(fd, &v, sizeof(v));
    (void)s;
}
static inline void nw_eventfd_close(int fd) { close(fd); }

struct nw_signalfd_info {
    uint32_t ssi_signo;
};
static inline int nw_signalfd_create(const sigset_t *mask) {
    return signalfd(-1, mask, SFD_NONBLOCK | SFD_CLOEXEC);
}
static inline int nw_signalfd_read(int fd, struct nw_signalfd_info *info) {
    struct signalfd_siginfo fdsi;
    ssize_t s = read(fd, &fdsi, sizeof(fdsi));
    if (s != (ssize_t)sizeof(fdsi)) return 0;
    info->ssi_signo = fdsi.ssi_signo;
    return 1;
}
static inline void nw_signalfd_close(int fd) { close(fd); }

#endif /* __APPLE__ */

#endif /* NEOWALL_PLATFORM_FD_COMPAT_H */
