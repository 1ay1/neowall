#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/signalfd.h>
#include <ctype.h>
#include "neowall.h"
#include "config_access.h"
#include "constants.h"
#include "compositor.h"
#include "egl/egl_core.h"
#include "output/output.h"

/* Get path to the set-index command file */
static const char *get_set_index_file_path(void) {
    static char path[MAX_PATH_LENGTH];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowall-set-index", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowall-set-index", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/neowall-set-index-%d", getuid());
        }
    }

    return path;
}

/* Write the requested index to a file for the daemon to read.
 *
 * Atomic via O_CREAT|O_EXCL + rename(): if two `neowall set N` commands race,
 * the daemon will never observe a half-written file or the wrong index from
 * an earlier write (audit fix #21). */
static bool write_set_index_file(int index) {
    const char *path = get_set_index_file_path();
    char tmp[MAX_PATH_LENGTH];
    int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        fprintf(stderr, "set-index path too long\n");
        return false;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "Failed to create set-index file: %s\n", strerror(errno));
        return false;
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", index);
    if (len <= 0 || write(fd, buf, (size_t)len) != len) {
        fprintf(stderr, "Failed to write set-index file: %s\n", strerror(errno));
        close(fd);
        unlink(tmp);
        return false;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "Failed to install set-index file: %s\n", strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

/* Read the requested index from the file (called by daemon) */
int read_set_index_file(void) {
    const char *path = get_set_index_file_path();
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    int index = -1;
    if (fscanf(fp, "%d", &index) != 1) {
        index = -1;
    }
    fclose(fp);
    /* Remove the file after reading */
    unlink(path);
    return index;
}

static struct neowall_state *global_state = NULL;

/* Forward declarations */
static void handle_crash(int signum);
static const char *get_pid_file_path(void);
static void remove_pid_file(void);
static bool can_cycle_wallpaper(void);

/* Command descriptor structure */
typedef struct {
    const char *name;           /* Command name (e.g., "next") */
    int signal;                 /* Signal to send */
    const char *description;    /* Help text */
    const char *action_message; /* Message shown when executed */
    void (*handler)(int);       /* Signal handler function */
    bool needs_state_check;     /* Whether to check wallpaper state instead of signaling */
    bool check_cycle;           /* Whether to check if cycling is possible before sending signal */
} DaemonCommand;

/* Centralized command registry - Single source of truth
 * Note: 'set' command is handled specially, not via this table */
static const DaemonCommand daemon_commands[] = {
    {"next",              SIGUSR1,      "Skip to next wallpaper",                  "Skipping to next wallpaper...",      NULL,  false, true},   /* check_cycle = true */
    {"pause",             SIGUSR2,      "Pause wallpaper cycling",                 "Pausing wallpaper cycling...",       NULL,  false, false},
    {"resume",            SIGCONT,      "Resume wallpaper cycling",                "Resuming wallpaper cycling...",      NULL,  false, false},
    {"set",               0,            "Set wallpaper by index (set <index>)",    NULL,                                 NULL,  false, false},   /* Handled specially */
    {"list",              0,            "List all wallpapers with indices",        NULL,                                 NULL,  false, false},   /* Handled specially */
    {"current",           0,            "Show current wallpaper",                  NULL,                                 NULL,  true,  false},
    {"status",            0,            "Show current wallpaper",                  NULL,                                 NULL,  true,  false},
    {NULL, 0, NULL, NULL, NULL, false, false}  /* Sentinel */
};



/* Get PID file path */
static const char *get_pid_file_path(void) {
    static char pid_path[MAX_PATH_LENGTH];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(pid_path, sizeof(pid_path), "%s/neowall.pid", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(pid_path, sizeof(pid_path), "%s/.neowall.pid", home);
        } else {
            snprintf(pid_path, sizeof(pid_path), "/tmp/neowall-%d.pid", getuid());
        }
    }

    return pid_path;
}

/* Write PID file atomically.
 *
 * Race-free against concurrent neowall invocations: O_CREAT|O_EXCL ensures
 * only one process can create the file. If creation fails because the file
 * already exists, the caller is expected to look at the existing PID, decide
 * whether it's stale, and try again. */
