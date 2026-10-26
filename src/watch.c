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
#include "neowall/shader/shader.h"
#include "neowall/shader/shader_error_log.h"
#include "neowall/shader/shader_multipass.h"
#include "neowall/shader/shader_diag.h"
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
 * message, the source line with a caret, and a "did you mean". Falls back to
 * the raw driver log if it can't be parsed. */
void watch_print_errors(const char *source, const char *path) {
    static bool known_loaded = false;
    if (!known_loaded) {
        const char *chunks[32];
        size_t n = multipass_preamble_chunks(chunks, 32);
        shader_diag_set_known(chunks, n);
        known_loaded = true;
    }
    const char *log = shader_get_last_error_log();
    static char out[16384];
    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    if (shader_diag_format(log, source, path, color, out, sizeof(out))) {
        fputs(out, stdout);
    } else if (log && *log) {
        fputs(log, stdout);
        if (log[strlen(log) - 1] != '\n') putchar('\n');
    }
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
    if (!probe) {
        free(source);
        printf("parse FAILED - keeping the previous shader on screen\n");
        fflush(stdout);
        return;
    }

    /* Compile against a small offscreen size: we only care whether the GLSL is
     * accepted, not how it looks. Driver chatter goes to the debug log; the
     * author gets the readable version below. */
    log_quiet_errors(true);
    bool compiles = multipass_init_gl(probe, 64, 64) && multipass_compile_all(probe);
    log_quiet_errors(false);
    multipass_destroy(probe);

    if (!compiles) {
        watch_print_errors(source, path);
        free(source);
        printf("keeping the previous shader on screen - fix and save to retry\n");
        fflush(stdout);
        return;
    }
    free(source);

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
