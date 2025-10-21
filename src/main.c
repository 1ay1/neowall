/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 *
 * Main entry point
 */

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

static struct staticwall_state *global_state = NULL;

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
    struct timespec sleep_time = {0, 100000000};  /* 100ms */
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

static void print_usage(const char *program_name) {
    printf("Staticwall v%s - Sets wallpapers until it... doesn't.\n\n", STATICWALL_VERSION);
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -c, --config PATH     Path to configuration file\n");
    printf("  -d, --daemon          Run as daemon (background process)\n");
    printf("  -f, --foreground      Run in foreground (default, explicit)\n");
    printf("  -w, --watch           Watch config file for changes and reload\n");
    printf("  -k, --kill            Stop running daemon\n");
    printf("  -v, --verbose         Enable verbose logging\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -V, --version         Show version information\n");
    printf("\n");
    printf("Note: By default, staticwall runs in foreground. Use -d for background.\n");
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

static void signal_handler(int signum) {
    switch (signum) {
        case SIGINT:
        case SIGTERM:
            log_info("Received signal %d, shutting down gracefully...", signum);
            if (global_state) {
                global_state->running = false;
            }
            break;
        case SIGHUP:
            log_info("Received SIGHUP, reloading configuration...");
            if (global_state) {
                global_state->reload_requested = true;
            }
            break;
        default:
            break;
    }
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

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
    struct staticwall_state state = {0};
    char config_path[MAX_PATH_LENGTH] = {0};
    bool daemon_mode = false;
    bool watch_config = false;
    int opt;

    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"daemon",     no_argument,       0, 'd'},
        {"foreground", no_argument,       0, 'f'},
        {"watch",      no_argument,       0, 'w'},
        {"kill",       no_argument,       0, 'k'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "c:dfwkvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                strncpy(config_path, optarg, sizeof(config_path) - 1);
                break;
            case 'd':
                daemon_mode = true;
                break;
            case 'f':
                daemon_mode = false;  /* Explicit foreground */
                break;
            case 'w':
                watch_config = true;
                break;
            case 'k':
                /* Kill daemon and exit */
                return kill_daemon() ? EXIT_SUCCESS : EXIT_FAILURE;
            case 'v':
                /* Verbose mode - would set log level */
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
            fscanf(fp, "%d", &existing_pid);
            fclose(fp);
        }
        log_error("Staticwall is already running (PID %d)", existing_pid);
        fprintf(stderr, "Error: Staticwall is already running (PID %d)\n", existing_pid);
        fprintf(stderr, "PID file: %s\n", pid_path);
        fprintf(stderr, "Use 'staticwall --kill' to stop the running instance.\n");
        return EXIT_FAILURE;
    }

    /* Daemonize if requested */
    if (daemon_mode) {
        log_info("Running as daemon...");
        if (!daemonize()) {
            return EXIT_FAILURE;
        }
    }

    /* Set up signal handlers */
    setup_signal_handlers();
    global_state = &state;

    /* Initialize state */
    state.running = true;
    state.watch_config = watch_config;
    strncpy(state.config_path, config_path, sizeof(state.config_path) - 1);
    state.config_path[sizeof(state.config_path) - 1] = '\0';
    pthread_mutex_init(&state.state_mutex, NULL);

    /* Initialize Wayland connection first */
    if (!wayland_init(&state)) {
        log_error("Failed to initialize Wayland");
        return EXIT_FAILURE;
    }

    /* Initialize EGL/OpenGL */
    if (!egl_init(&state)) {
        log_error("Failed to initialize EGL");
        wayland_cleanup(&state);
        return EXIT_FAILURE;
    }

    /* Load configuration and apply to outputs */
    if (!config_load(&state, config_path)) {
        log_error("Failed to load configuration");
        egl_cleanup(&state);
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

    egl_cleanup(&state);
    wayland_cleanup(&state);
    pthread_mutex_destroy(&state.state_mutex);

    /* Remove PID file if running as daemon */
    if (daemon_mode) {
        remove_pid_file();
    }

    log_info("Staticwall terminated successfully");

    return EXIT_SUCCESS;
}