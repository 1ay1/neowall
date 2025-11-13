/*
 * NeoWall Output Commands
 * Commands for per-output (per-monitor) control and management
 *
 * Config modifications now use: neowall set-config output.<name>.<key> <value>
 */

#include "output_commands.h"
#include "commands.h"
#include "neowall.h"
#include "config/vibe_path.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Forward declarations of command handlers */
command_result_t cmd_list_outputs(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_output_info(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_next_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_prev_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_reload_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_pause_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_resume_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_jump_to_output(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Output command registry */
static const command_info_t output_command_registry[] = {
    COMMAND_ENTRY_CUSTOM("list-outputs", cmd_list_outputs, "output",
                        "List all connected outputs",
                        CMD_CAP_REQUIRES_STATE, NULL, NULL),

    COMMAND_ENTRY_CUSTOM("output-info", cmd_output_info, "output",
                        "Get information about specific output",
                        CMD_CAP_REQUIRES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"output-info\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("next-output", cmd_next_output, "output",
                        "Switch to next wallpaper (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>} (optional)",
                        "{\"command\":\"next-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("prev-output", cmd_prev_output, "output",
                        "Switch to previous wallpaper (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>} (optional)",
                        "{\"command\":\"prev-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("reload-output", cmd_reload_output, "output",
                        "Reload wallpaper (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>} (optional)",
                        "{\"command\":\"reload-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("pause-output", cmd_pause_output, "output",
                        "Pause cycling (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>} (optional)",
                        "{\"command\":\"pause-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("resume-output", cmd_resume_output, "output",
                        "Resume cycling (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>} (optional)",
                        "{\"command\":\"resume-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("jump-to-output", cmd_jump_to_output, "output",
                        "Jump to specific cycle index (on specific output or all outputs if not specified)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string> (optional), \"index\": <integer>}",
                        "{\"command\":\"jump-to-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\",\\\"index\\\":3}\"}"),

    COMMAND_SENTINEL
};

/* Export command registry */
const command_info_t *output_get_commands(void) {
    return output_command_registry;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Find output by name
 */
static struct output_state *find_output_by_name(struct neowall_state *state, const char *output_name) {
    if (!state || !output_name) return NULL;

    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    while (output) {
        if (strcmp(output->connector_name, output_name) == 0) {
            pthread_rwlock_unlock(&state->output_list_lock);
            return output;
        }
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);
    return NULL;
}

/**
 * Extract output name from args (optional - returns true with empty string if not present)
 */
static bool extract_output_name(const char *args, char *output_buf, size_t buf_size) {
    if (!output_buf || buf_size == 0) return false;

    /* Default to empty string (means "all outputs") */
    output_buf[0] = '\0';

    /* If no args, apply to all outputs */
    if (!args || strlen(args) == 0) return true;

    /* Simple JSON parsing for "output" field */
    const char *output_start = strstr(args, "\"output\"");
    if (!output_start) return true;  /* No output specified = all outputs */

    output_start = strchr(output_start, ':');
    if (!output_start) return true;

    output_start = strchr(output_start, '"');
    if (!output_start) return true;

    output_start++;
    const char *output_end = strchr(output_start, '"');
    if (!output_end) return true;

    size_t len = output_end - output_start;
    if (len >= buf_size) return false;

    memcpy(output_buf, output_start, len);
    output_buf[len] = '\0';
    return true;
}

/**
 * Extract integer value from args
 */
static bool extract_int_arg(const char *args, const char *key, int *out_value) {
    if (!args || !key || !out_value) return false;

    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *key_start = strstr(args, search);
    if (!key_start) return false;

    const char *value_start = strchr(key_start, ':');
    if (!value_start) return false;

    /* Skip whitespace */
    value_start++;
    while (*value_start && isspace(*value_start)) value_start++;

    *out_value = atoi(value_start);
    return true;
}

/* ============================================================================
 * Command Implementations
 * ============================================================================ */

command_result_t cmd_list_outputs(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    (void)req;  /* Unused parameter */

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    static char data[4096];
    char *ptr = data;
    size_t remaining = sizeof(data);
    int written;

    written = snprintf(ptr, remaining, "{\"outputs\":[");
    if (written < 0 || (size_t)written >= remaining) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Buffer overflow");
        return CMD_ERROR_FAILED;
    }
    ptr += written;
    remaining -= written;

    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    bool first = true;

    while (output) {
        written = snprintf(ptr, remaining, "%s{\"name\":\"%s\",\"width\":%d,\"height\":%d}",
                          first ? "" : ",",
                          output->connector_name,
                          output->width,
                          output->height);

        if (written < 0 || (size_t)written >= remaining) {
            pthread_rwlock_unlock(&state->output_list_lock);
            commands_build_error(resp, CMD_ERROR_FAILED, "Buffer overflow");
            return CMD_ERROR_FAILED;
        }

        ptr += written;
        remaining -= written;
        first = false;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    written = snprintf(ptr, remaining, "]}");
    if (written < 0 || (size_t)written >= remaining) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Buffer overflow");
        return CMD_ERROR_FAILED;
    }

    commands_build_success(resp, "Listed all outputs", data);
    return CMD_SUCCESS;
}

command_result_t cmd_output_info(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* output-info requires an output name */
    if (output_name[0] == '\0') {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "output-info requires an output name");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    static char data[8192];
    snprintf(data, sizeof(data),
             "{\"name\":\"%s\",\"width\":%d,\"height\":%d,"
             "\"current_wallpaper\":\"%s\"}",
             output->connector_name,
             output->width,
             output->height,
             output->config.path[0] ? output->config.path : "");

    commands_build_success(resp, "Output information", data);
    return CMD_SUCCESS;
}

command_result_t cmd_next_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        output_cycle_wallpaper(output);

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"next\"}", output_name);
        commands_build_success(resp, "Switched to next wallpaper", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    struct output_state *output = state->outputs;
    while (output) {
        output_cycle_wallpaper(output);
        count++;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"action\":\"next\"}", count);
    commands_build_success(resp, "Switched all outputs to next wallpaper", data);
    return CMD_SUCCESS;
}

command_result_t cmd_prev_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        /* Cycle to previous wallpaper */
        if (output->config.cycle && output->config.cycle_count > 0) {
            if (output->config.current_cycle_index > 0) {
                output->config.current_cycle_index--;
            } else {
                output->config.current_cycle_index = output->config.cycle_count - 1;
            }

            const char *wallpaper = output->config.cycle_paths[output->config.current_cycle_index];
            output_set_wallpaper(output, wallpaper);
        }

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"prev\"}", output_name);
        commands_build_success(resp, "Switched to previous wallpaper", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    struct output_state *output = state->outputs;
    while (output) {
        if (output->config.cycle && output->config.cycle_count > 0) {
            if (output->config.current_cycle_index > 0) {
                output->config.current_cycle_index--;
            } else {
                output->config.current_cycle_index = output->config.cycle_count - 1;
            }

            const char *wallpaper = output->config.cycle_paths[output->config.current_cycle_index];
            output_set_wallpaper(output, wallpaper);
        }
        count++;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"action\":\"prev\"}", count);
    commands_build_success(resp, "Switched all outputs to previous wallpaper", data);
    return CMD_SUCCESS;
}

command_result_t cmd_reload_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        /* Reload current wallpaper */
        if (output->config.path[0]) {
            output_set_wallpaper(output, output->config.path);
        }

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"reload\"}", output_name);
        commands_build_success(resp, "Reloaded wallpaper", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    struct output_state *output = state->outputs;
    while (output) {
        if (output->config.path[0]) {
            output_set_wallpaper(output, output->config.path);
        }
        count++;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"action\":\"reload\"}", count);
    commands_build_success(resp, "Reloaded wallpaper on all outputs", data);
    return CMD_SUCCESS;
}

