/*
 * NeoWall Configuration Commands
 * Commands for querying runtime configuration
 */

#include "config_commands.h"
#include "commands.h"
#include "neowall.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations of command handlers */
command_result_t cmd_get_config(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_list_config_keys(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);
command_result_t cmd_reload(struct neowall_state *state, const ipc_request_t *req, ipc_response_t *resp);

/* Config command registry */
static const command_info_t config_command_registry[] = {
    COMMAND_ENTRY_CUSTOM("get-config", cmd_get_config, "config",
                        "Get configuration value(s)",
                        CMD_CAP_REQUIRES_STATE,
                        "{\"key\": <string>}",
                        "{\"command\":\"get-config\",\"args\":\"{\\\"key\\\":\\\"general.cycle_interval\\\"}\"}"),

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
 * Configuration Query Commands
 * ============================================================================ */

command_result_t cmd_get_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp) {
    (void)req; /* Unused - no args needed for basic config query */

    if (!state) {
        commands_build_error(resp, CMD_ERROR_STATE, "Daemon state not available");
        return CMD_ERROR_STATE;
    }

    /* For now, return a simple message that config querying is not yet fully implemented */
    /* In the future, this would introspect the state->config structure */

    static char data[4096];
    snprintf(data, sizeof(data),
             "{\"note\":\"Config query not yet implemented\","
             "\"suggestion\":\"Use 'neowall config' to edit config file, then 'neowall restart'\"}");

    commands_build_success(resp, NULL, data);
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

    /* Return available config categories and common keys */
    static char data[2048];
    snprintf(data, sizeof(data),
             "{\"keys\":["
             "{\"key\":\"general.cycle_interval\",\"type\":\"integer\",\"description\":\"Default cycle interval in seconds\"},"
             "{\"key\":\"general.log_level\",\"type\":\"string\",\"description\":\"Logging level (debug|info|warn|error)\"},"
             "{\"key\":\"shader.speed\",\"type\":\"float\",\"description\":\"Global shader speed multiplier\"},"
             "{\"key\":\"output.<name>.type\",\"type\":\"string\",\"description\":\"Wallpaper type (image|shader)\"},"
             "{\"key\":\"output.<name>.path\",\"type\":\"string\",\"description\":\"Wallpaper or shader path\"},"
             "{\"key\":\"output.<name>.mode\",\"type\":\"string\",\"description\":\"Display mode (fill|fit|center|stretch|tile)\"},"
             "{\"key\":\"output.<name>.cycle_interval\",\"type\":\"integer\",\"description\":\"Per-output cycle interval\"}"
             "],\"note\":\"Use 'neowall config' to edit configuration file\"}");

    commands_build_success(resp, NULL, data);
    return CMD_SUCCESS;
}

command_result_t cmd_reload(struct neowall_state *state,
                           const ipc_request_t *req,
                           ipc_response_t *resp) {
    (void)state;
    (void)req;

    /* Reload is deprecated - return error with helpful message */
    commands_build_error(resp, CMD_ERROR_INVALID_ARGS,
                        "Reload not supported. Please restart the daemon: neowall restart");
    return CMD_ERROR_INVALID_ARGS;
}