static bool write_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    int fd = open(pid_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (errno != EEXIST) {
            log_error("Failed to create PID file %s: %s", pid_path, strerror(errno));
        }
        return false;
    }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    if (n <= 0 || write(fd, buf, (size_t)n) != n) {
        log_error("Failed to write PID to %s: %s", pid_path, strerror(errno));
        close(fd);
        unlink(pid_path);
        return false;
    }
    close(fd);

    log_debug("Created PID file: %s", pid_path);
    return true;
}

/* Rewrite the PID file in-place (no O_EXCL). Used by the daemonize grandchild
 * to record its own PID after the parent has already created the file. */
static bool update_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    int fd = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        log_error("Failed to update PID file %s: %s", pid_path, strerror(errno));
        return false;
    }
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    bool ok = (n > 0 && write(fd, buf, (size_t)n) == n);
    close(fd);
    if (!ok) {
        log_error("Failed to write PID to %s: %s", pid_path, strerror(errno));
    }
    return ok;
}

/* Remove PID file */
static void remove_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    if (unlink(pid_path) == 0) {
        log_debug("Removed PID file: %s", pid_path);
    }
}

/* Try to atomically claim the PID file. Returns true if we now own it.
 *
 * If the file already exists and points to a live process, returns false
 * (another daemon is running). If it points to a dead process or contains
 * garbage, unlinks it and tries once more. This collapses the old
 * is_daemon_running() + write_pid_file() TOCTOU into a single race-free
 * sequence. */
static bool try_take_pid_file(pid_t *existing_pid_out) {
    const char *pid_path = get_pid_file_path();

    for (int attempt = 0; attempt < 2; attempt++) {
        if (write_pid_file()) {
            return true;
        }
        /* EEXIST: someone else owns the file. Check who. */
        FILE *fp = fopen(pid_path, "r");
        if (!fp) {
            /* Vanished between failing O_EXCL and our open — retry. */
            continue;
        }
        pid_t pid = 0;
        int got = fscanf(fp, "%d", &pid);
        fclose(fp);
        if (got != 1 || pid <= 0) {
            log_debug("PID file %s contains garbage, removing", pid_path);
            unlink(pid_path);
            continue;
        }
        if (kill(pid, 0) == 0) {
            /* Live. We're not the daemon. */
            if (existing_pid_out) *existing_pid_out = pid;
            return false;
        }
        if (errno == ESRCH) {
            log_debug("Stale PID file found (PID %d not running), removing", pid);
            unlink(pid_path);
            continue;
        }
        /* EPERM or similar — process exists, just not ours to signal. */
        if (existing_pid_out) *existing_pid_out = pid;
        return false;
    }
    log_error("Failed to claim PID file after retries");
    return false;
}

/* Check if daemon is already running (read-only — may return stale info if
 * the daemon dies immediately after this returns true; race-free claiming is
 * done by try_take_pid_file()). */
/* Removed: try_take_pid_file() does both check-and-claim atomically. */


/* Kill running daemon */
static bool kill_daemon(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) {
        printf("No running neowall daemon found (no PID file at %s)\n", pid_path);
        return false;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        log_error("Failed to read PID from %s", pid_path);
        return false;
    }
    fclose(fp);

    /* Check if process exists */
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) {
            printf("NeoWall daemon (PID %d) is not running. Cleaning up stale PID file.\n", pid);
            remove_pid_file();
            return false;
        }
    }

    /* Send SIGTERM */
    printf("Stopping neowall daemon (PID %d)...\n", pid);
    if (kill(pid, SIGTERM) == -1) {
        log_error("Failed to kill process %d: %s", pid, strerror(errno));
        return false;
    }

    /* Wait a bit for graceful shutdown */
    struct timespec sleep_time = {0, SLEEP_100MS_NS};  /* 100ms */
    int attempts = 0;
    while (attempts < 50) {  /* Wait up to 5 seconds */
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            printf("NeoWall daemon stopped successfully.\n");
            remove_pid_file();
            return true;
        }
        nanosleep(&sleep_time, NULL);
        attempts++;
    }

    /* Force kill if still running */
    printf("Daemon didn't stop gracefully, forcing...\n");
    if (kill(pid, SIGKILL) == 0) {
        printf("NeoWall daemon killed.\n");
        remove_pid_file();
        return true;
    }

    log_error("Failed to kill daemon process");
    return false;
}

