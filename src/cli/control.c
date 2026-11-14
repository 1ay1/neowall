/* Talking to a running daemon: the control socket first (one request, one
 * real answer), then signals for an older daemon without one.
 * See neowall/cli/cli.h. */

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
#include "neowall/runtime/ipc.h"
#include "neowall/core/log.h"
#include "neowall/core/util.h"
#include "neowall/output/state_file.h"
#include "version.h"
#include "neowall/proto/proto.h"

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
/* clang-format off */
static const DaemonCommand daemon_commands[] = {
    {"next",              SIGUSR1,      "Skip to next wallpaper",                  "Skipping to next wallpaper...",      NULL,  false, true},   /* check_cycle = true */
    {"pause",             SIGUSR2,      "Pause wallpaper cycling",                 "Pausing wallpaper cycling...",       NULL,  false, false},
    {"resume",            SIGCONT,      "Resume wallpaper cycling",                "Resuming wallpaper cycling...",      NULL,  false, false},
    {"reload",            SIGHUP,       "Reload configuration from disk",          "Reloading configuration...",         NULL,  false, false},
    {"set",               0,            "Set wallpaper by index (set <index>)",    NULL,                                 NULL,  false, false},   /* Handled specially */
    {"list",              0,            "List all wallpapers with indices",        NULL,                                 NULL,  false, false},   /* Handled specially */
    {"current",           0,            "Show current wallpaper",                  NULL,                                 NULL,  true,  false},
    {"status",            0,            "Show current wallpaper",                  NULL,                                 NULL,  true,  false},
    {NULL, 0, NULL, NULL, NULL, false, false}  /* Sentinel */
};
/* clang-format on */

/* Kill running daemon */
static bool kill_daemon(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp             = fopen(pid_path, "r");

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
            remove_stale_pid_file();
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
    struct timespec sleep_time = {0, SLEEP_100MS_NS}; /* 100ms */
    int attempts               = 0;
    while (attempts < 50) { /* Wait up to 5 seconds */
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            printf("NeoWall daemon stopped successfully.\n");
            remove_stale_pid_file();
            return true;
        }
        nanosleep(&sleep_time, NULL);
        attempts++;
    }

    /* Force kill if still running */
    printf("Daemon didn't stop gracefully, forcing...\n");
    if (kill(pid, SIGKILL) == 0) {
        printf("NeoWall daemon killed.\n");
        remove_stale_pid_file();
        return true;
    }

    log_error("Failed to kill daemon process");
    return false;
}

/* Send signal to running daemon */
/* Largest cycle_total across all outputs in the state file, or 0 if the state
 * file is absent or carries no usable cycle_total. This is the number of
 * wallpapers `set <index>` can address. */
static int state_max_cycle_total(void) {
    /* Use file-level locking (flock/fcntl) for cross-process synchronization.
     * We can't use the state_file_lock mutex here because this runs in a
     * separate process from the daemon; only file locks coordinate across
     * processes. */
    const char *state_path = get_state_file_path();
    FILE *fp               = fopen(state_path, "r");

    if (!fp) {
        return 0; /* No state file = unknown, let daemon handle it */
    }

    /* Acquire read lock on the file */
    int fd            = fileno(fp);
    struct flock lock = {
        .l_type   = F_RDLCK, /* Read lock */
        .l_whence = SEEK_SET,
        .l_start  = 0,
        .l_len    = 0 /* Lock entire file */
    };

    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        /* Failed to lock, but continue anyway (non-critical) */
        log_debug("Failed to lock state file for reading: %s", strerror(errno));
    }

    char line[NW_PATH_MAX];
    int max_cycle_total = 0;

    /* Check ALL outputs for cycle_total, not just the first one. The state file
     * contains multiple [output] sections; cycling is possible if ANY of them
     * has cycle_total > 1. (A previous version stopped at the first cycle_total
     * line, which could be 0 for a non-cycling output.) */
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "cycle_total=", 12) == 0) {
            /* The state file is on disk and can be truncated or hand-edited, so
             * its contents are untrusted input: parse defensively rather than
             * feeding an arbitrary digit string to atoi(). */
            const char *value = line + 12;
            long cycle_total  = 0;
            if (!neowall_parse_index(value, &cycle_total) || cycle_total > INT_MAX) {
                log_debug("Ignoring malformed cycle_total in state file: '%s'", value);
                continue;
            }
            if ((int)cycle_total > max_cycle_total) {
                max_cycle_total = (int)cycle_total;
            }
            /* Don't break - continue checking all outputs */
        }
    }

    /* Release lock */
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    fclose(fp);
    return max_cycle_total;
}

