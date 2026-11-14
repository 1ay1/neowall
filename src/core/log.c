/* Logging and monotonic time. See neowall/core/log.h. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE   /* lstat, localtime_r, PATH_MAX, clock_gettime */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>  /* PRIu64 */
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>  /* POSIX strcasecmp — don't redefine it ourselves */
#include <unistd.h>
#include <errno.h>

#include "neowall/core/constants.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"

/* Current log level */
static int log_level = LOG_LEVEL_INFO;

/* Enable colors in terminal */
static bool use_colors = true;

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

/* Get current time in milliseconds */
uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * MS_PER_SECOND + (uint64_t)ts.tv_nsec / MS_PER_NANOSECOND;
}

/* Microsecond-precision monotonic clock. Same epoch as get_time_ms() so the
 * two can be mixed (ms*1000 == us). Used for shader animation time, where
 * millisecond quantization causes visible micro-stutter at 60+ FPS. */
uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* Get formatted timestamp. localtime_r: log lines come from several threads,
 * and localtime() shares one static buffer between them. */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_buf;
    if (localtime_r(&now, &tm_buf)) {
        strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_buf);
    } else if (size) {
        buf[0] = '\0';
    }
}

/* Generic logging function */
static void log_message(const char *level, const char *color,
                       const char *format, va_list args) {
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    /* Check if stderr (where we actually write) is a TTY for color support */
    if (use_colors && isatty(STDERR_FILENO)) {
        fprintf(stderr, "%s[%s]%s %s%s%s: ",
                COLOR_GRAY, timestamp, COLOR_RESET,
                color, level, COLOR_RESET);
    } else {
        fprintf(stderr, "[%s] %s: ", timestamp, level);
    }

    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* While set, log_error is demoted to debug: the caller reports the problem
 * itself, in a better form. Loop thread only. */
static bool g_quiet_errors = false;
void log_quiet_errors(bool quiet) { g_quiet_errors = quiet; }

/* Log error message */
void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (g_quiet_errors) {
        if (log_level >= LOG_LEVEL_DEBUG) log_message("DEBUG", COLOR_CYAN, format, args);
    } else {
        log_message("ERROR", COLOR_RED, format, args);
    }
    va_end(args);
}

/* Log warning message */
void log_warn(const char *format, ...) {
    if (log_level < LOG_LEVEL_WARN) {
        return;
    }

    va_list args;
    va_start(args, format);
    log_message("WARN", COLOR_YELLOW, format, args);
    va_end(args);
}

/* Log info message */
void log_info(const char *format, ...) {
    if (log_level < LOG_LEVEL_INFO) {
        return;
    }

    va_list args;
    va_start(args, format);
    log_message("INFO", COLOR_GREEN, format, args);
    va_end(args);
}

/* Log debug message */
void log_debug(const char *format, ...) {
    if (log_level < LOG_LEVEL_DEBUG) {
        return;
    }

    va_list args;
    va_start(args, format);
    log_message("DEBUG", COLOR_CYAN, format, args);
    va_end(args);
}

/* Set log level */
void log_set_level(int level) {
    if (level >= LOG_LEVEL_ERROR && level <= LOG_LEVEL_DEBUG) {
        log_level = level;
    }
}

int log_get_level(void) {
    return log_level;
}