/* Send signal to running daemon */
/* Check if cycling is possible by reading state file */
static bool can_cycle_wallpaper(void) {
    /* BUG FIX #7: Add file-level locking for cross-process synchronization
     * Note: We can't use state_file_lock mutex here because we're in a different
     * process. We need file-level locking (flock/fcntl) for cross-process sync. */
    const char *state_path = get_state_file_path();
    FILE *fp = fopen(state_path, "r");

    if (!fp) {
        return false;  /* No state file = unknown, let daemon handle it */
    }

    /* Acquire read lock on the file */
    int fd = fileno(fp);
    struct flock lock = {
        .l_type = F_RDLCK,      /* Read lock */
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0              /* Lock entire file */
    };

    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        /* Failed to lock, but continue anyway (non-critical) */
        log_debug("Failed to lock state file for reading: %s", strerror(errno));
    }

    char line[MAX_PATH_LENGTH];
    int max_cycle_total = 0;

    /* BUG FIX #8: Check ALL outputs for cycle_total, not just the first one.
     * The state file contains multiple [output] sections, and we need to find
     * if ANY of them have cycle_total > 1 (can cycle). Previously, we stopped
     * at the first cycle_total= line, which might be 0 for a non-cycling output. */
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "cycle_total=", 12) == 0) {
            int cycle_total = atoi(line + 12);
            if (cycle_total > max_cycle_total) {
                max_cycle_total = cycle_total;
            }
            /* Don't break - continue checking all outputs */
        }
    }

    /* Release lock */
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    fclose(fp);
    return max_cycle_total > 1;  /* Can cycle if ANY output has more than 1 wallpaper */
}

static bool send_daemon_signal(int signal, const char *action, bool check_cycle) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) {
        printf("No running neowall daemon found.\n");
        printf("Start the daemon first with: neowall\n");
        return false;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        log_error("Failed to read PID from %s", pid_path);
        return false;
    }
    fclose(fp);

    /* Check if process exists */
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) {
            printf("NeoWall daemon (PID %d) is not running.\n", pid);
            remove_pid_file();
            return false;
        }
    }

    /* For 'next' command, check if cycling is possible before claiming to skip */
    if (check_cycle && !can_cycle_wallpaper()) {
        printf("Cannot cycle wallpaper: Only one wallpaper/shader configured.\n");
        printf("\n");
        printf("To enable cycling:\n");
        printf("  - Use a directory path ending with '/' in your config\n");
        printf("    Example: path ~/Pictures/Wallpapers/\n");
        printf("  - Or configure a 'duration' to cycle through wallpapers\n");
        printf("  - Multiple files will be loaded and cycled alphabetically\n");
        printf("\n");
        printf("Check current status with: neowall current\n");
        return false;
    }

    /* Send signal */
    if (kill(pid, signal) == -1) {
        log_error("Failed to send signal to daemon: %s", strerror(errno));
        return false;
    }

    printf("%s\n", action);
    return true;
}

static void print_usage(const char *program_name) {
    printf("NeoWall v%s - GPU-accelerated wallpapers for Wayland. Take the red pill. 🔴\n\n", NEOWALL_VERSION_STRING);
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("       %s set <index>   Set wallpaper by index (0-based)\n\n", program_name);
    printf("Options:\n");
    printf("  -c, --config PATH     Path to configuration file\n");
    printf("  -f, --foreground      Run in foreground (for debugging)\n");
    printf("  -v, --verbose         Enable verbose logging\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -V, --version         Show version information\n");
    printf("\n");
    printf("Daemon Control Commands (when daemon is running):\n");
    printf("  kill                  Stop running daemon\n");

    /* Auto-generate command list from table - DRY principle */
    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        /* Skip duplicate "status" command (alias for "current") */
        if (strcmp(daemon_commands[i].name, "status") == 0) {
            continue;
        }
        printf("  %-21s %s\n", daemon_commands[i].name, daemon_commands[i].description);
    }
    /* Shader-animation freeze controls (handled outside the table — RT signals). */
    printf("  %-21s %s\n", "pause-shader", "Freeze the shader animation in place");
    printf("  %-21s %s\n", "resume-shader", "Resume a frozen shader animation");
    printf("\n");
    printf("Note: By default, neowall runs as a daemon. Use -f for foreground.\n");
    printf("If a daemon is already running, subsequent calls act as control commands.\n");
    printf("\n");
    printf("Configuration file locations (in order of preference):\n");
    printf("  1. $XDG_CONFIG_HOME/neowall/config.vibe\n");
    printf("  2. $HOME/.config/neowall/config.vibe\n");
    printf("  3. /etc/neowall/config.vibe\n");
    printf("\n");
    printf("Example config.vibe:\n");
    printf("  default {\n");
    printf("    path ~/Pictures/wallpaper.png\n");
    printf("    mode fill\n");
    printf("  }\n");
    printf("\n");
    printf("  output {\n");
    printf("    eDP-1 {\n");
    printf("      path ~/Pictures/laptop-wallpaper.jpg\n");
    printf("      mode fit\n");
    printf("    }\n");
    printf("  }\n");
    printf("\n");
}