/* Check if cycling is possible: ANY output has more than one wallpaper. */
static bool can_cycle_wallpaper(void) {
    return state_max_cycle_total() > 1;
}

/* Ask the daemon over the control socket. Prints its answer and sets *rc.
 * Returns false only when no daemon answers on the socket (an older daemon
 * that predates it, or none at all): the caller then falls back to signals,
 * which still say "no daemon" properly. */
static bool ipc_command(const char *verb, const char *arg, int *rc) {
    char reply[NW_IPC_MAX_LINE];
    /* reload answers after the new config is applied, which can mean a
     * shader compile: a second or more on software GL or a loaded machine */
    int timeout_ms = strcmp(verb, "reload") == 0 ? 15000 : 3000;
    nw_ipc_status st =
        nw_ipc_request(proto_socket_path(), verb, arg, reply, sizeof(reply), timeout_ms);
    switch (st) {
    case NW_IPC_OK:
        if (reply[0]) printf("%s\n", reply);
        *rc = EXIT_SUCCESS;
        return true;
    case NW_IPC_ERR:
        fprintf(stderr, "%s\n", reply[0] ? reply : "the daemon refused the command");
        *rc = EXIT_FAILURE;
        return true;
    case NW_IPC_FAILED:
        fprintf(stderr, "The daemon didn't answer on %s\n", proto_socket_path());
        *rc = EXIT_FAILURE;
        return true;
    case NW_IPC_NO_DAEMON: break;
    }
    return false;
}

static bool send_daemon_signal(int signal, const char *action, bool check_cycle) {
    const char *pid_path = get_pid_file_path();
    FILE *fp             = fopen(pid_path, "r");

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
            remove_stale_pid_file();
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

/* Print which binary the running daemon is actually executing, and shout if it
 * differs from this CLI.
 *
 * A stale daemon is a silent failure mode: `neowall reload` succeeds, the
 * config is re-read, and a shader using newer uniforms renders as if nothing
 * is wrong — only frozen. Surfacing the running binary makes "you upgraded but
 * never restarted" a one-line diagnosis instead of an hour with /proc.
 *
 * Best-effort and silent when the daemon is not running or /proc is absent. */
static void report_daemon_version(void) {
    printf("  CLI:       v%s\n", NEOWALL_VERSION_STRING);

    const char *pid_path = get_pid_file_path();
    FILE *fp             = pid_path ? fopen(pid_path, "r") : NULL;
    if (!fp) return;

    long pid    = 0;
    int scanned = fscanf(fp, "%ld", &pid);
    fclose(fp);
    if (scanned != 1 || pid <= 0) return;

    if (kill((pid_t)pid, 0) != 0) return; /* not running */

    char exe_link[64];
    char exe_path[NW_PATH_MAX];
    snprintf(exe_link, sizeof(exe_link), "/proc/%ld/exe", pid);

    ssize_t len = readlink(exe_link, exe_path, sizeof(exe_path) - 1);
    if (len <= 0) return;
    exe_path[len] = '\0';

    printf("  Daemon:    PID %ld (%s)\n", pid, exe_path);

    /* Compare against our own binary. Different paths are not automatically
     * wrong (an installed daemon plus a dev CLI is a normal combination), but
     * it is exactly the situation where a shader silently misbehaves. */
    char self_path[NW_PATH_MAX];
    ssize_t self_len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (self_len <= 0) return;
    self_path[self_len] = '\0';

    if (strcmp(exe_path, self_path) != 0) {
        printf("\n  Note: the daemon is running a different binary than this CLI.\n");
        printf("        If a shader looks frozen or ignores new features, restart it:\n");
        printf("          neowall kill && neowall\n");
    }
}


/* ── controlling a running daemon ─────────────────────────────────────── */

/* Read the daemon's pid from its pid file. Prints why when there's none. */
static bool running_daemon_pid(pid_t *out) {
    FILE *fp = fopen(get_pid_file_path(), "r");
    if (!fp) {
        printf("No running neowall daemon found.\n");
        printf("Start the daemon first with: neowall\n");
        return false;
    }
    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        fprintf(stderr, "Failed to read PID\n");
        return false;
    }
    fclose(fp);
    if (kill(pid, 0) == -1 && errno == ESRCH) {
        printf("NeoWall daemon (PID %d) is not running.\n", pid);
        remove_stale_pid_file();
        return false;
    }
    *out = pid;
    return true;
}

