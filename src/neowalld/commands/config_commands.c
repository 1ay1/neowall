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
    static char data[512];
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
    static char data[256];
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
