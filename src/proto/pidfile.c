/* The pid file: held with flock for the daemon's whole life. See
 * neowall/proto/proto.h. */

/* Being a daemon: the pid file lock, daemonizing, crash handlers, blocked
 * signals, the GPU-loss restart budget. See neowall/app/daemon.h. */

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

#include "neowall/proto/proto.h"
#include "neowall/state/state.h"
#include "neowall/core/log.h"
#include "neowall/core/util.h"

static int pid_lock_fd = -1;

/* Get PID file path */
const char *get_pid_file_path(void) {
    static char pid_path[NW_PATH_MAX];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(pid_path, sizeof(pid_path), "%s/neowall.pid", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(pid_path, sizeof(pid_path), "%s/.neowall.pid", home);
        } else {
            const char *rt = neowall_secure_runtime_dir();
            if (rt) {
                snprintf(pid_path, sizeof(pid_path), "%s/neowall.pid", rt);
            } else {
                pid_path[0] = '\0';
            }
        }
    }

    return pid_path;
}

/* Rewrite the PID through the already-locked descriptor. The open file
 * description (and flock) survives both daemonization forks and remains held
 * until shutdown, making singleton ownership independent of PID reuse. */
bool update_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    int fd               = pid_lock_fd;
    if (fd < 0 || ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
        log_error("Failed to update locked PID file %s: %s", pid_path, strerror(errno));
        return false;
    }
    char buf[32];
    int n   = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    bool ok = n > 0 && write(fd, buf, (size_t)n) == n && fsync(fd) == 0;
    if (!ok) log_error("Failed to write PID to %s: %s", pid_path, strerror(errno));
    return ok;
}

/* Unlink path only while holding a lock on the exact inode currently named by
 * it. This prevents a client observing an old PID and unlinking a replacement
 * daemon's locked PID file. */
static bool unlink_locked_pid_inode(int fd) {
    const char *pid_path = get_pid_file_path();
    struct stat opened, named;
    if (fd < 0 || fstat(fd, &opened) != 0 || lstat(pid_path, &named) != 0 ||
        opened.st_dev != named.st_dev || opened.st_ino != named.st_ino) {
        return false;
    }
    return unlink(pid_path) == 0 || errno == ENOENT;
}

/* Client-side stale cleanup is permitted only after acquiring the stale
 * inode's lock and verifying that the pathname still names that inode. */
bool remove_stale_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    int fd               = open(pid_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT;
    bool removed = false;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) removed = unlink_locked_pid_inode(fd);
    close(fd);
    return removed;
}

/* Remove PID file and release singleton ownership. */
void remove_pid_file(void) {
    const char *pid_path = get_pid_file_path();
    if (pid_lock_fd >= 0) {
        (void)unlink_locked_pid_inode(pid_lock_fd);
        close(pid_lock_fd);
        pid_lock_fd = -1;
        log_debug("Removed PID file: %s", pid_path);
    }
}

/* Claim and exclusively lock the PID file. flock is attached to the open file
 * description and therefore survives the daemonization forks. A stale file is
 * harmless: only the held lock denotes a live daemon. */
pid_claim try_take_pid_file(pid_t *existing_pid_out) {
    const char *pid_path = get_pid_file_path();
    int fd               = open(pid_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        log_error("Failed to open PID file %s: %s", pid_path, strerror(errno));
        return PID_CLAIM_FAILED;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        FILE *fp    = fdopen(dup(fd), "r");
        long parsed = 0;
        char line[64];
        if (fp && fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (neowall_parse_index(line, &parsed) && parsed <= INT_MAX && existing_pid_out)
                *existing_pid_out = (pid_t)parsed;
        }
        if (fp) fclose(fp);
        close(fd);
        return PID_CLAIM_HELD;
    }
    pid_lock_fd = fd;
    if (!update_pid_file()) {
        remove_pid_file();
        return PID_CLAIM_FAILED;
    }
    return PID_CLAIM_OK;
}
