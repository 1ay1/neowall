/* NeoWall Tray - Daemon Detection Component
 * Provides functions to check daemon status and read PID
 */

#ifndef NEOWALL_TRAY_DAEMON_CHECK_H
#define NEOWALL_TRAY_DAEMON_CHECK_H

#include <stdbool.h>
#include <sys/types.h>

/**
 * Get the path to the daemon PID file
 * @return Path to PID file (static buffer, don't free)
 */
const char *daemon_get_pid_file_path(void);

/**
 * Read the daemon PID from the PID file
 * @return PID if daemon is running and valid, -1 otherwise
 */
pid_t daemon_read_pid(void);

/**
 * Check if the daemon is currently running
 * @return true if daemon is running, false otherwise
 */
bool daemon_is_running(void);

/**
 * Get the daemon status as a string
 * @return "Running" or "Stopped" (static buffer, don't free)
 */
const char *daemon_get_status_string(void);

#endif /* NEOWALL_TRAY_DAEMON_CHECK_H */