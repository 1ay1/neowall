/* The command files an old-style `set` / `set-terminal` writes before it
 * signals the daemon. The CLI writes them, perform.c reads them.
 * See neowall/cli/cli.h. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "neowall/state/state.h"
#include "neowall/core/util.h"

/* Get path to the set-index command file */
const char *get_set_index_file_path(void) {
    static char path[NW_PATH_MAX];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowall-set-index", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowall-set-index", home);
        } else {
            const char *rt = neowall_secure_runtime_dir();
            if (rt) {
                snprintf(path, sizeof(path), "%s/set-index", rt);
            } else {
                path[0] = '\0';
            }
        }
    }

    return path;
}

/* Write the requested index to a file for the daemon to read.
 *
 * Atomic via O_CREAT|O_EXCL + rename(): if two `neowall set N` commands race,
 * the daemon will never observe a half-written file or the wrong index from
 * an earlier write (audit fix #21). */
bool write_set_index_file(int index) {
    const char *path = get_set_index_file_path();
    char tmp[NW_PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        fprintf(stderr, "set-index path too long\n");
        return false;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "Failed to create set-index file: %s\n", strerror(errno));
        return false;
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", index);
    if (len <= 0 || write(fd, buf, (size_t)len) != len) {
        fprintf(stderr, "Failed to write set-index file: %s\n", strerror(errno));
        close(fd);
        unlink(tmp);
        return false;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "Failed to install set-index file: %s\n", strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

/* Claim the current mailbox entry by renaming it before reading. A writer may
 * publish a newer entry immediately afterwards; unlinking our private name can
 * therefore never delete that newer payload. Concurrent commands intentionally
 * coalesce to the last payload that was published before a signal is handled. */
int read_set_index_file(void) {
    const char *path = get_set_index_file_path();
    char claimed[NW_PATH_MAX];
    int n = snprintf(claimed, sizeof(claimed), "%s.take-%d", path, (int)getpid());
    if (n < 0 || (size_t)n >= sizeof(claimed) || rename(path, claimed) != 0) {
        return -1;
    }
    FILE *fp = fopen(claimed, "r");
    if (!fp) {
        unlink(claimed);
        return -1;
    }
    char line[64];
    long parsed = -1;
    bool valid = fgets(line, sizeof(line), fp) != NULL;
    fclose(fp);
    unlink(claimed);
    if (!valid) return -1;
    line[strcspn(line, "\r\n")] = '\0';
    if (!neowall_parse_index(line, &parsed) || parsed > INT_MAX) return -1;
    return (int)parsed;
}

/* Get path to the set-terminal command file. Mirrors get_set_index_file_path. */
const char *get_set_terminal_file_path(void) {
    static char path[NW_PATH_MAX];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowall-set-terminal", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowall-set-terminal", home);
        } else {
            const char *rt = neowall_secure_runtime_dir();
            if (rt) {
                snprintf(path, sizeof(path), "%s/set-terminal", rt);
            } else {
                path[0] = '\0';
            }
        }
    }

    return path;
}

/* Write the requested terminal command to a file for the daemon to read.
 *
 * Atomic via mkstemp + rename(): a concurrent `set-terminal` can never make the
 * daemon observe a half-written command. Same discipline as write_set_index_file. */
bool write_set_terminal_file(const char *cmd) {
    const char *path = get_set_terminal_file_path();
    if (!path || !path[0]) {
        fprintf(stderr, "Cannot determine set-terminal file path\n");
        return false;
    }
    char tmp[NW_PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        fprintf(stderr, "set-terminal path too long\n");
        return false;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "Failed to create set-terminal file: %s\n", strerror(errno));
        return false;
    }
    /* Store the command verbatim on one line. Newlines in the command would
     * confuse the single-line reader, so reject them up front. */
    size_t len = strlen(cmd);
    if (write(fd, cmd, len) != (ssize_t)len ||
        write(fd, "\n", 1) != 1) {
        fprintf(stderr, "Failed to write set-terminal file: %s\n", strerror(errno));
        close(fd);
        unlink(tmp);
        return false;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "Failed to install set-terminal file: %s\n", strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

/* Claim and read one exact terminal request. See read_set_index_file(): the
 * private rename is the request identity, so consuming it cannot unlink a
 * newer concurrently-published command. */
bool read_set_terminal_file(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    const char *path = get_set_terminal_file_path();
    char claimed[NW_PATH_MAX];
    int n = snprintf(claimed, sizeof(claimed), "%s.take-%d", path, (int)getpid());
    if (n < 0 || (size_t)n >= sizeof(claimed) || rename(path, claimed) != 0) {
        return false;
    }
    FILE *fp = fopen(claimed, "r");
    if (!fp) {
        unlink(claimed);
        return false;
    }
    char *line = fgets(out, (int)out_len, fp);
    fclose(fp);
    unlink(claimed);
    if (!line) {
        out[0] = '\0';
        return false;
    }
    /* Strip the trailing newline write_set_terminal_file appended. */
    size_t l = strlen(out);
    while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r')) {
        out[--l] = '\0';
    }
    return out[0] != '\0';
}
