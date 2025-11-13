#ifndef CONFIG_WRITE_H
#define CONFIG_WRITE_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
struct neowall_state;
struct output_state;

/* ============================================================================
 * ATOMIC CONFIGURATION WRITE API
 * ============================================================================
 * This module handles safe, atomic writing of configuration to disk.
 * 
 * Key Features:
 * - Atomic operations (write to temp, then rename)
 * - Automatic backups before changes
 * - Fsync for durability
 * - Thread-safe operations
 * - Rollback on error
 * 
 * Write Flow:
 * 1. Validate new value
 * 2. Create backup of current config
 * 3. Update in-memory state
 * 4. Write to temporary file
 * 5. Fsync temp file
 * 6. Atomic rename to actual config
 * 7. Fsync directory
 * 8. On error: restore from backup
 * ============================================================================ */

/**
 * Set a configuration value and persist atomically
 * 
 * This is the main API for runtime configuration updates.
 * It handles validation, in-memory updates, disk persistence,
 * and applying changes to affected outputs.
 * 
 * @param state NeoWall state
 * @param key Config key (e.g., "general.cycle_interval")
 * @param value New value as string
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_len Size of error buffer
 * @return true on success, false on error
 * 
 * Example:
 *   char error[256];
 *   if (!config_set_value(state, "general.cycle_interval", "600", error, sizeof(error))) {
 *       log_error("Config update failed: %s", error);
 *   }
 */
bool config_set_value(struct neowall_state *state, 
                      const char *key, 
                      const char *value,
                      char *error_buf, 
                      size_t error_len);

/**
 * Set per-output configuration value
 * 
 * @param state NeoWall state
 * @param output_name Output name (e.g., "DP-1")
 * @param key Config key (e.g., "cycle_interval")
 * @param value New value as string
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_len Size of error buffer
 * @return true on success, false on error
 * 
 * Example:
 *   config_set_output_value(state, "DP-1", "cycle_interval", "300", error, sizeof(error));
 */
bool config_set_output_value(struct neowall_state *state,
                             const char *output_name,
                             const char *key,
                             const char *value,
                             char *error_buf,
                             size_t error_len);

/**
 * Write current in-memory configuration to disk (atomic)
 * 
 * This flushes the entire config state to disk atomically.
 * Used by config_set_value internally, but can also be called
 * directly for batch updates.
 * 
 * @param state NeoWall state
 * @param backup Create backup before writing
 * @return true on success, false on error
 */
bool config_write_to_disk(struct neowall_state *state, bool backup);

/**
 * Create backup of current configuration
 * 
 * Backs up config file to <path>.backup with timestamp
 * 
 * @param config_path Path to config file
 * @return true on success, false on error
 */
bool config_create_backup(const char *config_path);

/**
 * Restore configuration from backup
 * 
 * @param config_path Path to config file
 * @return true on success, false on error
 */
bool config_restore_from_backup(const char *config_path);

/**
 * Get current value of a configuration key
 * 
 * @param state NeoWall state
 * @param key Config key
 * @param value_buf Buffer for value string
 * @param value_len Size of value buffer
 * @return true if key found, false otherwise
 */
bool config_get_value(struct neowall_state *state,
                     const char *key,
                     char *value_buf,
                     size_t value_len);

/**
 * Get current value of per-output configuration key
 * 
 * @param state NeoWall state
 * @param output_name Output name
 * @param key Config key
 * @param value_buf Buffer for value string
 * @param value_len Size of value buffer
 * @return true if key found, false otherwise
 */
bool config_get_output_value(struct neowall_state *state,
                            const char *output_name,
                            const char *key,
                            char *value_buf,
                            size_t value_len);

/**
 * Export configuration to VIBE format string
 * 
 * @param state NeoWall state
 * @return Allocated string with config in VIBE format (caller must free)
 */
char *config_export_vibe(struct neowall_state *state);

/**
 * Import configuration from VIBE format string
 * 
 * @param state NeoWall state
 * @param vibe_content VIBE format configuration
 * @param apply Apply changes immediately (vs just parse)
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_len Size of error buffer
 * @return true on success, false on error
 */
bool config_import_vibe(struct neowall_state *state,
                       const char *vibe_content,
                       bool apply,
                       char *error_buf,
                       size_t error_len);

/**
 * Apply configuration changes to runtime state
 * 
 * Called after in-memory config is updated to propagate
 * changes to affected outputs and runtime state.
 * 
 * @param state NeoWall state
 * @param key Config key that changed
 * @param value New value
 * @return true on success, false on error
 */
bool config_apply_change(struct neowall_state *state,
                        const char *key,
                        const char *value);

/**
 * Apply output-specific configuration change
 * 
 * @param output Output state
 * @param key Config key
 * @param value New value
 * @return true on success, false on error
 */
bool config_apply_output_change(struct output_state *output,
                               const char *key,
                               const char *value);

#endif /* CONFIG_WRITE_H */