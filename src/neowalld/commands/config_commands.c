/*
 * NeoWall Configuration Commands
 * Simple VIBE path-based configuration with persistence
 *
 * Commands:
 *   - get-config <key>        - Read value using VIBE path
 *   - set-config <key> <value> - Write value using VIBE path (persists!)
 *   - reset-config <key|--all> - Delete keys from config
 *   - list-config-keys        - List all available keys
 */

#include "config_commands.h"
#include "commands.h"
#include "neowall.h"
#include "config/config.h"
#include "config/vibe.h"
#include "config/vibe_path.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Forward declarations */
command_result_t cmd_get_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_set_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_reset_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_list_config_keys(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Config command registry */
static const command_info_t config_command_registry[] = {
    COMMAND_ENTRY_CUSTOM("get-config", cmd_get_config, "config",
                        "Get configuration value using VIBE path",
                        CMD_CAP_REQUIRES_STATE,
                        "{\"key\": <string>}",
                        "{\"command\":\"get-config\",\"args\":\"{\\\"key\\\":\\\"default.shader\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("set-config", cmd_set_config, "config",
                        "Set configuration value using VIBE path (persists to config.vibe)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"key\": <string>, \"value\": <string>}",
                        "{\"command\":\"set-config\",\"args\":\"{\\\"key\\\":\\\"default.shader\\\",\\\"value\\\":\\\"plasma.glsl\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("reset-config", cmd_reset_config, "config",
                        "Reset configuration (delete keys from config.vibe)",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE,
                        "{\"key\": <string>} or --all",
                        "{\"command\":\"reset-config\",\"args\":\"{\\\"key\\\":\\\"default.shader_speed\\\"}\"}"),

    COMMAND_ENTRY_CUSTOM("list-config-keys", cmd_list_config_keys, "config",
                        "List all configuration keys",
                        CMD_CAP_REQUIRES_STATE, NULL, NULL),

    COMMAND_ENTRY_CUSTOM("reload", cmd_reload, "config",
                        "Reload configuration file",
                        CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE, NULL, NULL),

    COMMAND_SENTINEL
};

/* Export command registry */
const command_info_t *config_get_commands(void) {
    return config_command_registry;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Parse JSON args to extract key and value
 */
static bool parse_key_value_args(const char *args, char *key_buf, size_t key_size,
                                 char *value_buf, size_t value_size) {
    if (!args || !key_buf || !value_buf) return false;

    key_buf[0] = '\0';
    value_buf[0] = '\0';

    /* Parse key */
    const char *key_start = strstr(args, "\"key\"");
    if (key_start) {
        key_start = strchr(key_start, ':');
        if (key_start) {
            key_start = strchr(key_start, '"');
            if (key_start) {
                key_start++;
                const char *key_end = strchr(key_start, '"');
                if (key_end) {
                    size_t len = key_end - key_start;
                    if (len < key_size) {
                        memcpy(key_buf, key_start, len);
                        key_buf[len] = '\0';
                    }
                }
            }
        }
    }

    /* Parse value */
    const char *value_start = strstr(args, "\"value\"");
    if (value_start) {
        value_start = strchr(value_start, ':');
        if (value_start) {
            value_start = strchr(value_start, '"');
            if (value_start) {
                value_start++;
                const char *value_end = strchr(value_start, '"');
                if (value_end) {
                    size_t len = value_end - value_start;
                    if (len < value_size) {
                        memcpy(value_buf, value_start, len);
                        value_buf[len] = '\0';
                    }
                }
            }
        }
    }

    return (key_buf[0] != '\0');
}

/**
 * Load config.vibe file
 */
static VibeValue *load_config_file(const char *config_path) {
    VibeParser *parser = vibe_parser_new();
    if (!parser) return NULL;

    VibeValue *root = vibe_parse_file(parser, config_path);
    vibe_parser_free(parser);

    return root;
}

/**
 * Get config file path
 */
static const char *get_config_file_path(struct neowall_state *state) {
    if (state->config_path[0] != '\0') {
        return state->config_path;
    }
    return config_get_default_path();
}

/* ============================================================================
 * Command Implementations
 * ============================================================================ */

/**
 * get-config - Get configuration value using VIBE path
 *
 * Examples:
 *   neowall get-config default.shader
 *   neowall get-config output.DP-1.mode
 *   neowall get-config output.HDMI-A-1.duration
 */
command_result_t cmd_get_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args */
    char key[512] = {0};
    char value_buf[1024] = {0};

    if (req->args[0] != '\0') {
        parse_key_value_args(req->args, key, sizeof(key), value_buf, sizeof(value_buf));
    }

    /* Load config file */
    const char *config_path = get_config_file_path(state);
    VibeValue *root = load_config_file(config_path);

    if (!root) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to load config file");
        return CMD_ERROR_FAILED;
    }

    /* If no key provided, return all config */
    if (key[0] == '\0') {
        static char data[65536];
        snprintf(data, sizeof(data), "{\"config_path\":\"%s\",\"note\":\"Use 'list-config-keys' to see available keys\"}", config_path);
        commands_build_success(resp, "Config file loaded", data);
        vibe_value_free(root);
        return CMD_SUCCESS;
    }

    /* Validate path */
    if (!vibe_path_validate(key)) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid key path");
        vibe_value_free(root);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Get value */
    VibeValue *value = vibe_path_get(root, key);
    if (!value) {
        static char error[512];
        snprintf(error, sizeof(error), "Key not found: %s", key);
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, error);
        vibe_value_free(root);
        return CMD_ERROR_NOT_FOUND;
    }

    /* Format response based on type */
    static char data[8192];
    switch (value->type) {
        case VIBE_TYPE_STRING:
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"value\":\"%s\",\"type\":\"string\"}",
                    key, value->as_string);
            break;
        case VIBE_TYPE_INTEGER:
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"value\":%ld,\"type\":\"integer\"}",
                    key, value->as_integer);
            break;
        case VIBE_TYPE_FLOAT:
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"value\":%.2f,\"type\":\"float\"}",
                    key, value->as_float);
            break;
        case VIBE_TYPE_BOOLEAN:
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"value\":%s,\"type\":\"boolean\"}",
                    key, value->as_boolean ? "true" : "false");
            break;
        default:
            snprintf(data, sizeof(data), "{\"key\":\"%s\",\"type\":\"object or array\"}", key);
            break;
    }

    commands_build_success(resp, "Config value retrieved", data);
    vibe_value_free(root);
    return CMD_SUCCESS;
}

