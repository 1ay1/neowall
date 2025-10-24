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
#include "staticwall.h"
#include "constants.h"
#include "egl/egl_core.h"

static struct staticwall_state *global_state = NULL;

/* Forward declarations for signal handlers */
static void handle_shutdown(int signum);
static void handle_reload(int signum);
static void handle_next_wallpaper(int signum);
static void handle_pause(int signum);
static void handle_resume(int signum);
static void handle_shader_speed_adjust(int signum);
static void handle_crash(int signum);

/* Command descriptor structure */
typedef struct {
    const char *name;           /* Command name (e.g., "next") */
    int signal;                 /* Signal to send */
    const char *description;    /* Help text */
    const char *action_message; /* Message shown when executed */
    void (*handler)(int);       /* Signal handler function */
    bool needs_state_check;     /* Whether to check wallpaper state instead of signaling */
} DaemonCommand;

/* Special signal numbers for shader speed control (runtime-initialized) */
static int SHADER_SPEED_UP_SIGNAL = 0;
static int SHADER_SPEED_DOWN_SIGNAL = 0;

/* Centralized command registry - Single source of truth */
static DaemonCommand daemon_commands[] = {
    {"next",              SIGUSR1,      "Skip to next wallpaper",                  "Skipping to next wallpaper...",      handle_next_wallpaper,       false},
    {"pause",             SIGUSR2,      "Pause wallpaper cycling",                 "Pausing wallpaper cycling...",       handle_pause,                false},
    {"resume",            SIGCONT,      "Resume wallpaper cycling",                "Resuming wallpaper cycling...",      handle_resume,               false},
    {"reload",            SIGHUP,       "Reload configuration",                    "Reloading configuration...",         handle_reload,               false},
    {"shader_speed_up",   0,            "Increase shader animation speed by 1.0x", "Increasing shader speed...",         handle_shader_speed_adjust,  false},  /* Initialized at runtime */
    {"shader_speed_down", 0,            "Decrease shader animation speed by 1.0x", "Decreasing shader speed...",         handle_shader_speed_adjust,  false},  /* Initialized at runtime */
    {"current",           0,            "Show current wallpaper",                  NULL,                                 NULL,                        true},
    {"status",            0,            "Show current wallpaper",                  NULL,                                 NULL,                        true},
    {NULL, 0, NULL, NULL, NULL, false}  /* Sentinel */
};

/* Initialize runtime signal values */
static void init_command_signals(void) {
    SHADER_SPEED_UP_SIGNAL = SIGRTMIN;
    SHADER_SPEED_DOWN_SIGNAL = SIGRTMIN + 1;

    /* Update command table with runtime values */
    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        if (strcmp(daemon_commands[i].name, "shader_speed_up") == 0) {
            daemon_commands[i].signal = SHADER_SPEED_UP_SIGNAL;
        } else if (strcmp(daemon_commands[i].name, "shader_speed_down") == 0) {
            daemon_commands[i].signal = SHADER_SPEED_DOWN_SIGNAL;
        }
    }
}

/* Get PID file path */
static const char *get_pid_file_path(void) {
    static char pid_path[MAX_PATH_LENGTH];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(pid_path, sizeof(pid_path), "%s/staticwall.pid", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(pid_path, sizeof(pid_path), "%s/.staticwall.pid", home);
        } else {
            snprintf(pid_path, sizeof(pid_path), "/tmp/staticwall-%d.pid", getuid());
        }
    }

    return pid_path;
}

/* Write PID file */
static bool write_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "w");

    if (!fp) {
        log_error("Failed to create PID file %s: %s", pid_path, strerror(errno));
        return false;
    }

    fprintf(fp, "%d\n", getpid());
    fclose(fp);

    log_debug("Created PID file: %s", pid_path);
    return true;
}

/* Remove PID file */
static void remove_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    if (unlink(pid_path) == 0) {
        log_debug("Removed PID file: %s", pid_path);
    }
}

/* Check if daemon is already running */
static bool is_daemon_running(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) {
        /* No PID file, daemon not running */
        return false;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        /* Invalid PID file, clean it up */
        log_debug("Invalid PID file, removing");
        remove_pid_file();
        return false;
    }
    fclose(fp);

    /* Check if process exists */
    if (kill(pid, 0) == 0) {
        /* Process exists and we can signal it */
        return true;
    }

    if (errno == ESRCH) {
        /* Process doesn't exist, stale PID file */
        log_debug("Stale PID file found (PID %d not running), removing", pid);
        remove_pid_file();
        return false;
    }

    /* Other error (EPERM, etc.) - assume process exists */
    return true;
}

