/* NeoWall Tray - Daemon Detection Component
 * Implementation of daemon status checking functions
 */

#include "daemon_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

/* Get the path to the daemon PID file */
const char *daemon_get_pid_file_path(void) {
    static char path[256] = {0};

    /* Cache: only compute once */
    if (path[0] == '\0') {
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (runtime_dir) {
            snprintf(path, sizeof(path), "%s/neowalld.pid", runtime_dir);
        } else {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(path, sizeof(path), "%s/.neowalld.pid", home);
            } else {
                snprintf(path, sizeof(path), "/tmp/neowalld-%d.pid", getuid());
            }
        }
    }

    return path;
}

/* Read the daemon PID from the PID file */
pid_t daemon_read_pid(void) {
    const char *pid_path = daemon_get_pid_file_path();

    /* Open PID file */
    int fd = open(pid_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    /* Read PID */
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    /* Parse PID from buffer */
    pid_t pid = 0;
    for (ssize_t i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) {
        pid = pid * 10 + (buf[i] - '0');
    }

    if (pid <= 0) {
        return -1;
    }

    /* Verify process exists using kill(0) - doesn't actually send signal */
    if (kill(pid, 0) != 0) {
        if (errno == ESRCH) {
            /* Process not found - remove stale PID file */
            unlink(pid_path);
        }
        return -1;
    }

    return pid;
}

/* Check if the daemon is currently running */
bool daemon_is_running(void) {
    return (daemon_read_pid() > 0);
}

/* Get the daemon status as a string */
const char *daemon_get_status_string(void) {
    return daemon_is_running() ? "Running" : "Stopped";
}