/**
 * set-config - Set configuration value using VIBE path (persists!)
 *
 * Examples:
 *   neowall set-config default.shader plasma.glsl
 *   neowall set-config output.DP-1.mode fill
 *   neowall set-config default.shader_speed 2.0
 */
command_result_t cmd_set_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args */
    char key[512] = {0};
    char value[4096] = {0};

    if (!parse_key_value_args(req->args, key, sizeof(key), value, sizeof(value))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                           "Missing 'key' or 'value' in arguments");
        return CMD_ERROR_INVALID_ARGS;
    }

    if (key[0] == '\0' || value[0] == '\0') {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                           "Both 'key' and 'value' are required");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Validate path */
    if (!vibe_path_validate(key)) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid key path");
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Load config file */
    const char *config_path = get_config_file_path(state);
    VibeValue *root = load_config_file(config_path);

    if (!root) {
        /* Create new root if file doesn't exist */
        root = vibe_value_new_object();
        if (!root) {
            commands_build_error(resp, CMD_ERROR_FAILED, "Failed to create config object");
            return CMD_ERROR_FAILED;
        }
    }

    /* Determine value type and set */
    bool success = false;

    /* Try to parse as number */
    char *endptr;
    if (strchr(value, '.')) {
        /* Try float */
        double float_val = strtod(value, &endptr);
        if (*endptr == '\0') {
            success = vibe_path_set_float(root, key, float_val);
        }
    } else {
        /* Try integer */
        long long_val = strtol(value, &endptr, 10);
        if (*endptr == '\0') {
            success = vibe_path_set_int(root, key, long_val);
        }
    }

    /* Try boolean */
    if (!success) {
        if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0) {
            success = vibe_path_set_bool(root, key, true);
        } else if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0) {
            success = vibe_path_set_bool(root, key, false);
        }
    }

    /* Default to string */
    if (!success) {
        success = vibe_path_set_string(root, key, value);
    }

    if (!success) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to set config value");
        vibe_value_free(root);
        return CMD_ERROR_FAILED;
    }

    /* Write back to file (atomic) */
    if (!vibe_path_write_file(root, config_path)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to write config file");
        vibe_value_free(root);
        return CMD_ERROR_FAILED;
    }

    vibe_value_free(root);

    /* Reload daemon config */
    config_load(state, config_path);

    /* Success response */
    static char data[8192];
    snprintf(data, sizeof(data),
             "{\"key\":\"%s\",\"value\":\"%s\",\"persisted\":true,\"config_path\":\"%s\"}",
             key, value, config_path);

    commands_build_success(resp, "Configuration updated and persisted", data);
    return CMD_SUCCESS;
}