command_result_t cmd_pause_output(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        output->config.duration = 0.0f;  /* Pause by setting duration to 0 */

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"pause\"}", output_name);
        commands_build_success(resp, "Paused cycling", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    struct output_state *output = state->outputs;
    while (output) {
        output->config.duration = 0.0f;  /* Pause by setting duration to 0 */
        count++;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"action\":\"pause\"}", count);
    commands_build_success(resp, "Paused cycling on all outputs", data);
    return CMD_SUCCESS;
}

command_result_t cmd_resume_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        /* Resume by restoring duration - in a real implementation,
         * we'd need to read the original duration from config */
        if (output->config.duration == 0.0f) {
            output->config.duration = 300.0f;  /* Default 5 minutes */
        }

        /* If shader animation was paused, resume it */
        if (output->config.type == WALLPAPER_SHADER) {
            output->shader_paused = false;
        }

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"resume\"}", output_name);
        commands_build_success(resp, "Resumed cycling", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    struct output_state *output = state->outputs;
    while (output) {
        /* Resume by restoring duration */
        if (output->config.duration == 0.0f) {
            output->config.duration = 300.0f;  /* Default 5 minutes */
        }

        /* If shader animation was paused, resume it */
        if (output->config.type == WALLPAPER_SHADER) {
            output->shader_paused = false;
        }

        count++;
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"action\":\"resume\"}", count);
    commands_build_success(resp, "Resumed cycling on all outputs", data);
    return CMD_SUCCESS;
}

command_result_t cmd_jump_to_output(struct neowall_state *state,
                                     const ipc_request_t *req,
                                     ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name (optional) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Extract index (required) */
    int index;
    if (!extract_int_arg(req->args, "index", &index)) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'index' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* If output is specified, apply to that output only */
    if (output_name[0] != '\0') {
        struct output_state *output = find_output_by_name(state, output_name);
        if (!output) {
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
            return CMD_ERROR_NOT_FOUND;
        }

        /* Validate index */
        if (!output->config.cycle || output->config.cycle_count == 0) {
            commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "No cycle configured for this output");
            return CMD_ERROR_INVALID_ARGS;
        }

        if (index < 0 || (size_t)index >= output->config.cycle_count) {
            commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Index out of range");
            return CMD_ERROR_INVALID_ARGS;
        }

        /* Jump to index */
        output->config.current_cycle_index = index;
        const char *wallpaper = output->config.cycle_paths[index];
        output_set_wallpaper(output, wallpaper);

        static char data[512];
        snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"jump\",\"index\":%d}", output_name, index);
        commands_build_success(resp, "Jumped to cycle index", data);
        return CMD_SUCCESS;
    }

    /* No output specified - apply to ALL outputs */
    pthread_rwlock_rdlock(&state->output_list_lock);

    int count = 0;
    int skipped = 0;
    struct output_state *output = state->outputs;
    while (output) {
        if (output->config.cycle && output->config.cycle_count > 0 &&
            index >= 0 && (size_t)index < output->config.cycle_count) {
            output->config.current_cycle_index = index;
            const char *wallpaper = output->config.cycle_paths[index];
            output_set_wallpaper(output, wallpaper);
            count++;
        } else {
            skipped++;
        }
        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    static char data[512];
    snprintf(data, sizeof(data), "{\"outputs_affected\":%d,\"skipped\":%d,\"action\":\"jump\",\"index\":%d}",
             count, skipped, index);
    commands_build_success(resp, "Jumped all outputs to cycle index", data);
    return CMD_SUCCESS;
}