static void print_version(void) {
    printf("NeoWall v%s\n", NEOWALL_VERSION_STRING);
    printf("GPU-accelerated wallpapers for Wayland.\n");
    printf("Take the red pill. 🔴💊\n");
    printf("\nSupported features:\n");
    printf("  - Live GPU shaders at 60 FPS (Shadertoy compatible)\n");
    printf("  - 2%% CPU usage (lighter than video wallpapers)\n");
    printf("  - Multi-monitor support\n");
    printf("  - Smooth transitions (fade, slide, glitch, pixelate)\n");
    printf("  - Works on Hyprland, Sway, River, and other Wayland compositors\n");
    printf("\nSupported image formats:\n");
    printf("  - PNG\n");
    printf("  - JPEG/JPG\n");
}

/* ============================================================================
 * Signal Handling Using signalfd - RACE-FREE APPROACH
 * ============================================================================
 *
 * Instead of traditional signal handlers, we use signalfd which converts
 * signals into file descriptor events. This approach:
 *
 * 1. Eliminates ALL signal handler race conditions
 * 2. No async-signal-safe restrictions (can use any functions)
 * 3. Integrates cleanly with poll() event loop
 * 4. Guaranteed signal delivery and ordering
 * 5. No signal handler execution context issues
 *
 * Signals are blocked for all threads and read from signalfd in main loop.
 * ============================================================================ */

/* Process signals received via signalfd - called from eventloop.c */
void handle_signal_from_fd(struct neowall_state *state, int signum) {
    switch (signum) {
        case SIGTERM:
        case SIGINT:
            log_info("Received shutdown signal %d, exiting gracefully...", signum);
            atomic_store_explicit(&state->running, false, memory_order_release);
            break;

        case SIGUSR1: {
            /* Cap-before-increment via CAS loop. The previous code did a
             * fetch_add then a blind store(10) on overflow — which races with
             * the main loop's fetch_sub and can corrupt the counter (audit
             * fix #22). */
            int cur = atomic_load_explicit(&state->next_requested, memory_order_acquire);
            for (;;) {
                if (cur >= MAX_NEXT_REQUESTS) {
                    log_warn("Skip-next queue is full (%d), ignoring SIGUSR1", cur);
                    break;
                }
                if (atomic_compare_exchange_weak_explicit(
                        &state->next_requested, &cur, cur + 1,
                        memory_order_acq_rel, memory_order_acquire)) {
                    log_info("Received SIGUSR1, skipping to next wallpaper (queue: %d -> %d)",
                             cur, cur + 1);
                    break;
                }
                /* CAS failed; cur has been reloaded — retry. */
            }
            break;
        }

        case SIGUSR2:
            log_info("Received SIGUSR2, pausing wallpaper cycling...");
            atomic_store_explicit(&state->paused, true, memory_order_release);
            break;

        case SIGCONT:
            log_info("Received SIGCONT, resuming wallpaper cycling...");
            atomic_store_explicit(&state->paused, false, memory_order_release);
            break;

        default:
            /* Real-time signals (SIGRTMIN+N) aren't compile-time constants, so
             * they can't sit in the switch above — dispatch them here. */
            if (signum == SIGRTMIN) {
                /* Read the requested index from file */
                int index = read_set_index_file();
                if (index >= 0) {
                    log_info("Received SIGRTMIN, setting wallpaper to index %d", index);
                    atomic_store_explicit(&state->set_index_requested, index, memory_order_release);
                } else {
                    log_error("Received SIGRTMIN but no valid index file found");
                }
            } else if (signum == SIGRTMIN + 1) {
                log_info("Received SIGRTMIN+1, freezing shader animation...");
                atomic_store_explicit(&state->shader_paused, true, memory_order_release);
            } else if (signum == SIGRTMIN + 2) {
                log_info("Received SIGRTMIN+2, resuming shader animation...");
                atomic_store_explicit(&state->shader_paused, false, memory_order_release);
            } else {
                log_debug("Received signal: %d", signum);
            }
            break;
    }
}

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
        case SIGBUS:  msg = "CRASH: SIGBUS (bus error)\n"; break;
        case SIGILL:  msg = "CRASH: SIGILL (illegal instruction)\n"; break;
        case SIGFPE:  msg = "CRASH: SIGFPE (floating point exception)\n"; break;
        case SIGABRT: msg = "CRASH: SIGABRT (abort)\n"; break;
        default:      msg = "CRASH: fatal signal\n"; break;
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

