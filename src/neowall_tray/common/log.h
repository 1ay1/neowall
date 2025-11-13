/* NeoWall Tray - Logging Utilities
 * Shared logging functions for all tray components
 */

#ifndef NEOWALL_TRAY_LOG_H
#define NEOWALL_TRAY_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

/* ANSI color codes */
#define TRAY_COLOR_RESET   "\033[0m"
#define TRAY_COLOR_RED     "\033[31m"
#define TRAY_COLOR_GREEN   "\033[32m"
#define TRAY_COLOR_YELLOW  "\033[33m"
#define TRAY_COLOR_BLUE    "\033[34m"
#define TRAY_COLOR_MAGENTA "\033[35m"
#define TRAY_COLOR_CYAN    "\033[36m"
#define TRAY_COLOR_GRAY    "\033[90m"

/* Log levels */
typedef enum {
    TRAY_LOG_ERROR = 0,
    TRAY_LOG_INFO  = 1,
    TRAY_LOG_DEBUG = 2
} tray_log_level_t;

/* Global log configuration */
extern tray_log_level_t g_tray_log_level;
extern bool g_tray_use_colors;

/* Logging macros - automatically include component name */
#define TRAY_LOG_ERROR(component, fmt, ...) \
    tray_log_error("[%s] " fmt, component, ##__VA_ARGS__)

#define TRAY_LOG_INFO(component, fmt, ...) \
    tray_log_info("[%s] " fmt, component, ##__VA_ARGS__)

#define TRAY_LOG_DEBUG(component, fmt, ...) \
    tray_log_debug("[%s] " fmt, component, ##__VA_ARGS__)

/* Core logging functions */

/**
 * Log an error message (always shown)
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void tray_log_error(const char *format, ...);

/**
 * Log an info message (shown at INFO level and above)
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void tray_log_info(const char *format, ...);

/**
 * Log a debug message (shown at DEBUG level only)
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void tray_log_debug(const char *format, ...);

/**
 * Set the global log level
 * @param level Log level to set
 */
void tray_log_set_level(tray_log_level_t level);

/**
 * Enable or disable colored output
 * @param enabled true to enable colors, false to disable
 */
void tray_log_set_colors(bool enabled);

/**
 * Initialize logging system
 * Detects TTY and sets defaults
 */
void tray_log_init(void);

#endif /* NEOWALL_TRAY_LOG_H */