/* Kill running daemon */
static bool kill_daemon(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) {
        printf("No running staticwall daemon found (no PID file at %s)\n", pid_path);
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
            printf("Staticwall daemon (PID %d) is not running. Cleaning up stale PID file.\n", pid);
            remove_pid_file();
            return false;
        }
    }

    /* Send SIGTERM */
    printf("Stopping staticwall daemon (PID %d)...\n", pid);
    if (kill(pid, SIGTERM) == -1) {
        log_error("Failed to kill process %d: %s", pid, strerror(errno));
        return false;
    }

    /* Wait a bit for graceful shutdown */
    struct timespec sleep_time = {0, SLEEP_100MS_NS};  /* 100ms */
    int attempts = 0;
    while (attempts < 50) {  /* Wait up to 5 seconds */
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            printf("Staticwall daemon stopped successfully.\n");
            remove_pid_file();
            return true;
        }
        nanosleep(&sleep_time, NULL);
        attempts++;
    }

    /* Force kill if still running */
    printf("Daemon didn't stop gracefully, forcing...\n");
    if (kill(pid, SIGKILL) == 0) {
        printf("Staticwall daemon killed.\n");
        remove_pid_file();
        return true;
    }

    log_error("Failed to kill daemon process");
    return false;
}

/* Send signal to running daemon */
static bool send_daemon_signal(int signal, const char *action) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) {
        printf("No running staticwall daemon found.\n");
        printf("Start the daemon first with: staticwall\n");
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
            printf("Staticwall daemon (PID %d) is not running.\n", pid);
            remove_pid_file();
            return false;
        }
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
    printf("Staticwall v%s - Sets wallpapers until it... doesn't.\n\n", STATICWALL_VERSION);
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -c, --config PATH     Path to configuration file\n");
    printf("  -f, --foreground      Run in foreground (for debugging)\n");
    printf("  -w, --watch           Watch config file for changes and reload\n");
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
    printf("\n");
    printf("Note: By default, staticwall runs as a daemon. Use -f for foreground.\n");
    printf("If a daemon is already running, subsequent calls act as control commands.\n");
    printf("\n");
    printf("Configuration file locations (in order of preference):\n");
    printf("  1. $XDG_CONFIG_HOME/staticwall/config.vibe\n");
    printf("  2. $HOME/.config/staticwall/config.vibe\n");
    printf("  3. /etc/staticwall/config.vibe\n");
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
    printf("Staticwall v%s\n", STATICWALL_VERSION);
    printf("Sets wallpapers until it... doesn't.\n");
    printf("(Statically compiled, dynamically cycling. We contain multitudes.)\n");
    printf("\nSupported features:\n");
    printf("  - Wayland (wlr-layer-shell)\n");
    printf("  - Multi-monitor support\n");
    printf("  - Multiple display modes (center, stretch, fit, fill, tile)\n");
    printf("  - Hot-reload configuration\n");
    printf("  - Wallpaper transitions (the irony is not lost on us)\n");
    printf("\nSupported image formats:\n");
    printf("  - PNG\n");
    printf("  - JPEG/JPG\n");
}

/* ============================================================================
 * Signal Handler Implementations - Each handler has single responsibility
 * ============================================================================ */

static void handle_shutdown(int signum) {
    log_info("Received signal %d, shutting down gracefully...", signum);
    if (global_state) {
        global_state->running = false;
    }
}

/* Handle crash signals (SIGSEGV, SIGBUS, etc.) */
static void handle_crash(int signum) {
    const char *signame = "UNKNOWN";
    switch (signum) {
        case SIGSEGV: signame = "SIGSEGV (Segmentation fault)"; break;
        case SIGBUS: signame = "SIGBUS (Bus error)"; break;
        case SIGILL: signame = "SIGILL (Illegal instruction)"; break;
        case SIGFPE: signame = "SIGFPE (Floating point exception)"; break;
        case SIGABRT: signame = "SIGABRT (Abort)"; break;
    }
    
    log_error("CRASH: Received %s (signal %d)", signame, signum);
    log_error("This likely occurred due to GPU/display disconnection or driver issue");
    log_error("Error count: %lu, Frames rendered: %lu", 
              global_state ? global_state->errors_count : 0,
              global_state ? global_state->frames_rendered : 0);
    
    /* Log backtrace hint */
    log_error("To get a backtrace, run: gdb -p %d", getpid());
    log_error("Then use 'bt' command in gdb");
    
    /* Try to cleanup gracefully */
    if (global_state) {
        log_error("Attempting graceful shutdown...");
        global_state->running = false;
    }
    
    /* Exit with error code */
    exit(EXIT_FAILURE);
}

