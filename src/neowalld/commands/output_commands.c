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
                        "Switch to next wallpaper on specific output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"next-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("prev-output", cmd_prev_output, "output",
                        "Switch to previous wallpaper on specific output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"prev-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("reload-output", cmd_reload_output, "output",
                        "Reload wallpaper on specific output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"reload-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("pause-output", cmd_pause_output, "output",
                        "Pause cycling on specific output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"pause-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("resume-output", cmd_resume_output, "output",
                        "Resume cycling on specific output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>}",
                        "{\"command\":\"resume-output\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("jump-to-output", cmd_jump_to_output, "output",
                        "Jump to specific cycle index on output",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"output\": <string>, \"index\": <integer>}",
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
 * Extract output name from args
 */
static bool extract_output_name(const char *args, char *output_buf, size_t buf_size) {
    if (!args || !output_buf || buf_size == 0) return false;

    /* Simple JSON parsing for "output" field */
    const char *output_start = strstr(args, "\"output\"");
    if (!output_start) return false;

    output_start = strchr(output_start, ':');
    if (!output_start) return false;

    output_start = strchr(output_start, '"');
    if (!output_start) return false;

    output_start++;
    const char *output_end = strchr(output_start, '"');
    if (!output_end) return false;

    size_t len = output_end - output_start;
    if (len >= buf_size) return false;

    memcpy(output_buf, output_start, len);
    output_buf[len] = '\0';
    return true;
}

/* ============================================================================
 * Command Implementations
 * ============================================================================ */

command_result_t cmd_list_outputs(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Build JSON array of outputs */
    static char data[8192];
    int offset = snprintf(data, sizeof(data), "{\"outputs\":[");

    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    bool first = true;

    while (output) {
        if (!first) {
            offset += snprintf(data + offset, sizeof(data) - offset, ",");
        }
        first = false;

        const char *type_str = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
        const char *current = output->config.path;

        offset += snprintf(data + offset, sizeof(data) - offset,
                          "{\"name\":\"%s\",\"width\":%d,\"height\":%d,"
                          "\"scale\":%d,\"type\":\"%s\",\"current\":\"%s\","
                          "\"mode\":\"%s\"}",
                          output->connector_name,
                          output->width,
                          output->height,
                          output->scale,
                          type_str,
                          current,
                          wallpaper_mode_to_string(output->config.mode));

        output = output->next;
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    offset += snprintf(data + offset, sizeof(data) - offset, "]}");

    commands_build_success(resp, NULL, data);
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
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Build detailed info */
    static char data[8192];
    const char *type_str = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
    const char *current = output->config.path;

    snprintf(data, sizeof(data),
             "{\"name\":\"%s\",\"width\":%d,\"height\":%d,\"scale\":%d,"
             "\"make\":\"%s\",\"model\":\"%s\","
             "\"type\":\"%s\",\"current\":\"%s\",\"mode\":\"%s\","
             "\"duration\":%.0f,\"transition\":\"%s\",\"shader_speed\":%.2f,"
             "\"cycle_enabled\":%s,\"cycle_index\":%zu,\"cycle_count\":%zu,"
             "\"frames_rendered\":%lu}",
             output->connector_name,
             output->width,
             output->height,
             output->scale,
             output->make,
             output->model,
             type_str,
             current,
             wallpaper_mode_to_string(output->config.mode),
             output->config.duration,
             transition_type_to_string(output->config.transition),
             output->config.shader_speed,
             output->config.cycle ? "true" : "false",
             output->config.current_cycle_index,
             output->config.cycle_count,
             output->frames_rendered);

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

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Cycle to next wallpaper */
    output_cycle_wallpaper(output);

    static char data[512];
    snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"next\"}", output_name);
    commands_build_success(resp, "Switched to next wallpaper", data);
    return CMD_SUCCESS;
}

command_result_t cmd_prev_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
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

command_result_t cmd_reload_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Reload current wallpaper */
    if (output->config.type == WALLPAPER_SHADER) {
        output_set_shader(output, output->config.path);
    } else {
        output_set_wallpaper(output, output->config.path);
    }

    static char data[512];
    snprintf(data, sizeof(data), "{\"output\":\"%s\",\"action\":\"reload\"}", output_name);
    commands_build_success(resp, "Reloaded wallpaper", data);
    return CMD_SUCCESS;
}

command_result_t cmd_pause_output(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Pause cycling by setting duration to 0 */
    output->config.duration = 0.0f;

    static char data[512];
    snprintf(data, sizeof(data), "{\"output\":\"%s\",\"paused\":true}", output_name);
    commands_build_success(resp, "Paused output cycling", data);
    return CMD_SUCCESS;
}

command_result_t cmd_resume_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Resume cycling - restore duration from config file */
    if (output->config.cycle && output->config.duration == 0.0f) {
        /* Try to read duration from config.vibe */
        if (state->config_path[0] != '\0') {
            VibeParser *parser = vibe_parser_new();
            if (parser) {
                VibeValue *root = vibe_parse_file(parser, state->config_path);
                if (root) {
                    /* Try output-specific duration first */
                    char path[512];
                    snprintf(path, sizeof(path), "output.%s.duration", output_name);
                    double duration = 0.0;

                    if (vibe_path_get_float(root, path, &duration) && duration > 0.0) {
                        output->config.duration = (float)duration;
                    } else if (vibe_path_get_float(root, "default.duration", &duration) && duration > 0.0) {
                        /* Fall back to default duration */
                        output->config.duration = (float)duration;
                    } else {
                        /* Final fallback: hardcoded default */
                        output->config.duration = 300.0f;
                        log_info("No duration found in config for output %s, using 300s default", output_name);
                    }

                    vibe_value_free(root);
                } else {
                    /* Config parse failed, use default */
                    output->config.duration = 300.0f;
                    log_info("Failed to parse config file, using 300s default for output %s", output_name);
                }
                vibe_parser_free(parser);
            }
        } else {
            /* No config path, use default */
            output->config.duration = 300.0f;
        }
    }

    static char data[512];
    snprintf(data, sizeof(data), "{\"output\":\"%s\",\"paused\":false,\"duration\":%.1f}",
             output_name, output->config.duration);
    commands_build_success(resp, "Resumed output cycling", data);
    return CMD_SUCCESS;
}

command_result_t cmd_jump_to_output(struct neowall_state *state,
                                     const ipc_request_t *req,
                                     ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract output name */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Extract index */
    const char *index_start = strstr(req->args, "\"index\"");
    if (!index_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'index' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    index_start = strchr(index_start, ':');
    if (!index_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid 'index' format");
        return CMD_ERROR_INVALID_ARGS;
    }

    int index = atoi(index_start + 1);

    /* Find output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }

    /* Validate index */
    if (!output->config.cycle || output->config.cycle_count == 0) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Output is not in cycling mode");
        return CMD_ERROR_FAILED;
    }

    if (index < 0 || (size_t)index >= output->config.cycle_count) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Index out of range");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Jump to index */
    output->config.current_cycle_index = (size_t)index;
    const char *wallpaper = output->config.cycle_paths[index];
    output_set_wallpaper(output, wallpaper);

    static char data[512];
    snprintf(data, sizeof(data), "{\"output\":\"%s\",\"index\":%d}", output_name, index);
    commands_build_success(resp, "Jumped to wallpaper", data);
    return CMD_SUCCESS;
}
