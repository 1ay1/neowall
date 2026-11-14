/* Children start with a clean signal state, even though the daemon blocks
 * its control signals and ignores SIGPIPE. */

#define _GNU_SOURCE
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "neowall/core/child_signals.h"

extern char **environ;

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* The child is `cat /proc/self/status`: cat leaves its signal state alone,
 * unlike sh (blocks SIGCHLD while waiting) or awk (ignores SIGPIPE). */
static const char *field(const char *status, const char *name, char *out, size_t n) {
    const char *p = strstr(status, name);
    out[0] = '\0';
    if (p) sscanf(p + strlen(name), " %31s", out);
    (void)n;
    return out;
}

static unsigned long long mask_has(const char *hex, int sig) {
    return (strtoull(hex, NULL, 16) >> (sig - 1)) & 1ULL;
}

static void check_masks(const char *what, const char *status) {
    char blk[32], ign[32];
    field(status, "SigBlk:", blk, sizeof(blk));
    field(status, "SigIgn:", ign, sizeof(ign));
    CHECK(blk[0] && ign[0], "%s: no masks in child status", what);
    CHECK(strtoull(blk, NULL, 16) == 0, "%s: child has blocked signals %s", what, blk);
    CHECK(!mask_has(ign, SIGPIPE), "%s: child ignores SIGPIPE (%s)", what, ign);
}

static void read_all(int fd, char *buf, size_t n) {
    size_t got = 0;
    ssize_t r;
    while (got + 1 < n && (r = read(fd, buf + got, n - 1 - got)) > 0) got += (size_t)r;
    buf[got] = '\0';
}

int main(void) {
    /* Daemon-like state: block the control signals, ignore SIGPIPE. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    signal(SIGPIPE, SIG_IGN);

    char *argv[] = {"cat", "/proc/self/status", NULL};
    char out[8192];

    /* posix_spawn path (parec, nvidia-smi). */
    int p[2];
    CHECK(pipe(p) == 0, "pipe");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawnattr_t attr;
    CHECK(nw_spawnattr_reset_signals(&attr) == 0, "spawnattr");
    pid_t pid;
    int rc = posix_spawnp(&pid, "cat", &fa, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    CHECK(rc == 0, "posix_spawnp: %s", strerror(rc));
    read_all(p[0], out, sizeof(out));
    close(p[0]);
    waitpid(pid, NULL, 0);
    check_masks("posix_spawn", out);

    /* fork path (terminal shell, exec sources). */
    CHECK(pipe(p) == 0, "pipe");
    pid = fork();
    if (pid == 0) {
        nw_child_reset_signals();
        dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);
        execvp("cat", argv);
        _exit(127);
    }
    close(p[1]);
    read_all(p[0], out, sizeof(out));
    close(p[0]);
    waitpid(pid, NULL, 0);
    check_masks("fork", out);

    /* The parent's own state is untouched. */
    sigset_t now;
    pthread_sigmask(SIG_BLOCK, NULL, &now);
    CHECK(sigismember(&now, SIGTERM), "parent mask changed");

    /* Fds: an fd the parent forgot to mark close-on-exec (like a pty master
     * or another child's pipe) must not reach either kind of child. */
    int stray = dup(STDERR_FILENO);          /* no FD_CLOEXEC */
    char *lsfd[] = {"ls", "/proc/self/fd", NULL};

    CHECK(nw_pipe_cloexec(p) == 0, "pipe");
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], STDOUT_FILENO);
    nw_spawn_close_fds(&fa, -1);
    rc = posix_spawnp(&pid, "ls", &fa, NULL, lsfd, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    CHECK(rc == 0, "posix_spawnp ls");
    read_all(p[0], out, sizeof(out));
    close(p[0]);
    waitpid(pid, NULL, 0);
    /* ls opens one fd for the directory itself; anything else leaked */
    int n = 0;
    for (char *t = strtok(out, "\n"); t; t = strtok(NULL, "\n")) n++;
    CHECK(n <= 4, "posix_spawn child sees %d fds (0-2 + ls's own)", n);

    CHECK(nw_pipe_cloexec(p) == 0, "pipe");
    pid = fork();
    if (pid == 0) {
        nw_child_reset_signals();
        dup2(p[1], STDOUT_FILENO);
        nw_child_close_fds();
        execvp("ls", lsfd);
        _exit(127);
    }
    close(p[1]);
    read_all(p[0], out, sizeof(out));
    close(p[0]);
    waitpid(pid, NULL, 0);
    n = 0;
    for (char *t = strtok(out, "\n"); t; t = strtok(NULL, "\n")) n++;
    CHECK(n <= 4, "fork child sees %d fds (0-2 + ls's own)", n);
    close(stray);

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
