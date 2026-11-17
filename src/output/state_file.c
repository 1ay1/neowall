/* The wallpaper state file and cycle list: what each output shows, for
 * `neowall current` and `neowall list`, and to resume after a restart.
 * See neowall/output/state_file.h. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* lstat, localtime_r, PATH_MAX, clock_gettime */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h> /* PRIu64 */
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h> /* POSIX strcasecmp — don't redefine it ourselves */
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "neowall/output/state_file.h"
#include "neowall/core/constants.h"
#include "neowall/core/log.h"
#include "neowall/core/util.h"


#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"

/* Get path to the cycle list file (shows all wallpapers with indices) */
/* Must use same fallback logic as get_state_file_path to stay in sync */
static const char *get_cycle_list_file_path(void) {
    static char path[NW_PATH_MAX];
    const char *state_home  = getenv("XDG_STATE_HOME");
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home        = getenv("HOME");

    /* Prefer XDG_STATE_HOME for persistent state (usually ~/.local/state) */
    if (state_home && state_home[0] != '\0') {
        snprintf(path, sizeof(path), "%s/neowall/cycle_list", state_home);
    }
    /* Fall back to config directory - same as get_state_file_path */
    else if (config_home && config_home[0] != '\0') {
        snprintf(path, sizeof(path), "%s/neowall/cycle_list", config_home);
    }
    /* Fall back to ~/.config/neowall/cycle_list */
    else if (home && home[0] != '\0') {
        snprintf(path, sizeof(path), "%s/.config/neowall/cycle_list", home);
    }
    /* Last resort: tmpfs (will be lost on reboot) */
    else {
        const char *rt = neowall_secure_runtime_dir();
        int n          = rt ? snprintf(path, sizeof(path), "%s/cycle_list", rt) : -1;
        if (rt == NULL || n < 0 || (size_t)n >= sizeof(path)) {
            /* No safe directory available (or it would truncate): return an
             * empty path so callers fail closed rather than writing into a
             * world-writable /tmp or a clipped path. */
            path[0] = '\0';
        }
    }

    return path;
}

const char *get_state_file_path(void) {
    static char state_path[NW_PATH_MAX];
    const char *state_home  = getenv("XDG_STATE_HOME");
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home        = getenv("HOME");

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
        const char *rt = neowall_secure_runtime_dir();
        int n          = rt ? snprintf(state_path, sizeof(state_path), "%s/state.txt", rt) : -1;
        if (rt == NULL || n < 0 || (size_t)n >= sizeof(state_path)) {
            state_path[0] = '\0';
        }
    }

    return state_path;
}

static bool parse_disk_long(const char *text, long max, long *out) {
    long value;
    if (!neowall_parse_index(text, &value) || value > max) return false;
    *out = value;
    return true;
}

/* Open a same-directory temporary file suitable for atomic rename. */
static FILE *open_atomic_temp(const char *path, char *tmp, size_t tmp_size) {
    int n = snprintf(tmp, tmp_size, "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= tmp_size) return NULL;
    int fd = mkstemp(tmp);
    if (fd < 0) return NULL;
    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmp);
    }
    return fp;
}

static bool publish_atomic(FILE *fp, const char *tmp, const char *path) {
    bool ok = fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    if (fclose(fp) != 0) ok = false;
    if (ok && rename(tmp, path) == 0) return true;
    unlink(tmp);
    return false;
}

/* Structure to hold output state data */
typedef struct {
    char output_name[256];
    char wallpaper_path[NW_PATH_MAX];
    char mode[64];
    int cycle_index;
    int cycle_total;
    char status[128];
    long timestamp;
} output_state_entry_t;

/* Serializes every read-modify-rename transaction on the singleton state file. */
static pthread_mutex_t state_file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* A `neowall preview` / `watch` is a second, throwaway instance on the same
 * outputs. It must never touch the daemon's state file: if it did, its shader
 * would overwrite the daemon's entry for that connector and `neowall current`
 * would keep reporting the preview long after it exited. */
static bool state_file_readonly = false;

void state_file_set_readonly(bool readonly) {
    state_file_readonly = readonly;
}

bool state_file_is_readonly(void) {
    return state_file_readonly;
}

