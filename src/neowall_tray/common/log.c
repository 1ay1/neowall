/* NeoWall Tray - Logging Utilities Implementation
 * Shared logging functions for all tray components
 */

#include "log.h"
#include <string.h>
#include <stdlib.h>

/* Global log configuration */
tray_log_level_t g_tray_log_level = TRAY_LOG_INFO;
bool g_tray_use_colors = true;

/* Get current timestamp string */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Generic logging function */
static void tray_log_message(const char *level, const char *color,
                             const char *format, va_list args) {
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    /* Check if stderr is a TTY for color support */
    if (g_tray_use_colors && isatty(STDERR_FILENO)) {
        fprintf(stderr, "%s[%s]%s %s%s%s: ",
                TRAY_COLOR_GRAY, timestamp, TRAY_COLOR_RESET,
                color, level, TRAY_COLOR_RESET);
    } else {
        fprintf(stderr, "[%s] %s: ", timestamp, level);
    }

    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Log error message */
void tray_log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    tray_log_message("TRAY-ERROR", TRAY_COLOR_RED, format, args);
    va_end(args);
}

/* Log info message */
void tray_log_info(const char *format, ...) {
    if (g_tray_log_level < TRAY_LOG_INFO) {
        return;
    }

    va_list args;
    va_start(args, format);
    tray_log_message("TRAY-INFO", TRAY_COLOR_GREEN, format, args);
    va_end(args);
}

/* Log debug message */
void tray_log_debug(const char *format, ...) {
    if (g_tray_log_level < TRAY_LOG_DEBUG) {
        return;
    }

    va_list args;
    va_start(args, format);
    tray_log_message("TRAY-DEBUG", TRAY_COLOR_CYAN, format, args);
    va_end(args);
}

/* Set log level */
void tray_log_set_level(tray_log_level_t level) {
    if (level >= TRAY_LOG_ERROR && level <= TRAY_LOG_DEBUG) {
        g_tray_log_level = level;
    }
}

/* Enable/disable colors */
void tray_log_set_colors(bool enabled) {
    g_tray_use_colors = enabled;
}

/* Initialize logging system */
void tray_log_init(void) {
    /* Auto-detect TTY */
    g_tray_use_colors = isatty(STDERR_FILENO);

    /* Check environment variable for log level */
    const char *log_level_env = getenv("NEOWALL_TRAY_LOG_LEVEL");
    if (log_level_env) {
        if (strcmp(log_level_env, "ERROR") == 0) {
            g_tray_log_level = TRAY_LOG_ERROR;
        } else if (strcmp(log_level_env, "INFO") == 0) {
            g_tray_log_level = TRAY_LOG_INFO;
        } else if (strcmp(log_level_env, "DEBUG") == 0) {
            g_tray_log_level = TRAY_LOG_DEBUG;
        }
    }

    /* Check environment variable for colors */
    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') {
        g_tray_use_colors = false;
    }
}
