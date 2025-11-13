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

/* ============================================================================
 * Output-Specific Configuration
 * ============================================================================ */

/**
 * Set wallpaper mode for specific output
 * 
 * Args: {"output": "DP-1", "mode": "fill|fit|center|stretch|tile"}
 */
command_result_t cmd_set_output_mode(struct neowall_state *state,
                                      const ipc_request_t *req,
                                      ipc_response_t *resp);

/**
 * Set cycle interval for specific output
 * 
 * Args: {"output": "DP-1", "interval": 300}
 */
command_result_t cmd_set_output_interval(struct neowall_state *state,
                                          const ipc_request_t *req,
                                          ipc_response_t *resp);

/**
 * Set wallpaper/shader for specific output
 * 
 * Args: {"output": "DP-1", "path": "/path/to/wallpaper.jpg"}
 */
command_result_t cmd_set_output_wallpaper(struct neowall_state *state,
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
 * Utility Functions
 * ============================================================================ */

/**
 * Extract output name from JSON args
 * Returns true on success, false if "output" key not found
 */
bool extract_output_name(const char *args_json, char *output_name, size_t size);

/**
 * Extract integer value from JSON args
 * Returns true on success
 */
bool extract_json_int(const char *args_json, const char *key, int *value);

/**
 * Extract string value from JSON args
 * Returns true on success
 */
bool extract_json_string(const char *args_json, const char *key, char *value, size_t size);

/**
 * Find output by name (connector name or model)
 * Returns NULL if not found
 * NOTE: Caller must hold output_list_lock (read or write)
 */
struct output_state *find_output_by_name(struct neowall_state *state, 
                                          const char *name);

#endif /* NEOWALL_OUTPUT_COMMANDS_H */