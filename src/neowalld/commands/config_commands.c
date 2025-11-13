/*
 * NeoWall Configuration Commands
 * Commands for querying and modifying runtime configuration
 */

#include "config_commands.h"
#include "commands.h"
#include "neowall.h"
#include "config/config_schema.h"
#include "config/config_write.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations of command handlers */
command_result_t cmd_get_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_reset_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_list_config_keys(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_reload(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Default config setting commands (match config key names) */
command_result_t cmd_set_default_path(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_shader(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_mode(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_duration(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_shader_speed(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_transition(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_default_transition_duration(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Config command registry */
static const command_info_t config_command_registry[] = {
    COMMAND_ENTRY_CUSTOM("get-config", cmd_get_config, "config",
                        "Get configuration value(s)",
                        CMD_CAP_REQUIRES_STATE,
                        "{\"key\": <string>}",
                        "{\"command\":\"get-config\",\"args\":\"{\\\"key\\\":\\\"general.cycle_interval\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-config", cmd_set_config, "config",
                        "Set configuration value",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"key\": <string>, \"value\": <string>}",
                        "{\"command\":\"set-config\",\"args\":\"{\\\"key\\\":\\\"general.cycle_interval\\\",\\\"value\\\":\\\"600\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("reset-config", cmd_reset_config, "config",
                        "Reset configuration to defaults",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"key\": <string>}",
                        "{\"command\":\"reset-config\",\"args\":\"{\\\"key\\\":\\\"general.cycle_interval\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("list-config-keys", cmd_list_config_keys, "config",
                        "List all configuration keys",
                        CMD_CAP_REQUIRES_STATE, NULL, NULL),

    COMMAND_ENTRY(reload, "config", "Reload configuration from disk",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),

    /* Default config setting commands (match config key names: default.*) */
    COMMAND_ENTRY_CUSTOM("set-default-path", cmd_set_default_path, "config",
                        "Set default image wallpaper path (matches config key: default.path)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"path\": <string>}",
                        "{\"command\":\"set-default-path\",\"args\":\"{\\\"path\\\":\\\"/path/to/wallpaper.jpg\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-shader", cmd_set_default_shader, "config",
                        "Set default shader wallpaper (matches config key: default.shader)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"shader\": <string>}",
                        "{\"command\":\"set-default-shader\",\"args\":\"{\\\"shader\\\":\\\"matrix_rain.glsl\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-mode", cmd_set_default_mode, "config",
                        "Set default wallpaper mode (matches config key: default.mode)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"mode\": <string>}",
                        "{\"command\":\"set-default-mode\",\"args\":\"{\\\"mode\\\":\\\"fill\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-duration", cmd_set_default_duration, "config",
                        "Set default cycle duration (matches config key: default.duration)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"duration\": <integer>}",
                        "{\"command\":\"set-default-duration\",\"args\":\"{\\\"duration\\\":600}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-shader-speed", cmd_set_default_shader_speed, "config",
                        "Set default shader speed (matches config key: default.shader_speed)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"speed\": <float>}",
                        "{\"command\":\"set-default-shader-speed\",\"args\":\"{\\\"speed\\\":2.0}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-transition", cmd_set_default_transition, "config",
                        "Set default transition effect (matches config key: default.transition)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"transition\": <string>}",
                        "{\"command\":\"set-default-transition\",\"args\":\"{\\\"transition\\\":\\\"fade\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-default-transition-duration", cmd_set_default_transition_duration, "config",
                        "Set default transition duration (matches config key: default.transition_duration)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"duration\": <integer>}",
                        "{\"command\":\"set-default-transition-duration\",\"args\":\"{\\\"duration\\\":500}\"}"),

    COMMAND_SENTINEL
};

/* Export command registry */
const command_info_t *config_get_commands(void) {
    return config_command_registry;
}

/* ============================================================================
 * Configuration Commands
 * ============================================================================ */

command_result_t cmd_get_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args to get key (optional) */
    const char *key = NULL;
    if (req->args[0] != '\0') {
        /* Simple JSON parsing for key */
        const char *key_start = strstr(req->args, "\"key\"");
        if (key_start) {
            key_start = strchr(key_start, ':');
            if (key_start) {
                key_start = strchr(key_start, '"');
                if (key_start) {
                    key_start++;
                    const char *key_end = strchr(key_start, '"');
                    if (key_end) {
                        static char key_buf[256];
                        size_t len = key_end - key_start;
                        if (len < sizeof(key_buf)) {
                            memcpy(key_buf, key_start, len);
                            key_buf[len] = '\0';
                            key = key_buf;
                        }
                    }
                }
            }
        }
    }

    static char data[4096];

    if (key) {
        /* Get specific key value */
        char value[1024];
        if (config_get_value(state, key, value, sizeof(value))) {
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"value\":\"%s\"}", key, value);
        } else {
            commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Unknown config key");
            return CMD_ERROR_INVALID_ARGS;
        }
    } else {
        /* Export all config as JSON */
        char *json = config_export_schema_json();
        if (json) {
            snprintf(data, sizeof(data), "%s", json);
            free(json);
        } else {
            commands_build_error(resp, CMD_ERROR_FAILED, "Failed to export config");
            return CMD_ERROR_FAILED;
        }
    }

    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args to get key and value */
    if (req->args[0] == '\0') {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                           "Missing arguments: requires 'key' and 'value'");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Simple JSON parsing for key */
    const char *key = NULL;
    const char *value = NULL;
    static char key_buf[256];
    static char value_buf[1024];

    const char *key_start = strstr(req->args, "\"key\"");
    if (key_start) {
        key_start = strchr(key_start, ':');
        if (key_start) {
            key_start = strchr(key_start, '"');
            if (key_start) {
                key_start++;
                const char *key_end = strchr(key_start, '"');
                if (key_end) {
                    size_t len = key_end - key_start;
                    if (len < sizeof(key_buf)) {
                        memcpy(key_buf, key_start, len);
                        key_buf[len] = '\0';
                        key = key_buf;
                    }
                }
            }
        }
    }

    const char *value_start = strstr(req->args, "\"value\"");
    if (value_start) {
        value_start = strchr(value_start, ':');
        if (value_start) {
            value_start = strchr(value_start, '"');
            if (value_start) {
                value_start++;
                const char *value_end = strchr(value_start, '"');
                if (value_end) {
                    size_t len = value_end - value_start;
                    if (len < sizeof(value_buf)) {
                        memcpy(value_buf, value_start, len);
                        value_buf[len] = '\0';
                        value = value_buf;
                    }
                }
            }
        }
    }

    if (!key || !value) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                           "Missing 'key' or 'value' in arguments");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Set the config value */
    char error[256];
    if (!config_set_value(state, key, value, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    /* Success */
    static char data[2048];
    snprintf(data, sizeof(data),
             "{\"key\":\"%s\",\"value\":\"%s\",\"status\":\"applied\"}",
             key, value);

    commands_build_success(resp, "Configuration updated successfully", data);
    return CMD_SUCCESS;
}

command_result_t cmd_reset_config(struct neowall_state *state,
                                 const ipc_request_t *req,
                                 ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args to get key (optional - if not provided, reset all) */
    const char *key = NULL;
    if (req->args[0] != '\0') {
        const char *key_start = strstr(req->args, "\"key\"");
        if (key_start) {
            key_start = strchr(key_start, ':');
            if (key_start) {
                key_start = strchr(key_start, '"');
                if (key_start) {
                    key_start++;
                    const char *key_end = strchr(key_start, '"');
                    if (key_end) {
                        static char key_buf[256];
                        size_t len = key_end - key_start;
                        if (len < sizeof(key_buf)) {
                            memcpy(key_buf, key_start, len);
                            key_buf[len] = '\0';
                            key = key_buf;
                        }
                    }
                }
            }
        }
    }

    /* Reset to defaults */
    if (!config_reset_to_default(state, key)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to reset configuration");
        return CMD_ERROR_FAILED;
    }

    /* Write to disk */
    if (!config_write_to_disk(state, true)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to write config to disk");
        return CMD_ERROR_FAILED;
    }

    /* Success */
    static char data[512];
    if (key) {
        snprintf(data, sizeof(data), "{\"key\":\"%s\",\"status\":\"reset\"}", key);
        commands_build_success(resp, "Configuration key reset to default", data);
    } else {
        snprintf(data, sizeof(data), "{\"status\":\"reset_all\"}");
        commands_build_success(resp, "All configuration reset to defaults", data);
    }

    return CMD_SUCCESS;
}

command_result_t cmd_list_config_keys(struct neowall_state *state,
                                      const ipc_request_t *req,
                                      ipc_response_t *resp) {
    (void)req;

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Use schema to export all config keys */
    char *schema_json = config_export_schema_json();
    if (!schema_json) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to export config schema");
        return CMD_ERROR_FAILED;
    }

    commands_build_success(resp, NULL, schema_json);
    free(schema_json);
    return CMD_SUCCESS;
}

command_result_t cmd_reload(struct neowall_state *state,
                           const ipc_request_t *req,
                           ipc_response_t *resp) {
    (void)state;
    (void)req;

    /* Reload is deprecated - config changes are applied immediately */
    commands_build_error(resp, CMD_ERROR_NOT_IMPLEMENTED,
                        "Config reload is deprecated. Use 'set-config' for runtime updates.");
    return CMD_ERROR_NOT_IMPLEMENTED;
}

/* ============================================================================
 * Default Config Setting Commands (match config key names: default.*)
 * ============================================================================ */

command_result_t cmd_set_default_path(struct neowall_state *state,
                                       const ipc_request_t *req,
                                       ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract path from args */
    static char path[4096];
    const char *path_start = strstr(req->args, "\"path\"");
    if (!path_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'path' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    path_start = strchr(path_start, ':');
    if (path_start) {
        path_start = strchr(path_start, '"');
        if (path_start) {
            path_start++;
            const char *path_end = strchr(path_start, '"');
            if (path_end) {
                size_t len = path_end - path_start;
                if (len < sizeof(path)) {
                    memcpy(path, path_start, len);
                    path[len] = '\0';
                }
            }
        }
    }

    /* Use config_set_value to set default.path */
    char error[256];
    if (!config_set_value(state, "default.path", path, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[4608];
    snprintf(data, sizeof(data), "{\"key\":\"default.path\",\"value\":\"%s\"}", path);
    commands_build_success(resp, "Default path updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_shader(struct neowall_state *state,
                                         const ipc_request_t *req,
                                         ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract shader from args */
    static char shader[4096];
    const char *shader_start = strstr(req->args, "\"shader\"");
    if (!shader_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'shader' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    shader_start = strchr(shader_start, ':');
    if (shader_start) {
        shader_start = strchr(shader_start, '"');
        if (shader_start) {
            shader_start++;
            const char *shader_end = strchr(shader_start, '"');
            if (shader_end) {
                size_t len = shader_end - shader_start;
                if (len < sizeof(shader)) {
                    memcpy(shader, shader_start, len);
                    shader[len] = '\0';
                }
            }
        }
    }

    /* Use config_set_value to set default.shader */
    char error[256];
    if (!config_set_value(state, "default.shader", shader, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[4608];
    snprintf(data, sizeof(data), "{\"key\":\"default.shader\",\"value\":\"%s\"}", shader);
    commands_build_success(resp, "Default shader updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_mode(struct neowall_state *state,
                                       const ipc_request_t *req,
                                       ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract mode from args */
    static char mode[64];
    const char *mode_start = strstr(req->args, "\"mode\"");
    if (!mode_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'mode' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    mode_start = strchr(mode_start, ':');
    if (mode_start) {
        mode_start = strchr(mode_start, '"');
        if (mode_start) {
            mode_start++;
            const char *mode_end = strchr(mode_start, '"');
            if (mode_end) {
                size_t len = mode_end - mode_start;
                if (len < sizeof(mode)) {
                    memcpy(mode, mode_start, len);
                    mode[len] = '\0';
                }
            }
        }
    }

    /* Use config_set_value to set default.mode */
    char error[256];
    if (!config_set_value(state, "default.mode", mode, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[256];
    snprintf(data, sizeof(data), "{\"key\":\"default.mode\",\"value\":\"%s\"}", mode);
    commands_build_success(resp, "Default mode updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_duration(struct neowall_state *state,
                                           const ipc_request_t *req,
                                           ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract duration from args */
    const char *duration_start = strstr(req->args, "\"duration\"");
    if (!duration_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'duration' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    duration_start = strchr(duration_start, ':');
    int duration = 0;
    if (duration_start) {
        duration = atoi(duration_start + 1);
    }

    static char value[32];
    snprintf(value, sizeof(value), "%d", duration);

    /* Use config_set_value to set default.duration */
    char error[256];
    if (!config_set_value(state, "default.duration", value, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[256];
    snprintf(data, sizeof(data), "{\"key\":\"default.duration\",\"value\":%d}", duration);
    commands_build_success(resp, "Default duration updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_shader_speed(struct neowall_state *state,
                                                const ipc_request_t *req,
                                                ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract speed from args */
    const char *speed_start = strstr(req->args, "\"speed\"");
    if (!speed_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'speed' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    speed_start = strchr(speed_start, ':');
    float speed = 1.0f;
    if (speed_start) {
        speed = strtof(speed_start + 1, NULL);
    }

    static char value[32];
    snprintf(value, sizeof(value), "%.2f", speed);

    /* Use config_set_value to set default.shader_speed */
    char error[256];
    if (!config_set_value(state, "default.shader_speed", value, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[256];
    snprintf(data, sizeof(data), "{\"key\":\"default.shader_speed\",\"value\":%.2f}", speed);
    commands_build_success(resp, "Default shader speed updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_transition(struct neowall_state *state,
                                             const ipc_request_t *req,
                                             ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract transition from args */
    static char transition[64];
    const char *transition_start = strstr(req->args, "\"transition\"");
    if (!transition_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'transition' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    transition_start = strchr(transition_start, ':');
    if (transition_start) {
        transition_start = strchr(transition_start, '"');
        if (transition_start) {
            transition_start++;
            const char *transition_end = strchr(transition_start, '"');
            if (transition_end) {
                size_t len = transition_end - transition_start;
                if (len < sizeof(transition)) {
                    memcpy(transition, transition_start, len);
                    transition[len] = '\0';
                }
            }
        }
    }

    /* Use config_set_value to set default.transition */
    char error[256];
    if (!config_set_value(state, "default.transition", transition, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[256];
    snprintf(data, sizeof(data), "{\"key\":\"default.transition\",\"value\":\"%s\"}", transition);
    commands_build_success(resp, "Default transition updated", data);
    return CMD_SUCCESS;
}

command_result_t cmd_set_default_transition_duration(struct neowall_state *state,
                                                      const ipc_request_t *req,
                                                      ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Extract duration from args */
    const char *duration_start = strstr(req->args, "\"duration\"");
    if (!duration_start) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'duration' argument");
        return CMD_ERROR_INVALID_ARGS;
    }

    duration_start = strchr(duration_start, ':');
    int duration = 300;
    if (duration_start) {
        duration = atoi(duration_start + 1);
    }

    static char value[32];
    snprintf(value, sizeof(value), "%d", duration);

    /* Use config_set_value to set default.transition_duration */
    char error[256];
    if (!config_set_value(state, "default.transition_duration", value, error, sizeof(error))) {
        commands_build_error(resp, CMD_ERROR_FAILED, error);
        return CMD_ERROR_FAILED;
    }

    static char data[256];
    snprintf(data, sizeof(data), "{\"key\":\"default.transition_duration\",\"value\":%d}", duration);
    commands_build_success(resp, "Default transition duration updated", data);
    return CMD_SUCCESS;
}