/* ============================================================================
 * Signal Setup Using signalfd - RACE-FREE APPROACH
 * ============================================================================ */

static int setup_signalfd(void) {
    sigset_t mask;
    sigemptyset(&mask);

    /* Block all signals we want to handle via signalfd */
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGCONT);
    sigaddset(&mask, SIGRTMIN);      /* For set-index command */
    sigaddset(&mask, SIGRTMIN + 1);  /* For pause-shader command */
    sigaddset(&mask, SIGRTMIN + 2);  /* For resume-shader command */

    /* Block these signals for all threads */
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
        log_error("Failed to block signals: %s", strerror(errno));
        return -1;
    }

    /* Create signalfd to receive signals as file descriptor events */
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd < 0) {
        log_error("Failed to create signalfd: %s", strerror(errno));
        return -1;
    }

    log_info("Signal handling configured with signalfd (race-free)");
    return sfd;
}

static void setup_crash_handlers(void) {
    /* Crash signals still need traditional handlers since they're fatal */
    struct sigaction crash_sa;
    memset(&crash_sa, 0, sizeof(crash_sa));
    crash_sa.sa_handler = handle_crash;
    sigemptyset(&crash_sa.sa_mask);
    crash_sa.sa_flags = SA_RESETHAND;  /* Reset to default after first crash */

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

static bool daemonize(void) {
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
    if (dup2(devnull, STDIN_FILENO)  < 0 ||
        dup2(devnull, STDOUT_FILENO) < 0 ||
        dup2(devnull, STDERR_FILENO) < 0) {
        log_error("Failed to dup2 /dev/null onto std fds: %s", strerror(errno));
        close(devnull);
        return false;
    }
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }

    /* Write PID file */
    if (!update_pid_file()) {
        log_error("Failed to write PID file, but continuing anyway");
    }

    return true;
}

static bool create_config_directory(void) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    char config_dir[MAX_PATH_LENGTH];

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
    char tmp[MAX_PATH_LENGTH];
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

