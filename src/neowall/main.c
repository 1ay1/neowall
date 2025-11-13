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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "../ipc/socket.h"
#include "../ipc/protocol.h"

#include <version.h>

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
static void format_data_output(const char *json_data);
int cmd_start(int argc, char *argv[]);
int cmd_stop(int argc, char *argv[]);
int cmd_restart(int argc, char *argv[]);
int cmd_status(int argc, char *argv[]);
int cmd_next(int argc, char *argv[]);
int cmd_prev(int argc, char *argv[]);
int cmd_pause(int argc, char *argv[]);
int cmd_resume(int argc, char *argv[]);
int cmd_reload(int argc, char *argv[]);
int cmd_current(int argc, char *argv[]);
int cmd_speed_up(int argc, char *argv[]);
int cmd_speed_down(int argc, char *argv[]);
int cmd_shader_pause(int argc, char *argv[]);
int cmd_shader_resume(int argc, char *argv[]);
int cmd_config(int argc, char *argv[]);
int cmd_tray(int argc, char *argv[]);
int cmd_version(int argc, char *argv[]);
int cmd_ping(int argc, char *argv[]);
int cmd_help(int argc, char *argv[]);

/* Output-specific command handlers */
int cmd_list_outputs(int argc, char *argv[]);
int cmd_output_info(int argc, char *argv[]);
int cmd_next_output(int argc, char *argv[]);
int cmd_prev_output(int argc, char *argv[]);
int cmd_reload_output(int argc, char *argv[]);
int cmd_pause_output(int argc, char *argv[]);
int cmd_resume_output(int argc, char *argv[]);
int cmd_jump_to_output(int argc, char *argv[]);
int cmd_set_output_mode(int argc, char *argv[]);
int cmd_set_output_wallpaper(int argc, char *argv[]);
int cmd_set_output_interval(int argc, char *argv[]);
int cmd_jump_output(int argc, char *argv[]);

/* Config command handlers */
int cmd_get_config(int argc, char *argv[]);
int cmd_set_config(int argc, char *argv[]);
int cmd_reset_config(int argc, char *argv[]);
int cmd_list_config_keys(int argc, char *argv[]);
int cmd_list_commands(int argc, char *argv[]);

/* Command registry */
static Command commands[] = {
    {"start",              "Start the neowalld daemon",              cmd_start},
    {"stop",               "Stop the neowalld daemon",               cmd_stop},
    {"restart",            "Restart the neowalld daemon",            cmd_restart},
    {"status",             "Show daemon status",                     cmd_status},
    {"next",               "Switch to next wallpaper (all outputs)", cmd_next},
    {"prev",               "Switch to previous wallpaper (all outputs)", cmd_prev},
    {"pause",              "Pause wallpaper cycling (all outputs)",  cmd_pause},
    {"resume",             "Resume wallpaper cycling (all outputs)", cmd_resume},
    {"reload",             "Reload configuration",                   cmd_reload},
    {"current",            "Show current wallpaper [output]",        cmd_current},
    {"speed-up",           "Increase shader animation speed [output]", cmd_speed_up},
    {"speed-down",         "Decrease shader animation speed [output]", cmd_speed_down},
    {"shader-pause",       "Pause shader animation [output]",        cmd_shader_pause},
    {"shader-resume",      "Resume shader animation [output]",       cmd_shader_resume},

    /* Output-specific commands */
    {"list-outputs",       "List all connected outputs",             cmd_list_outputs},
    {"output-info",        "Get info about specific output",         cmd_output_info},
    {"next-output",        "Next wallpaper on output",               cmd_next_output},
    {"prev-output",        "Previous wallpaper on output",           cmd_prev_output},
    {"reload-output",      "Reload wallpaper on output",             cmd_reload_output},
    {"pause-output",       "Pause cycling on output",                cmd_pause_output},
    {"resume-output",      "Resume cycling on output",               cmd_resume_output},
    {"jump-to-output",     "Jump to cycle index on output",          cmd_jump_to_output},
    {"set-output-mode",    "Set mode for output",                    cmd_set_output_mode},
    {"set-output-wallpaper", "Set wallpaper for output",             cmd_set_output_wallpaper},
    {"set-output-interval", "Set cycle interval for output",         cmd_set_output_interval},
    {"jump-output",        "Jump to cycle index on output (alias)",  cmd_jump_output},

    /* Config commands */
    {"get-config",         "Get configuration value",                cmd_get_config},
    {"set-config",         "Set configuration value",                cmd_set_config},
    {"reset-config",       "Reset configuration to defaults",        cmd_reset_config},
    {"list-config-keys",   "List all config keys",                   cmd_list_config_keys},

    /* Introspection */
    {"list-commands",      "List all available commands",            cmd_list_commands},
    {"ping",               "Ping daemon (health check)",             cmd_ping},

    {"config",             "Edit configuration",                     cmd_config},
    {"tray",               "Launch system tray application",         cmd_tray},
    {"version",            "Show version information",               cmd_version},
    {"help",               "Show this help message",                 cmd_help},
    {NULL, NULL, NULL}
};

