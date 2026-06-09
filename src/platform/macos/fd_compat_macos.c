/*
 * macOS implementations of the Linux fd primitives the engine is built on.
 * See include/neowall/platform/fd_compat.h for the contract.
 *
 * All three are backed by kqueue file descriptors, which poll(2) treats as
 * readable whenever events are pending — exactly the semantic the event loop
 * relies on for timerfd/signalfd. The eventfd stand-in is a plain pipe.
 */
#ifdef __APPLE__

#include "neowall/platform/fd_compat.h"

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* ============================================================================
 * timerfd — kqueue EVFILT_TIMER
 * ========================================================================== */

int nw_timerfd_create(void) {
    int kq = kqueue();
    if (kq < 0) return -1;
    fcntl(kq, F_SETFD, FD_CLOEXEC);
    return kq;
}

int nw_timerfd_settime(int fd, const struct nw_itimerspec *spec) {
    if (fd < 0 || !spec) {
        errno = EINVAL;
        return -1;
    }

    /* Drop any existing timer first (ignore ENOENT). */
    struct kevent del;
    EV_SET(&del, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    (void)kevent(fd, &del, 1, NULL, 0, NULL);

    int64_t value_ns = (int64_t)spec->it_value.tv_sec * 1000000000LL +
                       spec->it_value.tv_nsec;
    int64_t interval_ns = (int64_t)spec->it_interval.tv_sec * 1000000000LL +
                          spec->it_interval.tv_nsec;

    if (value_ns == 0 && interval_ns == 0) {
        /* timerfd semantics: zero it_value disarms. Already deleted above. */
        return 0;
    }

    struct kevent kev;
    if (interval_ns > 0) {
        /* Periodic. kqueue has no separate initial value; the engine always
         * sets it_value == it_interval for periodic timers, so a plain
         * recurring timer matches. */
        EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS,
               interval_ns, NULL);
    } else {
        /* One-shot. */
        EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
               NOTE_NSECONDS, value_ns, NULL);
    }

    return kevent(fd, &kev, 1, NULL, 0, NULL);
}

int64_t nw_timerfd_read(int fd) {
    struct kevent ev;
    struct timespec zero = {0, 0};
    int64_t expirations = 0;

    /* Drain every pending event; ev.data carries the fire count. */
    for (;;) {
        int n = kevent(fd, NULL, 0, &ev, 1, &zero);
        if (n <= 0) break;
        if (ev.filter == EVFILT_TIMER) {
            expirations += ev.data > 0 ? ev.data : 1;
        }
    }
    return expirations > 0 ? expirations : -1;
}

void nw_timerfd_close(int fd) {
    if (fd >= 0) close(fd);
}

/* ============================================================================
 * eventfd — non-blocking pipe + registry mapping read fd -> write fd
 * ========================================================================== */

#define NW_EVENTFD_MAX 8

static struct {
    int read_fd;
    int write_fd;
} g_eventfds[NW_EVENTFD_MAX];
static pthread_mutex_t g_eventfd_lock = PTHREAD_MUTEX_INITIALIZER;

int nw_eventfd_create(void) {
    int p[2];
    if (pipe(p) < 0) return -1;
    for (int i = 0; i < 2; i++) {
        fcntl(p[i], F_SETFL, O_NONBLOCK);
        fcntl(p[i], F_SETFD, FD_CLOEXEC);
    }

    pthread_mutex_lock(&g_eventfd_lock);
    for (int i = 0; i < NW_EVENTFD_MAX; i++) {
        if (g_eventfds[i].read_fd == 0 && g_eventfds[i].write_fd == 0) {
            g_eventfds[i].read_fd = p[0];
            g_eventfds[i].write_fd = p[1];
            pthread_mutex_unlock(&g_eventfd_lock);
            return p[0];
        }
    }
    pthread_mutex_unlock(&g_eventfd_lock);

    close(p[0]);
    close(p[1]);
    errno = ENOMEM;
    return -1;
}

static int eventfd_write_fd(int read_fd) {
    int wfd = -1;
    pthread_mutex_lock(&g_eventfd_lock);
    for (int i = 0; i < NW_EVENTFD_MAX; i++) {
        if (g_eventfds[i].read_fd == read_fd) {
            wfd = g_eventfds[i].write_fd;
            break;
        }
    }
    pthread_mutex_unlock(&g_eventfd_lock);
    return wfd;
}

void nw_eventfd_write(int read_fd) {
    int wfd = eventfd_write_fd(read_fd);
    if (wfd >= 0) {
        uint8_t one = 1;
        ssize_t s = write(wfd, &one, 1);
        (void)s; /* Pipe full == poller already pending; that's fine. */
    }
}

void nw_eventfd_drain(int read_fd) {
    uint8_t buf[64];
    while (read(read_fd, buf, sizeof(buf)) > 0) { /* drain */ }
}

void nw_eventfd_close(int read_fd) {
    pthread_mutex_lock(&g_eventfd_lock);
    for (int i = 0; i < NW_EVENTFD_MAX; i++) {
        if (g_eventfds[i].read_fd == read_fd) {
            close(g_eventfds[i].write_fd);
            g_eventfds[i].read_fd = 0;
            g_eventfds[i].write_fd = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_eventfd_lock);
    close(read_fd);
}

/* ============================================================================
 * signalfd — kqueue EVFILT_SIGNAL
 * ========================================================================== */

int nw_signalfd_create(const sigset_t *mask) {
    int kq = kqueue();
    if (kq < 0) return -1;
    fcntl(kq, F_SETFD, FD_CLOEXEC);

    /* macOS kqueue subtlety: EVFILT_SIGNAL records DELIVERY attempts. A
     * blocked signal is never delivered (it sits pending), so the Linux
     * signalfd model (block + read) doesn't transfer. The BSD idiom is to
     * set the disposition to SIG_IGN — the delivery attempt is then counted
     * by kqueue even though the signal itself is discarded — and ensure the
     * signals are NOT blocked. */
    for (int sig = 1; sig < NSIG; sig++) {
        if (!sigismember(mask, sig)) continue;
        struct kevent kev;
        EV_SET(&kev, sig, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, NULL);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
            close(kq);
            return -1;
        }
        signal(sig, SIG_IGN);
    }
    pthread_sigmask(SIG_UNBLOCK, mask, NULL);
    return kq;
}

int nw_signalfd_read(int fd, struct nw_signalfd_info *info) {
    struct kevent ev;
    struct timespec zero = {0, 0};
    int n = kevent(fd, NULL, 0, &ev, 1, &zero);
    if (n <= 0 || ev.filter != EVFILT_SIGNAL) return 0;
    info->ssi_signo = (uint32_t)ev.ident;
    return 1;
}

void nw_signalfd_close(int fd) {
    if (fd >= 0) close(fd);
}

#endif /* __APPLE__ */
