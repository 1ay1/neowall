/* Being a daemon: the pid file lock, daemonizing, crash handlers, blocked
 * signals, the GPU-loss restart budget. See neowall/app/daemon.h. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "neowall/state/state.h"
#include "neowall/runtime/rt.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/core/util.h"
#include "neowall/proto/proto.h"

/* For the crash handler: tell other threads to stop. */
static struct neowall_state *global_state = NULL;

/* Handle crash signals - these still need traditional handlers.
 *
 * MUST be async-signal-safe: no printf, no malloc, no localtime, no exit().
 * We use write(2) directly and _exit(2). Stash the running flag too, but
 * everything else is decoration. See signal-safety(7). */
static void handle_crash(int signum) {
    /* Map known signals to short string literals. */
    const char *msg;
    switch (signum) {
    case SIGSEGV: msg = "CRASH: SIGSEGV (segfault)\n"; break;
    case SIGBUS: msg = "CRASH: SIGBUS (bus error)\n"; break;
    case SIGILL: msg = "CRASH: SIGILL (illegal instruction)\n"; break;
    case SIGFPE: msg = "CRASH: SIGFPE (floating point exception)\n"; break;
    case SIGABRT: msg = "CRASH: SIGABRT (abort)\n"; break;
    default: msg = "CRASH: fatal signal\n"; break;
    }

    /* write() is async-signal-safe; printf-family functions are not. */
    ssize_t n = write(STDERR_FILENO, msg, strlen(msg));
    (void)n;

    /* Best-effort: ask the event loop to shut down. atomic_store on a lock-free
     * atomic is async-signal-safe (we're not taking any libc lock). */
    if (global_state) {
        atomic_store_explicit(&global_state->running, false, memory_order_release);
    }

    /* _exit (not exit) — don't run atexit handlers / stdio cleanup from a
     * signal handler; SA_RESETHAND ensures a repeat signal core-dumps cleanly. */
    _exit(EXIT_FAILURE);
}

/* Block the signals the runtime turns into messages (app.c), in this thread
 * and so in every thread started after it. The runtime reads them from a
 * signalfd; nothing ever runs in a handler. */
bool block_signals(void) {
    const int sigs[] = {SIGTERM,  SIGINT,       SIGUSR1,      SIGUSR2,      SIGCONT, SIGHUP,
                        SIGRTMIN, SIGRTMIN + 1, SIGRTMIN + 2, SIGRTMIN + 3, 0};
    if (!rt_block_signals(sigs)) {
        log_error("Failed to block signals: %s", strerror(errno));
        return false;
    }
    return true;
}

void setup_crash_handlers(struct neowall_state *state) {
    global_state = state;
    /* Crash signals still need traditional handlers since they're fatal */
    struct sigaction crash_sa;
    memset(&crash_sa, 0, sizeof(crash_sa));
    crash_sa.sa_handler = handle_crash;
    sigemptyset(&crash_sa.sa_mask);
    crash_sa.sa_flags = SA_RESETHAND; /* Reset to default after first crash */

    sigaction(SIGSEGV, &crash_sa, NULL);
    sigaction(SIGBUS, &crash_sa, NULL);
    sigaction(SIGILL, &crash_sa, NULL);
    sigaction(SIGFPE, &crash_sa, NULL);
    sigaction(SIGABRT, &crash_sa, NULL);

    /* Ignore SIGPIPE via sigaction — signal(3) semantics are
     * implementation-defined across SysV/BSD (audit fix #27). */
    struct sigaction pipe_sa;
    memset(&pipe_sa, 0, sizeof(pipe_sa));
    pipe_sa.sa_handler = SIG_IGN;
    sigemptyset(&pipe_sa.sa_mask);
    sigaction(SIGPIPE, &pipe_sa, NULL);

    log_debug("Crash signal handlers installed");
}

/* Allow a GPU-loss restart unless we've already restarted several times in
 * quick succession; a GPU that never comes back shouldn't loop forever. */
bool gpu_restart_allowed(void) {
    const char *v = getenv("NEOWALL_GPU_RESTARTS");
    const char *t = getenv("NEOWALL_GPU_RESTART_AT");
    long count = 0, at = 0;
    if (v) neowall_parse_index(v, &count);
    if (t) neowall_parse_index(t, &at);
    long now = (long)(get_time_ms() / 1000); /* monotonic, survives exec */
    if (now - at > 60) count = 0;            /* a minute of health resets it */
    if (count >= 3) {
        log_error("GPU lost %ld times in a minute; not restarting", count);
        return false;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", count + 1);
    setenv("NEOWALL_GPU_RESTARTS", buf, 1);
    snprintf(buf, sizeof(buf), "%ld", now);
    setenv("NEOWALL_GPU_RESTART_AT", buf, 1);
    return true;
}

bool daemonize(void) {
    pid_t pid = fork();

    if (pid < 0) {
        log_error("Failed to fork: %s", strerror(errno));
        return false;
    }

    if (pid > 0) {
        /* Parent process exits */
        exit(EXIT_SUCCESS);
    }

    /* Child process continues */
    if (setsid() < 0) {
        log_error("Failed to create new session: %s", strerror(errno));
        return false;
    }

    /* Fork again to prevent acquiring a controlling terminal */
    pid = fork();
    if (pid < 0) {
        log_error("Failed to fork second time: %s", strerror(errno));
        return false;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change working directory to root */
    if (chdir("/") < 0) {
        log_error("Failed to change directory: %s", strerror(errno));
        return false;
    }

    /* Detach stdio: open /dev/null first, then dup2 onto 0/1/2.
     * The previous code closed 0/1/2 first, which meant if open() failed for
     * any reason, log_error writes would land on whatever fd table slot got
     * recycled next — a fun way to corrupt the PID file. */
    int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (devnull < 0) {
        log_error("Failed to open /dev/null: %s", strerror(errno));
        return false;
    }
    if (dup2(devnull, STDIN_FILENO) < 0 || dup2(devnull, STDOUT_FILENO) < 0 ||
        dup2(devnull, STDERR_FILENO) < 0) {
        log_error("Failed to dup2 /dev/null onto std fds: %s", strerror(errno));
        close(devnull);
        return false;
    }
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }

    /* Publish the final daemon PID while retaining the inherited lock. */
    if (!update_pid_file()) {
        return false;
    }

    return true;
}

bool create_config_directory(void) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    char config_dir[NW_PATH_MAX];

    if (config_home) {
        snprintf(config_dir, sizeof(config_dir), "%s/neowall", config_home);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot determine home directory");
            return false;
        }
        snprintf(config_dir, sizeof(config_dir), "%s/.config/neowall", home);
    }

    /* Create directories recursively. We append a trailing '/' before walking
     * so the loop creates the leaf as well (cleaner than the previous "loop
     * for parents, then stat-and-mkdir leaf" dance — the stat was dead code
     * because the loop already created the leaf, see audit fix #33). */
    char tmp[NW_PATH_MAX];
    int written = snprintf(tmp, sizeof(tmp), "%s/", config_dir);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        log_error("Config directory path too long: %s", config_dir);
        return false;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
                log_error("Failed to create directory %s: %s", tmp, strerror(errno));
                return false;
            }
            *p = '/';
        }
    }

    return true;
}
