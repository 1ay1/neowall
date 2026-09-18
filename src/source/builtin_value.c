/*
 * builtin_value.c — the providers that need no hardware: const, exec, file.
 *
 * These three are what turn the data plane from a fixed list into an open one.
 * Every "can neowall show X" request that used to need a C patch and a release
 * is now one of:
 *
 *   uRain    exec("curl -s wttr.in/?format=%p", 300s)
 *   uUnread  exec("notmuch count tag:unread", 30s)
 *   uFan     file("/sys/class/hwmon/hwmon2/fan1_input", 2s)
 *   uExp     0.85
 *
 * exec() runs a command. That is a real capability change for a wallpaper, so
 * it is off unless the user turns it on — see nw_source_exec_set_allowed().
 * The engine enables it from an explicit config/CLI opt-in, never by default,
 * and a shader downloaded from a gallery cannot quietly gain it.
 *
 * Both exec and file are non-blocking by construction. exec forks and reads on
 * later ticks; a command that hangs is reaped at its deadline and never stalls
 * a frame. That matters because these tick on the render thread.
 */

#include "neowall/source/source.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* A command still running this long after spawn is killed. Keeps a wedged
 * `curl` from accumulating one zombie per interval forever. */
#define EXEC_DEADLINE_MS 10000
/* Cap on bytes read from a command; we only ever parse a leading number. */
#define EXEC_READ_MAX 256

/* ==========================================================================
 * const — a fixed number
 * ========================================================================== */

typedef struct {
    float value;
} const_state;

static void *const_open(const nw_source_spec *spec, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    const_state *st = calloc(1, sizeof(*st));
    if (!st) {
        return NULL;
    }
    st->value = (float)spec->number;
    return st;
}

static void  const_close(void *self) { free(self); }
static float const_scalar(void *self) { return ((const_state *)self)->value; }

static const nw_source_vtable const_vtable = {
    .name   = "const",
    .kind   = NW_SOURCE_SCALAR,
    .open   = const_open,
    .close  = const_close,
    .scalar = const_scalar,
};

/* ==========================================================================
 * file — read a number out of a file, e.g. anything under /sys or /proc
 * ========================================================================== */

typedef struct {
    char  path[NW_SOURCE_ARG_MAX];
    float value;
    float scale; /* divisor, so raw counters can be normalised in the manifest */
} file_state;

