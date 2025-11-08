#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "neowall.h"
#include "constants.h"

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

/* Get state file path - use persistent location that survives reboots */
const char *get_state_file_path(void) {
    static char state_path[MAX_PATH_LENGTH];
    const char *state_home = getenv("XDG_STATE_HOME");
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    
    /* Prefer XDG_STATE_HOME for persistent state (usually ~/.local/state) */
    if (state_home && state_home[0] != '\0') {
        snprintf(state_path, sizeof(state_path), "%s/neowall/state", state_home);
    }
    /* Fall back to config directory */
    else if (config_home && config_home[0] != '\0') {
        snprintf(state_path, sizeof(state_path), "%s/neowall/state", config_home);
    }
    /* Fall back to ~/.config/neowall/state */
    else if (home && home[0] != '\0') {
        snprintf(state_path, sizeof(state_path), "%s/.config/neowall/state", home);
    }
    /* Last resort: tmpfs (will be lost on reboot) */
    else {
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (runtime_dir) {
            snprintf(state_path, sizeof(state_path), "%s/neowall-state.txt", runtime_dir);
        } else {
            snprintf(state_path, sizeof(state_path), "/tmp/neowall-state-%d.txt", getuid());
        }
    }
    
    return state_path;
}

/* Structure to hold output state data */
typedef struct {
    char output_name[256];
    char wallpaper_path[MAX_PATH_LENGTH];
    char mode[64];
    int cycle_index;
    int cycle_total;
    char status[128];
    long timestamp;
} output_state_entry_t;