/* `set <index>` for a daemon without the control socket. */
static int cmd_set_index_signal(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s set <index>\n", argv[0]);
        fprintf(stderr, "  <index>  Wallpaper index (0-based)\n");
        fprintf(stderr, "\nUse '%s current' to see available wallpapers and their indices.\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    /* atoi() can't report failure and is undefined on overflow. */
    const char *index_str = argv[2];
    long parsed           = 0;
    if (!neowall_parse_index(index_str, &parsed) || parsed > INT_MAX) {
        fprintf(stderr, "Error: Index must be a non-negative integer, got '%s'\n", index_str);
        return EXIT_FAILURE;
    }

    pid_t pid;
    if (!running_daemon_pid(&pid)) return EXIT_FAILURE;

    int cycle_total = state_max_cycle_total();
    if (cycle_total <= 1) {
        printf("Cannot set wallpaper index: Only one wallpaper/shader configured.\n");
        return EXIT_FAILURE;
    }
    /* Otherwise the CLI reports success for an index no output can honour. */
    if (parsed >= cycle_total) {
        fprintf(stderr, "Error: Index %ld is out of range (0-%d)\n", parsed, cycle_total - 1);
        return EXIT_FAILURE;
    }
    int index = (int)parsed;

    if (!write_set_index_file(index)) return EXIT_FAILURE;
    if (kill(pid, SIGRTMIN) == -1) {
        fprintf(stderr, "Failed to send signal to daemon: %s\n", strerror(errno));
        unlink(get_set_index_file_path());
        return EXIT_FAILURE;
    }
    printf("Setting wallpaper to index %d...\n", index);
    return EXIT_SUCCESS;
}

/* `set-terminal <command...>` for a daemon without the control socket. The
 * daemon kills the old terminal's process group before spawning the new one. */
static int cmd_set_terminal_signal(int argc, char *argv[]) {
    if (argc < 3 || argv[2][0] == '\0') {
        fprintf(stderr, "Usage: %s set-terminal \"<command>\"\n", argv[0]);
        fprintf(stderr, "  <command>  Program to run as the terminal wallpaper,\n");
        fprintf(stderr, "             e.g. %s set-terminal \"htop\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Join argv[2..] so an unquoted `set-terminal htop --tree` still works. */
    char cmd_buf[512];
    size_t off = 0;
    for (int i = 2; i < argc; i++) {
        int w = snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "%s%s", (i > 2) ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof(cmd_buf) - off) {
            fprintf(stderr, "Error: terminal command too long\n");
            return EXIT_FAILURE;
        }
        off += (size_t)w;
    }
    /* Newlines would break the single-line command file protocol. */
    if (strchr(cmd_buf, '\n') || strchr(cmd_buf, '\r')) {
        fprintf(stderr, "Error: terminal command must not contain newlines\n");
        return EXIT_FAILURE;
    }

    pid_t pid;
    if (!running_daemon_pid(&pid)) return EXIT_FAILURE;
    if (!write_set_terminal_file(cmd_buf)) return EXIT_FAILURE;
    if (kill(pid, SIGRTMIN + 3) == -1) {
        fprintf(stderr, "Failed to send signal to daemon: %s\n", strerror(errno));
        unlink(get_set_terminal_file_path());
        return EXIT_FAILURE;
    }
    printf("Setting terminal wallpaper to: %s\n", cmd_buf);
    return EXIT_SUCCESS;
}

/* `neowall <command>` with a daemon running: the control socket first (one
 * request, one real answer), then the signal paths for an older daemon. */
int cmd_control(int argc, char *argv[]) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "ping") == 0) {
        /* For scripts: exit 0 only when a daemon answers. */
        int rc;
        if (ipc_command("ping", NULL, &rc)) return rc;
        fprintf(stderr, "No running neowall daemon\n");
        return EXIT_FAILURE;
    }

    static const char *const socket_cmds[] = {"next",         "pause",         "resume", "reload",
                                              "pause-shader", "resume-shader", NULL};
    for (size_t i = 0; socket_cmds[i]; i++) {
        int rc;
        if (strcmp(cmd, socket_cmds[i]) == 0 && ipc_command(cmd, NULL, &rc)) return rc;
    }
    if (strcmp(cmd, "set") == 0 && argc >= 3) {
        long parsed = 0;
        if (neowall_parse_index(argv[2], &parsed) && parsed <= INT_MAX) {
            int rc;
            if (ipc_command("set", argv[2], &rc)) return rc;
        }
    }
    if (strcmp(cmd, "set-terminal") == 0 && argc >= 3 && argv[2][0]) {
        int rc;
        if (ipc_command("set-terminal", argv[2], &rc)) return rc;
    }

    if (strcmp(cmd, "kill") == 0) return kill_daemon() ? EXIT_SUCCESS : EXIT_FAILURE;
    if (strcmp(cmd, "list") == 0) return read_cycle_list() ? EXIT_SUCCESS : EXIT_FAILURE;

    /* Real-time signals aren't compile-time constants, so these can't live
     * in the daemon_commands table. */
    if (strcmp(cmd, "pause-shader") == 0) {
        return send_daemon_signal(SIGRTMIN + 1, "Freezing shader animation...", false)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if (strcmp(cmd, "resume-shader") == 0) {
        return send_daemon_signal(SIGRTMIN + 2, "Resuming shader animation...", false)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if (strcmp(cmd, "set") == 0) return cmd_set_index_signal(argc, argv);
    if (strcmp(cmd, "set-terminal") == 0) return cmd_set_terminal_signal(argc, argv);

    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        if (strcmp(cmd, daemon_commands[i].name) != 0) continue;
        if (daemon_commands[i].needs_state_check) {
            bool ok = read_wallpaper_state();
            report_daemon_version();
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        return send_daemon_signal(daemon_commands[i].signal, daemon_commands[i].action_message,
                                  daemon_commands[i].check_cycle)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    fprintf(stderr, "Unknown command: %s\n\n", cmd);
    fprintf(stderr, "Available commands:\n");
    fprintf(stderr, "  kill");
    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        fprintf(stderr, ", %s", daemon_commands[i].name);
    }
    fprintf(stderr, ", ping, pause-shader, resume-shader, set-terminal");
    fprintf(stderr, "\n\nRun '%s --help' for more information.\n", argv[0]);
    return EXIT_FAILURE;
}

bool cli_control_command(size_t i, const char **name, const char **description) {
    for (size_t k = 0, n = 0; daemon_commands[k].name; k++) {
        if (strcmp(daemon_commands[k].name, "status") == 0) continue; /* alias */
        if (n++ == i) {
            *name        = daemon_commands[k].name;
            *description = daemon_commands[k].description;
            return true;
        }
    }
    return false;
}