/**
 * reset-config - Delete keys from config (reset to defaults)
 *
 * Examples:
 *   neowall reset-config default.shader_speed
 *   neowall reset-config output.DP-1
 *   neowall reset-config --all
 */
command_result_t cmd_reset_config(struct neowall_state *state,
                                 const ipc_request_t *req,
                                 ipc_response_t *resp) {
    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Parse args */
    char key[512] = {0};
    char value_buf[1024] = {0};

    if (req->args[0] != '\0') {
        parse_key_value_args(req->args, key, sizeof(key), value_buf, sizeof(value_buf));
    }

    /* Check for --all flag */
    bool reset_all = (strcmp(key, "--all") == 0);

    /* Load config file */
    const char *config_path = get_config_file_path(state);
    VibeValue *root = load_config_file(config_path);

    if (!root) {
        commands_build_error(resp, CMD_ERROR_FAILED, "No config file to reset");
        return CMD_ERROR_FAILED;
    }

    if (reset_all) {
        /* Clear entire config - create new empty root */
        vibe_value_free(root);
        root = vibe_value_new_object();
        if (!root) {
            commands_build_error(resp, CMD_ERROR_FAILED, "Failed to create empty config");
            return CMD_ERROR_FAILED;
        }
    } else if (key[0] != '\0') {
        /* Delete specific key */
        if (!vibe_path_validate(key)) {
            commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Invalid key path");
            vibe_value_free(root);
            return CMD_ERROR_INVALID_ARGS;
        }

        if (!vibe_path_delete(root, key)) {
            static char error[512];
            snprintf(error, sizeof(error), "Key not found: %s", key);
            commands_build_error(resp, CMD_ERROR_NOT_FOUND, error);
            vibe_value_free(root);
            return CMD_ERROR_NOT_FOUND;
        }
    } else {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                           "Specify a key or use --all to reset everything");
        vibe_value_free(root);
        return CMD_ERROR_INVALID_ARGS;
    }

    /* Write back to file */
    if (!vibe_path_write_file(root, config_path)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to write config file");
        vibe_value_free(root);
        return CMD_ERROR_FAILED;
    }

    vibe_value_free(root);

    /* Reload daemon config */
    config_load(state, config_path);

    /* Success response */
    static char data[8192];
    if (reset_all) {
        snprintf(data, sizeof(data),
                 "{\"reset\":\"all\",\"config_path\":\"%s\"}", config_path);
    } else {
        snprintf(data, sizeof(data),
                 "{\"deleted_key\":\"%s\",\"config_path\":\"%s\"}", key, config_path);
    }

    commands_build_success(resp, "Configuration reset", data);
    return CMD_SUCCESS;
}

/**
 * list-config-keys - List all configuration keys
 */
command_result_t cmd_list_config_keys(struct neowall_state *state,
                                      const ipc_request_t *req,
                                      ipc_response_t *resp) {
    (void)req; /* Unused */

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Load config file */
    const char *config_path = get_config_file_path(state);
    VibeValue *root = load_config_file(config_path);

    if (!root) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to load config file");
        return CMD_ERROR_FAILED;
    }

    /* List all keys */
    const char *keys[1000];
    size_t count = vibe_path_list_all_keys(root, keys, 1000);

    /* Build JSON response */
    static char data[65536];
    int offset = snprintf(data, sizeof(data), "{\"count\":%zu,\"keys\":[", count);

    for (size_t i = 0; i < count && offset < (int)sizeof(data) - 100; i++) {
        offset += snprintf(data + offset, sizeof(data) - offset,
                          "%s\"%s\"", (i > 0) ? "," : "", keys[i]);
        free((void*)keys[i]); /* Free duplicated strings */
    }

    offset += snprintf(data + offset, sizeof(data) - offset, "]}");

    vibe_value_free(root);

    commands_build_success(resp, "Config keys listed", data);
    return CMD_SUCCESS;
}

/* ============================================================================
 * Reload Configuration Command
 * ============================================================================ */

command_result_t cmd_reload(struct neowall_state *state,
                           const ipc_request_t *req,
                           ipc_response_t *resp) {
    (void)req; /* Unused */

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* Reload configuration from file */
    if (state->config_path[0] == '\0') {
        commands_build_error(resp, CMD_ERROR_STATE, "No configuration file path set");
        return CMD_ERROR_STATE;
    }

    if (!config_load(state, state->config_path)) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to reload configuration");
        return CMD_ERROR_FAILED;
    }

    commands_build_success(resp, "Configuration reloaded successfully", NULL);
    return CMD_SUCCESS;
}
