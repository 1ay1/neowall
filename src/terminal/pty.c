/*
 * pty.c — the full `terminal`: PTY + child process + reader thread + screen.
 *
 * Spawns a command under a pseudo-terminal (forkpty, glibc — no dependency),
 * runs a background reader thread that drains the PTY and feeds the screen
 * model under a mutex, and exposes a frame-coherent snapshot to the GL thread.
 *
 * Thread discipline mirrors the slideshow preload thread documented in
 * ARCHITECTURE.md §6: cooperative cancellation via an atomic stop flag + a
 * self-pipe to unblock the read(), and ALWAYS pthread_join on destroy — never
 * pthread_cancel, because the parser is not async-cancel-safe.
 */
#define _DEFAULT_SOURCE   /* forkpty (<pty.h>) */

#include "neowall/terminal/terminal.h"
#include "vtparse.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* term_screen is defined in screen.c; we only use its public API here, plus one
 * accessor to build the snapshot. To copy the grid we read rows via
 * term_screen_row(). */

struct terminal {
    term_screen *screen;
    int          master_fd;    /* PTY master */
    pid_t        child;
    int          selfpipe[2];  /* wake the reader to exit */

    pthread_t    reader;
    pthread_mutex_t lock;       /* guards screen + snapshot build */
    atomic_bool  stop;
    atomic_bool  child_exited;
    atomic_int   child_status;

    int cols, rows;

    /* snapshot buffer handed to the GL thread (double-buffer: build under lock,
     * expose a stable pointer). */
    term_cell  *snap_cells;
    term_frame  frame;
    uint64_t    epoch;
    atomic_ullong dirty_epoch;  /* bumped by reader when grid changes */
};

