/*
 * NeoWall Command Registry
 * Manages command registration, lookup, and metadata
 */

#include "commands.h"
#include "output_commands.h"
#include "config_commands.h"
#include "neowall.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/* Forward declarations of command handlers */
static command_result_t cmd_next(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_prev(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_speed_up(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_speed_down(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_shader_pause(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_shader_resume(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Status & information */
static command_result_t cmd_status(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_current(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_ping(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_version(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_list_commands(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
static command_result_t cmd_command_stats(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Command statistics tracking */
typedef struct {
    command_stats_t stats;
    struct timespec last_call;
    uint64_t total_time_ns;  /* Internal: total time in nanoseconds for averaging */
} command_stats_internal_t;

static command_stats_internal_t command_statistics[64]; /* Max 64 commands */

/* Core command registry (wallpaper, cycling, shader, info) */
static const command_info_t command_registry_core[] = {
    /* Wallpaper Control */
    COMMAND_ENTRY(next, "wallpaper", "Switch to next wallpaper",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    COMMAND_ENTRY(prev, "wallpaper", "Switch to previous wallpaper",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    COMMAND_ENTRY(current, "wallpaper", "Get current wallpaper information",
                  CMD_CAP_REQUIRES_STATE),

    /* Cycling Control */
    COMMAND_ENTRY(pause, "cycling", "Pause automatic wallpaper cycling",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    COMMAND_ENTRY(resume, "cycling", "Resume automatic wallpaper cycling",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    COMMAND_ENTRY_CUSTOM("speed-up", cmd_speed_up, "cycling",
                        "Decrease cycle interval (speed up transitions)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE, NULL, NULL),
    COMMAND_ENTRY_CUSTOM("speed-down", cmd_speed_down, "cycling",
                        "Increase cycle interval (slow down transitions)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE, NULL, NULL),

    /* Shader Control */
    COMMAND_ENTRY_CUSTOM("shader-pause", cmd_shader_pause, "shader",
                        "Pause shader animation",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE, NULL, NULL),
    COMMAND_ENTRY_CUSTOM("shader-resume", cmd_shader_resume, "shader",
                        "Resume shader animation",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE, NULL, NULL),

    /* Status & Information */
    COMMAND_ENTRY(status, "info", "Get daemon status and wallpaper info",
                  CMD_CAP_REQUIRES_STATE),
    COMMAND_ENTRY(version, "info", "Get daemon version information",
                  CMD_CAP_NONE),
    COMMAND_ENTRY(ping, "info", "Ping daemon (health check)",
                  CMD_CAP_NONE),
    COMMAND_ENTRY_CUSTOM("list-commands", cmd_list_commands, "info",
                        "List all available commands with metadata",
                        CMD_CAP_NONE, NULL, NULL),
    COMMAND_ENTRY_CUSTOM("command-stats", cmd_command_stats, "info",
                        "Get command execution statistics",
                        CMD_CAP_NONE,
                        "{\"command\": <string>}",
                        "{\"command\":\"command-stats\",\"args\":\"{\\\"command\\\":\\\"next\\\"}\"}"),

    COMMAND_SENTINEL
};

/* Build unified command registry from all modules */
static command_info_t command_registry[64]; /* Populated at init */
static size_t command_registry_size = 0;



/* Get registry size */
static size_t get_registry_size(void) {
    return command_registry_size;
}

/* Build unified registry from all modules */
static void build_unified_registry(void) {
    /* Get external module registries */
    extern const command_info_t *output_get_commands(void);
    extern const command_info_t *config_get_commands(void);

    size_t idx = 0;

    /* Add core commands */
    for (size_t i = 0; command_registry_core[i].name != NULL && idx < 63; i++) {
        command_registry[idx++] = command_registry_core[i];
    }

    /* Add output commands */
    const command_info_t *output_cmds = output_get_commands();
    for (size_t i = 0; output_cmds[i].name != NULL && idx < 63; i++) {
        command_registry[idx++] = output_cmds[i];
    }

    /* Add config commands */
    const command_info_t *config_cmds = config_get_commands();
    for (size_t i = 0; config_cmds[i].name != NULL && idx < 63; i++) {
        command_registry[idx++] = config_cmds[i];
    }

    /* Add sentinel */
    command_registry[idx] = (command_info_t)COMMAND_SENTINEL;
    command_registry_size = idx;
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
            /* Capture last error */
            snprintf(command_statistics[cmd_index].stats.last_error,
                     sizeof(command_statistics[cmd_index].stats.last_error),
                     "%s", resp->message);
        }

        /* Calculate execution time in microseconds */
        uint64_t elapsed_us = (end.tv_sec - start.tv_sec) * 1000000ULL +
                              (end.tv_nsec - start.tv_nsec) / 1000ULL;

        /* Update timing statistics */
        command_statistics[cmd_index].stats.total_time_us += elapsed_us;
        command_statistics[cmd_index].total_time_ns += (end.tv_sec - start.tv_sec) * 1000000000ULL +
                                                       (end.tv_nsec - start.tv_nsec);

        if (elapsed_us < command_statistics[cmd_index].stats.min_time_us) {
            command_statistics[cmd_index].stats.min_time_us = elapsed_us;
        }
        if (elapsed_us > command_statistics[cmd_index].stats.max_time_us) {
            command_statistics[cmd_index].stats.max_time_us = elapsed_us;
        }

        /* Calculate average */
        command_statistics[cmd_index].stats.avg_time_us =
            command_statistics[cmd_index].stats.total_time_us / command_statistics[cmd_index].stats.calls_total;

        /* Update last call timestamp */
        command_statistics[cmd_index].stats.last_called = time(NULL);
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
    /* Build unified registry from all modules */
    build_unified_registry();

    /* Initialize statistics */
    memset(command_statistics, 0, sizeof(command_statistics));
    for (size_t i = 0; i < get_registry_size(); i++) {
        command_statistics[i].stats.min_time_us = UINT64_MAX;
    }

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

size_t commands_generate_json_list(char *buffer, size_t size,
                                   const char *category_filter,
                                   const char *file_filter) {
    if (!buffer || size == 0) return 0;

    char *p = buffer;
    size_t remaining = size;
    int written;
    size_t matched = 0;

    written = snprintf(p, remaining, "{\"commands\":[");
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;
    remaining -= written;

    for (size_t i = 0; command_registry[i].name; i++) {
        /* Apply filters */
        if (category_filter && strcmp(command_registry[i].category, category_filter) != 0) {
            continue;
        }
        if (file_filter && strstr(command_registry[i].implementation_file, file_filter) == NULL) {
            continue;
        }

        written = snprintf(p, remaining,
                          "%s{\"name\":\"%s\",\"category\":\"%s\",\"handler\":\"%s\",\"file\":\"%s\"}",
                          matched > 0 ? "," : "",
                          command_registry[i].name,
                          command_registry[i].category,
                          command_registry[i].handler_name,
                          command_registry[i].implementation_file);
        if (written < 0 || (size_t)written >= remaining) return size;
        p += written;
        remaining -= written;
        matched++;
    }

    written = snprintf(p, remaining, "],\"total\":%zu,\"matched\":%zu}", get_registry_size(), matched);
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;

    return p - buffer;
}

size_t commands_get_all_stats_json(char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;

    char *p = buffer;
    size_t remaining = size;
    int written;

    written = snprintf(p, remaining, "{\"stats\":[");
    if (written < 0 || (size_t)written >= remaining) return size;
    p += written;
    remaining -= written;

    for (size_t i = 0; command_registry[i].name && i < get_registry_size(); i++) {
        const command_stats_internal_t *s = &command_statistics[i];

        written = snprintf(p, remaining,
                          "%s{\"command\":\"%s\",\"calls_total\":%lu,\"calls_success\":%lu,"
                          "\"calls_failed\":%lu,\"avg_time_us\":%lu,\"min_time_us\":%lu,"
                          "\"max_time_us\":%lu,\"last_called\":%ld}",
                          i > 0 ? "," : "",
                          command_registry[i].name,
                          s->stats.calls_total,
                          s->stats.calls_success,
                          s->stats.calls_failed,
                          s->stats.avg_time_us,
                          s->stats.min_time_us == UINT64_MAX ? 0 : s->stats.min_time_us,
                          s->stats.max_time_us,
                          (long)s->stats.last_called);
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

    static char buffer[32768];  /* Large buffer for detailed status responses */
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

    static char data[16384];  /* Increased buffer size for long paths */
    size_t offset = 0;

    /* Start JSON object */
    offset += snprintf(data + offset, sizeof(data) - offset,
                      "{\"daemon\":\"running\",\"pid\":%d,\"outputs\":%u,\"paused\":%s,\"shader_paused\":%s,\"shader_speed\":%.2f",
                      getpid(),
                      state->output_count,
                      atomic_load(&state->paused) ? "true" : "false",
                      atomic_load(&state->shader_paused) ? "true" : "false",
                      atomic_load(&state->shader_speed));

    /* Add wallpaper information */
    offset += snprintf(data + offset, sizeof(data) - offset, ",\"wallpapers\":[");

    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    bool first = true;

    while (output && offset < sizeof(data) - 512) {
        if (!first) {
            offset += snprintf(data + offset, sizeof(data) - offset, ",");
        }
        first = false;

        const char *type = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
        const char *path = (output->config.type == WALLPAPER_SHADER)
            ? output->config.shader_path
            : output->config.path;

        /* Escape path for JSON */
        char escaped_path[512];
        size_t j = 0;
        for (size_t i = 0; path[i] && j < sizeof(escaped_path) - 2; i++) {
            if (path[i] == '"' || path[i] == '\\') {
                escaped_path[j++] = '\\';
            }
            escaped_path[j++] = path[i];
        }
        escaped_path[j] = '\0';

        offset += snprintf(data + offset, sizeof(data) - offset,
                          "{\"output\":\"%s\",\"type\":\"%s\",\"path\":\"%s\",\"mode\":%u,\"cycle_index\":%zu,\"cycle_total\":%zu}",
                          output->connector_name,
                          type,
                          escaped_path,
                          (unsigned int)output->config.mode,
                          output->config.current_cycle_index,
                          output->config.cycle_count);

        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    /* Close wallpapers array and main object */
    offset += snprintf(data + offset, sizeof(data) - offset, "]}");

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

static command_result_t cmd_list_commands(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)state;

    /* Parse optional filters from args */
    const char *category_filter = NULL;
    const char *file_filter = NULL;

    if (req->args[0] != '\0') {
        /* Simple JSON parsing for filters */
        static char cat_buf[64], file_buf[128];

        const char *cat_key = strstr(req->args, "\"category\"");
        if (cat_key) {
            const char *colon = strchr(cat_key, ':');
            if (colon) {
                const char *val = colon + 1;
                while (*val && (*val == ' ' || *val == '"')) val++;
                const char *end = val;
                while (*end && *end != '"' && *end != ',' && *end != '}') end++;
                size_t len = end - val;
                if (len > 0 && len < sizeof(cat_buf)) {
                    strncpy(cat_buf, val, len);
                    cat_buf[len] = '\0';
                    category_filter = cat_buf;
                }
            }
        }

        const char *file_key = strstr(req->args, "\"file\"");
        if (file_key) {
            const char *colon = strchr(file_key, ':');
            if (colon) {
                const char *val = colon + 1;
                while (*val && (*val == ' ' || *val == '"')) val++;
                const char *end = val;
                while (*end && *end != '"' && *end != ',' && *end != '}') end++;
                size_t len = end - val;
                if (len > 0 && len < sizeof(file_buf)) {
                    strncpy(file_buf, val, len);
                    file_buf[len] = '\0';
                    file_filter = file_buf;
                }
            }
        }
    }

    /* Generate command list JSON directly into response data buffer */
    size_t len = commands_generate_json_list(resp->data, sizeof(resp->data),
                                            category_filter, file_filter);

    if (len >= sizeof(resp->data)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Command list too large for response buffer");
        return CMD_ERROR_FAILED;
    }

    /* Set response status */
    resp->status = IPC_STATUS_OK;
    strncpy(resp->message, "OK", sizeof(resp->message) - 1);

    return CMD_SUCCESS;
}

static command_result_t cmd_command_stats(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp) {
    (void)state;

    /* Check if specific command requested */
    const char *cmd_name = NULL;
    if (req->args[0] != '\0') {
        /* Simple JSON parsing for "command" field */
        const char *cmd_key = strstr(req->args, "\"command\"");
        if (cmd_key) {
            const char *colon = strchr(cmd_key, ':');
            if (colon) {
                const char *value_start = colon + 1;
                while (*value_start && (*value_start == ' ' || *value_start == '"')) {
                    value_start++;
                }
                static char cmd_buf[128];
                const char *value_end = value_start;
                while (*value_end && *value_end != '"' && *value_end != ',' && *value_end != '}') {
                    value_end++;
                }
                size_t len = value_end - value_start;
                if (len > 0 && len < sizeof(cmd_buf)) {
                    strncpy(cmd_buf, value_start, len);
                    cmd_buf[len] = '\0';
                    cmd_name = cmd_buf;
                }
            }
        }
    }

    if (cmd_name) {
        /* Get stats for specific command */
        command_stats_t stats;
        if (commands_get_stats(cmd_name, &stats) == 0) {
            snprintf(resp->data, sizeof(resp->data),
                    "{\"command\":\"%s\",\"calls_total\":%lu,\"calls_success\":%lu,"
                    "\"calls_failed\":%lu,\"avg_time_us\":%lu,\"min_time_us\":%lu,"
                    "\"max_time_us\":%lu,\"last_called\":%ld,\"last_error\":\"%s\"}",
                    cmd_name, stats.calls_total, stats.calls_success, stats.calls_failed,
                    stats.avg_time_us, stats.min_time_us == UINT64_MAX ? 0 : stats.min_time_us,
                    stats.max_time_us, (long)stats.last_called, stats.last_error);
            resp->status = IPC_STATUS_OK;
            strncpy(resp->message, "OK", sizeof(resp->message) - 1);
            return CMD_SUCCESS;
        } else {
            commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Command not found");
            return CMD_ERROR_INVALID_ARGS;
        }
    } else {
        /* Get stats for all commands */
        size_t len = commands_get_all_stats_json(resp->data, sizeof(resp->data));
        if (len >= sizeof(resp->data)) {
            commands_build_error(resp, CMD_ERROR_FAILED, "Stats too large for response buffer");
            return CMD_ERROR_FAILED;
        }
        resp->status = IPC_STATUS_OK;
        strncpy(resp->message, "OK", sizeof(resp->message) - 1);
        return CMD_SUCCESS;
    }
}
