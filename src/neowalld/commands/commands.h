/*
 * NeoWall Command System
 * Modern, extensible command handling API
 */

#ifndef NEOWALL_COMMANDS_H
#define NEOWALL_COMMANDS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "ipc/protocol.h"

/* Forward declarations */
struct neowall_state;

/**
 * Command execution result
 */
typedef enum {
    CMD_SUCCESS = 0,           /* Command executed successfully */
    CMD_ERROR_INVALID_ARGS,    /* Invalid or missing arguments */
    CMD_ERROR_STATE,           /* Invalid daemon state */
    CMD_ERROR_FAILED,          /* Command execution failed */
    CMD_ERROR_NOT_IMPLEMENTED, /* Command not yet implemented */
    CMD_ERROR_PERMISSION,      /* Permission denied */
    CMD_ERROR_NOT_FOUND,       /* Resource not found */
} command_result_t;

/**
 * Command handler function signature
 * 
 * @param state: Daemon state (may be NULL for stateless commands)
 * @param req: IPC request containing command and arguments
 * @param resp: IPC response to populate with result
 * @return command_result_t indicating success or failure type
 */
typedef command_result_t (*command_handler_t)(
    struct neowall_state *state,
    const ipc_request_t *req,
    ipc_response_t *resp
);

/**
 * Command capability flags
 */
typedef enum {
    CMD_CAP_NONE = 0,
    CMD_CAP_REQUIRES_STATE = (1 << 0),  /* Needs daemon state */
    CMD_CAP_MODIFIES_STATE = (1 << 1),  /* Changes daemon state */
    CMD_CAP_ADMIN_ONLY = (1 << 2),      /* Requires admin privileges */
    CMD_CAP_ASYNC = (1 << 3),           /* Can execute asynchronously */
} command_capabilities_t;

/**
 * Command metadata
 * Contains all information about a registered command
 */
typedef struct {
    const char *name;                    /* Command name (e.g., "next") */
    const char *category;                /* Category (e.g., "wallpaper") */
    const char *description;             /* Short description */
    const char *args_schema;             /* JSON schema for args (or NULL) */
    const char *example;                 /* Example usage (or NULL) */
    const char *handler_name;            /* Handler function name string (e.g., "cmd_next") */
    const char *implementation_file;     /* Source file implementing handler */
    command_handler_t handler;           /* Handler function pointer */
    command_capabilities_t capabilities; /* Command capabilities/flags */
    unsigned int version;                /* Command version (for compatibility) */
} command_info_t;

/**
 * Command statistics
 */
typedef struct {
    unsigned long calls_total;     /* Total number of calls */
    unsigned long calls_success;   /* Successful executions */
    unsigned long calls_failed;    /* Failed executions */
    uint64_t total_time_us;        /* Total execution time (microseconds) */
    uint64_t avg_time_us;          /* Average execution time (microseconds) */
    uint64_t min_time_us;          /* Minimum execution time (microseconds) */
    uint64_t max_time_us;          /* Maximum execution time (microseconds) */
    time_t last_called;            /* Last call timestamp */
    char last_error[512];          /* Last error message */
} command_stats_t;

/* ============================================================================
 * Command Registration Macros
 * ============================================================================ */

/**
 * COMMAND_ENTRY - Simplified command registration macro
 * 
 * Usage:
 *   COMMAND_ENTRY(next, "wallpaper", "Switch to next wallpaper", 
 *                 CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE)
 * 
 * This macro automatically:
 * - Stringifies the command name
 * - Generates handler_name as "cmd_<name>"
 * - Captures implementation file with __FILE__
 * - Links to handler function cmd_<name>
 * - Sets version to 1
 * - Sets args_schema and example to NULL (can be overridden)
 */
#define COMMAND_ENTRY(cmd_name, cat, desc, caps) \
    { \
        .name = #cmd_name, \
        .category = cat, \
        .description = desc, \
        .args_schema = NULL, \
        .example = NULL, \
        .handler_name = "cmd_" #cmd_name, \
        .implementation_file = __FILE__, \
        .handler = cmd_##cmd_name, \
        .capabilities = caps, \
        .version = 1 \
    }

/**
 * COMMAND_ENTRY_WITH_ARGS - Command registration with args schema and example
 * 
 * Usage:
 *   COMMAND_ENTRY_WITH_ARGS(output_info, "output", "Get output info",
 *                           CMD_CAP_REQUIRES_STATE,
 *                           "{\"output\": <string>}",
 *                           "{\"command\":\"output-info\",\"args\":\"{\\\"output\\\":\\\"DP-1\\\"}\"}")
 */
#define COMMAND_ENTRY_WITH_ARGS(cmd_name, cat, desc, caps, schema, ex) \
    { \
        .name = #cmd_name, \
        .category = cat, \
        .description = desc, \
        .args_schema = schema, \
        .example = ex, \
        .handler_name = "cmd_" #cmd_name, \
        .implementation_file = __FILE__, \
        .handler = cmd_##cmd_name, \
        .capabilities = caps, \
        .version = 1 \
    }