/* ------------------------------------------------------------------------ */

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static bool write_reply(terminal *t, const char *reply, size_t len) {
    size_t off = 0;
    while (off < len && !atomic_load(&t->stop)) {
        ssize_t w = write(t->master_fd, reply + off, len - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EAGAIN) {
            struct pollfd fds[2] = {
                {.fd = t->master_fd, .events = POLLOUT},
                {.fd = t->selfpipe[0], .events = POLLIN},
            };
            int rv;
            do { rv = poll(fds, 2, -1); } while (rv < 0 && errno == EINTR && !atomic_load(&t->stop));
            if (rv <= 0 || (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) return false;
            continue;
        }
        return false;
    }
    return off == len;
}

static void *reader_main(void *arg) {
    terminal *t = arg;
    uint8_t buf[8192];

    for (;;) {
        if (atomic_load(&t->stop)) break;

        struct pollfd fds[2] = {
            {.fd = t->master_fd, .events = POLLIN},
            {.fd = t->selfpipe[0], .events = POLLIN},
        };
        int rv = poll(fds, 2, -1);
        if (rv < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) break;

        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(t->master_fd, buf, sizeof(buf));
            if (n > 0) {
                char reply[128];
                size_t rlen;
                pthread_mutex_lock(&t->lock);
                term_screen_feed(t->screen, buf, (size_t)n);
                /* Drain any query responses (DSR/DA/…) the app requested and
                 * send them straight back to the child. This is what unblocks
                 * probing TUIs (btop, vim) that stall on the startup handshake
                 * waiting for a cursor-position / device-attributes reply. */
                rlen = term_screen_take_reply(t->screen, reply, sizeof(reply));
                pthread_mutex_unlock(&t->lock);
                if (rlen > 0) (void)write_reply(t, reply, rlen);
                atomic_fetch_add(&t->dirty_epoch, 1);
            } else if (n == 0) {
                /* EOF: child closed the PTY. */
                break;
            } else {
                if (errno == EAGAIN || errno == EINTR) continue;
                break;
            }
        }
    }

    /* reap the child if it has exited */
    int status = 0;
    pid_t r = waitpid(t->child, &status, WNOHANG);
    if (r == t->child) {
        atomic_store(&t->child_status, status);
        atomic_store(&t->child_exited, true);
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */

nw_result term_spawn(const term_spawn_opts *opts, terminal **out) {
    if (!opts || !opts->cmd || !out) return nw_err(NW_ERR_INVALID_ARG, "term_spawn: null arg");

    int cols = opts->cols > 0 ? opts->cols : 80;
    int rows = opts->rows > 0 ? opts->rows : 24;
    if (cols > TERM_SCREEN_MAX_COLS) cols = TERM_SCREEN_MAX_COLS;
    if (rows > TERM_SCREEN_MAX_ROWS) rows = TERM_SCREEN_MAX_ROWS;

    terminal *t = calloc(1, sizeof(*t));
    if (!t) return nw_err(NW_ERR_OOM, "term_spawn: alloc");
    t->master_fd = -1;
    t->selfpipe[0] = t->selfpipe[1] = -1;
    t->cols = cols; t->rows = rows;

    t->screen = term_screen_create(cols, rows);
    if (!t->screen) { free(t); return nw_err(NW_ERR_OOM, "term_spawn: screen"); }

    t->snap_cells = calloc((size_t)cols * rows, sizeof(term_cell));
    if (!t->snap_cells) { term_screen_destroy(t->screen); free(t); return nw_err(NW_ERR_OOM, "snap"); }

    if (pthread_mutex_init(&t->lock, NULL) != 0) {
        free(t->snap_cells); term_screen_destroy(t->screen); free(t);
        return nw_err(NW_ERR_STATE, "mutex");
    }
    if (pipe(t->selfpipe) != 0) {
        pthread_mutex_destroy(&t->lock);
        free(t->snap_cells); term_screen_destroy(t->screen); free(t);
        return nw_err(NW_ERR_IO, "selfpipe");
    }

    /* Resolve everything that needs libc allocation before fork. The child only
     * calls async-signal-safe chdir/execve/_exit operations. */
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) sh = "/bin/sh";
    const char *term_name = opts->term_env ? opts->term_env : "xterm-256color";
    size_t term_len = strlen(term_name) + sizeof("TERM=");
    char *term_assignment = malloc(term_len);
    char *color_assignment = strdup("COLORTERM=truecolor");
    extern char **environ;
    size_t env_count = 0;
    while (environ[env_count]) env_count++;
    char **child_env = calloc(env_count + 3, sizeof(*child_env));
    if (!term_assignment || !color_assignment || !child_env) {
        free(term_assignment); free(color_assignment); free(child_env);
        close(t->selfpipe[0]); close(t->selfpipe[1]);
        pthread_mutex_destroy(&t->lock);
        free(t->snap_cells); term_screen_destroy(t->screen); free(t);
        return nw_err(NW_ERR_OOM, "child environment");
    }
    snprintf(term_assignment, term_len, "TERM=%s", term_name);
    size_t child_env_count = 0;
    for (size_t i = 0; i < env_count; i++) {
        if (strncmp(environ[i], "TERM=", 5) == 0 ||
            strncmp(environ[i], "COLORTERM=", 10) == 0) continue;
        child_env[child_env_count++] = environ[i];
    }
    child_env[child_env_count++] = term_assignment;
    child_env[child_env_count++] = color_assignment;

    struct winsize ws = {.ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols};
    pid_t pid = forkpty(&t->master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        free(child_env); free(color_assignment); free(term_assignment);
        close(t->selfpipe[0]); close(t->selfpipe[1]);
        pthread_mutex_destroy(&t->lock);
        free(t->snap_cells); term_screen_destroy(t->screen); free(t);
        return nw_err(NW_ERR_IO, "forkpty");
    }

    if (pid == 0) {
        /* child */
#ifdef __linux__
        /* Kill this child (and thus its whole PTY session) automatically when
         * neowall dies by ANY means — a clean shutdown, a crash that skips
         * term_destroy(), or an uncatchable SIGKILL. Without this a TUI keeps
         * spinning on its dead PTY, reparented to init. PR_SET_PDEATHSIG is
         * cleared across execve only for the signal *disposition*, not the
         * setting, so it survives the exec below.
         *
         * The signal fires when the parent THREAD (the one that called
         * forkpty) exits, so guard against a race where that thread already
         * died between fork and here: if our parent is now init, act on it. */
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        if (getppid() == 1) _exit(129);
#endif
        if (opts->cwd && opts->cwd[0]) (void)chdir(opts->cwd);
        char *const argv[] = {(char *)sh, (char *)"-c", (char *)opts->cmd, NULL};
        execve(sh, argv, child_env);
        _exit(127); /* exec failed */
    }

    /* parent */
    free(child_env); free(color_assignment); free(term_assignment);
    t->child = pid;
    set_nonblock(t->master_fd);
    atomic_store(&t->stop, false);
    atomic_store(&t->child_exited, false);
    atomic_store(&t->dirty_epoch, 1);

    if (pthread_create(&t->reader, NULL, reader_main, t) != 0) {
        atomic_store(&t->stop, true);
        killpg(t->child, SIGKILL);
        kill(t->child, SIGKILL);
        while (waitpid(t->child, NULL, 0) < 0 && errno == EINTR) {}
        close(t->master_fd);
        close(t->selfpipe[0]); close(t->selfpipe[1]);
        pthread_mutex_destroy(&t->lock);
        free(t->snap_cells); term_screen_destroy(t->screen); free(t);
        return nw_err(NW_ERR_STATE, "pthread_create");
    }

    *out = t;
    return nw_ok();
}

void term_destroy(terminal *t) {
    if (!t) return;
    atomic_store(&t->stop, true);
    /* wake the reader out of select() */
    if (t->selfpipe[1] >= 0) { char b = 1; ssize_t w = write(t->selfpipe[1], &b, 1); (void)w; }
    pthread_join(t->reader, NULL);

    /* terminate the child if still alive.
     *
     * forkpty() put the child in its own session with the PTY as controlling
     * terminal, so t->child is a process-GROUP leader. The command usually runs
     * under a shell ($SHELL -c "..."), and that shell may FORK the real program
     * (e.g. `fish -c rb` keeps fish as a wrapper around a child rb) rather than
     * exec-replacing itself. Signalling only t->child then kills the wrapper and
     * ORPHANS the grandchild (reparented to init), which keeps running — and a
     * TUI whose PTY just closed can spin at 100%% CPU on the dead fd.
     *
     * Signal the whole process group (negative pid) so the shell AND anything
     * it spawned die together. SIGHUP first (clean "terminal went away"), then
     * SIGKILL for anything that ignored it. */
    if (!atomic_load(&t->child_exited) && t->child > 0) {
        killpg(t->child, SIGHUP);
        kill(t->child, SIGHUP);   /* belt-and-suspenders if it's not a pgrp leader */
        int status = 0;
        bool reaped = false;
        for (int i = 0; i < 50; i++) {
            if (waitpid(t->child, &status, WNOHANG) == t->child) { reaped = true; break; }
            usleep(2000);
        }
        if (!reaped) {
            killpg(t->child, SIGKILL);
            kill(t->child, SIGKILL);
            waitpid(t->child, &status, 0);
        }
    }

    if (t->master_fd >= 0) close(t->master_fd);
    if (t->selfpipe[0] >= 0) close(t->selfpipe[0]);
    if (t->selfpipe[1] >= 0) close(t->selfpipe[1]);
    pthread_mutex_destroy(&t->lock);
    term_screen_destroy(t->screen);
    free(t->snap_cells);
    free(t);
}

nw_result term_resize(terminal *t, int cols, int rows) {
    if (!t || cols <= 0 || rows <= 0) return nw_err(NW_ERR_INVALID_ARG, "term_resize");
    if (cols > TERM_SCREEN_MAX_COLS) cols = TERM_SCREEN_MAX_COLS;
    if (rows > TERM_SCREEN_MAX_ROWS) rows = TERM_SCREEN_MAX_ROWS;
    term_cell *ns = calloc((size_t)cols * (size_t)rows, sizeof(term_cell));
    if (!ns) return nw_err(NW_ERR_OOM, "term_resize: snapshot");

    pthread_mutex_lock(&t->lock);
    if (!term_screen_resize(t->screen, cols, rows)) {
        pthread_mutex_unlock(&t->lock);
        free(ns);
        return nw_err(NW_ERR_OOM, "term_resize: screen");
    }
    free(t->snap_cells);
    t->snap_cells = ns;
    t->cols = term_screen_cols(t->screen);
    t->rows = term_screen_rows(t->screen);
    cols = t->cols;
    rows = t->rows;
    pthread_mutex_unlock(&t->lock);

    struct winsize ws = {.ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols};
    ioctl(t->master_fd, TIOCSWINSZ, &ws);   /* child receives SIGWINCH */
    atomic_fetch_add(&t->dirty_epoch, 1);
    return nw_ok();
}

/* ------------------------------------------------------------------------ */
/* Input: host → child                                                      */
/* ------------------------------------------------------------------------ */

nw_result term_write(terminal *t, const void *bytes, size_t len) {
    if (!t || t->master_fd < 0) return nw_err(NW_ERR_INVALID_ARG, "term_write: bad terminal");
    if (!bytes || len == 0) return nw_ok();
    if (atomic_load(&t->child_exited)) return nw_err(NW_ERR_STATE, "term_write: child gone");

    const uint8_t *p = bytes;
    size_t off = 0;
    /* The master fd is non-blocking; retry short/EAGAIN writes briefly. TUIs
     * consume input promptly, so a full pipe here is transient. */
    for (int attempts = 0; off < len && attempts < 1000; ) {
        ssize_t w = write(t->master_fd, p + off, len - off);
        if (w > 0) { off += (size_t)w; attempts = 0; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) { attempts++; usleep(200); continue; }
        return nw_err(NW_ERR_IO, "term_write: write failed");
    }
    return off == len ? nw_ok() : nw_err(NW_ERR_IO, "term_write: short write");
}

bool term_wants_mouse(const terminal *t) {
    if (!t) return false;
    int proto = 0;
    pthread_mutex_lock(&((terminal *)t)->lock);
    term_screen_mouse_mode(t->screen, &proto, NULL);
    pthread_mutex_unlock(&((terminal *)t)->lock);
    return proto != 0;
}

bool term_mouse(terminal *t, int cell_x, int cell_y, int button, bool pressed, bool motion) {
    if (!t || t->master_fd < 0) return false;

    int proto = 0; bool sgr = false;
    pthread_mutex_lock(&t->lock);
    term_screen_mouse_mode(t->screen, &proto, &sgr);
    int cols = t->cols, rows = t->rows;
    pthread_mutex_unlock(&t->lock);

    if (proto == 0) return false;               /* app doesn't want mouse */
    if (motion && proto < 1002) return false;   /* click-only: ignore motion */

    /* clamp to grid */
    if (cell_x < 0) cell_x = 0;
    if (cell_x >= cols) cell_x = cols - 1;
    if (cell_y < 0) cell_y = 0;
    if (cell_y >= rows) cell_y = rows - 1;

    /* Assemble the button code. Bit 5 (0x20) marks a motion event; wheel codes
     * (64/65) already carry their high bits. In legacy encoding a release is
     * button 3; in SGR the true button rides with a trailing 'm'. */
    int cb = button;
    if (motion) cb |= 0x20;

    char seq[32];
    int  n;
    if (sgr) {
        /* CSI < b ; x ; y M   (press/motion)   or   ... m  (release) */
        char final = pressed ? 'M' : 'm';
        n = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%d%c", cb, cell_x + 1, cell_y + 1, final);
    } else {
        /* Legacy X10: CSI M  Cb Cx Cy, each offset by 32. On release the button
         * bits become 3. Coordinates cap at 223 (255-32).
         * NOTE the parentheses: `cb & ~0x03 | 0x03` parses as
         * `cb & (~0x03 | 0x03)` == `cb & ~0` == cb, so releases were being
         * reported as presses. */
        int b = pressed ? cb : ((cb & ~0x03) | 0x03);
        int cx = cell_x + 1, cy = cell_y + 1;
        if (cx > 223) cx = 223;
        if (cy > 223) cy = 223;
        n = snprintf(seq, sizeof(seq), "\x1b[M%c%c%c",
                     (char)(b + 32), (char)(cx + 32), (char)(cy + 32));
    }
    if (n <= 0) return false;
    return nw_is_ok(term_write(t, seq, (size_t)n));
}

bool term_child_exited(const terminal *t, int *exit_status_out) {
    if (!t) return true;
    bool ex = atomic_load(&((terminal *)t)->child_exited);
    if (ex && exit_status_out) *exit_status_out = atomic_load(&((terminal *)t)->child_status);
    return ex;
}

const term_frame *term_snapshot(terminal *t) {
    if (!t) return NULL;
    uint64_t de = atomic_load(&t->dirty_epoch);

    pthread_mutex_lock(&t->lock);
    int cols = term_screen_cols(t->screen);
    int rows = term_screen_rows(t->screen);
    for (int y = 0; y < rows; y++) {
        const term_cell *row = term_screen_row(t->screen, y);
        if (row) memcpy(&t->snap_cells[(size_t)y * cols], row, (size_t)cols * sizeof(term_cell));
    }
    int cx, cy;
    term_screen_cursor(t->screen, &cx, &cy);
    bool cvis = term_screen_cursor_visible(t->screen);
    pthread_mutex_unlock(&t->lock);

    t->frame.cols = cols;
    t->frame.rows = rows;
    t->frame.cells = t->snap_cells;
    t->frame.cursor_x = cx;
    t->frame.cursor_y = cy;
    t->frame.cursor_visible = cvis;
    t->frame.epoch = de;
    return &t->frame;
}

unsigned long long term_dirty_epoch(const terminal *t) {
    if (!t) return 0;
    /* dirty_epoch is atomic; the const cast is safe (atomic_load reads only). */
    return (unsigned long long)atomic_load(&((terminal *)t)->dirty_epoch);
}
