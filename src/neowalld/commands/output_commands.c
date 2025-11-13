/*
 * NeoWall Output Commands
 * Commands for per-output (per-monitor) control and management
 */

#include "output_commands.h"
#include "neowall.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Extract output name from JSON args
 * Simple JSON parsing for "output" field
 */
bool extract_output_name(const char *args_json, char *output_name, size_t size) {
    if (!args_json || !output_name || size == 0) {
        return false;
    }

    /* Find "output" key */
    const char *output_key = strstr(args_json, "\"output\"");
    if (!output_key) {
        return false;
    }

    /* Find the value after the colon */
    const char *colon = strchr(output_key, ':');
    if (!colon) {
        return false;
    }

    /* Skip whitespace and opening quote */
    const char *value_start = colon + 1;
    while (*value_start && isspace(*value_start)) {
        value_start++;
    }
    if (*value_start == '"') {
        value_start++;
    }

    /* Find closing quote */
    const char *value_end = strchr(value_start, '"');
    if (!value_end) {
        return false;
    }

    /* Copy the value */
    size_t len = value_end - value_start;
    if (len >= size) {
        len = size - 1;
    }
    memcpy(output_name, value_start, len);
    output_name[len] = '\0';

    return len > 0;
}

/**
 * Extract integer value from JSON args
 */
bool extract_json_int(const char *args_json, const char *key, int *value) {
    if (!args_json || !key || !value) {
        return false;
    }

    char key_pattern[128];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);

    const char *key_pos = strstr(args_json, key_pattern);
    if (!key_pos) {
        return false;
    }

    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return false;
    }

    /* Skip whitespace */
    const char *value_start = colon + 1;
    while (*value_start && isspace(*value_start)) {
        value_start++;
    }

    /* Parse integer */
    char *endptr;
    long val = strtol(value_start, &endptr, 10);
    if (endptr == value_start) {
        return false;
    }

    *value = (int)val;
    return true;
}

/**
 * Extract string value from JSON args
 */
bool extract_json_string(const char *args_json, const char *key, char *value, size_t size) {
    if (!args_json || !key || !value || size == 0) {
        return false;
    }

    char key_pattern[128];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);

    const char *key_pos = strstr(args_json, key_pattern);
    if (!key_pos) {
        return false;
    }

    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return false;
    }

    /* Skip whitespace and opening quote */
    const char *value_start = colon + 1;
    while (*value_start && isspace(*value_start)) {
        value_start++;
    }
    if (*value_start == '"') {
        value_start++;
    }

    /* Find closing quote */
    const char *value_end = strchr(value_start, '"');
    if (!value_end) {
        return false;
    }

    /* Copy the value */
    size_t len = value_end - value_start;
    if (len >= size) {
        len = size - 1;
    }
    memcpy(value, value_start, len);
    value[len] = '\0';

    return len > 0;
}

/**
 * Find output by name (connector name or model)
 * NOTE: Caller must hold output_list_lock (read or write)
 */
struct output_state *find_output_by_name(struct neowall_state *state, const char *name) {
    if (!state || !name) {
        return NULL;
    }

    struct output_state *output = state->outputs;
    while (output) {
        /* Try connector name first (e.g., HDMI-A-2, DP-1) */
        if (output->connector_name[0] != '\0' && strcmp(output->connector_name, name) == 0) {
            return output;
        }
        /* Fall back to model name */
        if (strcmp(output->model, name) == 0) {
            return output;
        }
        output = output->next;
    }

    return NULL;
}

/* ============================================================================
 * Output Information Commands
 * ============================================================================ */