static void *file_open(const nw_source_spec *spec, char *err, size_t err_cap) {
    if (!spec->arg[0]) {
        snprintf(err, err_cap, "file() needs a path");
        return NULL;
    }
    file_state *st = calloc(1, sizeof(*st));
    if (!st) {
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    snprintf(st->path, sizeof(st->path), "%s", spec->arg);
    st->scale = 1.0f;

    /* Fail at open rather than silently reading 0 every tick forever. A typo in
     * a manifest path should be a startup error the user sees. */
    FILE *fp = fopen(st->path, "r");
    if (!fp) {
        snprintf(err, err_cap, "cannot read '%s': %s", st->path, strerror(errno));
        free(st);
        return NULL;
    }
    fclose(fp);
    return st;
}

static void file_close(void *self) { free(self); }

static void file_tick(void *self, const nw_source_ctx *ctx) {
    (void)ctx;
    file_state *st = self;
    FILE       *fp = fopen(st->path, "r");
    if (!fp) {
        return; /* keep the last good value; transient /sys races are normal */
    }
    double v = 0.0;
    if (fscanf(fp, "%lf", &v) == 1) {
        st->value = (float)(v / (double)st->scale);
    }
    fclose(fp);
}

static float file_scalar(void *self) { return ((file_state *)self)->value; }

static const nw_source_vtable file_vtable = {
    .name                = "file",
    .kind                = NW_SOURCE_SCALAR,
    .default_interval_ms = 1000,
    .open                = file_open,
    .close               = file_close,
    .tick                = file_tick,
    .scalar              = file_scalar,
};

/* ==========================================================================
 * exec — run a command, parse a number from its stdout
 * ========================================================================== */

static bool g_exec_allowed = false;

void nw_source_exec_set_allowed(bool allowed) { g_exec_allowed = allowed; }
bool nw_source_exec_allowed(void) { return g_exec_allowed; }

typedef struct {
    char     command[NW_SOURCE_ARG_MAX];
    float    value;
    pid_t    pid;        /* 0 when nothing is in flight */
    int      fd;         /* read end of the pipe, -1 when idle */
    uint64_t started_ms; /* for the deadline */
    char     buf[EXEC_READ_MAX];
    size_t   buf_len;
} exec_state;

static void *exec_open(const nw_source_spec *spec, char *err, size_t err_cap) {
    if (!g_exec_allowed) {
        snprintf(err, err_cap, "exec() is disabled; pass --allow-exec to enable it");
        return NULL;
    }
    if (!spec->arg[0]) {
        snprintf(err, err_cap, "exec() needs a command");
        return NULL;
    }
    exec_state *st = calloc(1, sizeof(*st));
    if (!st) {
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    snprintf(st->command, sizeof(st->command), "%s", spec->arg);
    st->pid = 0;
    st->fd  = -1;
    return st;
}

/* Stop tracking the child. `hard` also signals it, for deadline and shutdown. */
static void exec_reap(exec_state *st, bool hard) {
    if (st->fd >= 0) {
        close(st->fd);
        st->fd = -1;
    }
    if (st->pid > 0) {
        if (hard) {
            kill(st->pid, SIGKILL);
        }
        /* Blocking wait is safe: either it already exited, or we just killed
         * it. Without this each interval would leak a zombie. */
        int status = 0;
        waitpid(st->pid, &status, 0);
        st->pid = 0;
    }
    st->buf_len = 0;
}

static void exec_close(void *self) {
    exec_state *st = self;
    exec_reap(st, true);
    free(st);
}

static void exec_spawn(exec_state *st, uint64_t now_ms) {
    int fds[2];
    if (pipe(fds) != 0) {
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return;
    }

    if (pid == 0) {
        /* Child. Wire stdout to the pipe, silence stdin/stderr so a chatty
         * command cannot scribble on the daemon's log. */
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
        execl("/bin/sh", "sh", "-c", st->command, (char *)NULL);
        _exit(127);
    }

    /* Parent. Non-blocking read end so ticking never stalls a frame. */
    close(fds[1]);
    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }
    st->fd         = fds[0];
    st->pid        = pid;
    st->started_ms = now_ms;
    st->buf_len    = 0;
}

/* Drain whatever is ready. Returns true once the command is done. */
static bool exec_collect(exec_state *st) {
    for (;;) {
        if (st->buf_len >= sizeof(st->buf) - 1) {
            return true; /* seen enough; a number is at the front or nowhere */
        }
        ssize_t n = read(st->fd, st->buf + st->buf_len, sizeof(st->buf) - 1 - st->buf_len);
        if (n > 0) {
            st->buf_len += (size_t)n;
            continue;
        }
        if (n == 0) {
            return true; /* EOF: child closed stdout */
        }
        if (errno == EINTR) {
            continue;
        }
        return false; /* EAGAIN: nothing yet, try again next tick */
    }
}

static void exec_finish(exec_state *st) {
    st->buf[st->buf_len] = '\0';

    /* Take the first number anywhere in the output, so "cpu: 42%" works as
     * well as a bare "42". Anything with no number leaves the old value. */
    const char *p = st->buf;
    while (*p) {
        if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+' || *p == '.') && p[1])) {
            char  *end = NULL;
            double v   = strtod(p, &end);
            if (end != p) {
                st->value = (float)v;
                break;
            }
        }
        p++;
    }
    exec_reap(st, false);
}

static void exec_tick(void *self, const nw_source_ctx *ctx) {
    exec_state *st = self;

    if (st->pid > 0) {
        /* A command still running when its next tick arrives is slower than
         * its interval. Let it finish; kill it only at the deadline. */
        if (exec_collect(st)) {
            exec_finish(st);
        } else if (ctx->now_ms - st->started_ms > EXEC_DEADLINE_MS) {
            exec_reap(st, true);
        }
        return;
    }
    exec_spawn(st, ctx->now_ms);
}

static float exec_scalar(void *self) { return ((exec_state *)self)->value; }

/* Hidden means no subprocess, full stop. Without this a covered output would
 * keep forking every interval, which is exactly the cost neowall exists to
 * avoid. */
static void exec_on_visibility(void *self, bool visible) {
    if (!visible) {
        exec_reap((exec_state *)self, true);
    }
}

static const nw_source_vtable exec_vtable = {
    .name                = "exec",
    .kind                = NW_SOURCE_SCALAR,
    .default_interval_ms = 5000,
    .open                = exec_open,
    .close               = exec_close,
    .tick                = exec_tick,
    .scalar              = exec_scalar,
    .on_visibility       = exec_on_visibility,
};

/* ==========================================================================
 * Registration
 * ========================================================================== */

void nw_source_register_builtins(void) {
    /* Idempotent: re-registering is a no-op error we deliberately ignore, so
     * callers do not have to track whether init already happened. */
    nw_source_register(&const_vtable);
    nw_source_register(&file_vtable);
    nw_source_register(&exec_vtable);
}