/* Write current wallpaper state for multi-monitor support */
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path, 
                           const char *mode, int cycle_index, int cycle_total,
                           const char *status) {
    /* CRITICAL: This function is called from multiple contexts (main thread, render path)
     * We need to use file locking to prevent concurrent writes from corrupting the file.
     * However, we don't have direct access to neowall_state here, so we use a static mutex.
     * This is safe because the state file is a singleton resource. */
    
    static pthread_mutex_t state_file_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    const char *state_path = get_state_file_path();
    
    /* Acquire lock before file operations */
    pthread_mutex_lock(&state_file_mutex);
    
    /* Ensure state directory exists */
    char dir_path[MAX_PATH_LENGTH];
    strncpy(dir_path, state_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* Create directory recursively - simple approach for at most 2 levels */
        struct stat st = {0};
        if (stat(dir_path, &st) == -1) {
            /* Try to create parent first */
            char parent_path[MAX_PATH_LENGTH];
            strncpy(parent_path, dir_path, sizeof(parent_path) - 1);
            parent_path[sizeof(parent_path) - 1] = '\0';
            char *parent_slash = strrchr(parent_path, '/');
            if (parent_slash) {
                *parent_slash = '\0';
                mkdir(parent_path, 0755);
            }
            /* Now create the target directory */
            mkdir(dir_path, 0755);
        }
    }
    
    /* Read existing states from file */
    output_state_entry_t states[MAX_OUTPUTS];
    int state_count = 0;
    bool found_output = false;
    
    FILE *fp_read = fopen(state_path, "r");
    if (fp_read) {
        char line[MAX_PATH_LENGTH];
        output_state_entry_t current_entry = {0};
        bool reading_entry = false;
        
        while (fgets(line, sizeof(line), fp_read)) {
            line[strcspn(line, "\n")] = 0;
            
            if (strncmp(line, "[output]", 8) == 0) {
                /* Save previous entry if exists */
                if (reading_entry && current_entry.output_name[0] != '\0') {
                    if (state_count < MAX_OUTPUTS) {
                        states[state_count++] = current_entry;
                    }
                }
                /* Start new entry */
                memset(&current_entry, 0, sizeof(current_entry));
                reading_entry = true;
            } else if (reading_entry) {
                if (strncmp(line, "name=", 5) == 0) {
                    size_t len = strlen(line + 5);
                    if (len > sizeof(current_entry.output_name) - 1) len = sizeof(current_entry.output_name) - 1;
                    memcpy(current_entry.output_name, line + 5, len);
                    current_entry.output_name[len] = '\0';
                } else if (strncmp(line, "wallpaper=", 10) == 0) {
                    size_t len = strlen(line + 10);
                    if (len > sizeof(current_entry.wallpaper_path) - 1) len = sizeof(current_entry.wallpaper_path) - 1;
                    memcpy(current_entry.wallpaper_path, line + 10, len);
                    current_entry.wallpaper_path[len] = '\0';
                } else if (strncmp(line, "mode=", 5) == 0) {
                    size_t len = strlen(line + 5);
                    if (len > sizeof(current_entry.mode) - 1) len = sizeof(current_entry.mode) - 1;
                    memcpy(current_entry.mode, line + 5, len);
                    current_entry.mode[len] = '\0';
                } else if (strncmp(line, "cycle_index=", 12) == 0) {
                    current_entry.cycle_index = atoi(line + 12);
                } else if (strncmp(line, "cycle_total=", 12) == 0) {
                    current_entry.cycle_total = atoi(line + 12);
                } else if (strncmp(line, "status=", 7) == 0) {
                    size_t len = strlen(line + 7);
                    if (len > sizeof(current_entry.status) - 1) len = sizeof(current_entry.status) - 1;
                    memcpy(current_entry.status, line + 7, len);
                    current_entry.status[len] = '\0';
                } else if (strncmp(line, "timestamp=", 10) == 0) {
                    current_entry.timestamp = atol(line + 10);
                }
            }
        }
        
        /* Save last entry */
        if (reading_entry && current_entry.output_name[0] != '\0') {
            if (state_count < MAX_OUTPUTS) {
                states[state_count++] = current_entry;
            }
        }
        
        fclose(fp_read);
    }
    
    /* Update or add the current output's state */
    for (int i = 0; i < state_count; i++) {
        if (output_name && strcmp(states[i].output_name, output_name) == 0) {
            /* Update existing entry */
            strncpy(states[i].wallpaper_path, wallpaper_path ? wallpaper_path : "none", 
                    sizeof(states[i].wallpaper_path) - 1);
            strncpy(states[i].mode, mode ? mode : "fill", sizeof(states[i].mode) - 1);
            states[i].cycle_index = cycle_index;
            states[i].cycle_total = cycle_total;
            strncpy(states[i].status, status ? status : "active", sizeof(states[i].status) - 1);
            states[i].timestamp = (long)time(NULL);
            found_output = true;
            break;
        }
    }
    
    /* Add new entry if not found */
    if (!found_output && state_count < MAX_OUTPUTS) {
        strncpy(states[state_count].output_name, output_name ? output_name : "unknown", 
                sizeof(states[state_count].output_name) - 1);
        strncpy(states[state_count].wallpaper_path, wallpaper_path ? wallpaper_path : "none", 
                sizeof(states[state_count].wallpaper_path) - 1);
        strncpy(states[state_count].mode, mode ? mode : "fill", sizeof(states[state_count].mode) - 1);
        states[state_count].cycle_index = cycle_index;
        states[state_count].cycle_total = cycle_total;
        strncpy(states[state_count].status, status ? status : "active", 
                sizeof(states[state_count].status) - 1);
        states[state_count].timestamp = (long)time(NULL);
        state_count++;
    }
    
    /* Write all states back to file */
    FILE *fp_write = fopen(state_path, "w");
    if (!fp_write) {
        log_error("Failed to write state file %s: %s", state_path, strerror(errno));
        pthread_mutex_unlock(&state_file_mutex);
        return false;
    }
    
    for (int i = 0; i < state_count; i++) {
        fprintf(fp_write, "[output]\n");
        fprintf(fp_write, "name=%s\n", states[i].output_name);
        fprintf(fp_write, "wallpaper=%s\n", states[i].wallpaper_path);
        fprintf(fp_write, "mode=%s\n", states[i].mode);
        fprintf(fp_write, "cycle_index=%d\n", states[i].cycle_index);
        fprintf(fp_write, "cycle_total=%d\n", states[i].cycle_total);
        fprintf(fp_write, "status=%s\n", states[i].status);
        fprintf(fp_write, "timestamp=%ld\n", states[i].timestamp);
        fprintf(fp_write, "\n");
    }
    
    fclose(fp_write);
    pthread_mutex_unlock(&state_file_mutex);
    return true;
}

/* Restore cycle index from state file for the given output */
int restore_cycle_index_from_state(const char *output_name) {
    const char *state_path = get_state_file_path();
    FILE *fp = fopen(state_path, "r");
    
    if (!fp) {
        log_debug("No state file found, starting from index 0");
        return 0;
    }
    
    char line[MAX_PATH_LENGTH];
    char current_output[256] = "";
    int cycle_index = 0;
    bool in_matching_output = false;
    
    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "[output]", 8) == 0) {
            /* Start of new output section */
            in_matching_output = false;
            current_output[0] = '\0';
        } else if (strncmp(line, "name=", 5) == 0) {
            /* Check if this is the output we're looking for */
            size_t len = strlen(line + 5);
            if (len > sizeof(current_output) - 1) len = sizeof(current_output) - 1;
            memcpy(current_output, line + 5, len);
            current_output[len] = '\0';
            if (output_name && strcmp(current_output, output_name) == 0) {
                in_matching_output = true;
            }
        } else if (in_matching_output && strncmp(line, "cycle_index=", 12) == 0) {
            cycle_index = atoi(line + 12);
            /* Found it, can stop reading */
            break;
        }
    }
    
    fclose(fp);
    
    if (in_matching_output) {
        log_info("Restored cycle index %d for output %s from state", cycle_index, output_name);
        return cycle_index;
    }
    
    return 0;
}

