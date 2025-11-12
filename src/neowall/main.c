/*
 * NeoWall Client - Unified CLI using IPC sockets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include "../ipc/socket.h"
#include "../ipc/protocol.h"

#define NEOWALL_VERSION "0.3.0"

/* Global flags */
static bool flag_json_output = false;

/* Command handlers */
typedef int (*command_handler_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    const char *description;
    command_handler_t handler;
} Command;

/* Forward declarations */
static int cmd_start(int argc, char *argv[]);
static int cmd_stop(int argc, char *argv[]);
static int cmd_restart(int argc, char *argv[]);
static int cmd_status(int argc, char *argv[]);
static int cmd_next(int argc, char *argv[]);
static int cmd_pause(int argc, char *argv[]);
static int cmd_resume(int argc, char *argv[]);
static int cmd_reload(int argc, char *argv[]);
static int cmd_current(int argc, char *argv[]);
static int cmd_speed_up(int argc, char *argv[]);
static int cmd_speed_down(int argc, char *argv[]);
static int cmd_shader_pause(int argc, char *argv[]);
static int cmd_shader_resume(int argc, char *argv[]);
static int cmd_config(int argc, char *argv[]);
static int cmd_tray(int argc, char *argv[]);
static int cmd_version(int argc, char *argv[]);
static int cmd_help(int argc, char *argv[]);

/* Command registry */
static Command commands[] = {
    {"start",         "Start the neowalld daemon",              cmd_start},
    {"stop",          "Stop the neowalld daemon",               cmd_stop},
    {"restart",       "Restart the neowalld daemon",            cmd_restart},
    {"status",        "Show daemon status",                     cmd_status},
    {"next",          "Switch to next wallpaper",               cmd_next},
    {"pause",         "Pause wallpaper cycling",                cmd_pause},
    {"resume",        "Resume wallpaper cycling",               cmd_resume},
    {"reload",        "Reload configuration",                   cmd_reload},
    {"current",       "Show current wallpaper",                 cmd_current},
    {"speed-up",      "Increase shader animation speed",        cmd_speed_up},
    {"speed-down",    "Decrease shader animation speed",        cmd_speed_down},
    {"shader-pause",  "Pause shader animation",                 cmd_shader_pause},
    {"shader-resume", "Resume shader animation",                cmd_shader_resume},
    {"config",        "Edit configuration",                     cmd_config},
    {"tray",          "Launch system tray application",         cmd_tray},
    {"version",       "Show version information",               cmd_version},
    {"help",          "Show this help message",                 cmd_help},
    {NULL, NULL, NULL}
};

/* Get PID file path */
static const char *get_pid_file_path(void) {
    static char path[256];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowalld.pid", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowalld.pid", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/neowalld-%d.pid", getuid());
        }
    }

    return path;
}

/* Read daemon PID */
static pid_t read_daemon_pid(void) {
    const char *pid_path = get_pid_file_path();
    FILE *fp = fopen(pid_path, "r");

    if (!fp) return -1;

    pid_t pid = -1;
    if (fscanf(fp, "%d", &pid) != 1) pid = -1;
    fclose(fp);

    /* Verify process exists */
    if (pid > 0 && kill(pid, 0) != 0) {
        unlink(pid_path);  /* Stale PID file */
        return -1;
    }

    return pid;
}

/* Check if daemon is running */
static bool is_daemon_running(void) {
    return (read_daemon_pid() > 0);
}

/* Get IPC socket path */
static const char *get_ipc_socket_path(void) {
    static char path[256];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowalld.sock", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowalld.sock", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/neowalld-%d.sock", getuid());
        }
    }

    return path;
}

/* Get ready marker file path */
static const char *get_ready_marker_path(void) {
    static char path[256];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowalld.ready", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowalld.ready", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/neowalld-%d.ready", getuid());
        }
    }

    return path;
}

/* Check if daemon is fully initialized and ready */
static bool is_daemon_ready(void) {
    if (!is_daemon_running()) {
        return false;
    }

    /* Check if ready marker exists (daemon fully initialized) */
    const char *ready_path = get_ready_marker_path();
    return (access(ready_path, F_OK) == 0);
}