command_result_t cmd_list_outputs(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    static char data[16384];
    size_t offset = 0;

    /* Start JSON object */
    offset += snprintf(data + offset, sizeof(data) - offset, "{\"outputs\":[");

    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    bool first = true;

    while (output && offset < sizeof(data) - 1024) {
        if (!first) {
            offset += snprintf(data + offset, sizeof(data) - offset, ",");
        }
        first = false;

        const char *type = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
        const char *path = (output->config.type == WALLPAPER_SHADER)
            ? output->config.shader_path
            : output->config.path;

        /* Escape path for JSON */
        char escaped_path[1024];
        size_t j = 0;
        for (size_t i = 0; path[i] && j < sizeof(escaped_path) - 2; i++) {
            if (path[i] == '"' || path[i] == '\\') {
                escaped_path[j++] = '\\';
            }
            escaped_path[j++] = path[i];
        }
        escaped_path[j] = '\0';

        /* Get mode string */
        const char *mode_str = "unknown";
        switch (output->config.mode) {
            case MODE_FILL: mode_str = "fill"; break;
            case MODE_FIT: mode_str = "fit"; break;
            case MODE_CENTER: mode_str = "center"; break;
            case MODE_STRETCH: mode_str = "stretch"; break;
            case MODE_TILE: mode_str = "tile"; break;
        }

        offset += snprintf(data + offset, sizeof(data) - offset,
                          "{\"name\":\"%s\",\"model\":\"%s\",\"width\":%d,\"height\":%d,\"scale\":%d,"
                          "\"wallpaper_type\":\"%s\",\"wallpaper_path\":\"%s\",\"mode\":\"%s\","
                          "\"cycle_index\":%zu,\"cycle_total\":%zu}",
                          output->connector_name,
                          output->model,
                          output->logical_width,
                          output->logical_height,
                          output->scale,
                          type,
                          escaped_path,
                          mode_str,
                          output->config.current_cycle_index,
                          output->config.cycle_count);

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

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    const char *type = (output->config.type == WALLPAPER_SHADER) ? "shader" : "image";
    const char *path = (output->config.type == WALLPAPER_SHADER)
        ? output->config.shader_path
        : output->config.path;

    /* Escape path for JSON */
    char escaped_path[1024];
    size_t j = 0;
    for (size_t i = 0; path[i] && j < sizeof(escaped_path) - 2; i++) {
        if (path[i] == '"' || path[i] == '\\') {
            escaped_path[j++] = '\\';
        }
        escaped_path[j++] = path[i];
    }
    escaped_path[j] = '\0';

    /* Get mode string */
    const char *mode_str = "unknown";
    switch (output->config.mode) {
        case MODE_FILL: mode_str = "fill"; break;
        case MODE_FIT: mode_str = "fit"; break;
        case MODE_CENTER: mode_str = "center"; break;
        case MODE_STRETCH: mode_str = "stretch"; break;
        case MODE_TILE: mode_str = "tile"; break;
    }

    char data[2048];
    snprintf(data, sizeof(data),
             "{\"name\":\"%s\",\"model\":\"%s\",\"width\":%d,\"height\":%d,\"scale\":%d,"
             "\"wallpaper_type\":\"%s\",\"wallpaper_path\":\"%s\",\"mode\":\"%s\","
             "\"cycle_index\":%zu,\"cycle_total\":%zu}",
             output->connector_name,
             output->model,
             output->logical_width,
             output->logical_height,
             output->scale,
             type,
             escaped_path,
             mode_str,
             output->config.current_cycle_index,
             output->config.cycle_count);

    pthread_rwlock_unlock(&state->output_list_lock);

    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

/* ============================================================================
 * Output-Specific Wallpaper Control
 * ============================================================================ */

command_result_t cmd_next_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Increment cycle index and load the wallpaper */
    if (output->config.cycle_count > 0 && output->config.cycle_paths) {
        output->config.current_cycle_index = (output->config.current_cycle_index + 1) % output->config.cycle_count;

        /* Get the wallpaper path at new index */
        const char *next_path = output->config.cycle_paths[output->config.current_cycle_index];

        /* Determine if it's a shader or image and load it */
        const char *ext = strrchr(next_path, '.');
        if (ext && strcmp(ext, ".glsl") == 0) {
            output_set_shader(output, next_path);
        } else {
            output_set_wallpaper(output, next_path);
        }

        /* Update cycle time */
        output->last_cycle_time = get_time_ms();

        log_info("Output %s: advanced to wallpaper %zu/%zu: %s",
                 output->connector_name,
                 output->config.current_cycle_index + 1,
                 output->config.cycle_count,
                 next_path);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Switched to next wallpaper on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_prev_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Decrement cycle index and load the wallpaper */
    if (output->config.cycle_count > 0 && output->config.cycle_paths) {
        if (output->config.current_cycle_index == 0) {
            output->config.current_cycle_index = output->config.cycle_count - 1;
        } else {
            output->config.current_cycle_index--;
        }

        /* Get the wallpaper path at new index */
        const char *prev_path = output->config.cycle_paths[output->config.current_cycle_index];

        /* Determine if it's a shader or image and load it */
        const char *ext = strrchr(prev_path, '.');
        if (ext && strcmp(ext, ".glsl") == 0) {
            output_set_shader(output, prev_path);
        } else {
            output_set_wallpaper(output, prev_path);
        }

        /* Update cycle time */
        output->last_cycle_time = get_time_ms();

        log_info("Output %s: went back to wallpaper %zu/%zu: %s",
                 output->connector_name,
                 output->config.current_cycle_index + 1,
                 output->config.cycle_count,
                 prev_path);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Switched to previous wallpaper on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_reload_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Reload current wallpaper */
    if (output->config.cycle_count > 0 && output->config.cycle_paths) {
        const char *current_path = output->config.cycle_paths[output->config.current_cycle_index];

        /* Determine if it's a shader or image and reload it */
        const char *ext = strrchr(current_path, '.');
        if (ext && strcmp(ext, ".glsl") == 0) {
            output_set_shader(output, current_path);
        } else {
            output_set_wallpaper(output, current_path);
        }

        log_info("Output %s: reloaded wallpaper: %s", output->connector_name, current_path);
    } else if (output->config.type == WALLPAPER_SHADER) {
        output_set_shader(output, output->config.shader_path);
        log_info("Output %s: reloaded shader: %s", output->connector_name, output->config.shader_path);
    } else {
        output_set_wallpaper(output, output->config.path);
        log_info("Output %s: reloaded wallpaper: %s", output->connector_name, output->config.path);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Reloaded wallpaper on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_pause_output(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Pause cycling for this output (set duration to very large value) */
    output->config.duration = 86400.0f * 365.0f; /* Effectively pause: 1 year */
    log_info("Output %s: paused cycling (duration set to max)", output->connector_name);

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Paused cycling on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_resume_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Resume cycling for this output (reset to default duration) */
    output->config.duration = 300.0f; /* Reset to 5 minutes default */
    output->last_cycle_time = 0; /* Trigger immediate cycle */
    log_info("Output %s: resumed cycling (duration reset)", output->connector_name);

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Resumed cycling on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

/* ============================================================================
 * Output-Specific Configuration
 * ============================================================================ */

command_result_t cmd_set_output_mode(struct neowall_state *state,
                                      const ipc_request_t *req,
                                      ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    char mode_str[32];
    if (!extract_json_string(req->args, "mode", mode_str, sizeof(mode_str))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'mode' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Parse mode */
    enum wallpaper_mode mode;
    if (strcmp(mode_str, "fill") == 0) {
        mode = MODE_FILL;
    } else if (strcmp(mode_str, "fit") == 0) {
        mode = MODE_FIT;
    } else if (strcmp(mode_str, "center") == 0) {
        mode = MODE_CENTER;
    } else if (strcmp(mode_str, "stretch") == 0) {
        mode = MODE_STRETCH;
    } else if (strcmp(mode_str, "tile") == 0) {
        mode = MODE_TILE;
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Invalid mode '%s'. Must be: fill, fit, center, stretch, or tile", mode_str);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Set mode and trigger redraw */
    output->config.mode = mode;
    output->needs_redraw = true;
    log_info("Output %s: set mode to %s", output->connector_name, mode_str);

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Set mode to %s on %s", mode_str, output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_set_output_interval(struct neowall_state *state,
                                          const ipc_request_t *req,
                                          ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    int interval;
    if (!extract_json_int(req->args, "interval", &interval) || interval < 1 || interval > 86400) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'interval' argument (must be 1-86400)");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Set interval (duration in seconds) and reset timer */
    output->config.duration = (float)interval;
    output->last_cycle_time = 0; /* Reset cycle timer */
    log_info("Output %s: set cycle interval to %d seconds", output->connector_name, interval);

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Set cycle interval to %ds on %s", interval, output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_set_output_wallpaper(struct neowall_state *state,
                                           const ipc_request_t *req,
                                           ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    char path[4096];
    if (!extract_json_string(req->args, "path", path, sizeof(path))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'path' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* TODO: Validate path exists and is readable */

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Determine type based on extension */
    const char *ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".glsl") == 0) {
        output->config.type = WALLPAPER_SHADER;
        strncpy(output->config.shader_path, path, sizeof(output->config.shader_path) - 1);
        output->config.shader_path[sizeof(output->config.shader_path) - 1] = '\0';
    } else {
        output->config.type = WALLPAPER_IMAGE;
        strncpy(output->config.path, path, sizeof(output->config.path) - 1);
        output->config.path[sizeof(output->config.path) - 1] = '\0';
    }

    output->needs_redraw = true;
    output->last_cycle_time = 0; /* Force immediate load */
    log_info("Output %s: set wallpaper to %s", output->connector_name, path);

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Set wallpaper on %s", output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}

command_result_t cmd_jump_to_output(struct neowall_state *state,
                                     const ipc_request_t *req,
                                     ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    char output_name[128];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    int index;
    if (!extract_json_int(req->args, "index", &index) || index < 0) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing or invalid 'index' argument (must be >= 0)");
        return CMD_ERROR_INVALID_ARGS;
    }

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = find_output_by_name(state, output_name);

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Output '%s' not found", output_name);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Validate index */
    if ((size_t)index >= output->config.cycle_count) {
        pthread_rwlock_unlock(&state->output_list_lock);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Index %d out of range (0-%zu)", index, output->config.cycle_count - 1);
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, error_msg);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Jump to index and load the wallpaper */
    output->config.current_cycle_index = (size_t)index;

    if (output->config.cycle_paths) {
        const char *jump_path = output->config.cycle_paths[output->config.current_cycle_index];

        /* Determine if it's a shader or image and load it */
        const char *ext = strrchr(jump_path, '.');
        if (ext && strcmp(ext, ".glsl") == 0) {
            output_set_shader(output, jump_path);
        } else {
            output_set_wallpaper(output, jump_path);
        }

        /* Update cycle time */
        output->last_cycle_time = get_time_ms();

        log_info("Output %s: jumped to wallpaper %d/%zu: %s",
                 output->connector_name,
                 index + 1,
                 output->config.cycle_count,
                 jump_path);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    char message[256];
    snprintf(message, sizeof(message), "Jumped to index %d on %s", index, output_name);
    commands_build_success(resp, message, NULL);
    return CMD_SUCCESS;
}