/* Get PID file path - CACHED for blazing fast repeated calls */
static const char *get_pid_file_path(void) {
    static char path[256] = {0};

    /* Cache: only compute once */
    if (path[0] != '\0') {
        return path;
    }

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

/* Read daemon PID - OPTIMIZED for speed */
static pid_t read_daemon_pid(void) {
    const char *pid_path = get_pid_file_path();

    /* Fast path: use open() + read() instead of fopen() + fscanf() */
    int fd = open(pid_path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Fast integer parsing */
    pid_t pid = 0;
    for (ssize_t i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) {
        pid = pid * 10 + (buf[i] - '0');
    }

    if (pid <= 0) return -1;

    /* Verify process exists with kill(0) - blazing fast syscall */
    if (kill(pid, 0) != 0) {
        if (errno == ESRCH) {
            /* Process not found - remove stale PID file */
            unlink(pid_path);
        }
        return -1;
    }

    return pid;
}

/* Check if daemon is running - INLINED for zero function call overhead */
static inline bool is_daemon_running(void) {
    return (read_daemon_pid() > 0);
}

/* Get IPC socket path */
static const char *get_ipc_socket_path(void) __attribute__((unused));
static const char *get_ipc_socket_path(void) {
    static char path[256] = {0};

    /* Cache: only compute once */
    if (path[0] != '\0') {
        return path;
    }

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

/* Get ready marker file path - CACHED for blazing fast repeated calls */
static const char *get_ready_marker_path(void) {
    static char path[256] = {0};

    /* Cache: only compute once */
    if (path[0] != '\0') {
        return path;
    }

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

/* Check if daemon is fully initialized and ready - OPTIMIZED */
static inline bool is_daemon_ready(void) {
    /* Fast path: single syscall with access() is faster than separate checks */
    const char *ready_path = get_ready_marker_path();
    return (access(ready_path, F_OK) == 0);
}

/* Send IPC command to daemon - OPTIMIZED for minimal overhead */
static bool send_ipc_command(const char *command, const char *args_json, size_t args_len) {
    ipc_client_t *client = ipc_client_connect(NULL);
    if (!client) {
        if (!flag_json_output) {
            fprintf(stderr, "Error: Cannot connect to daemon\n");
        } else {
            printf("{\"status\":\"error\",\"message\":\"Cannot connect to daemon\"}\n");
        }
        return false;
    }

    /* Stack-allocated request - no malloc overhead */
    ipc_request_t req;
    memset(&req, 0, sizeof(req));

    /* Fast string copy - use memcpy instead of strncpy when we know the length */
    size_t cmd_len = strlen(command);
    if (cmd_len >= sizeof(req.command)) cmd_len = sizeof(req.command) - 1;
    memcpy(req.command, command, cmd_len);
    req.command[cmd_len] = '\0';

    /* Copy args or use empty object if none provided */
    if (args_json && args_len > 0 && args_len < sizeof(req.args) - 1) {
        memcpy(req.args, args_json, args_len);
        req.args[args_len] = '\0';
    } else {
        req.args[0] = '{';
        req.args[1] = '}';
        req.args[2] = '\0';
    }

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

    /* In non-JSON mode, display both message and data if available */
    if (resp.message[0] && strcmp(resp.message, "OK") != 0) {
        printf("%s\n", resp.message);
    }

    /* If there's data, display it formatted (but skip if it's just a message wrapper) */
    if (resp.data[0] && strcmp(resp.data, "{}") != 0) {
        /* Skip data that only contains a message (already displayed above) */
        if (strstr(resp.data, "\"message\":") && !strstr(resp.data, "\"outputs\"") &&
            !strstr(resp.data, "\"name\":") && !strstr(resp.data, "\"keys\":")) {
            /* This is just a message wrapper, skip it */
        } else {
            /* Try to format the JSON data for human readability */
            format_data_output(resp.data);
        }
    }

    return (resp.status == IPC_STATUS_OK);
}

/* Format JSON data for human-readable output */
static void format_data_output(const char *json_data) {
    if (!json_data || !json_data[0]) {
        return;
    }

    /* Simple JSON parsing for common data structures */
    /* This is a basic formatter - for complex formatting, commands should handle it themselves */

    /* Check if it's an outputs array (list-outputs command) */
    if (strstr(json_data, "\"outputs\":[")) {
        printf("\nConnected Outputs:\n");
        printf("─────────────────────────────────────────────────────────\n");

        /* Parse each output entry */
        const char *pos = json_data;
        while ((pos = strstr(pos, "\"name\":\"")) != NULL) {
            pos += 8; /* Skip "name":" */
            const char *name_end = strchr(pos, '"');
            if (!name_end) break;

            char name[128];
            size_t name_len = name_end - pos;
            if (name_len >= sizeof(name)) name_len = sizeof(name) - 1;
            memcpy(name, pos, name_len);
            name[name_len] = '\0';

            /* Extract model */
            const char *model_pos = strstr(pos, "\"model\":\"");
            char model[128] = "Unknown";
            if (model_pos && model_pos < pos + 500) {
                model_pos += 9;
                const char *model_end = strchr(model_pos, '"');
                if (model_end) {
                    size_t model_len = model_end - model_pos;
                    if (model_len >= sizeof(model)) model_len = sizeof(model) - 1;
                    memcpy(model, model_pos, model_len);
                    model[model_len] = '\0';
                }
            }

            /* Extract resolution */
            int width = 0, height = 0;
            const char *width_pos = strstr(pos, "\"width\":");
            if (width_pos && width_pos < pos + 500) {
                sscanf(width_pos + 8, "%d", &width);
            }
            const char *height_pos = strstr(pos, "\"height\":");
            if (height_pos && height_pos < pos + 500) {
                sscanf(height_pos + 9, "%d", &height);
            }

            /* Extract wallpaper info */
            const char *type_pos = strstr(pos, "\"wallpaper_type\":\"");
            char type[32] = "unknown";
            if (type_pos && type_pos < pos + 500) {
                type_pos += 18;
                const char *type_end = strchr(type_pos, '"');
                if (type_end) {
                    size_t type_len = type_end - type_pos;
                    if (type_len >= sizeof(type)) type_len = sizeof(type) - 1;
                    memcpy(type, type_pos, type_len);
                    type[type_len] = '\0';
                }
            }

            /* Extract cycle info */
            int cycle_index = 0, cycle_total = 0;
            const char *idx_pos = strstr(pos, "\"cycle_index\":");
            if (idx_pos && idx_pos < pos + 500) {
                sscanf(idx_pos + 14, "%d", &cycle_index);
            }
            const char *total_pos = strstr(pos, "\"cycle_total\":");
            if (total_pos && total_pos < pos + 500) {
                sscanf(total_pos + 14, "%d", &cycle_total);
            }

            printf("  • %s (%s)\n", name, model);
            printf("    Resolution: %dx%d\n", width, height);
            printf("    Wallpaper:  %s", type);
            if (cycle_total > 0) {
                printf(" [%d/%d]", cycle_index + 1, cycle_total);
            }
            printf("\n\n");

            pos = name_end + 1;
        }
        return;
    }

    /* Check if it's a single output info */
    if (strstr(json_data, "\"name\":\"") && strstr(json_data, "\"model\":\"")) {
        printf("\nOutput Information:\n");
        printf("─────────────────────────────────────────────────────────\n");

        /* Extract and display fields */
        const char *name_pos = strstr(json_data, "\"name\":\"");
        if (name_pos) {
            name_pos += 8;
            const char *name_end = strchr(name_pos, '"');
            if (name_end) {
                printf("  Name:       %.*s\n", (int)(name_end - name_pos), name_pos);
            }
        }

        const char *model_pos = strstr(json_data, "\"model\":\"");
        if (model_pos) {
            model_pos += 9;
            const char *model_end = strchr(model_pos, '"');
            if (model_end) {
                printf("  Model:      %.*s\n", (int)(model_end - model_pos), model_pos);
            }
        }

        int width = 0, height = 0, scale = 1;
        const char *width_pos = strstr(json_data, "\"width\":");
        if (width_pos) sscanf(width_pos + 8, "%d", &width);
        const char *height_pos = strstr(json_data, "\"height\":");
        if (height_pos) sscanf(height_pos + 9, "%d", &height);
        const char *scale_pos = strstr(json_data, "\"scale\":");
        if (scale_pos) sscanf(scale_pos + 8, "%d", &scale);

        printf("  Resolution: %dx%d @ %dx scale\n", width, height, scale);

        const char *type_pos = strstr(json_data, "\"wallpaper_type\":\"");
        if (type_pos) {
            type_pos += 18;
            const char *type_end = strchr(type_pos, '"');
            if (type_end) {
                printf("  Type:       %.*s\n", (int)(type_end - type_pos), type_pos);
            }
        }

        const char *mode_pos = strstr(json_data, "\"mode\":\"");
        if (mode_pos) {
            mode_pos += 8;
            const char *mode_end = strchr(mode_pos, '"');
            if (mode_end) {
                printf("  Mode:       %.*s\n", (int)(mode_end - mode_pos), mode_pos);
            }
        }

        const char *path_pos = strstr(json_data, "\"wallpaper_path\":\"");
        if (path_pos) {
            path_pos += 18;
            const char *path_end = strchr(path_pos, '"');
            if (path_end) {
                const char *basename_start = path_pos;
                for (const char *p = path_pos; p < path_end; p++) {
                    if (*p == '/') basename_start = p + 1;
                }
                printf("  Current:    %.*s\n", (int)(path_end - basename_start), basename_start);
                printf("  Path:       %.*s\n", (int)(path_end - path_pos), path_pos);
            }
        }

        int cycle_index = 0, cycle_total = 0;
        const char *idx_pos = strstr(json_data, "\"cycle_index\":");
        if (idx_pos) sscanf(idx_pos + 14, "%d", &cycle_index);
        const char *total_pos = strstr(json_data, "\"cycle_total\":");
        if (total_pos) sscanf(total_pos + 14, "%d", &cycle_total);

        if (cycle_total > 0) {
            printf("  Cycling:    %d of %d\n", cycle_index + 1, cycle_total);
        }

        printf("\n");
        return;
    }

    /* Check if it's config keys list */
    if (strstr(json_data, "\"keys\":[")) {
        printf("\nAvailable Configuration Keys:\n");
        printf("─────────────────────────────────────────────────────────\n");

        const char *pos = json_data;
        while ((pos = strstr(pos, "\"key\":\"")) != NULL) {
            pos += 7;
            const char *key_end = strchr(pos, '"');
            if (!key_end) break;

            printf("  • %.*s\n", (int)(key_end - pos), pos);

            /* Show description if available */
            const char *desc_pos = strstr(pos, "\"description\":\"");
            if (desc_pos && desc_pos < key_end + 200) {
                desc_pos += 15;
                const char *desc_end = strchr(desc_pos, '"');
                if (desc_end) {
                    printf("    %.*s\n", (int)(desc_end - desc_pos), desc_pos);
                }
            }

            pos = key_end + 1;
        }
        printf("\n");
        return;
    }

    /* Check if it's a commands list */
    if (strstr(json_data, "\"commands\":[")) {
        printf("\nAvailable Commands:\n");
        printf("═════════════════════════════════════════════════════════\n");

        const char *pos = json_data;
        int count = 0;
        while ((pos = strstr(pos, "\"name\":\"")) != NULL) {
            pos += 8;
            const char *name_end = strchr(pos, '"');
            if (!name_end) break;

            char name[64];
            size_t name_len = name_end - pos;
            if (name_len >= sizeof(name)) name_len = sizeof(name) - 1;
            memcpy(name, pos, name_len);
            name[name_len] = '\0';

            /* Extract category */
            const char *cat_pos = strstr(pos, "\"category\":\"");
            char category[32] = "unknown";
            if (cat_pos && cat_pos < pos + 500) {
                cat_pos += 12;
                const char *cat_end = strchr(cat_pos, '"');
                if (cat_end) {
                    size_t cat_len = cat_end - cat_pos;
                    if (cat_len >= sizeof(category)) cat_len = sizeof(category) - 1;
                    memcpy(category, cat_pos, cat_len);
                    category[cat_len] = '\0';
                }
            }

            /* Extract handler */
            const char *handler_pos = strstr(pos, "\"handler\":\"");
            char handler[64] = "unknown";
            if (handler_pos && handler_pos < pos + 500) {
                handler_pos += 11;
                const char *handler_end = strchr(handler_pos, '"');
                if (handler_end) {
                    size_t handler_len = handler_end - handler_pos;
                    if (handler_len >= sizeof(handler)) handler_len = sizeof(handler) - 1;
                    memcpy(handler, handler_pos, handler_len);
                    handler[handler_len] = '\0';
                }
            }

            /* Extract file */
            const char *file_pos = strstr(pos, "\"file\":\"");
            char file[128] = "";
            if (file_pos && file_pos < pos + 500) {
                file_pos += 8;
                const char *file_end = strchr(file_pos, '"');
                if (file_end) {
                    size_t file_len = file_end - file_pos;
                    if (file_len >= sizeof(file)) file_len = sizeof(file) - 1;
                    memcpy(file, file_pos, file_len);
                    file[file_len] = '\0';
                }
            }

            /* Extract description */
            const char *desc_pos = strstr(pos, "\"description\":\"");
            char description[128] = "";
            if (desc_pos && desc_pos < pos + 500) {
                desc_pos += 15;
                const char *desc_end = strchr(desc_pos, '"');
                if (desc_end) {
                    size_t desc_len = desc_end - desc_pos;
                    if (desc_len >= sizeof(description)) desc_len = sizeof(description) - 1;
                    memcpy(description, desc_pos, desc_len);
                    description[desc_len] = '\0';
                }
            }

            /* Extract capabilities */
            const char *caps_pos = strstr(pos, "\"capabilities\":\"");
            char capabilities[64] = "none";
            if (caps_pos && caps_pos < pos + 500) {
                caps_pos += 16;
                const char *caps_end = strchr(caps_pos, '"');
                if (caps_end && caps_end > caps_pos) {
                    size_t caps_len = caps_end - caps_pos;
                    if (caps_len >= sizeof(capabilities)) caps_len = sizeof(capabilities) - 1;
                    memcpy(capabilities, caps_pos, caps_len);
                    capabilities[caps_len] = '\0';
                }
            }

            count++;
            printf("\n%3d. %-25s [%s]\n", count, name, category);
            if (description[0]) {
                printf("     %s\n", description);
            }
            printf("     Handler: %s\n", handler);
            if (file[0]) {
                printf("     File:    %s\n", file);
            }
            if (strcmp(capabilities, "none") != 0 && capabilities[0] != '\0') {
                printf("     Capabilities: %s\n", capabilities);
            }

            pos = name_end + 1;
        }

        /* Show total count */
        const char *total_pos = strstr(json_data, "\"total\":");
        if (total_pos) {
            int total = 0;
            sscanf(total_pos + 8, "%d", &total);
            printf("\n─────────────────────────────────────────────────────────\n");
            printf("Total: %d commands\n\n", total);
        } else {
            printf("\n─────────────────────────────────────────────────────────\n");
            printf("Total: %d commands\n\n", count);
        }
        return;
    }

    /* For unrecognized data, just print it as-is (it's already formatted by daemon) */
    printf("%s\n", json_data);
}

/* Command implementations */

/* Find daemon binary path */
static const char *find_daemon_path(void) {
    static char daemon_path[512];
    static bool path_found = false;

    /* Return cached path if already found */
    if (path_found) {
        return daemon_path;
    }

    /* Try 1: Same directory as client binary (typical for installed version) */
    ssize_t len = readlink("/proc/self/exe", daemon_path, sizeof(daemon_path) - 20);
    if (len > 0) {
        daemon_path[len] = '\0';
        char *last_slash = strrchr(daemon_path, '/');
        if (last_slash) {
            strcpy(last_slash + 1, "neowalld");
            if (access(daemon_path, X_OK) == 0) {
                path_found = true;
                return daemon_path;
            }
        }
    }

    /* Try 2: Build directory structure (for development)
     * If client is in build/src/neowall/neowall, daemon is in build/src/neowalld/neowalld */
    len = readlink("/proc/self/exe", daemon_path, sizeof(daemon_path) - 50);
    if (len > 0) {
        daemon_path[len] = '\0';
        char *build_marker = strstr(daemon_path, "/build/");
        if (build_marker) {
            /* Found build directory - construct daemon path */
            char *last_slash = strrchr(daemon_path, '/');
            if (last_slash) {
                /* Replace "src/neowall/neowall" with "src/neowalld/neowalld" */
                char *src_neowall = strstr(daemon_path, "/src/neowall/");
                if (src_neowall) {
                    strcpy(src_neowall, "/src/neowalld/neowalld");
                    if (access(daemon_path, X_OK) == 0) {
                        path_found = true;
                        return daemon_path;
                    }
                }
            }
        }
    }

    /* Try 3: Common installation paths */
    const char *install_paths[] = {
        "/usr/local/bin/neowalld",
        "/usr/bin/neowalld",
        NULL
    };

    for (int i = 0; install_paths[i]; i++) {
        if (access(install_paths[i], X_OK) == 0) {
            strncpy(daemon_path, install_paths[i], sizeof(daemon_path) - 1);
            daemon_path[sizeof(daemon_path) - 1] = '\0';
            path_found = true;
            return daemon_path;
        }
    }

    /* Try 4: Check PATH (fallback) */
    strncpy(daemon_path, "neowalld", sizeof(daemon_path) - 1);
    daemon_path[sizeof(daemon_path) - 1] = '\0';
    return daemon_path;
}

int cmd_start(int argc, char *argv[]) {
    /* FAST: check if already running before doing anything else */
    pid_t existing_pid = read_daemon_pid();
    if (existing_pid > 0) {
        printf("neowalld is already running (PID %d)\n", existing_pid);
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



int cmd_stop(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* FAST: read PID once instead of checking twice */
    pid_t pid = read_daemon_pid();
    if (pid <= 0) {
        printf("neowalld is not running\n");
        return 0;
    }

    printf("Stopping neowalld (PID %d)...\n", pid);

    if (kill(pid, SIGTERM) != 0) {
        fprintf(stderr, "Error: Failed to signal daemon: %s\n", strerror(errno));
        return 1;
    }

    /* BLAZING FAST: Use waitpid with timeout for instant notification */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        /* Check if process is gone - kill(0) is blazing fast */
        if (kill(pid, 0) != 0) {
            if (errno == ESRCH) {
                /* Process exited - cleanup PID file */
                unlink(get_pid_file_path());
                printf("✓ neowalld stopped\n");
                return 0;
            }
        }

        /* Check timeout (5 seconds) */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_nsec - start.tv_nsec) / 1000000;

        if (elapsed_ms > 5000) {
            break;
        }

        /* Minimal sleep - 10ms granularity for fast response */
        usleep(10000);
    }

    /* Timeout - force kill */
    fprintf(stderr, "Daemon not responding, sending SIGKILL...\n");
    kill(pid, SIGKILL);
    usleep(100000); /* 100ms for SIGKILL to take effect */

    if (kill(pid, 0) != 0 && errno == ESRCH) {
        unlink(get_pid_file_path());
        printf("✓ neowalld killed\n");
        return 0;
    } else {
        fprintf(stderr, "✗ Failed to stop neowalld\n");
        return 1;
    }

    return 0;
}

int cmd_restart(int argc, char *argv[]) {
    printf("Restarting neowalld...\n");
    cmd_stop(0, NULL);
    /* No sleep needed - stop already waits for clean shutdown */
    return cmd_start(argc, argv);
}

/* Simple JSON parser helpers */
static const char* json_find_key(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    return strstr(json, search);
}

static bool json_get_int(const char *json, const char *key, int *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;
    pos += strlen(key) + 3;  /* Skip "key": */
    while (*pos && (*pos == ' ' || *pos == '"')) pos++;
    *out = atoi(pos);
    return true;
}

static bool json_get_bool(const char *json, const char *key) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;
    return strstr(pos, "true") != NULL;
}

static float json_get_float(const char *json, const char *key) {
    const char *pos = json_find_key(json, key);
    if (!pos) return 0.0f;
    pos += strlen(key) + 3;
    while (*pos && (*pos == ' ' || *pos == '"')) pos++;
    return atof(pos);
}

int cmd_status(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (!is_daemon_running()) {
        if (flag_json_output) {
            printf("{\"status\":\"error\",\"message\":\"neowalld is not running\",\"running\":false}\n");
        } else {
            printf("● neowalld is not running\n");
        }
        return 3;
    }

    char data[8192];
    if (send_ipc_command("status", data, sizeof(data))) {
        if (flag_json_output) {
            printf("%s\n", data);
        } else {
            /* Parse and display status nicely */
            int pid = 0, outputs = 0;
            bool paused = false, shader_paused = false;
            float shader_speed = 1.0f;

            json_get_int(data, "pid", &pid);
            json_get_int(data, "outputs", &outputs);
            paused = json_get_bool(data, "paused");
            shader_paused = json_get_bool(data, "shader_paused");
            shader_speed = json_get_float(data, "shader_speed");

            printf("● neowalld is running\n\n");
            printf("Process Information:\n");
            printf("  PID:     %d\n", pid);
            printf("  Outputs: %d\n\n", outputs);

            printf("Wallpaper State:\n");
            printf("  Cycling: %s\n\n", paused ? "⏸  Paused" : "▶  Active");

            printf("Shader State:\n");
            printf("  Animation: %s\n", shader_paused ? "⏸  Paused" : "▶  Active");
            printf("  Speed:     %.2fx\n\n", shader_speed);

            /* Parse and display wallpapers */
            const char *wallpapers = strstr(data, "\"wallpapers\":[");
            if (wallpapers) {
                printf("Current Wallpapers:\n");
                wallpapers += 14;  /* Skip "wallpapers":[ */

                const char *obj_start = wallpapers;
                int wp_num = 1;
                while ((obj_start = strchr(obj_start, '{')) != NULL) {
                    const char *obj_end = strchr(obj_start, '}');
                    if (!obj_end) break;

                    char obj[2048];
                    size_t len = obj_end - obj_start + 1;
                    if (len >= sizeof(obj)) len = sizeof(obj) - 1;
                    strncpy(obj, obj_start, len);
                    obj[len] = '\0';

                    /* Extract wallpaper info */
                    const char *output_pos = strstr(obj, "\"output\":\"");
                    const char *type_pos = strstr(obj, "\"type\":\"");
                    const char *path_pos = strstr(obj, "\"path\":\"");
                    int cycle_index = 0, cycle_total = 0;
                    json_get_int(obj, "cycle_index", &cycle_index);
                    json_get_int(obj, "cycle_total", &cycle_total);

                    if (output_pos && type_pos && path_pos) {
                        output_pos += 10;
                        type_pos += 8;
                        path_pos += 8;

                        /* Extract output name */
                        char output_name[64] = {0};
                        const char *q = strchr(output_pos, '"');
                        if (q && (q - output_pos) < 63) {
                            strncpy(output_name, output_pos, q - output_pos);
                        }

                        /* Extract type */
                        char type[16] = {0};
                        q = strchr(type_pos, '"');
                        if (q && (q - type_pos) < 15) {
                            strncpy(type, type_pos, q - type_pos);
                        }

                        /* Extract path */
                        char path[512] = {0};
                        q = path_pos;
                        size_t pi = 0;
                        while (*q && *q != '"' && pi < sizeof(path) - 1) {
                            if (*q == '\\' && *(q+1) == '"') {
                                path[pi++] = '"';
                                q += 2;
                            } else if (*q == '\\' && *(q+1) == '\\') {
                                path[pi++] = '\\';
                                q += 2;
                            } else {
                                path[pi++] = *q++;
                            }
                        }
                        path[pi] = '\0';

                        printf("  %d. %s (%s)\n", wp_num++, output_name, type);
                        printf("     Path:  %s\n", path);
                        if (cycle_total > 0) {
                            printf("     Cycle: %d/%d\n", cycle_index + 1, cycle_total);
                        }
                    }

                    obj_start = obj_end + 1;
                }
            }
        }
        return 0;
    }

    return 1;
}

/* BLAZING FAST IPC command wrappers - support optional output parameter */
int cmd_next(int argc, char *argv[]) {
    (void)argc; (void)argv; /* Global only - no arguments */
    return send_ipc_command("next", NULL, 0) ? 0 : 1;
}

int cmd_pause(int argc, char *argv[]) {
    (void)argc; (void)argv; /* Global only - no arguments */
    return send_ipc_command("pause", NULL, 0) ? 0 : 1;
}

int cmd_resume(int argc, char *argv[]) {
    (void)argc; (void)argv; /* Global only - no arguments */
    return send_ipc_command("resume", NULL, 0) ? 0 : 1;
}

int cmd_reload(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("reload", NULL, 0) ? 0 : 1;
}

int cmd_current(int argc, char *argv[]) {
    if (argc >= 2) {
        char args[512];
        snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
        return send_ipc_command("current", args, strlen(args)) ? 0 : 1;
    }
    return cmd_status(argc, argv);
}

int cmd_speed_up(int argc, char *argv[]) {
    if (argc >= 2) {
        char args[512];
        snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
        return send_ipc_command("speed-up", args, strlen(args)) ? 0 : 1;
    }
    return send_ipc_command("speed-up", NULL, 0) ? 0 : 1;
}

int cmd_speed_down(int argc, char *argv[]) {
    if (argc >= 2) {
        char args[512];
        snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
        return send_ipc_command("speed-down", args, strlen(args)) ? 0 : 1;
    }
    return send_ipc_command("speed-down", NULL, 0) ? 0 : 1;
}

int cmd_shader_pause(int argc, char *argv[]) {
    if (argc >= 2) {
        char args[512];
        snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
        return send_ipc_command("shader-pause", args, strlen(args)) ? 0 : 1;
    }
    return send_ipc_command("shader-pause", NULL, 0) ? 0 : 1;
}

int cmd_shader_resume(int argc, char *argv[]) {
    if (argc >= 2) {
        char args[512];
        snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
        return send_ipc_command("shader-resume", args, strlen(args)) ? 0 : 1;
    }
    return send_ipc_command("shader-resume", NULL, 0) ? 0 : 1;
}

int cmd_prev(int argc, char *argv[]) {
    (void)argc; (void)argv; /* Global only - no arguments */
    return send_ipc_command("prev", NULL, 0) ? 0 : 1;
}

int cmd_ping(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("ping", NULL, 0) ? 0 : 1;
}

int cmd_config(int argc, char *argv[]) {
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

int cmd_tray(int argc, char *argv[]) {
    (void)argc;  /* Unused - no arguments needed for tray command */

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
    execvp("neowall_tray", argv);

    fprintf(stderr, "Error: Failed to launch neowall_tray: %s\n", strerror(errno));
    return 1;
}

/* BLAZING FAST version display - minimal I/O */
int cmd_version(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Single write() call for maximum speed */
    const char *msg =
        NEOWALL_VERSION_STRING "\n"
        NEOWALL_PROJECT_DESCRIPTION "\n\n"
        "License: " NEOWALL_PROJECT_LICENSE "\n"
        "Website: " NEOWALL_PROJECT_URL "\n"
        "Author: " NEOWALL_PROJECT_AUTHOR "\n";

    fputs(msg, stdout);
    return 0;
}

/* ============================================================================
 * Output-Specific Commands
 * ============================================================================ */

int cmd_list_outputs(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("list-outputs", NULL, 0) ? 0 : 1;
}

int cmd_output_info(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: output-info requires output name\n");
        fprintf(stderr, "Usage: neowall output-info <output-name>\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("output-info", args, strlen(args)) ? 0 : 1;
}

int cmd_next_output(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: next-output requires output name\n");
        fprintf(stderr, "Usage: neowall next-output <output-name>\n");
        fprintf(stderr, "  Tip: Use 'neowall next' to affect all outputs\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("next-output", args, strlen(args)) ? 0 : 1;
}

int cmd_prev_output(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: prev-output requires output name\n");
        fprintf(stderr, "Usage: neowall prev-output <output-name>\n");
        fprintf(stderr, "  Tip: Use 'neowall prev' to affect all outputs\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("prev-output", args, strlen(args)) ? 0 : 1;
}

int cmd_reload_output(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: reload-output requires output name\n");
        fprintf(stderr, "Usage: neowall reload-output <output-name>\n");
        return 1;
    }

    char args[512];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("reload-output", args, strlen(args)) ? 0 : 1;
}

int cmd_pause_output(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: pause-output requires output name\n");
        fprintf(stderr, "Usage: neowall pause-output <output-name>\n");
        fprintf(stderr, "  Tip: Use 'neowall pause' to affect all outputs\n");
        return 1;
    }

    char args[512];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("pause-output", args, strlen(args)) ? 0 : 1;
}

int cmd_resume_output(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: resume-output requires output name\n");
        fprintf(stderr, "Usage: neowall resume-output <output-name>\n");
        fprintf(stderr, "  Tip: Use 'neowall resume' to affect all outputs\n");
        return 1;
    }

    char args[512];
    snprintf(args, sizeof(args), "{\"output\":\"%s\"}", argv[1]);
    return send_ipc_command("resume-output", args, strlen(args)) ? 0 : 1;
}

int cmd_jump_to_output(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: jump-to-output requires output name and index\n");
        fprintf(stderr, "Usage: neowall jump-to-output <output-name> <index>\n");
        return 1;
    }

    char args[512];
    snprintf(args, sizeof(args), "{\"output\":\"%s\",\"index\":%s}", argv[1], argv[2]);
    return send_ipc_command("jump-to-output", args, strlen(args)) ? 0 : 1;
}

int cmd_set_output_mode(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: set-output-mode requires output name and mode\n");
        fprintf(stderr, "Usage: neowall set-output-mode <output-name> <mode>\n");
        fprintf(stderr, "Modes: fill, fit, center, stretch, tile\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\",\"mode\":\"%s\"}", argv[1], argv[2]);
    return send_ipc_command("set-output-mode", args, strlen(args)) ? 0 : 1;
}

int cmd_set_output_wallpaper(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: set-output-wallpaper requires output name and path\n");
        fprintf(stderr, "Usage: neowall set-output-wallpaper <output-name> <path>\n");
        return 1;
    }

    char args[512];
    snprintf(args, sizeof(args), "{\"output\":\"%s\",\"path\":\"%s\"}", argv[1], argv[2]);
    return send_ipc_command("set-output-wallpaper", args, strlen(args)) ? 0 : 1;
}

int cmd_set_output_interval(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: set-output-interval requires output name and interval\n");
        fprintf(stderr, "Usage: neowall set-output-interval <output-name> <seconds>\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\",\"interval\":%s}", argv[1], argv[2]);
    return send_ipc_command("set-output-interval", args, strlen(args)) ? 0 : 1;
}

int cmd_jump_output(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: jump-output requires output name and index\n");
        fprintf(stderr, "Usage: neowall jump-output <output-name> <index>\n");
        return 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"output\":\"%s\",\"index\":%s}", argv[1], argv[2]);
    return send_ipc_command("jump-to-output", args, strlen(args)) ? 0 : 1;
}

/* ============================================================================
 * Config Commands
 * ============================================================================ */

int cmd_get_config(int argc, char *argv[]) {
    if (argc < 2) {
        /* No key provided - get all config */
        return send_ipc_command("get-config", NULL, 0) ? 0 : 1;
    }

    char args[256];
    snprintf(args, sizeof(args), "{\"key\":\"%s\"}", argv[1]);
    return send_ipc_command("get-config", args, strlen(args)) ? 0 : 1;
}

int cmd_set_config(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: set-config requires <key> and <value>\n");
        fprintf(stderr, "Usage: neowall set-config <key> <value>\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  neowall set-config general.cycle_interval 600\n");
        fprintf(stderr, "  neowall set-config general.wallpaper_mode fill\n");
        fprintf(stderr, "  neowall set-config performance.shader_speed 1.5\n");
        return 1;
    }

    const char *key = argv[1];
    const char *value = argv[2];

    char args[1024];
    snprintf(args, sizeof(args), "{\"key\":\"%s\",\"value\":\"%s\"}", key, value);
    return send_ipc_command("set-config", args, strlen(args)) ? 0 : 1;
}