/* Write current wallpaper state for multi-monitor support */
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path, const char *mode,
                           int cycle_index, int cycle_total, const char *status) {
    if (state_file_readonly) return true;
    /* Serialize concurrent read-modify-rename transactions. */

    const char *state_path = get_state_file_path();

    /* Acquire lock before file operations */
    pthread_mutex_lock(&state_file_mutex);

    /* Ensure state directory exists */
    char dir_path[NW_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s", state_path);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* Create directory recursively - simple approach for at most 2 levels */
        struct stat st = {0};
        if (stat(dir_path, &st) == -1) {
            /* Try to create parent first */
            char parent_path[NW_PATH_MAX];
            snprintf(parent_path, sizeof(parent_path), "%s", dir_path);
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
    output_state_entry_t states[NW_MAX_OUTPUTS];
    int state_count   = 0;
    bool found_output = false;

    FILE *fp_read = fopen(state_path, "r");
    if (fp_read) {
        char line[NW_PATH_MAX];
        output_state_entry_t current_entry = {0};
        bool reading_entry                 = false;

        while (fgets(line, sizeof(line), fp_read)) {
            line[strcspn(line, "\n")] = 0;

            if (strncmp(line, "[output]", 8) == 0) {
                /* Save previous entry if exists */
                if (reading_entry && current_entry.output_name[0] != '\0') {
                    if (state_count < NW_MAX_OUTPUTS) {
                        states[state_count++] = current_entry;
                    }
                }
                /* Start new entry */
                memset(&current_entry, 0, sizeof(current_entry));
                reading_entry = true;
            } else if (reading_entry) {
                if (strncmp(line, "name=", 5) == 0) {
                    size_t len = strlen(line + 5);
                    if (len > sizeof(current_entry.output_name) - 1)
                        len = sizeof(current_entry.output_name) - 1;
                    memcpy(current_entry.output_name, line + 5, len);
                    current_entry.output_name[len] = '\0';
                } else if (strncmp(line, "wallpaper=", 10) == 0) {
                    size_t len = strlen(line + 10);
                    if (len > sizeof(current_entry.wallpaper_path) - 1)
                        len = sizeof(current_entry.wallpaper_path) - 1;
                    memcpy(current_entry.wallpaper_path, line + 10, len);
                    current_entry.wallpaper_path[len] = '\0';
                } else if (strncmp(line, "mode=", 5) == 0) {
                    size_t len = strlen(line + 5);
                    if (len > sizeof(current_entry.mode) - 1) len = sizeof(current_entry.mode) - 1;
                    memcpy(current_entry.mode, line + 5, len);
                    current_entry.mode[len] = '\0';
                } else if (strncmp(line, "cycle_index=", 12) == 0) {
                    long value;
                    if (parse_disk_long(line + 12, INT_MAX, &value))
                        current_entry.cycle_index = (int)value;
                } else if (strncmp(line, "cycle_total=", 12) == 0) {
                    long value;
                    if (parse_disk_long(line + 12, INT_MAX, &value))
                        current_entry.cycle_total = (int)value;
                } else if (strncmp(line, "status=", 7) == 0) {
                    size_t len = strlen(line + 7);
                    if (len > sizeof(current_entry.status) - 1)
                        len = sizeof(current_entry.status) - 1;
                    memcpy(current_entry.status, line + 7, len);
                    current_entry.status[len] = '\0';
                } else if (strncmp(line, "timestamp=", 10) == 0) {
                    long value;
                    if (parse_disk_long(line + 10, LONG_MAX, &value))
                        current_entry.timestamp = value;
                }
            }
        }

        /* Save last entry */
        if (reading_entry && current_entry.output_name[0] != '\0') {
            if (state_count < NW_MAX_OUTPUTS) {
                states[state_count++] = current_entry;
            }
        }

        fclose(fp_read);
    }

    /* Update or add the current output's state */
    for (int i = 0; i < state_count; i++) {
        if (output_name && strcmp(states[i].output_name, output_name) == 0) {
            /* Update existing entry */
            snprintf(states[i].wallpaper_path, sizeof(states[i].wallpaper_path), "%s",
                     wallpaper_path ? wallpaper_path : "none");
            snprintf(states[i].mode, sizeof(states[i].mode), "%s", mode ? mode : "fill");
            states[i].cycle_index = cycle_index;
            states[i].cycle_total = cycle_total;
            snprintf(states[i].status, sizeof(states[i].status), "%s", status ? status : "active");
            states[i].timestamp = (long)time(NULL);
            found_output        = true;
            break;
        }
    }

    /* Add new entry if not found */
    if (!found_output && state_count < NW_MAX_OUTPUTS) {
        /* snprintf both guarantees NUL-termination (the source is an unbounded
         * caller pointer) and silences GCC -O2 -Wstringop-truncation. */
        memset(&states[state_count], 0, sizeof(states[state_count]));
        snprintf(states[state_count].output_name, sizeof(states[state_count].output_name), "%s",
                 output_name ? output_name : "unknown");
        snprintf(states[state_count].wallpaper_path, sizeof(states[state_count].wallpaper_path),
                 "%s", wallpaper_path ? wallpaper_path : "none");
        snprintf(states[state_count].mode, sizeof(states[state_count].mode), "%s",
                 mode ? mode : "fill");
        states[state_count].cycle_index = cycle_index;
        states[state_count].cycle_total = cycle_total;
        snprintf(states[state_count].status, sizeof(states[state_count].status), "%s",
                 status ? status : "active");
        states[state_count].timestamp = (long)time(NULL);
        state_count++;
    }

    /* Write a complete replacement then atomically publish it. Readers see
     * either the old complete snapshot or the new one, never truncation. */
    char tmp_path[NW_PATH_MAX];
    FILE *fp_write = open_atomic_temp(state_path, tmp_path, sizeof(tmp_path));
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

    bool published = publish_atomic(fp_write, tmp_path, state_path);
    if (!published) log_error("Failed to publish state file %s: %s", state_path, strerror(errno));
    pthread_mutex_unlock(&state_file_mutex);
    return published;
}

static int load_state_entries(const char *path, output_state_entry_t *states, int capacity) {
    FILE *fp = fopen(path, "r");
    if (!fp) return errno == ENOENT ? 0 : -1;

    char line[NW_PATH_MAX];
    output_state_entry_t entry = {0};
    bool reading               = false;
    int count                  = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "[output]") == 0) {
            if (reading && entry.output_name[0] && count < capacity) states[count++] = entry;
            memset(&entry, 0, sizeof(entry));
            reading = true;
        } else if (reading && strncmp(line, "name=", 5) == 0) {
            size_t n = strnlen(line + 5, sizeof(entry.output_name) - 1);
            memcpy(entry.output_name, line + 5, n);
            entry.output_name[n] = '\0';
        } else if (reading && strncmp(line, "wallpaper=", 10) == 0) {
            /* the line buffer is the bound: it ends 10 bytes after line+10 */
            size_t n = strnlen(line + 10, sizeof(line) - 10);
            if (n > sizeof(entry.wallpaper_path) - 1) n = sizeof(entry.wallpaper_path) - 1;
            memcpy(entry.wallpaper_path, line + 10, n);
            entry.wallpaper_path[n] = '\0';
        } else if (reading && strncmp(line, "mode=", 5) == 0) {
            size_t n = strnlen(line + 5, sizeof(entry.mode) - 1);
            memcpy(entry.mode, line + 5, n);
            entry.mode[n] = '\0';
        } else if (reading && strncmp(line, "cycle_index=", 12) == 0) {
            long value;
            if (parse_disk_long(line + 12, INT_MAX, &value)) entry.cycle_index = (int)value;
        } else if (reading && strncmp(line, "cycle_total=", 12) == 0) {
            long value;
            if (parse_disk_long(line + 12, INT_MAX, &value)) entry.cycle_total = (int)value;
        } else if (reading && strncmp(line, "status=", 7) == 0) {
            size_t n = strnlen(line + 7, sizeof(entry.status) - 1);
            memcpy(entry.status, line + 7, n);
            entry.status[n] = '\0';
        } else if (reading && strncmp(line, "timestamp=", 10) == 0) {
            long value;
            if (parse_disk_long(line + 10, LONG_MAX, &value)) entry.timestamp = value;
        }
    }
    if (reading && entry.output_name[0] && count < capacity) states[count++] = entry;
    bool failed = ferror(fp);
    fclose(fp);
    return failed ? -1 : count;
}

