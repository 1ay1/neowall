#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "staticwall.h"

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

        snprintf(expanded, size, "%s%s", home, path + 1);
        return true;
    }

    /* No expansion needed */
    if (strlen(path) >= size) {
        log_error("Path too long");
        return false;
    }

    strncpy(expanded, path, size - 1);
    expanded[size - 1] = '\0';
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

/* Get state file path */
const char *get_state_file_path(void) {
    static char state_path[MAX_PATH_LENGTH];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    
    if (runtime_dir) {
        snprintf(state_path, sizeof(state_path), "%s/staticwall-state.txt", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(state_path, sizeof(state_path), "%s/.staticwall-state", home);
        } else {
            snprintf(state_path, sizeof(state_path), "/tmp/staticwall-state-%d.txt", getuid());
        }
    }
    
    return state_path;
}

/* Write current wallpaper state */
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path, 
                           const char *mode, int cycle_index, int cycle_total) {
    const char *state_path = get_state_file_path();
    FILE *fp = fopen(state_path, "w");
    
    if (!fp) {
        log_error("Failed to write state file %s: %s", state_path, strerror(errno));
        return false;
    }
    
    fprintf(fp, "output=%s\n", output_name ? output_name : "unknown");
    fprintf(fp, "wallpaper=%s\n", wallpaper_path ? wallpaper_path : "none");
    fprintf(fp, "mode=%s\n", mode ? mode : "unknown");
    fprintf(fp, "cycle_index=%d\n", cycle_index);
    fprintf(fp, "cycle_total=%d\n", cycle_total);
    fprintf(fp, "timestamp=%ld\n", (long)time(NULL));
    
    fclose(fp);
    return true;
}

/* Read and display current wallpaper state */
bool read_wallpaper_state(void) {
    const char *state_path = get_state_file_path();
    FILE *fp = fopen(state_path, "r");
    
    if (!fp) {
        printf("No wallpaper state found.\n");
        printf("The daemon may not be running or no wallpaper has been set yet.\n");
        return false;
    }
    
    char line[MAX_PATH_LENGTH];
    char output[256] = "unknown";
    char wallpaper[MAX_PATH_LENGTH] = "none";
    char mode[64] = "unknown";
    int cycle_index = 0;
    int cycle_total = 0;
    long timestamp = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "output=", 7) == 0) {
            strncpy(output, line + 7, sizeof(output) - 1);
            output[sizeof(output) - 1] = '\0';
        } else if (strncmp(line, "wallpaper=", 10) == 0) {
            strncpy(wallpaper, line + 10, sizeof(wallpaper) - 1);
            wallpaper[sizeof(wallpaper) - 1] = '\0';
        } else if (strncmp(line, "mode=", 5) == 0) {
            strncpy(mode, line + 5, sizeof(mode) - 1);
            mode[sizeof(mode) - 1] = '\0';
        } else if (strncmp(line, "cycle_index=", 12) == 0) {
            cycle_index = atoi(line + 12);
        } else if (strncmp(line, "cycle_total=", 12) == 0) {
            cycle_total = atoi(line + 12);
        } else if (strncmp(line, "timestamp=", 10) == 0) {
            timestamp = atol(line + 10);
        }
    }
    
    fclose(fp);
    
    /* Display the state */
    printf("Current Wallpaper Status:\n");
    printf("  Output: %s\n", output);
    printf("  Wallpaper: %s\n", wallpaper);
    printf("  Mode: %s\n", mode);
    
    if (cycle_total > 0) {
        printf("  Cycle: %d/%d\n", cycle_index + 1, cycle_total);
    }
    
    if (timestamp > 0) {
        time_t now = time(NULL);
        long elapsed = now - timestamp;
        printf("  Last changed: %ld seconds ago\n", elapsed);
    }
    
    return true;
}