static void handle_reload(int signum) {
    (void)signum;  /* Unused */
    log_info("Received SIGHUP, reloading configuration...");
    if (global_state) {
        global_state->reload_requested = true;
    }
}

static void handle_next_wallpaper(int signum) {
    (void)signum;  /* Unused */
    if (!global_state) {
        log_error("Received SIGUSR1 but global_state is NULL");
        return;
    }

    int old_count = atomic_fetch_add(&global_state->next_requested, 1);
    int new_count = old_count + 1;
    log_info("Received SIGUSR1, skipping to next wallpaper (queue: %d -> %d)",
             old_count, new_count);

    /* Prevent counter overflow from rapid signals */
    if (new_count > MAX_NEXT_REQUESTS) {
        log_error("Too many queued next requests (%d), resetting to 10", new_count);
        atomic_store(&global_state->next_requested, 10);
    }
}

static void handle_pause(int signum) {
    (void)signum;  /* Unused */
    log_info("Received SIGUSR2, pausing wallpaper cycling...");
    if (global_state) {
        global_state->paused = true;
        log_info("Wallpaper cycling paused");
    }
}

static void handle_resume(int signum) {
    (void)signum;  /* Unused */
    log_info("Received SIGCONT, resuming wallpaper cycling...");
    if (global_state) {
        global_state->paused = false;
        log_info("Wallpaper cycling resumed");
    }
}

/* Helper function to adjust shader speed - DRY principle */
static void adjust_shader_speed_for_all_outputs(float delta) {
    if (!global_state) {
        return;
    }

    struct output_state *output = global_state->outputs;
    while (output) {
        if (output->config.type == WALLPAPER_SHADER) {
            output->config.shader_speed += delta;

            /* Enforce minimum speed limit */
            if (output->config.shader_speed < 0.1f) {
                output->config.shader_speed = 0.1f;
            }

            const char *direction = (delta > 0) ? "Increased" : "Decreased";
            log_info("%s shader speed to %.1fx for output %s",
                     direction,
                     output->config.shader_speed,
                     output->model[0] ? output->model : "unknown");
        }
        output = output->next;
    }
}

static void handle_shader_speed_adjust(int signum) {
    if (signum == SHADER_SPEED_UP_SIGNAL) {
        adjust_shader_speed_for_all_outputs(1.0f);   /* Speed up */
    } else if (signum == SHADER_SPEED_DOWN_SIGNAL) {
        adjust_shader_speed_for_all_outputs(-1.0f);  /* Slow down */
    }
}

/* ============================================================================
 * Unified Signal Handler - Dispatcher using table lookup
 * ============================================================================ */

static void signal_handler(int signum) {
    /* Handle shutdown signals directly */
    if (signum == SIGINT || signum == SIGTERM) {
        handle_shutdown(signum);
        return;
    }

    /* Dispatch to appropriate handler via command table */
    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        if (daemon_commands[i].signal == signum && daemon_commands[i].handler) {
            daemon_commands[i].handler(signum);
            return;
        }
    }

    /* Unknown signal - log it */
    log_debug("Received unknown signal: %d", signum);
}

/* ============================================================================
 * Signal Handler Registration - Table-driven, automatically covers all commands
 * ============================================================================ */

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  /* Restart interrupted system calls */

    /* Register shutdown signals */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    /* Register crash handlers with separate handler */
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

    /* Register all command signals from the table - DRY principle */
    for (size_t i = 0; daemon_commands[i].name != NULL; i++) {
        if (daemon_commands[i].signal > 0) {
            /* SIGHUP (reload) should NOT use SA_RESTART - we want it to interrupt poll() */
            if (daemon_commands[i].signal == SIGHUP) {
                sa.sa_flags = 0;
            }

            sigaction(daemon_commands[i].signal, &sa, NULL);

            /* Restore SA_RESTART for other signals */
            if (daemon_commands[i].signal == SIGHUP) {
                sa.sa_flags = SA_RESTART;
            }
        }
    }

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
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

    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Redirect to /dev/null */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }

    /* Write PID file */
    if (!write_pid_file()) {
        log_error("Failed to write PID file, but continuing anyway");
    }

    return true;
}

