/* Shader hot-reload watcher. See include/neowall/watch.h. */

/* realpath() needs the XSI/BSD declaration in stdlib.h, which _DEFAULT_SOURCE
 * exposes on glibc alongside the build's _POSIX_C_SOURCE. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "neowall/watch.h"
#include "neowall/app.h"
#include "neowall/neowall.h"
#include "neowall/output/output.h"
#include "neowall/shader/wallshader.h"
#include "neowall/config/config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

/* Basename of the watched file, used to filter directory events. */
static char g_watch_name[NAME_MAX + 1];

/* Print the last compile errors against the user's file: file:line, the
 * message, and the source line with a caret (the engine maps them back
 * through #include and the injected preamble). */
void watch_print_errors(void) {
    const char *log = wallshader_last_errors();
    if (!log || !*log) return;
    fputs(log, stdout);
    if (log[strlen(log) - 1] != '\n') putchar('\n');
}

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
    fflush(stdout);   /* the log goes to stderr; keep this in order when piped */
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

    /* output_set_shader builds the new shader before dropping the old one,
     * so a broken save leaves what's on screen alone. Driver chatter goes to
     * the debug log; the author gets the readable version below. */
    int ok = 0, failed = 0;

    log_quiet_errors(true);
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        nw_result r = output_set_shader(o, path);
        if (nw_is_ok(r)) {
            ok++;
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        } else {
            failed++;
            /* every output fails the same way; once is enough */
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    log_quiet_errors(false);

    if (failed) {
        watch_print_errors();
        printf("keeping the previous shader on screen - fix and save to retry\n");
    } else {
        printf("compiled OK (%d output%s)\n", ok, ok == 1 ? "" : "s");
    }
    fflush(stdout);

    app_request_redraw(state);
}

void watch_cleanup(int fd) {
    if (fd >= 0) close(fd);
}

/* Shown while the watched shader doesn't compile: a slow dark gradient, so
 * it's obvious nothing is wrong with neowall itself. */
static const char *kPlaceholder =
    "void mainImage(out vec4 c, in vec2 f) {\n"
    "    vec2 uv = f / iResolution.xy;\n"
    "    float t = 0.5 + 0.5 * sin(iTime * 0.4 + uv.x * 2.0);\n"
    "    c = vec4(mix(vec3(0.05, 0.06, 0.09), vec3(0.10, 0.08, 0.14), t * uv.y), 1.0);\n"
    "}\n";

bool watch_load_placeholder(struct neowall_state *state, const char *config_path) {
    char shader_path[PATH_MAX];
    snprintf(shader_path, sizeof(shader_path), "%s.placeholder.glsl", config_path);
    FILE *f = fopen(shader_path, "w");
    if (!f) return false;
    fputs(kPlaceholder, f);
    fclose(f);

    FILE *c = fopen(config_path, "w");
    if (!c) return false;
    fprintf(c, "# Generated by `neowall watch`; safe to delete.\ndefault {\n  shader %s\n}\n",
            shader_path);
    fclose(c);
    return config_load(state, config_path);
}