/**
 * COMMAND_ENTRY_CUSTOM - Full control over all fields
 * For special cases where automatic naming doesn't work
 */
#define COMMAND_ENTRY_CUSTOM(cmd_name, handler_func, cat, desc, caps, schema, ex) \
    { \
        .name = cmd_name, \
        .category = cat, \
        .description = desc, \
        .args_schema = schema, \
        .example = ex, \
        .handler_name = #handler_func, \
        .implementation_file = __FILE__, \
        .handler = handler_func, \
        .capabilities = caps, \
        .version = 1 \
    }

/**
 * COMMAND_SENTINEL - Marks end of command array
 */
#define COMMAND_SENTINEL \
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 }

/* ============================================================================
 * Core API - Command Execution
 * ============================================================================ */

/**
 * Main command dispatcher
 * Routes incoming IPC requests to appropriate command handlers
 * 
 * @param req: IPC request from client
 * @param resp: IPC response to populate
 * @param user_data: Opaque pointer to neowall_state
 * @return 0 on success, -1 on error
 */
int commands_dispatch(const ipc_request_t *req, 
                      ipc_response_t *resp,
                      void *user_data);

/* ============================================================================
 * Command Registry - Introspection & Discovery
 * ============================================================================ */

/**
 * Get list of all registered commands
 * 
 * @param count: Output - number of commands (optional, can be NULL)
 * @return Array of command metadata (NULL-terminated)
 */
const command_info_t *commands_get_all(size_t *count);

/**
 * Find a command by name
 * 
 * @param name: Command name to look up
 * @return Command metadata or NULL if not found
 */
const command_info_t *commands_find(const char *name);

/**
 * Get commands in a specific category
 * 
 * @param category: Category name (e.g., "wallpaper", "config")
 * @param count: Output - number of commands in category
 * @return Array of command metadata (NULL-terminated)
 */
const command_info_t *commands_get_by_category(const char *category, size_t *count);

/**
 * Check if a command exists
 * 
 * @param name: Command name
 * @return true if command exists, false otherwise
 */
bool commands_exists(const char *name);

/* ============================================================================
 * Command Statistics & Monitoring
 * ============================================================================ */

/**
 * Get statistics for a specific command
 * 
 * @param name: Command name
 * @param stats: Output - statistics structure
 * @return 0 on success, -1 if command not found
 */
int commands_get_stats(const char *name, command_stats_t *stats);

/**
 * Get statistics for all commands
 * 
 * @param buffer: Output buffer for JSON statistics
 * @param size: Size of buffer
 * @return Number of bytes written (or would be written if buffer too small)
 */
size_t commands_get_all_stats_json(char *buffer, size_t size);

/**
 * Reset statistics for a command
 * 
 * @param name: Command name (or NULL for all commands)
 * @return 0 on success, -1 on error
 */
int commands_reset_stats(const char *name);

/* ============================================================================
 * Lifecycle Management
 * ============================================================================ */

/**
 * Initialize command subsystem
 * Must be called before any other command functions
 * 
 * @return 0 on success, -1 on error
 */
int commands_init(void);

/**
 * Shutdown command subsystem
 * Cleans up resources and prints statistics
 */
void commands_shutdown(void);

/* ============================================================================
 * Help & Documentation Generation
 * ============================================================================ */

/**
 * Generate formatted help text for all commands
 * 
 * @param buffer: Output buffer
 * @param size: Size of buffer
 * @return Number of bytes written (or would be written if buffer too small)
 */
size_t commands_generate_help(char *buffer, size_t size);

/**
 * Generate help text for a specific command
 * 
 * @param name: Command name
 * @param buffer: Output buffer
 * @param size: Size of buffer
 * @return Number of bytes written, or 0 if command not found
 */
size_t commands_generate_command_help(const char *name, char *buffer, size_t size);

/**
 * Generate JSON list of all commands with metadata
 * Useful for API clients and documentation generation
 * 
 * @param buffer: Output buffer
 * @param size: Size of buffer
 * @param category_filter: Optional category filter (NULL for all)
 * @param file_filter: Optional file filter (NULL for all)
 * @return Number of bytes written (or would be written if buffer too small)
 */
size_t commands_generate_json_list(char *buffer, size_t size, 
                                   const char *category_filter,
                                   const char *file_filter);



/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Convert command result to human-readable string
 * 
 * @param result: Command result code
 * @return String representation
 */
const char *commands_result_to_string(command_result_t result);

/**
 * Build a standard success response
 * Helper function for command handlers
 * 
 * @param resp: Response structure to populate
 * @param message: Success message
 * @param data: Optional JSON data (can be NULL)
 */
void commands_build_success(ipc_response_t *resp, 
                           const char *message,
                           const char *data);

/**
 * Build a standard error response
 * Helper function for command handlers
 * 
 * @param resp: Response structure to populate
 * @param result: Command result code
 * @param message: Error message
 */
void commands_build_error(ipc_response_t *resp,
                         command_result_t result,
                         const char *message);

#endif /* NEOWALL_COMMANDS_H */