/* Read and display current wallpaper state for all outputs */
bool read_wallpaper_state(void) {
    const char *state_path = get_state_file_path();
    FILE *fp = fopen(state_path, "r");
    
    if (!fp) {
        printf("No wallpaper state found.\n");
        printf("The daemon may not be running or no wallpaper has been set yet.\n");
        return false;
    }
    
    char line[MAX_PATH_LENGTH];
    output_state_entry_t current_entry = {0};
    bool reading_entry = false;
    int output_count = 0;
    
    printf("Current wallpaper state:\n");
    
    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "[output]", 8) == 0) {
            /* Display previous entry if exists */
            if (reading_entry && current_entry.output_name[0] != '\0') {
                printf("\n  Output:    %s\n", current_entry.output_name);
                printf("  Wallpaper: %s\n", current_entry.wallpaper_path);
                printf("  Mode:      %s\n", current_entry.mode);
                printf("  Status:    %s\n", current_entry.status);
                if (current_entry.cycle_total > 0) {
                    printf("  Cycling:   %d/%d\n", current_entry.cycle_index + 1, current_entry.cycle_total);
                }
                if (current_entry.timestamp > 0) {
                    char time_str[64];
                    struct tm *tm_info = localtime(&current_entry.timestamp);
                    if (tm_info) {
                        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                        printf("  Updated:   %s\n", time_str);
                    }
                }
                output_count++;
            }
            /* Start new entry */
            memset(&current_entry, 0, sizeof(current_entry));
            reading_entry = true;
        } else if (reading_entry) {
            if (strncmp(line, "name=", 5) == 0) {
                size_t len = strlen(line + 5);
                if (len > sizeof(current_entry.output_name) - 1) len = sizeof(current_entry.output_name) - 1;
                memcpy(current_entry.output_name, line + 5, len);
                current_entry.output_name[len] = '\0';
            } else if (strncmp(line, "wallpaper=", 10) == 0) {
                size_t len = strlen(line + 10);
                if (len > sizeof(current_entry.wallpaper_path) - 1) len = sizeof(current_entry.wallpaper_path) - 1;
                memcpy(current_entry.wallpaper_path, line + 10, len);
                current_entry.wallpaper_path[len] = '\0';
            } else if (strncmp(line, "mode=", 5) == 0) {
                size_t len = strlen(line + 5);
                if (len > sizeof(current_entry.mode) - 1) len = sizeof(current_entry.mode) - 1;
                memcpy(current_entry.mode, line + 5, len);
                current_entry.mode[len] = '\0';
            } else if (strncmp(line, "cycle_index=", 12) == 0) {
                current_entry.cycle_index = atoi(line + 12);
            } else if (strncmp(line, "cycle_total=", 12) == 0) {
                current_entry.cycle_total = atoi(line + 12);
            } else if (strncmp(line, "status=", 7) == 0) {
                size_t len = strlen(line + 7);
                if (len > sizeof(current_entry.status) - 1) len = sizeof(current_entry.status) - 1;
                memcpy(current_entry.status, line + 7, len);
                current_entry.status[len] = '\0';
            } else if (strncmp(line, "timestamp=", 10) == 0) {
                current_entry.timestamp = atol(line + 10);
            }
        }
    }
    
    /* Display last entry */
    if (reading_entry && current_entry.output_name[0] != '\0') {
        printf("\n  Output:    %s\n", current_entry.output_name);
        printf("  Wallpaper: %s\n", current_entry.wallpaper_path);
        printf("  Mode:      %s\n", current_entry.mode);
        printf("  Status:    %s\n", current_entry.status);
        if (current_entry.cycle_total > 0) {
            printf("  Cycling:   %d/%d\n", current_entry.cycle_index + 1, current_entry.cycle_total);
        }
        if (current_entry.timestamp > 0) {
            char time_str[64];
            struct tm *tm_info = localtime(&current_entry.timestamp);
            if (tm_info) {
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                printf("  Updated:   %s\n", time_str);
            }
        }
        output_count++;
    }
    
    fclose(fp);
    
    if (output_count == 0) {
        printf("\n  No outputs configured.\n");
    } else {
        printf("\nTotal outputs: %d\n", output_count);
    }
    
    return true;
}