/* Send IPC command to daemon */
static bool send_ipc_command(const char *command, char *response_data, size_t data_size) {
    ipc_client_t *client = ipc_client_connect(NULL);
    if (!client) {
        if (!flag_json_output) {
            fprintf(stderr, "Error: Cannot connect to daemon\n");
        } else {
            printf("{\"status\":\"error\",\"message\":\"Cannot connect to daemon\"}\n");
        }
        return false;
    }

    ipc_request_t req = {0};
    strncpy(req.command, command, sizeof(req.command) - 1);
    strcpy(req.args, "{}");

    ipc_response_t resp;
    bool success = ipc_client_send(client, &req, &resp);

    ipc_client_close(client);

    if (!success) {
        if (!flag_json_output) {
            fprintf(stderr, "Error: Failed to send command\n");
        } else {
            printf("{\"status\":\"error\",\"message\":\"Failed to send command\"}\n");
        }
        return false;
    }

    if (flag_json_output) {
        /* Output full JSON response */
        printf("{\"status\":\"%s\",\"message\":\"%s\",\"data\":%s}\n",
               resp.status == IPC_STATUS_OK ? "ok" : "error",
               resp.message[0] ? resp.message : "",
               resp.data[0] ? resp.data : "null");
        return resp.status == IPC_STATUS_OK;
    }

    if (resp.status != IPC_STATUS_OK) {
        fprintf(stderr, "Error: %s\n", resp.message[0] ? resp.message : "Command failed");
        return false;
    }

    if (response_data && data_size > 0 && resp.data[0]) {
        strncpy(response_data, resp.data, data_size - 1);
    }

    if (resp.message[0]) {
        printf("%s\n", resp.message);
    }

    return true;
}

/* Command implementations */

/* Find daemon binary path */
static const char *find_daemon_path(void) {
    static char daemon_path[512];

    /* Try 1: Same directory as client binary */
    ssize_t len = readlink("/proc/self/exe", daemon_path, sizeof(daemon_path) - 20);
    if (len > 0) {
        daemon_path[len] = '\0';
        char *last_slash = strrchr(daemon_path, '/');
        if (last_slash) {
            strcpy(last_slash + 1, "neowalld");
            if (access(daemon_path, X_OK) == 0) {
                return daemon_path;
            }
        }
    }

    /* Try 2: Common installation paths */
    const char *install_paths[] = {
        "/usr/local/bin/neowalld",
        "/usr/bin/neowalld",
        NULL
    };

    for (int i = 0; install_paths[i]; i++) {
        if (access(install_paths[i], X_OK) == 0) {
            return install_paths[i];
        }
    }

    /* Try 3: Check PATH */
    return "neowalld";
}

static int cmd_start(int argc, char *argv[]) {
    if (is_daemon_running()) {
        printf("neowalld is already running (PID %d)\n", read_daemon_pid());
        return 0;
    }

    const char *daemon_path = find_daemon_path();

    printf("Starting neowalld...\n");

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: Failed to fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        /* Child - exec daemon */
        char **daemon_args = malloc(sizeof(char*) * (argc + 2));
        daemon_args[0] = (char*)daemon_path;
        for (int i = 1; i < argc; i++) {
            daemon_args[i] = argv[i];
        }
        daemon_args[argc] = NULL;

        execvp(daemon_path, daemon_args);

        fprintf(stderr, "Error: Failed to execute neowalld: %s\n", strerror(errno));
        exit(1);
    }

    /* Parent - wait and check with retry */
    bool pid_created = false;
    bool socket_ready = false;

    for (int i = 0; i < 50; i++) {
        usleep(200000);  /* Wait 200ms between checks (10 second total timeout) */

        if (!pid_created && is_daemon_running()) {
            pid_created = true;
        }

        if (pid_created && is_daemon_ready()) {
            socket_ready = true;
            break;
        }

        /* If PID file disappeared after being created, daemon crashed */
        if (pid_created && !is_daemon_running()) {
            fprintf(stderr, "✗ Daemon started but crashed during initialization\n");
            fprintf(stderr, "   Check logs or run with: neowalld -f\n");
            return 1;
        }
    }

    if (socket_ready) {
        printf("✓ neowalld started successfully (PID %d)\n", read_daemon_pid());
        return 0;
    } else if (pid_created) {
        fprintf(stderr, "✗ Daemon started but IPC socket not ready (timeout)\n");
        fprintf(stderr, "   The daemon may still be initializing or failed to start\n");
        fprintf(stderr, "   Check status with: neowall status\n");
        return 1;
    } else {
        fprintf(stderr, "✗ Failed to start neowalld (PID file not created)\n");
        return 1;
    }
}



