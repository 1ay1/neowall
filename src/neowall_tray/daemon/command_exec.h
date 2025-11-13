/* NeoWall Tray - Command Execution Component
 * Provides functions to execute neowall commands
 */

#ifndef NEOWALL_TRAY_COMMAND_EXEC_H
#define NEOWALL_TRAY_COMMAND_EXEC_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Execute a neowall command asynchronously
 * @param cmd The command to execute (e.g., "next", "pause", "status")
 * @return true if command was launched successfully, false otherwise
 */
bool command_execute(const char *cmd);

/**
 * Execute a neowall command and capture its output
 * @param cmd The command to execute
 * @param output Buffer to store output
 * @param output_size Size of output buffer
 * @return true if command executed successfully, false otherwise
 */
bool command_execute_with_output(const char *cmd, char *output, size_t output_size);

/**
 * Execute a neowall command with arguments
 * @param argc Number of arguments (including command)
 * @param argv Array of arguments
 * @return true if command was launched successfully, false otherwise
 */
bool command_execute_argv(int argc, char **argv);

#endif /* NEOWALL_TRAY_COMMAND_EXEC_H */