int main(int argc, char *argv[]) {

    struct neowall_state state = {0};
    char config_path[MAX_PATH_LENGTH] = {0};
    bool daemon_mode = true;  /* Default to daemon mode */
    bool verbose = false;
    int opt;

    /* ========================================================================
     * Command Dispatch - Table-driven lookup (Command Pattern)
     * ======================================================================== */
    if (argc >= 2 && argv[1][0] != '-') {
        const char *cmd = argv[1];

        /* Special case: kill command */
        if (strcmp(cmd, "kill") == 0) {
            return kill_daemon() ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        /* Special case: list command shows all wallpapers with indices */
        if (strcmp(cmd, "list") == 0) {
            return read_cycle_list() ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        /* Special cases: freeze/resume the shader animation. Delivered via
         * real-time signals (SIGRTMIN+N), which aren't compile-time constants
         * and so can't live in the daemon_commands table. */
        if (strcmp(cmd, "pause-shader") == 0) {
            return send_daemon_signal(SIGRTMIN + 1, "Freezing shader animation...", false)
                   ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        if (strcmp(cmd, "resume-shader") == 0) {
            return send_daemon_signal(SIGRTMIN + 2, "Resuming shader animation...", false)
                   ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        /* Special case: set command requires an index argument */
        if (strcmp(cmd, "set") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Usage: %s set <index>\n", argv[0]);
                fprintf(stderr, "  <index>  Wallpaper index (0-based)\n");
                fprintf(stderr, "\nUse '%s current' to see available wallpapers and their indices.\n", argv[0]);
                return EXIT_FAILURE;
            }
            /* Validate index is a number */
            const char *index_str = argv[2];
            for (const char *p = index_str; *p; p++) {
                if (!isdigit((unsigned char)*p)) {
                    fprintf(stderr, "Error: Index must be a non-negative integer, got '%s'\n", index_str);
                    return EXIT_FAILURE;
                }
            }
            int index = atoi(index_str);
            
            /* Send set-index command to daemon */
            const char *pid_path = get_pid_file_path();
            FILE *fp = fopen(pid_path, "r");
            if (!fp) {
                printf("No running neowall daemon found.\n");
                printf("Start the daemon first with: neowall\n");
                return EXIT_FAILURE;
            }

            pid_t pid;
            if (fscanf(fp, "%d", &pid) != 1) {
                fclose(fp);
                fprintf(stderr, "Failed to read PID\n");
                return EXIT_FAILURE;
            }
            fclose(fp);

            /* Check if process exists */
            if (kill(pid, 0) == -1 && errno == ESRCH) {
                printf("NeoWall daemon (PID %d) is not running.\n", pid);
                remove_pid_file();
                return EXIT_FAILURE;
            }

            /* Check if cycling is possible */
            if (!can_cycle_wallpaper()) {
                printf("Cannot set wallpaper index: Only one wallpaper/shader configured.\n");
                return EXIT_FAILURE;
            }

            /* Write the index to a file for the daemon to read */
            if (!write_set_index_file(index)) {
                return EXIT_FAILURE;
            }

            /* Send SIGRTMIN to notify daemon */
            if (kill(pid, SIGRTMIN) == -1) {
                fprintf(stderr, "Failed to send signal to daemon: %s\n", strerror(errno));
                unlink(get_set_index_file_path());
                return EXIT_FAILURE;
            }

            printf("Setting wallpaper to index %d...\n", index);
            return EXIT_SUCCESS;
        }

        /* Lookup command in table and dispatch */
        for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
            if (strcmp(cmd, daemon_commands[i].name) == 0) {
                /* Command found - execute it */
                if (daemon_commands[i].needs_state_check) {
                    /* Commands like "current" that read state */
                    return read_wallpaper_state() ? EXIT_SUCCESS : EXIT_FAILURE;
                } else {
                    /* Commands that send signals to daemon */
                    return send_daemon_signal(daemon_commands[i].signal,
                                            daemon_commands[i].action_message,
                                            daemon_commands[i].check_cycle)
                           ? EXIT_SUCCESS : EXIT_FAILURE;
                }
            }
        }

        /* Command not found - print error with available commands */
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        fprintf(stderr, "Available commands:\n");
        fprintf(stderr, "  kill");
        for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
            fprintf(stderr, ", %s", daemon_commands[i].name);
        }
        fprintf(stderr, ", pause-shader, resume-shader");
        fprintf(stderr, "\n\nRun '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"foreground", no_argument,       0, 'f'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "c:fvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                strncpy(config_path, optarg, sizeof(config_path) - 1);
                break;
            case 'f':
                daemon_mode = false;  /* Explicit foreground */
                break;
            case 'v':
                /* Verbose mode - enable debug logging */
                verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case 'V':
                print_version();
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* Set up logging */
    if (verbose) {
        log_set_level(LOG_LEVEL_DEBUG);
    }
    log_info("NeoWall v%s starting...", NEOWALL_VERSION_STRING);

    /* Ensure config directory exists */
    if (!create_config_directory()) {
        log_error("Failed to create configuration directory");
        return EXIT_FAILURE;
    }

    /* Determine config file path */
    if (config_path[0] == '\0') {
        const char *default_path = config_get_default_path();
        if (default_path) {
            strncpy(config_path, default_path, sizeof(config_path) - 1);
        } else {
            log_error("Could not determine config file path");
            return EXIT_FAILURE;
        }
    }

    log_info("Using configuration file: %s", config_path);

    /* Atomically claim the PID file. This races correctly against another
     * concurrent `neowall` invocation: only one O_CREAT|O_EXCL succeeds. We
     * claim BEFORE forking so the grandchild can't be beaten to the file by
     * a sibling invocation during the fork dance. */
    pid_t existing_pid = 0;
    if (!try_take_pid_file(&existing_pid)) {
        const char *pid_path = get_pid_file_path();
        log_error("NeoWall is already running (PID %d)", existing_pid);
        fprintf(stderr, "Error: NeoWall is already running (PID %d)\n", existing_pid);
        fprintf(stderr, "PID file: %s\n", pid_path);
        fprintf(stderr, "Use 'neowall kill' to stop the running instance.\n");
        return EXIT_FAILURE;
    }

    /* Daemonize if requested. The grandchild will rewrite the PID file to its
     * own PID via update_pid_file() at the end of daemonize(). */
    if (daemon_mode) {
        log_info("Running as daemon...");
        if (!daemonize()) {
            remove_pid_file();
            return EXIT_FAILURE;
        }
    }

    /* Set up crash handlers first */
    setup_crash_handlers();
    global_state = &state;

    /* Initialize atomic state flags - MUST use atomic_init before any access */
    atomic_init(&state.running, true);
    atomic_init(&state.paused, false);
    atomic_init(&state.shader_paused, false);
    atomic_init(&state.outputs_need_init, false);
    atomic_init(&state.next_requested, 0);
    atomic_init(&state.set_index_requested, -1);  /* -1 means no request */
    atomic_init(&state.mouse_interaction, true);  /* default: pointer enabled, override from config */
    state.timer_fd = -1;
    state.wakeup_fd = -1;
    strncpy(state.config_path, config_path, sizeof(state.config_path) - 1);
    state.config_path[sizeof(state.config_path) - 1] = '\0';
    pthread_mutex_init(&state.state_mutex, NULL);
    pthread_rwlock_init(&state.output_list_lock, NULL);
    pthread_mutex_init(&state.state_file_lock, NULL);

    /* Set up signalfd for race-free signal handling */
    state.signal_fd = setup_signalfd();
    if (state.signal_fd < 0) {
        log_error("Failed to set up signal handling");
        return EXIT_FAILURE;
    }

    /* Initialize compositor backend (auto-detects Wayland/X11) */
    log_info("Initializing compositor backend...");
    state.compositor_backend = compositor_backend_init(&state);
    if (!state.compositor_backend) {
        log_error("Failed to initialize compositor backend");
        log_error("Ensure you're running under a Wayland compositor or X11 window manager");
        close(state.signal_fd);
        return EXIT_FAILURE;
    }

    log_info("Compositor backend initialized: %s", state.compositor_backend->name);
    log_info("Description: %s", state.compositor_backend->description);

    /* Initialize EGL/OpenGL */
    if (!egl_core_init(&state)) {
        log_error("Failed to initialize EGL");
        compositor_backend_cleanup(state.compositor_backend);
        close(state.signal_fd);
        return EXIT_FAILURE;
    }

    /* Load configuration and apply to outputs */
    if (!config_load(&state, config_path)) {
        log_error("Failed to load configuration");
        egl_core_cleanup(&state);
        compositor_backend_cleanup(state.compositor_backend);
        close(state.signal_fd);
        return EXIT_FAILURE;
    }

    log_info("Initialization complete, entering main loop...");

    /* Run main event loop */
    event_loop_run(&state);

    /* Cleanup */
    log_info("Shutting down...");

    /* Set alarm as last resort - force exit after 2 seconds if cleanup hangs */
    alarm(2);

    /* Quick cleanup - don't spend too much time on this during shutdown */
    egl_core_cleanup(&state);

    /* Cleanup compositor backend */
    if (state.compositor_backend) {
        compositor_backend_cleanup(state.compositor_backend);
        state.compositor_backend = NULL;
    }

    /* Close signal fd */
    if (state.signal_fd >= 0) {
        close(state.signal_fd);
    }

    /* Skip mutex/lock destruction during fast shutdown - OS will clean up */
    pthread_rwlock_destroy(&state.output_list_lock);
    pthread_mutex_destroy(&state.state_mutex);
    pthread_mutex_destroy(&state.state_file_lock);

    /* Remove PID file */
    remove_pid_file();

    /* Cancel alarm - we finished cleanup in time */
    alarm(0);

    log_info("NeoWall terminated successfully");

    return EXIT_SUCCESS;
}