static int cmd_stop(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (!is_daemon_running()) {
        printf("neowalld is not running\n");
        return 0;
    }

    pid_t pid = read_daemon_pid();
    printf("Stopping neowalld (PID %d)...\n", pid);

    if (kill(pid, SIGTERM) != 0) {
        fprintf(stderr, "Error: Failed to signal daemon: %s\n", strerror(errno));
        return 1;
    }

    /* Wait for daemon to exit */
    for (int i = 0; i < 10; i++) {
        if (!is_daemon_running()) {
            printf("✓ neowalld stopped\n");
            return 0;
        }
        usleep(500000);
    }

    /* Force kill */
    if (is_daemon_running()) {
        fprintf(stderr, "Daemon not responding, sending SIGKILL...\n");
        kill(pid, SIGKILL);
        sleep(1);

        if (!is_daemon_running()) {
            printf("✓ neowalld killed\n");
            return 0;
        } else {
            fprintf(stderr, "✗ Failed to stop neowalld\n");
            return 1;
        }
    }

    return 0;
}

static int cmd_restart(int argc, char *argv[]) {
    printf("Restarting neowalld...\n");
    cmd_stop(0, NULL);
    sleep(1);
    return cmd_start(argc, argv);
}

static int cmd_status(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (!is_daemon_running()) {
        if (flag_json_output) {
            printf("{\"status\":\"error\",\"message\":\"neowalld is not running\",\"running\":false}\n");
        } else {
            printf("● neowalld is not running\n");
        }
        return 3;
    }

    char data[2048];
    if (send_ipc_command("status", data, sizeof(data))) {
        if (!flag_json_output) {
            printf("● neowalld is running\n");
            /* TODO: Parse and display JSON data nicely */
        }
        return 0;
    }

    return 1;
}

static int cmd_next(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("next", NULL, 0) ? 0 : 1;
}

static int cmd_pause(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("pause", NULL, 0) ? 0 : 1;
}

static int cmd_resume(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("resume", NULL, 0) ? 0 : 1;
}

static int cmd_reload(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("reload", NULL, 0) ? 0 : 1;
}

static int cmd_current(int argc, char *argv[]) {
    return cmd_status(argc, argv);
}

static int cmd_speed_up(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("speed-up", NULL, 0) ? 0 : 1;
}

static int cmd_speed_down(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("speed-down", NULL, 0) ? 0 : 1;
}

static int cmd_shader_pause(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("shader-pause", NULL, 0) ? 0 : 1;
}

static int cmd_shader_resume(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("shader-resume", NULL, 0) ? 0 : 1;
}

static int cmd_config(int argc, char *argv[]) {
    (void)argc; (void)argv;

    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char config_path[256];

    if (config_home) {
        snprintf(config_path, sizeof(config_path), "%s/neowall/config.vibe", config_home);
    } else if (home) {
        snprintf(config_path, sizeof(config_path), "%s/.config/neowall/config.vibe", home);
    } else {
        fprintf(stderr, "Error: Cannot determine config path\n");
        return 1;
    }

    const char *editor = getenv("EDITOR");
    if (!editor) editor = getenv("VISUAL");
    if (!editor) editor = "nano";

    printf("Opening config file with %s...\n", editor);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s '%s'", editor, config_path);

    int ret = system(cmd);

    if (ret == 0 && is_daemon_running()) {
        printf("\nReload configuration? [Y/n] ");
        char response[10];
        if (fgets(response, sizeof(response), stdin)) {
            if (response[0] != 'n' && response[0] != 'N') {
                return cmd_reload(0, NULL);
            }
        }
    }

    return ret;
}

