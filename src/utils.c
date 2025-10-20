/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 * 
 * Utility functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "staticwall.h"

/* Log levels */
static enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_DEBUG = 2
} log_level = LOG_LEVEL_INFO;

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
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Get formatted timestamp */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Generic logging function */
static void log_message(const char *level, const char *color, 
                       const char *format, va_list args) {
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    /* Check if stdout is a TTY for color support */
    if (use_colors && isatty(STDOUT_FILENO)) {
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

/* Log error message */
void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_message("ERROR", COLOR_RED, format, args);
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

/* Enable/disable colors */
void log_set_colors(bool enabled) {
    use_colors = enabled;
}

/* String comparison (case-insensitive) */
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        
        if (c1 != c2) {
            return c1 - c2;
        }
        
        s1++;
        s2++;
    }
    
    return *s1 - *s2;
}

/* Path expansion (tilde expansion) */
bool expand_path(const char *path, char *expanded, size_t size) {
    if (!path || !expanded || size == 0) {
        return false;
    }
    
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~: HOME not set");
            return false;
        }
        
        size_t home_len = strlen(home);
        size_t path_len = strlen(path + 1);
        
        if (home_len + path_len + 1 > size) {
            log_error("Expanded path too long");
            return false;
        }
        
        strcpy(expanded, home);
        strcat(expanded, path + 1);
        return true;
    }
    
    /* No expansion needed */
    if (strlen(path) >= size) {
        log_error("Path too long");
        return false;
    }
    
    strcpy(expanded, path);
    return true;
}

/* Check if file exists */
bool file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

/* Get file size */
long file_size(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    
    return size;
}

/* Format bytes to human readable string */
void format_bytes(uint64_t bytes, char *buf, size_t size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double value = (double)bytes;
    
    while (value >= 1024.0 && unit_index < 4) {
        value /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buf, size, "%lu %s", (unsigned long)value, units[unit_index]);
    } else {
        snprintf(buf, size, "%.2f %s", value, units[unit_index]);
    }
}

/* Linear interpolation */
float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Clamp value between min and max */
float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* Ease in-out cubic function for smooth transitions */
float ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = (2.0f * t) - 2.0f;
        return 0.5f * f * f * f + 1.0f;
    }
}