static bool publish_state_entries(const char *path, const output_state_entry_t *states, int count) {
    char tmp_path[NW_PATH_MAX];
    FILE *fp = open_atomic_temp(path, tmp_path, sizeof(tmp_path));
    if (!fp) return false;
    for (int i = 0; i < count; i++) {
        fprintf(fp, "[output]\nname=%s\nwallpaper=%s\nmode=%s\n", states[i].output_name,
                states[i].wallpaper_path, states[i].mode);
        fprintf(fp, "cycle_index=%d\ncycle_total=%d\nstatus=%s\ntimestamp=%ld\n\n",
                states[i].cycle_index, states[i].cycle_total, states[i].status,
                states[i].timestamp);
    }
    return publish_atomic(fp, tmp_path, path);
}

static bool state_name_retained(const char *name, const char *const *names, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (names[i] && strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

bool prune_wallpaper_state(const char *const *output_names, size_t output_count) {
    if (state_file_readonly) return true;
    if (output_count > 0 && !output_names) return false;
    const char *path = get_state_file_path();
    if (!path || !path[0]) return false;

    pthread_mutex_lock(&state_file_mutex);
    output_state_entry_t states[NW_MAX_OUTPUTS];
    int count = load_state_entries(path, states, NW_MAX_OUTPUTS);
    if (count == 0 && access(path, F_OK) != 0 && errno == ENOENT) {
        pthread_mutex_unlock(&state_file_mutex);
        return true;
    }
    if (count < 0) {
        pthread_mutex_unlock(&state_file_mutex);
        return false;
    }

    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (state_name_retained(states[i].output_name, output_names, output_count)) {
            if (kept != i) states[kept] = states[i];
            kept++;
        }
    }
    bool ok = kept == count || publish_state_entries(path, states, kept);
    pthread_mutex_unlock(&state_file_mutex);
    return ok;
}

bool remove_wallpaper_state(const char *output_name) {
    if (state_file_readonly) return true;
    if (!output_name || !output_name[0]) return false;
    const char *path = get_state_file_path();
    if (!path || !path[0]) return false;

    pthread_mutex_lock(&state_file_mutex);
    output_state_entry_t states[NW_MAX_OUTPUTS];
    int count = load_state_entries(path, states, NW_MAX_OUTPUTS);
    if (count == 0 && access(path, F_OK) != 0 && errno == ENOENT) {
        pthread_mutex_unlock(&state_file_mutex);
        return true;
    }
    if (count < 0) {
        pthread_mutex_unlock(&state_file_mutex);
        return false;
    }

    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(states[i].output_name, output_name) != 0) {
            if (kept != i) states[kept] = states[i];
            kept++;
        }
    }
    bool ok = kept == count || publish_state_entries(path, states, kept);
    pthread_mutex_unlock(&state_file_mutex);
    return ok;
}

