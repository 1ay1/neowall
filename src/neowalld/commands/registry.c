/*
 * NeoWall Command Registry
 * Manages command registration, lookup, and metadata
 */

#include "commands.h"
#include "neowall.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Forward declarations of command handlers */
static command_result_t cmd_next(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_prev(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_reload(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_speed_up(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_speed_down(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_shader_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_shader_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_status(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_current(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_ping(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_version(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_list(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Command statistics tracking */
typedef struct {
    command_stats_t stats;
    struct timespec last_call;
} command_stats_internal_t;

static command_stats_internal_t command_statistics[32]; /* Max 32 commands */

/* Command registry */
static const command_info_t command_registry[] = {
    /* Wallpaper Control */
    {
        .name = "next",
        .category = "wallpaper",
        .description = "Switch to next wallpaper",
        .args_schema = NULL,
        .example = "{\"command\":\"next\"}",
        .handler = cmd_next,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "prev",
        .category = "wallpaper",
        .description = "Switch to previous wallpaper",
        .args_schema = NULL,
        .example = "{\"command\":\"prev\"}",
        .handler = cmd_prev,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "current",
        .category = "wallpaper",
        .description = "Get current wallpaper information",
        .args_schema = NULL,
        .example = "{\"command\":\"current\"}",
        .handler = cmd_current,
        .capabilities = CMD_CAP_REQUIRES_STATE,
        .version = 1,
    },

    /* Cycling Control */
    {
        .name = "pause",
        .category = "cycling",
        .description = "Pause automatic wallpaper cycling",
        .args_schema = NULL,
        .example = "{\"command\":\"pause\"}",
        .handler = cmd_pause,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "resume",
        .category = "cycling",
        .description = "Resume automatic wallpaper cycling",
        .args_schema = NULL,
        .example = "{\"command\":\"resume\"}",
        .handler = cmd_resume,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },

    /* Configuration */
    {
        .name = "reload",
        .category = "config",
        .description = "Reload configuration from disk",
        .args_schema = NULL,
        .example = "{\"command\":\"reload\"}",
        .handler = cmd_reload,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },

    /* Shader Control */
    {
        .name = "speed-up",
        .category = "shader",
        .description = "Increase shader animation speed",
        .args_schema = "{\"amount\": <float>}",
        .example = "{\"command\":\"speed-up\",\"args\":{\"amount\":0.5}}",
        .handler = cmd_speed_up,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "speed-down",
        .category = "shader",
        .description = "Decrease shader animation speed",
        .args_schema = "{\"amount\": <float>}",
        .example = "{\"command\":\"speed-down\",\"args\":{\"amount\":0.5}}",
        .handler = cmd_speed_down,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "shader-pause",
        .category = "shader",
        .description = "Pause shader animation",
        .args_schema = NULL,
        .example = "{\"command\":\"shader-pause\"}",
        .handler = cmd_shader_pause,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },
    {
        .name = "shader-resume",
        .category = "shader",
        .description = "Resume shader animation",
        .args_schema = NULL,
        .example = "{\"command\":\"shader-resume\"}",
        .handler = cmd_shader_resume,
        .capabilities = CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
        .version = 1,
    },

    /* Status & Information */
    {
        .name = "status",
        .category = "info",
        .description = "Get daemon status and statistics",
        .args_schema = NULL,
        .example = "{\"command\":\"status\"}",
        .handler = cmd_status,
        .capabilities = CMD_CAP_REQUIRES_STATE,
        .version = 1,
    },
    {
        .name = "version",
        .category = "info",
        .description = "Get daemon version information",
        .args_schema = NULL,
        .example = "{\"command\":\"version\"}",
        .handler = cmd_version,
        .capabilities = CMD_CAP_NONE,
        .version = 1,
    },
    {
        .name = "ping",
        .category = "info",
        .description = "Check if daemon is responsive",
        .args_schema = NULL,
        .example = "{\"command\":\"ping\"}",
        .handler = cmd_ping,
        .capabilities = CMD_CAP_NONE,
        .version = 1,
    },
    {
        .name = "list",
        .category = "info",
        .description = "List all available commands",
        .args_schema = NULL,
        .example = "{\"command\":\"list\"}",
        .handler = cmd_list,
        .capabilities = CMD_CAP_NONE,
        .version = 1,
    },

    /* Sentinel */
    { NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 }
};

/* Get registry size */
static size_t get_registry_size(void) {
    size_t count = 0;
    while (command_registry[count].name != NULL) {
        count++;
    }
    return count;
}

/* ============================================================================
 * Core API Implementation
 * ============================================================================ */

int commands_dispatch(const ipc_request_t *req, ipc_response_t *resp, void *user_data) {
    struct neowall_state *state = (struct neowall_state *)user_data;

    if (!req || !resp) {
        if (resp) {
            commands_build_error(resp, CMD_ERROR_FAILED, "Invalid request or response");
        }
        return -1;
    }

    /* Find command */
    const command_info_t *cmd = commands_find(req->command);
    if (!cmd) {
        char error[512];  /* Larger buffer to accommodate command name */
        /* Use %.255s to limit command name length in formatted string */
        snprintf(error, sizeof(error), "Unknown command: %.255s", req->command);
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return -1;
    }

    /* Check if state is required but missing */
    if ((cmd->capabilities & CMD_CAP_REQUIRES_STATE) && !state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Command requires daemon state");
        return -1;
    }

    /* Track execution time */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Execute command */
    command_result_t result = cmd->handler(state, req, resp);

    /* Update statistics */
    clock_gettime(CLOCK_MONOTONIC, &end);
    size_t cmd_index = cmd - command_registry;
    if (cmd_index < sizeof(command_statistics) / sizeof(command_statistics[0])) {
        command_statistics[cmd_index].stats.calls_total++;
        if (result == CMD_SUCCESS) {
            command_statistics[cmd_index].stats.calls_success++;
        } else {
            command_statistics[cmd_index].stats.calls_failed++;
        }

        /* Calculate execution time in microseconds */
        unsigned long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 +
                                  (end.tv_nsec - start.tv_nsec) / 1000;

        /* Update average (simple moving average) */
        if (command_statistics[cmd_index].stats.calls_total == 1) {
            command_statistics[cmd_index].stats.avg_time_us = elapsed_us;
        } else {
            command_statistics[cmd_index].stats.avg_time_us =
                (command_statistics[cmd_index].stats.avg_time_us * 9 + elapsed_us) / 10;
        }

        command_statistics[cmd_index].last_call = end;
    }

    return (result == CMD_SUCCESS) ? 0 : -1;
}

/* ============================================================================
 * Registry & Introspection
 * ============================================================================ */

const command_info_t *commands_get_all(size_t *count) {
    if (count) {
        *count = get_registry_size();
    }
    return command_registry;
}

const command_info_t *commands_find(const char *name) {
    if (!name) return NULL;

    for (size_t i = 0; command_registry[i].name; i++) {
        if (strcmp(command_registry[i].name, name) == 0) {
            return &command_registry[i];
        }
    }
    return NULL;
}

const command_info_t *commands_get_by_category(const char *category, size_t *count) {
    /* Note: Returns pointer to static array - not thread-safe for multiple categories */
    static const command_info_t *filtered[32];
    size_t filter_count = 0;

    if (!category) {
        if (count) *count = 0;
        return NULL;
    }

    for (size_t i = 0; command_registry[i].name && filter_count < 31; i++) {
        if (strcmp(command_registry[i].category, category) == 0) {
            filtered[filter_count++] = &command_registry[i];
        }
    }

    filtered[filter_count] = NULL;
    if (count) *count = filter_count;

    return filtered[0] ? filtered[0] : NULL;
}

bool commands_exists(const char *name) {
    return commands_find(name) != NULL;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

int commands_get_stats(const char *name, command_stats_t *stats) {
    if (!name || !stats) return -1;

    const command_info_t *cmd = commands_find(name);
    if (!cmd) return -1;

    size_t index = cmd - command_registry;
    if (index < sizeof(command_statistics) / sizeof(command_statistics[0])) {
        *stats = command_statistics[index].stats;
        return 0;
    }

    return -1;
}

int commands_reset_stats(const char *name) {
    if (!name) {
        /* Reset all */
        memset(command_statistics, 0, sizeof(command_statistics));
        return 0;
    }

    const command_info_t *cmd = commands_find(name);
    if (!cmd) return -1;

    size_t index = cmd - command_registry;
    if (index < sizeof(command_statistics) / sizeof(command_statistics[0])) {
        memset(&command_statistics[index], 0, sizeof(command_stats_internal_t));
        return 0;
    }

    return -1;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

int commands_init(void) {
    /* Initialize statistics */
    memset(command_statistics, 0, sizeof(command_statistics));
    return 0;
}

void commands_shutdown(void) {
    /* Could print statistics here if verbose logging enabled */
}

/* ============================================================================
 * Help & Documentation
 * ============================================================================ */

size_t commands_generate_help(char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;

    char *p = buffer;
    size_t remaining = size;
    int written;

    written = snprintf(p, remaining, "Available Commands:\n\n");
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;
    remaining -= written;

    const char *last_category = NULL;
    for (size_t i = 0; command_registry[i].name; i++) {
        /* Print category header */
        if (!last_category || strcmp(command_registry[i].category, last_category) != 0) {
            written = snprintf(p, remaining, "%s:\n", command_registry[i].category);
            if (written < 0 || (size_t)written >= remaining) return size;
            p += written;
            remaining -= written;
            last_category = command_registry[i].category;
        }

        /* Print command */
        written = snprintf(p, remaining, "  %-12s %s\n",
                          command_registry[i].name,
                          command_registry[i].description);
        if (written < 0 || (size_t)written >= remaining) return size;
        p += written;
        remaining -= written;
    }

    return p - buffer;
}

size_t commands_generate_command_help(const char *name, char *buffer, size_t size) {
    if (!name || !buffer || size == 0) return 0;

    const command_info_t *cmd = commands_find(name);
    if (!cmd) return 0;

    return snprintf(buffer, size,
                   "Command: %s\n"
                   "Category: %s\n"
                   "Description: %s\n"
                   "Arguments: %s\n"
                   "Example: %s\n",
                   cmd->name,
                   cmd->category,
                   cmd->description,
                   cmd->args_schema ? cmd->args_schema : "none",
                   cmd->example ? cmd->example : "N/A");
}

size_t commands_generate_json_list(char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;

    char *p = buffer;
    size_t remaining = size;
    int written;

    written = snprintf(p, remaining, "{\"commands\":[");
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;
    remaining -= written;

    for (size_t i = 0; command_registry[i].name; i++) {
        written = snprintf(p, remaining,
                          "%s{\"name\":\"%s\",\"category\":\"%s\",\"description\":\"%s\"}",
                          i > 0 ? "," : "",
                          command_registry[i].name,
                          command_registry[i].category,
                          command_registry[i].description);
        if (written < 0 || (size_t)written >= remaining) return size;
        p += written;
        remaining -= written;
    }

    written = snprintf(p, remaining, "]}");
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;

    return p - buffer;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *commands_result_to_string(command_result_t result) {
    switch (result) {
        case CMD_SUCCESS: return "success";
        case CMD_ERROR_INVALID_ARGS: return "invalid_arguments";
        case CMD_ERROR_STATE: return "invalid_state";
        case CMD_ERROR_FAILED: return "execution_failed";
        case CMD_ERROR_NOT_IMPLEMENTED: return "not_implemented";
        case CMD_ERROR_PERMISSION: return "permission_denied";
        default: return "unknown_error";
    }
}

void commands_build_success(ipc_response_t *resp, const char *message, const char *data) {
    if (!resp) return;

    char buffer[2048];
    if (data) {
        snprintf(buffer, sizeof(buffer), "{\"message\":\"%s\",\"data\":%s}",
                message ? message : "OK", data);
    } else if (message) {
        snprintf(buffer, sizeof(buffer), "{\"message\":\"%s\"}", message);
    } else {
        snprintf(buffer, sizeof(buffer), "{\"status\":\"ok\"}");
    }

    ipc_success_response(resp, buffer);
}

void commands_build_error(ipc_response_t *resp, command_result_t result, const char *message) {
    if (!resp) return;

    ipc_error_response(resp, IPC_STATUS_ERROR, message ? message : commands_result_to_string(result));
}

/* ============================================================================
 * Command Handler Stub Implementations
 * These will be replaced with actual implementations
 * ============================================================================ */

static command_result_t cmd_next(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Increment next_requested counter to trigger wallpaper change */
    atomic_fetch_add(&state->next_requested, 1);

    commands_build_success(resp, "Switched to next wallpaper", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_prev(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Increment prev_requested counter to trigger previous wallpaper change */
    atomic_fetch_add(&state->prev_requested, 1);

    commands_build_success(resp, "Switched to previous wallpaper", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Set paused flag */
    atomic_store(&state->paused, true);

    commands_build_success(resp, "Paused wallpaper cycling", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Clear paused flag */
    atomic_store(&state->paused, false);

    commands_build_success(resp, "Resumed wallpaper cycling", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_reload(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;
    (void)state;

    /* Hot reload removed for simplicity - instruct user to restart daemon */
    commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Reload not supported. Please restart the daemon: neowall restart");
    return CMD_ERROR_INVALID_ARGS;
}

static command_result_t cmd_speed_up(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Increase shader speed by 0.25x */
    float current_speed = atomic_load(&state->shader_speed);
    float new_speed = current_speed + 0.25f;
    if (new_speed > 10.0f) new_speed = 10.0f;  /* Cap at 10x */
    atomic_store(&state->shader_speed, new_speed);

    char data[128];
    snprintf(data, sizeof(data), "{\"shader_speed\":%.2f}", new_speed);
    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

static command_result_t cmd_speed_down(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Decrease shader speed by 0.25x */
    float current_speed = atomic_load(&state->shader_speed);
    float new_speed = current_speed - 0.25f;
    if (new_speed < 0.1f) new_speed = 0.1f;  /* Minimum 0.1x */
    atomic_store(&state->shader_speed, new_speed);

    char data[128];
    snprintf(data, sizeof(data), "{\"shader_speed\":%.2f}", new_speed);
    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

static command_result_t cmd_shader_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Pause shader animation (freezes time) */
    atomic_store(&state->shader_paused, true);

    commands_build_success(resp, "Paused shader animation", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_shader_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Resume shader animation */
    atomic_store(&state->shader_paused, false);

    commands_build_success(resp, "Resumed shader animation", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_status(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char data[1024];
    snprintf(data, sizeof(data),
             "{\"daemon\":\"running\",\"pid\":%d,\"outputs\":%u,\"paused\":%s,\"shader_paused\":%s,\"shader_speed\":%.2f}",
             getpid(),
             state->output_count,
             atomic_load(&state->paused) ? "true" : "false",
             atomic_load(&state->shader_paused) ? "true" : "false",
             atomic_load(&state->shader_speed));

    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

static command_result_t cmd_current(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Build current wallpaper info for all outputs */
    char data[8192] = "{\"outputs\":[";  /* Larger buffer to avoid truncation */
    size_t data_len = strlen(data);
    size_t data_remaining = sizeof(data) - data_len - 1;

    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    bool first = true;

    while (output && data_remaining > 100) {
        if (!first) {
            strncat(data, ",", data_remaining);
            data_len = strlen(data);
            data_remaining = sizeof(data) - data_len - 1;
        }
        first = false;

        char output_info[4096];  /* Increased buffer size for long paths */
        const char *type = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
        const char *path = (output->config.type == WALLPAPER_SHADER)
            ? output->config.shader_path
            : output->config.path;

        /* Truncate path if it's too long to fit in JSON */
        char safe_path[512];
        size_t path_len = strlen(path);
        if (path_len >= sizeof(safe_path)) {
            snprintf(safe_path, sizeof(safe_path), "...%s", path + path_len - (sizeof(safe_path) - 4));
        } else {
            strncpy(safe_path, path, sizeof(safe_path) - 1);
            safe_path[sizeof(safe_path) - 1] = '\0';
        }

        snprintf(output_info, sizeof(output_info),
                 "{\"name\":\"%.64s\",\"type\":\"%s\",\"path\":\"%s\",\"mode\":\"%d\",\"cycle_index\":%zu,\"cycle_total\":%zu}",
                 output->connector_name,
                 type,
                 safe_path,
                 output->config.mode,
                 output->config.current_cycle_index,
                 output->config.cycle_count);

        size_t info_len = strlen(output_info);
        if (info_len < data_remaining) {
            strncat(data, output_info, data_remaining);
            data_len = strlen(data);
            data_remaining = sizeof(data) - data_len - 1;
        } else {
            log_error("Output info too large to fit in response buffer");
            break;
        }

        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    if (data_remaining > 2) {
        strcat(data, "]}");
    } else {
        log_error("Insufficient buffer space for JSON closing");
    }

    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

static command_result_t cmd_ping(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)state; (void)req;
    commands_build_success(resp, "pong", NULL);
    return CMD_SUCCESS;
}

static command_result_t cmd_version(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)state; (void)req;
    char data[256];
    snprintf(data, sizeof(data), "{\"version\":\"0.3.0\",\"protocol\":\"1.0\"}");
    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

static command_result_t cmd_list(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)state; (void)req;
    char buffer[4096];
    commands_generate_json_list(buffer, sizeof(buffer));
    ipc_success_response(resp, buffer);
    return CMD_SUCCESS;
}
