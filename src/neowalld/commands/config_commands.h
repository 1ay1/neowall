/*
 * NeoWall Configuration Commands
 * Commands for querying runtime configuration
 */

#ifndef NEOWALL_CONFIG_COMMANDS_H
#define NEOWALL_CONFIG_COMMANDS_H

#include "commands.h"

/* ============================================================================
 * Configuration Query Commands
 * ============================================================================ */

/**
 * Get configuration value(s)
 * 
 * Args: {"key": "general.cycle_interval"}  (optional - if omitted, returns all config)
 * 
 * Returns: Configuration value or full config object
 */
command_result_t cmd_get_config(struct neowall_state *state,
                                const ipc_request_t *req,
                                ipc_response_t *resp);

/**
 * List all available configuration keys
 * 
 * Returns: Array of config keys with metadata:
 * {
 *   "keys": [
 *     {
 *       "key": "general.cycle_interval",
 *       "type": "integer",
 *       "description": "Default cycle interval in seconds",
 *       "current_value": 300
 *     },
 *     ...
 *   ]
 * }
 */
command_result_t cmd_list_config_keys(struct neowall_state *state,
                                      const ipc_request_t *req,
                                      ipc_response_t *resp);

/**
 * Reload configuration (deprecated)
 * Always returns error directing user to restart daemon
 */
command_result_t cmd_reload(struct neowall_state *state,
                           const ipc_request_t *req,
                           ipc_response_t *resp);

#endif /* NEOWALL_CONFIG_COMMANDS_H */