static int cmd_tray(int argc, char *argv[]) {
    if (!is_daemon_running()) {
        fprintf(stderr, "Warning: neowalld is not running\n");
        printf("Start daemon first? [Y/n] ");
        char response[10];
        if (fgets(response, sizeof(response), stdin)) {
            if (response[0] != 'n' && response[0] != 'N') {
                if (cmd_start(0, NULL) != 0) {
                    return 1;
                }
            }
        }
    }

    printf("Launching system tray application...\n");
    execvp("neowall-tray", argv);

    fprintf(stderr, "Error: Failed to launch neowall-tray: %s\n", strerror(errno));
    return 1;
}

static int cmd_version(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("NeoWall %s\n", NEOWALL_VERSION);
    printf("GPU-accelerated shader wallpapers for Wayland\n\n");
    printf("License: MIT\n");
    printf("Website: https://github.com/1ay1/neowall\n");

    return 0;
}

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("NeoWall %s - GPU-accelerated shader wallpapers for Wayland\n\n", NEOWALL_VERSION);
    printf("Usage: neowall [OPTIONS] [COMMAND]\n\n");
    printf("Global Options:\n");
    printf("  --json             Output in JSON format (for scripting)\n");
    printf("\n");
    printf("Daemon Management:\n");
    printf("  start              Start the neowalld daemon\n");
    printf("  stop               Stop the neowalld daemon\n");
    printf("  restart            Restart the daemon\n");
    printf("  status             Show daemon status\n");
    printf("\n");
    printf("Wallpaper Control:\n");
    printf("  next               Switch to next wallpaper\n");
    printf("  pause              Pause wallpaper cycling\n");
    printf("  resume             Resume wallpaper cycling\n");
    printf("  current            Show current wallpaper\n");
    printf("  reload             Reload configuration\n");
    printf("\n");
    printf("Shader Control:\n");
    printf("  speed-up           Increase shader animation speed\n");
    printf("  speed-down         Decrease shader animation speed\n");
    printf("  shader-pause       Pause shader animation\n");
    printf("  shader-resume      Resume shader animation\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  config             Edit configuration file\n");
    printf("  tray               Launch system tray application\n");
    printf("\n");
    printf("Information:\n");
    printf("  version            Show version information\n");
    printf("  help               Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  neowall start              # Start the daemon\n");
    printf("  neowall next               # Switch wallpaper\n");
    printf("  neowall --json status      # Get status in JSON format\n");
    printf("  neowall tray &             # Launch tray in background\n");
    printf("\n");
    printf("Report bugs: https://github.com/1ay1/neowall/issues\n");

    return 0;
}

/* Main entry point */
int main(int argc, char *argv[]) {
    int arg_offset = 1;

    /* Parse global flags */
    while (arg_offset < argc && argv[arg_offset][0] == '-' && argv[arg_offset][1] == '-') {
        if (strcmp(argv[arg_offset], "--json") == 0) {
            flag_json_output = true;
            arg_offset++;
        } else if (strcmp(argv[arg_offset], "--help") == 0) {
            return cmd_help(0, NULL);
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[arg_offset]);
            fprintf(stderr, "Run 'neowall help' for usage information\n");
            return 1;
        }
    }

    if (arg_offset >= argc) {
        if (is_daemon_running()) {
            return cmd_status(0, NULL);
        } else {
            if (flag_json_output) {
                printf("{\"status\":\"error\",\"message\":\"neowalld is not running\",\"running\":false}\n");
            } else {
                printf("neowalld is not running. Start it with: neowall start\n\n");
                return cmd_help(0, NULL);
            }
            return 3;
        }
    }

    const char *cmd_name = argv[arg_offset];

    /* Find and execute command */
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(cmd_name, commands[i].name) == 0) {
            return commands[i].handler(argc - arg_offset - 1, argv + arg_offset);
        }
    }

    if (flag_json_output) {
        printf("{\"status\":\"error\",\"message\":\"Unknown command '%s'\"}\n", cmd_name);
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", cmd_name);
        fprintf(stderr, "Run 'neowall help' for usage information\n");
    }
    return 1;
}