/* Restore cycle index from state file for the given output */
int restore_cycle_index_from_state(const char *output_name) {
    if (state_file_readonly) return -1; /* a preview has its own one-entry config */
    const char *state_path = get_state_file_path();
    FILE *fp               = fopen(state_path, "r");

    if (!fp) {
        log_debug("No state file found, starting from index 0");
        return 0;
    }

    char line[NW_PATH_MAX];
    char current_output[256] = "";
    int cycle_index          = 0;
    bool in_matching_output  = false;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;

        if (strncmp(line, "[output]", 8) == 0) {
            /* Start of new output section */
            in_matching_output = false;
            current_output[0]  = '\0';
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
            long value;
            if (parse_disk_long(line + 12, INT_MAX, &value))
                cycle_index = (int)value;
            else
                in_matching_output = false;
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
    FILE *fp               = fopen(state_path, "r");

    if (!fp) {
        printf("No wallpaper state found.\n");
        printf("The daemon may not be running or no wallpaper has been set yet.\n");
        return false;
    }

    char line[NW_PATH_MAX];
    output_state_entry_t current_entry = {0};
    bool reading_entry                 = false;
    int output_count                   = 0;

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
                    printf("  Cycling:   %d/%d\n", current_entry.cycle_index + 1,
                           current_entry.cycle_total);
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
                if (len > sizeof(current_entry.output_name) - 1)
                    len = sizeof(current_entry.output_name) - 1;
                memcpy(current_entry.output_name, line + 5, len);
                current_entry.output_name[len] = '\0';
            } else if (strncmp(line, "wallpaper=", 10) == 0) {
                size_t len = strlen(line + 10);
                if (len > sizeof(current_entry.wallpaper_path) - 1)
                    len = sizeof(current_entry.wallpaper_path) - 1;
                memcpy(current_entry.wallpaper_path, line + 10, len);
                current_entry.wallpaper_path[len] = '\0';
            } else if (strncmp(line, "mode=", 5) == 0) {
                size_t len = strlen(line + 5);
                if (len > sizeof(current_entry.mode) - 1) len = sizeof(current_entry.mode) - 1;
                memcpy(current_entry.mode, line + 5, len);
                current_entry.mode[len] = '\0';
            } else if (strncmp(line, "cycle_index=", 12) == 0) {
                long value;
                if (parse_disk_long(line + 12, INT_MAX, &value))
                    current_entry.cycle_index = (int)value;
            } else if (strncmp(line, "cycle_total=", 12) == 0) {
                long value;
                if (parse_disk_long(line + 12, INT_MAX, &value))
                    current_entry.cycle_total = (int)value;
            } else if (strncmp(line, "status=", 7) == 0) {
                size_t len = strlen(line + 7);
                if (len > sizeof(current_entry.status) - 1) len = sizeof(current_entry.status) - 1;
                memcpy(current_entry.status, line + 7, len);
                current_entry.status[len] = '\0';
            } else if (strncmp(line, "timestamp=", 10) == 0) {
                long value;
                if (parse_disk_long(line + 10, LONG_MAX, &value)) current_entry.timestamp = value;
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
            printf("  Cycling:   %d/%d\n", current_entry.cycle_index + 1,
                   current_entry.cycle_total);
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

/* Write cycle list to file (called by daemon when config is loaded) */
bool write_cycle_list(const char *output_name, char **paths, size_t count, size_t current_index) {
    if (state_file_readonly) return true;
    if (!output_name || !paths || count == 0) {
        return false;
    }

    const char *list_path = get_cycle_list_file_path();

    /* Ensure parent directory exists */
    char dir_path[NW_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s", list_path);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0755);
    }

    char tmp_path[NW_PATH_MAX];
    FILE *fp = open_atomic_temp(list_path, tmp_path, sizeof(tmp_path));
    if (!fp) {
        log_error("Failed to write cycle list: %s", strerror(errno));
        return false;
    }

    fprintf(fp, "[cycle]\n");
    fprintf(fp, "output=%s\n", output_name);
    fprintf(fp, "count=%zu\n", count);
    fprintf(fp, "current=%zu\n", current_index);
    fprintf(fp, "\n");

    for (size_t i = 0; i < count; i++) {
        fprintf(fp, "[%zu]\n", i);
        fprintf(fp, "path=%s\n", paths[i]);
        if (i == current_index) {
            fprintf(fp, "current=true\n");
        }
        fprintf(fp, "\n");
    }

    if (!publish_atomic(fp, tmp_path, list_path)) {
        log_error("Failed to publish cycle list: %s", strerror(errno));
        return false;
    }
    return true;
}

/* Read and display cycle list (called by client for 'list' command) */
bool read_cycle_list(void) {
    const char *list_path = get_cycle_list_file_path();
    FILE *fp              = fopen(list_path, "r");

    if (!fp) {
        printf("No cycle list found.\n");
        printf("The daemon may not be running or no wallpaper cycling is configured.\n");
        printf("\nTo enable cycling, use a directory path in your config:\n");
        printf("  default {\n");
        printf("    path ~/Pictures/Wallpapers/\n");
        printf("  }\n");
        return false;
    }

    char line[NW_PATH_MAX];
    char output_name[256] = {0};
    size_t count          = 0;
    size_t current        = 0;
    int current_index     = -1;

    printf("Wallpaper cycle list:\n\n");

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;

        if (strncmp(line, "output=", 7) == 0) {
            size_t len = strlen(line + 7);
            if (len >= sizeof(output_name)) len = sizeof(output_name) - 1;
            memcpy(output_name, line + 7, len);
            output_name[len] = '\0';
        } else if (strncmp(line, "count=", 6) == 0) {
            long value;
            if (parse_disk_long(line + 6, LONG_MAX, &value)) count = (size_t)value;
        } else if (strncmp(line, "current=", 8) == 0 && line[8] != 't') {
            long value;
            if (parse_disk_long(line + 8, LONG_MAX, &value)) current = (size_t)value;
        } else if (line[0] == '[' && line[1] >= '0' && line[1] <= '9') {
            char *end = strchr(line + 1, ']');
            long value;
            current_index = -1;
            if (end) {
                *end = '\0';
                if (parse_disk_long(line + 1, INT_MAX, &value)) current_index = (int)value;
            }
        } else if (strncmp(line, "path=", 5) == 0 && current_index >= 0) {
            const char *path = line + 5;
            /* Extract just the filename for cleaner display */
            const char *filename = strrchr(path, '/');
            filename             = filename ? filename + 1 : path;

            if ((size_t)current_index == current) {
                if (isatty(STDOUT_FILENO)) {
                    printf("  %s➜ [%d] %s%s\n", COLOR_GREEN, current_index, filename, COLOR_RESET);
                } else {
                    printf("  > [%d] %s\n", current_index, filename);
                }
            } else {
                if (isatty(STDOUT_FILENO)) {
                    printf("    [%d] %s\n", current_index, filename);
                } else {
                    printf("    [%d] %s\n", current_index, filename);
                }
            }
        }
    }

    fclose(fp);

    if (count > 0) {
        printf("\nOutput: %s\n", output_name);
        printf("Total:  %zu wallpapers\n", count);
        printf("\nUse 'neowall set <index>' to jump to a specific wallpaper.\n");
    }

    return true;
}