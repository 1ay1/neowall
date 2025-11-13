/*
 * NeoWall Output Commands
 * Commands for per-output (per-monitor) control and management
 */

#ifndef NEOWALL_OUTPUT_COMMANDS_H
#define NEOWALL_OUTPUT_COMMANDS_H

#include "commands.h"

/* ============================================================================
 * Output Information Commands
 * ============================================================================ */

/**
 * List all connected outputs with current state
 * 
 * Returns JSON array with output information:
 * {
 *   "outputs": [
 *     {
 *       "name": "DP-1",
 *       "model": "Dell U2720Q",
 *       "width": 3840,
 *       "height": 2160,
 *       "scale": 2,
 *       "wallpaper_type": "shader",
 *       "wallpaper_path": "/path/to/shader.glsl",
 *       "mode": "fill",
 *       "cycle_index": 2,
 *       "cycle_total": 10
 *     },
 *     ...
 *   ]
 * }
 */
command_result_t cmd_list_outputs(struct neowall_state *state, 
                                   const ipc_request_t *req, 
                                   ipc_response_t *resp);

/**
 * Get detailed information about a specific output
 * 
 * Args: {"output": "DP-1"}
 * 
 * Returns: {
 *   "name": "DP-1",
 *   "model": "Dell U2720Q",
 *   "width": 3840,
 *   "height": 2160,
 *   "scale": 2,
 *   "wallpaper_type": "shader",
 *   "wallpaper_path": "/path/to/shader.glsl",
 *   "mode": "fill",
 *   "cycle_index": 2,
 *   "cycle_total": 10,
 *   "cycle_interval": 300
 * }
 */
command_result_t cmd_output_info(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp);

/* ============================================================================
 * Output-Specific Wallpaper Control
 * ============================================================================ */

/**
 * Switch to next wallpaper on specific output
 * 
 * Args: {"output": "DP-1"}
 */
command_result_t cmd_next_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp);

/**
 * Switch to previous wallpaper on specific output
 * 
 * Args: {"output": "DP-1"}
 */
command_result_t cmd_prev_output(struct neowall_state *state,
                                  const ipc_request_t *req,
                                  ipc_response_t *resp);

/**
 * Reload current wallpaper on specific output
 * 
 * Args: {"output": "DP-1"}
 */
command_result_t cmd_reload_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp);

/**
 * Pause cycling on specific output
 * 
 * Args: {"output": "DP-1"}
 */
command_result_t cmd_pause_output(struct neowall_state *state,
                                   const ipc_request_t *req,
                                   ipc_response_t *resp);

/**
 * Resume cycling on specific output
 * 
 * Args: {"output": "DP-1"}
 */
command_result_t cmd_resume_output(struct neowall_state *state,
                                    const ipc_request_t *req,
                                    ipc_response_t *resp);

/**
 * Jump to specific index in wallpaper cycle
 * 
 * Args: {"output": "DP-1", "index": 3}
 */
command_result_t cmd_jump_to_output(struct neowall_state *state,
                                     const ipc_request_t *req,
                                     ipc_response_t *resp);

/* ============================================================================
 * Command Registry Export
 * ============================================================================ */

/**
 * Get output commands registry
 * Returns NULL-terminated array of command_info_t for output commands
 */
const command_info_t *output_get_commands(void);

#endif /* NEOWALL_OUTPUT_COMMANDS_H */