static bool create_config_directory(void) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    char config_dir[MAX_PATH_LENGTH];

    if (config_home) {
        snprintf(config_dir, sizeof(config_dir), "%s/staticwall", config_home);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot determine home directory");
            return false;
        }
        snprintf(config_dir, sizeof(config_dir), "%s/.config/staticwall", home);
    }

    struct stat st;
    if (stat(config_dir, &st) == -1) {
        if (mkdir(config_dir, 0755) == -1) {
            log_error("Failed to create config directory %s: %s",
                     config_dir, strerror(errno));
            return false;
        }
        log_info("Created config directory: %s", config_dir);
    }

    return true;
}

int main(int argc, char *argv[]) {
    /* Initialize runtime signal values for command table */
    init_command_signals();

    struct staticwall_state state = {0};
    char config_path[MAX_PATH_LENGTH] = {0};
    bool daemon_mode = true;  /* Default to daemon mode */
    bool watch_config = true;  /* Enable by default for better UX */
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
                                            daemon_commands[i].action_message)
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
        fprintf(stderr, "\n\nRun '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"foreground", no_argument,       0, 'f'},
        {"watch",      no_argument,       0, 'w'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "c:fwvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                strncpy(config_path, optarg, sizeof(config_path) - 1);
                break;
            case 'f':
                daemon_mode = false;  /* Explicit foreground */
                break;
            case 'w':
                watch_config = true;
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
    log_info("Staticwall v%s starting...", STATICWALL_VERSION);

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

    /* Check if already running */
    if (is_daemon_running()) {
        const char *pid_path = get_pid_file_path();
        FILE *fp = fopen(pid_path, "r");
        pid_t existing_pid = 0;
        if (fp) {
            if (fscanf(fp, "%d", &existing_pid) != 1) {
                existing_pid = 0;
            }
            fclose(fp);
        }
        log_error("Staticwall is already running (PID %d)", existing_pid);
        fprintf(stderr, "Error: Staticwall is already running (PID %d)\n", existing_pid);
        fprintf(stderr, "PID file: %s\n", pid_path);
        fprintf(stderr, "Use 'staticwall kill' to stop the running instance.\n");
        return EXIT_FAILURE;
    }

    /* Daemonize if requested */
    if (daemon_mode) {
        log_info("Running as daemon...");
        if (!daemonize()) {
            return EXIT_FAILURE;
        }
    } else {
        /* In foreground mode, still write PID file for client commands */
        if (!write_pid_file()) {
            log_error("Failed to write PID file, but continuing anyway");
        }
    }

    /* Set up signal handlers */
    setup_signal_handlers();
    global_state = &state;

    /* Initialize state */
    state.running = true;
    state.reload_requested = false;
    state.paused = false;
    state.outputs_need_init = false;
    atomic_init(&state.next_requested, 0);
    state.watch_config = watch_config;
    state.timer_fd = -1;
    state.wakeup_fd = -1;
    strncpy(state.config_path, config_path, sizeof(state.config_path) - 1);
    state.config_path[sizeof(state.config_path) - 1] = '\0';
    pthread_mutex_init(&state.state_mutex, NULL);

    /* Initialize Wayland connection first */
    if (!wayland_init(&state)) {
        log_error("Failed to initialize Wayland");
        return EXIT_FAILURE;
    }

    /* Initialize EGL/OpenGL */
    if (!egl_core_init(&state)) {
        log_error("Failed to initialize EGL");
        wayland_cleanup(&state);
        return EXIT_FAILURE;
    }

    /* Load configuration and apply to outputs */
    if (!config_load(&state, config_path)) {
        log_error("Failed to load configuration");
        egl_core_cleanup(&state);
        wayland_cleanup(&state);
        return EXIT_FAILURE;
    }

    log_info("Initialization complete, entering main loop...");

    /* Start config watcher thread if requested */
    if (watch_config) {
        log_info("Starting configuration file watcher...");
        pthread_create(&state.watch_thread, NULL, config_watch_thread, &state);
    }

    /* Run main event loop */
    event_loop_run(&state);

    /* Cleanup */
    log_info("Shutting down...");

    if (watch_config) {
        pthread_cancel(state.watch_thread);
        pthread_join(state.watch_thread, NULL);
    }

    egl_core_cleanup(&state);
    wayland_cleanup(&state);
    pthread_mutex_destroy(&state.state_mutex);

    /* Remove PID file */
    remove_pid_file();

    log_info("Staticwall terminated successfully");

    return EXIT_SUCCESS;
}
