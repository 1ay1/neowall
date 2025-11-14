/* Shader Library Logging Shim
 * Provides logging macros that work without the full daemon logging system
 */

#ifndef SHADER_LIB_LOG_H
#define SHADER_LIB_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

/* Log levels */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

/* Default log level (can be overridden) */
#ifndef SHADER_LIB_LOG_LEVEL
#define SHADER_LIB_LOG_LEVEL LOG_LEVEL_INFO
#endif

/* Get current timestamp for logging */
static inline void shader_log_timestamp(char *buf, size_t size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Core logging function */
static inline void shader_log(int level, const char *prefix, const char *fmt, ...) {
    if (level > SHADER_LIB_LOG_LEVEL) {
        return;
    }

    char timestamp[32];
    shader_log_timestamp(timestamp, sizeof(timestamp));

    fprintf(stderr, "[%s] [ShaderLib] [%s] ", timestamp, prefix);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

/* Logging macros compatible with daemon's log_* functions */
#define log_error(...) shader_log(LOG_LEVEL_ERROR, "ERROR", __VA_ARGS__)
#define log_warn(...)  shader_log(LOG_LEVEL_WARN,  "WARN",  __VA_ARGS__)
#define log_info(...)  shader_log(LOG_LEVEL_INFO,  "INFO",  __VA_ARGS__)
#define log_debug(...) shader_log(LOG_LEVEL_DEBUG, "DEBUG", __VA_ARGS__)

#endif /* SHADER_LIB_LOG_H */