int cmd_reset_config(int argc, char *argv[]) {
    if (argc < 2) {
        /* No key provided - reset all */
        fprintf(stderr, "Warning: This will reset ALL configuration to defaults.\n");
        fprintf(stderr, "Are you sure? Use: neowall reset-config <key> to reset specific key\n");
        fprintf(stderr, "Or use: neowall reset-config --all to confirm reset all\n");
        return 1;
    }

    const char *key = argv[1];

    if (strcmp(key, "--all") == 0) {
        /* Reset all */
        return send_ipc_command("reset-config", NULL, 0) ? 0 : 1;
    }

    /* Reset specific key */
    char args[512];
    snprintf(args, sizeof(args), "{\"key\":\"%s\"}", key);
    return send_ipc_command("reset-config", args, strlen(args)) ? 0 : 1;
}

int cmd_list_config_keys(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("list-config-keys", NULL, 0) ? 0 : 1;
}

int cmd_list_commands(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return send_ipc_command("list-commands", NULL, 0) ? 0 : 1;
}

/* ============================================================================
 * Help Command
 * ============================================================================ */

int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("%s - %s\n\n", NEOWALL_VERSION_STRING, NEOWALL_PROJECT_DESCRIPTION);
    printf("Usage: neowall [OPTIONS] [COMMAND] [ARGS...]\n\n");
    printf("Global Options:\n");
    printf("  --json             Output in JSON format (for scripting)\n");
    printf("\n");
    printf("Daemon Management:\n");
    printf("  start              Start the neowalld daemon\n");
    printf("  stop               Stop the neowalld daemon\n");
    printf("  restart            Restart the daemon\n");
    printf("  status             Show daemon status\n");
    printf("\n");
    printf("Wallpaper Control (Global):\n");
    printf("  next               Switch to next wallpaper (all outputs)\n");
    printf("  pause              Pause wallpaper cycling (all outputs)\n");
    printf("  resume             Resume wallpaper cycling (all outputs)\n");
    printf("  current            Show current wallpaper\n");
    printf("  reload             Reload configuration\n");
    printf("\n");
    printf("Output-Specific Control:\n");
    printf("  list-outputs                      List all connected outputs\n");
    printf("  output-info <output>              Get info about specific output\n");
    printf("  next-output <output>              Next wallpaper on specific output\n");
    printf("  prev-output <output>              Previous wallpaper on output\n");
    printf("  reload-output <output>            Reload wallpaper on output\n");
    printf("  set-output-mode <output> <mode>   Set display mode (fill/fit/center/stretch/tile)\n");
    printf("  set-output-wallpaper <output> <path>  Set wallpaper/shader for output\n");
    printf("  set-output-interval <output> <sec>    Set cycle interval (seconds)\n");
    printf("  jump-output <output> <index>      Jump to specific cycle index\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  get-config [key]                  Get configuration value(s)\n");
    printf("  set-config <key> <value>          Set configuration value\n");
    printf("  reset-config <key|--all>          Reset configuration to defaults\n");
    printf("  list-config-keys                  List all configuration keys\n");
    printf("\n");
    printf("Shader Control:\n");
    printf("  speed-up           Increase shader animation speed\n");
    printf("  speed-down         Decrease shader animation speed\n");
    printf("  shader-pause       Pause shader animation\n");
    printf("  shader-resume      Resume shader animation\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  get-config [key]   Get configuration value(s)\n");
    printf("  list-config-keys   List all configuration keys\n");
    printf("  config             Edit configuration file\n");
    printf("  tray               Launch system tray application\n");
    printf("\n");
    printf("Information:\n");
    printf("  list-commands      List all available IPC commands with metadata\n");
    printf("  version            Show version information\n");
    printf("  help               Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  neowall start                          # Start the daemon\n");
    printf("  neowall list-outputs                   # Show all connected monitors\n");
    printf("  neowall next-output DP-1               # Next wallpaper on DP-1\n");
    printf("  neowall set-output-mode HDMI-A-1 fill  # Set mode on HDMI-A-1\n");
    printf("  neowall --json status                  # Get status in JSON\n");
    printf("  neowall tray &                         # Launch tray in background\n");
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
            return commands[i].handler(argc - arg_offset, argv + arg_offset);
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
