/* Shader hot-reload watcher. See include/neowall/watch.h. */

/* realpath() needs the XSI/BSD declaration in stdlib.h, which _DEFAULT_SOURCE
 * exposes on glibc alongside the build's _POSIX_C_SOURCE. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "neowall/watch.h"
#include "neowall/neowall.h"
#include "neowall/output/output.h"
#include "neowall/shader/shader.h"
#include "neowall/shader/shader_error_log.h"
#include "neowall/shader/shader_multipass.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

/* Basename of the watched file, used to filter directory events. */
static char g_watch_name[NAME_MAX + 1];

int watch_init(const char *path) {
    if (!path || !*path) return -1;

    char resolved[MAX_PATH_LENGTH];
    if (!realpath(path, resolved)) {
        log_error("watch: cannot resolve %s: %s", path, strerror(errno));
        return -1;
    }

    /* Split into directory + name. Watching the directory (not the file) is
     * what makes this survive an editor's write-temp-then-rename save. */
    char dir[MAX_PATH_LENGTH];
    snprintf(dir, sizeof(dir), "%s", resolved);

    char *slash = strrchr(dir, '/');
    if (!slash) {
        log_error("watch: unexpected path form: %s", resolved);
        return -1;
    }
    snprintf(g_watch_name, sizeof(g_watch_name), "%s", slash + 1);
    *slash = '\0';
    if (dir[0] == '\0') {
        dir[0] = '/';
        dir[1] = '\0';
    }

    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        log_error("watch: inotify_init1 failed: %s", strerror(errno));
        return -1;
    }

    int wd = inotify_add_watch(fd, dir,
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) {
        log_error("watch: cannot watch %s: %s", dir, strerror(errno));
        close(fd);
        return -1;
    }

    printf("Watching %s for changes (Ctrl-C to stop)\n", resolved);
    return fd;
}

bool watch_consume(int fd) {
    if (fd < 0) return false;

    /* inotify delivers variable-length records; the buffer must be able to hold
     * at least one maximum-size event. */
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool changed = false;

    /* Drain fully: one save can produce several events, and leaving any queued
     * would make poll() return immediately forever. */
    for (;;) {
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            /* EAGAIN simply means the queue is empty. */
            break;
        }

        for (char *p = buf; p < buf + len; ) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (ev->len > 0 && strcmp(ev->name, g_watch_name) == 0) {
                changed = true;
            }
            p += sizeof(struct inotify_event) + ev->len;
        }
    }

    return changed;
}

void watch_reload(struct neowall_state *state, const char *path) {
    if (!state || !path) return;

    printf("\n--- reloading %s ---\n", path);
    fflush(stdout);

    /* Clear the compile log so anything printed below belongs to THIS attempt
     * rather than a previous failure. */
    shader_error_log_clear();

    /* Validate FIRST, on a throwaway shader, before touching what is on screen.
     *
     * output_set_shader() tears down the live shader before building the new
     * one, so calling it with broken source leaves the output with no program
     * at all: the screen goes empty and the renderer retries every frame. A
     * dry run keeps a failed save a non-event — which is the entire point of
     * hot-reload while you are mid-edit. */
    char *source = shader_load_file(path);
    if (!source) {
        printf("cannot read %s - keeping the previous shader on screen\n", path);
        fflush(stdout);
        return;
    }

    if (!shader_check_required_version(source, path)) {
        free(source);
        printf("version requirement not met - keeping the previous shader on screen\n");
        fflush(stdout);
        return;
    }

    multipass_shader_t *probe = multipass_create(source);
    free(source);
    if (!probe) {
        printf("parse FAILED - keeping the previous shader on screen\n");
        fflush(stdout);
        return;
    }

    /* Compile against a small offscreen size: we only care whether the GLSL is
     * accepted, not how it looks. */
    bool compiles = multipass_init_gl(probe, 64, 64) && multipass_compile_all(probe);
    multipass_destroy(probe);

    if (!compiles) {
        /* Print the GLSL diagnostics where the author is already looking,
         * instead of burying them in the daemon log. */
        const char *errors = shader_get_last_error_log();
        if (errors && *errors) {
            fputs(errors, stdout);
            if (errors[strlen(errors) - 1] != '\n') putchar('\n');
        }
        printf("compile FAILED - keeping the previous shader on screen\n");
        fflush(stdout);
        return;
    }

    /* The source is known good; now swap it in for real. */
    shader_error_log_clear();

    int ok = 0, failed = 0;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        nw_result r = output_set_shader(o, path);
        if (nw_is_ok(r)) {
            ok++;
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        } else {
            failed++;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    if (failed == 0) {
        printf("compiled OK (%d output%s)\n", ok, ok == 1 ? "" : "s");
    } else {
        printf("applied to %d output(s), %d failed\n", ok, failed);
    }
    fflush(stdout);

    event_loop_request_redraw(state);
}

void watch_cleanup(int fd) {
    if (fd >= 0) close(fd);
}
