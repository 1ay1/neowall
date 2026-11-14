/* _POSIX_C_SOURCE is set by the build; realpath() additionally needs the XSI
 * declaration from stdlib.h, which _DEFAULT_SOURCE exposes on glibc. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include "neowall/app/app.h"
#include "neowall/state/state.h"
#include "neowall/runtime/sd_notify.h"
#include "neowall/core/wall_clock.h"
#include "neowall/app/watch.h"
#include "neowall/shader/shader.h"
#include "neowall/surface/surface.h"
#include "neowall/egl/egl_core.h"
#include "neowall/egl/gpu_health.h"

#include "neowall/app/daemon.h"
#include "neowall/cli/cli.h"
#include "neowall/config/config.h"
#include "neowall/core/util.h"
#include "neowall/output/output.h"
#include "neowall/shader/term_gpu.h"
#include "neowall/shader/wallshader.h"
#include "version.h"
#include "neowall/proto/proto.h"

/* The throwaway config (and placeholder) a preview/watch writes. Removed on
 * every exit, not just the clean one. */
static char g_preview_config[NW_PATH_MAX];

static void remove_preview_files(void) {
    if (!g_preview_config[0]) return;
    unlink(g_preview_config);
    char ph[NW_PATH_MAX + 32];
    snprintf(ph, sizeof(ph), "%s.placeholder.glsl", g_preview_config);
    unlink(ph);
    g_preview_config[0] = '\0';
}

int main(int argc, char *argv[]) {
    struct neowall_state state    = {0};
    char config_path[NW_PATH_MAX] = {0};
    bool daemon_mode              = true; /* Default to daemon mode */
    bool verbose                  = false;
    bool clock_shifted            = false; /* --date/--timelapse moved the shader clock */
    const char *preview_shader    = NULL;  /* `preview <shader>`: run this, not the config */
    bool watch_mode               = false; /* `watch <shader>`: recompile on save */
    int opt;

    if (argc >= 2 && strcmp(argv[1], "new") == 0) return cmd_new(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "import") == 0) return cmd_import(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "docs") == 0) return cmd_docs(argc, argv);

    /* `preview`/`watch` are run modes, not daemon commands: they run one
     * shader in the foreground alongside the real wallpaper without touching
     * the config, the saved state or the daemon's pid file. */
    if (argc >= 2 && (strcmp(argv[1], "preview") == 0 || strcmp(argv[1], "watch") == 0)) {
        bool is_watch = (argv[1][0] == 'w');
        if (argc < 3 || argv[2][0] == '-') {
            fprintf(stderr, "Usage: %s %s <shader.glsl> [--date=+6d] [--timelapse=30d/20s]\n",
                    argv[0], is_watch ? "watch" : "preview");
            return EXIT_FAILURE;
        }
        preview_shader = argv[2];
        watch_mode     = is_watch;

        /* Drop the subcommand and its argument so getopt sees only flags;
         * it scans from index 1, so argv[0] must stay a program name. */
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }

    if (!preview_shader && argc >= 2 && argv[1][0] != '-') return cmd_control(argc, argv);

    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"foreground", no_argument,       0, 'f'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {"date",       required_argument, 0, 'D'},
        {"timelapse",  required_argument, 0, 'T'},
        {0,            0,                 0, 0  }
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "c:fvhV", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c': snprintf(config_path, sizeof(config_path), "%s", optarg); break;
        case 'f':
            daemon_mode = false; /* Explicit foreground */
            break;
        case 'v':
            /* Verbose mode - enable debug logging */
            verbose = true;
            break;
        case 'D': {
            /* Shift the shader-visible clock. Lets a day-scale shader be
             * inspected at an arbitrary point in its cycle instead of
             * waiting for the calendar to get there. */
            long secs = 0;
            if (!nw_clock_parse_offset(optarg, &secs)) {
                fprintf(stderr, "Invalid --date offset: %s\n", optarg);
                fprintf(stderr, "Expected a signed duration like +6d, -2h, 90m or 3600.\n");
                return EXIT_FAILURE;
            }
            nw_clock_set_offset(secs);
            clock_shifted = true;
            break;
        }
        case 'T': {
            /* Compress a span of shader time into a span of real time. */
            double scale = 1.0;
            if (!nw_clock_parse_timelapse(optarg, &scale)) {
                fprintf(stderr, "Invalid --timelapse spec: %s\n", optarg);
                fprintf(stderr, "Expected SPAN/DURATION, e.g. 30d/20s.\n");
                return EXIT_FAILURE;
            }
            nw_clock_set_scale(scale);
            clock_shifted = true;
            break;
        }
        case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
        case 'V': print_version(); return EXIT_SUCCESS;
        default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    /* Set up logging */
    if (verbose) {
        log_set_level(LOG_LEVEL_DEBUG);
    }
    log_info("NeoWall v%s starting...", NEOWALL_VERSION_STRING);

    if (clock_shifted) {
        time_t vnow = nw_clock_now();
        char stamp[64];
        struct tm tmv;
        if (localtime_r(&vnow, &tmv) && strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv)) {
            log_info("Shader clock: virtual (now=%s, offset=%lds, scale=%.4gx)", stamp,
                     nw_clock_offset(), nw_clock_scale());
        }
        log_info("Shader clock is NOT real time - iDate/iTimeOfDay/iSun are shifted");
    }

    /* Ensure config directory exists */
    if (!create_config_directory()) {
        log_error("Failed to create configuration directory");
        return EXIT_FAILURE;
    }

    /* Preview mode: synthesise a throwaway config pointing at the shader, so
     * the whole normal load/render path is exercised unchanged. Never touches
     * the user's config. Foreground is implied: a preview you cannot Ctrl-C is
     * not a preview. */
    char preview_config[NW_PATH_MAX]   = {0};
    char preview_resolved[NW_PATH_MAX] = {0};
    if (preview_shader) {
        /* A path that exists wins (./x.glsl, ../x.glsl). Otherwise look the
         * name up in the shader dirs, the same way the config does, so the
         * `neowall watch hello.glsl` that `neowall new` prints works from
         * anywhere. */
        char resolved[NW_PATH_MAX];
        char found[NW_PATH_MAX];
        bool ok = realpath(preview_shader, resolved) != NULL;
        if (!ok && !strchr(preview_shader, '/') &&
            shader_resolve_path(preview_shader, found, sizeof(found)))
            ok = realpath(found, resolved) != NULL;
        if (!ok) {
            fprintf(stderr,
                    "Cannot find shader: %s\n"
                    "Looked in the current directory and ~/.config/neowall/shaders/\n"
                    "Make one with: neowall new <name>\n",
                    preview_shader);
            return EXIT_FAILURE;
        }
        snprintf(preview_resolved, sizeof(preview_resolved), "%s", resolved);

        /* $XDG_RUNTIME_DIR, else the 0700 per-user dir; never a guessable
         * name in shared /tmp. */
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir || !runtime_dir[0]) runtime_dir = neowall_secure_runtime_dir();
        if (!runtime_dir) {
            fprintf(stderr, "No private directory for the preview config\n");
            return EXIT_FAILURE;
        }
        snprintf(preview_config, sizeof(preview_config), "%s/neowall-preview-%d.vibe", runtime_dir,
                 (int)getpid());

        FILE *pf = fopen(preview_config, "w");
        if (!pf) {
            fprintf(stderr, "Cannot create preview config: %s\n", preview_config);
            return EXIT_FAILURE;
        }
        fprintf(pf, "# Generated by `neowall preview`; safe to delete.\n");
        fprintf(pf, "default {\n  shader %s\n}\n", resolved);
        fclose(pf);
        snprintf(g_preview_config, sizeof(g_preview_config), "%s", preview_config);
        atexit(remove_preview_files);

        snprintf(config_path, sizeof(config_path), "%s", preview_config);
        daemon_mode = false;
        printf("Previewing %s (Ctrl-C to stop)\n", resolved);
        fflush(stdout);
    }

    /* Determine config file path */
    if (config_path[0] == '\0') {
        const char *default_path = config_get_default_path();
        if (default_path) {
            snprintf(config_path, sizeof(config_path), "%s", default_path);
        } else {
            log_error("Could not determine config file path");
            return EXIT_FAILURE;
        }
    }

    log_info("Using configuration file: %s", config_path);

    /* Atomically claim the PID file. This races correctly against another
     * concurrent `neowall` invocation: only one O_CREAT|O_EXCL succeeds. We
     * claim BEFORE forking so the grandchild can't be beaten to the file by
     * a sibling invocation during the fork dance.
     *
     * Preview is exempt: it is a transient foreground instance meant to run
     * alongside your real wallpaper. Claiming the pid file would either refuse
     * to start or, worse, delete the running daemon's file on exit and leave
     * `neowall reload/next/kill` unable to find it. */
    pid_t existing_pid = 0;
    pid_claim claim    = preview_shader ? PID_CLAIM_OK : try_take_pid_file(&existing_pid);
    if (claim == PID_CLAIM_FAILED) {
        fprintf(stderr, "Error: can't create the PID file %s (see the log above)\n",
                get_pid_file_path());
        return EXIT_FAILURE;
    }
    if (claim == PID_CLAIM_HELD) {
        const char *pid_path = get_pid_file_path();
        log_error("NeoWall is already running (PID %d)", existing_pid);
        fprintf(stderr, "Error: NeoWall is already running (PID %d)\n", existing_pid);
        fprintf(stderr, "PID file: %s\n", pid_path);
        fprintf(stderr, "Use 'neowall kill' to stop the running instance.\n");
        fprintf(stderr, "To try a shader without stopping it: neowall preview <shader.glsl>\n");
        return EXIT_FAILURE;
    }

    /* Daemonize if requested. The grandchild will rewrite the PID file to its
     * own PID via update_pid_file() at the end of daemonize(). Under systemd
     * we stay in the foreground: a fork looks like the service exiting. */
    if (daemon_mode && nw_under_systemd()) {
        daemon_mode = false;
    }
    if (daemon_mode) {
        log_info("Running as daemon...");
        if (!daemonize()) {
            remove_pid_file();
            return EXIT_FAILURE;
        }
    }

    /* Set up crash handlers first */
    setup_crash_handlers();

    /* Initialize atomic state flags - MUST use atomic_init before any access */
    atomic_init(&state.running, true);
    atomic_init(&state.mouse_interaction,
                true); /* default: pointer enabled, override from config */
    atomic_init(
        &state.term_raw_input,
        false); /* default: drop signal/EOF keys so a stray Ctrl-C can't kill the wallpaper app */
    /* snprintf guarantees NUL-termination and silences GCC -O2
     * -Wstringop-truncation (config_path and state.config_path are the same
     * size, which is exactly the case the warning targets). */
    snprintf(state.config_path, sizeof(state.config_path), "%s", config_path);
    state.is_preview = preview_shader != NULL;
    pthread_mutex_init(&state.state_mutex, NULL);
    pthread_rwlock_init(&state.output_list_lock, NULL);
    app_attach(&state);
    pthread_mutex_init(&state.state_file_lock, NULL);

    /* Block the command/shutdown signals before any thread starts. */
    if (!block_signals()) {
        log_error("Failed to set up signal handling");
        remove_pid_file();
        return EXIT_FAILURE;
    }

    /* Start the shader watcher for `neowall watch`. Failure is non-fatal: the
     * shader still renders, it just will not hot-reload. */
    state.watch_fd      = -1;
    state.watch_path[0] = '\0';
    if (watch_mode && preview_shader) {
        snprintf(state.watch_path, sizeof(state.watch_path), "%s", preview_resolved);
        state.watch_fd = watch_init(preview_resolved);
        if (state.watch_fd < 0) {
            log_warn("watch: hot-reload unavailable; running as a plain preview");
        }
    }

    /* Initialize compositor backend (auto-detects Wayland/X11) */
    log_info("Initializing compositor backend...");
    state.compositor_backend = compositor_backend_init(&state);
    if (!state.compositor_backend) {
        log_error("Failed to initialize compositor backend");
        log_error("Ensure you're running under a Wayland compositor or X11 window manager");
        remove_pid_file();
        return EXIT_FAILURE;
    }

    log_info("Compositor backend initialized: %s", state.compositor_backend->name);
    log_info("Description: %s", state.compositor_backend->description);

    /* Initialize EGL/OpenGL */
    if (!app_graphics_up(&state)) {
        log_error("Failed to initialize EGL");
        compositor_backend_cleanup(state.compositor_backend);
        remove_pid_file();
        return EXIT_FAILURE;
    }

    /* Load configuration and apply to outputs */
    bool loaded;
    if (watch_mode && preview_shader) {
        /* A shader that doesn't compile yet is the normal state mid-edit, so
         * don't exit: show its errors, run a placeholder, and swap the real
         * file in on the first save that compiles. */
        log_quiet_errors(true);
        loaded = app_config_load(&state, config_path);
        log_quiet_errors(false);
        if (!loaded) {
            printf("\n%s doesn't compile yet:\n", preview_resolved);
            watch_print_errors();
            printf("showing a placeholder - fix and save to load it\n");
            fflush(stdout);
            loaded = watch_load_placeholder(&state, preview_config);
        }
    } else {
        loaded = app_config_load(&state, config_path);
    }
    if (!loaded) {
        log_error("Failed to load configuration");
        app_graphics_down(&state);
        compositor_backend_cleanup(state.compositor_backend);
        remove_pid_file();
        return EXIT_FAILURE;
    }

    log_info("Initialization complete, entering main loop...");

    /* Run the program on the runtime loop until it's told to stop. */
    int run_code = app_run(&state);

    /* Cleanup */
    log_info("Shutting down...");

    /* Kill any terminal-wallpaper children FIRST, before the alarm and GL
     * teardown. Freeing the shader (which frees the terminal) is NOT reached
     * on this exit path, so without this the child is orphaned to init and a
     * TUI keeps spinning on its dead PTY. This only signals a process group,
     * so it's fast and must not be skipped. */
    pthread_rwlock_rdlock(&state.output_list_lock);
    for (struct output_state *o = state.outputs; o; o = o->next) {
        term_gpu_shutdown(wallshader_terminal(o->shader));
    }
    pthread_rwlock_unlock(&state.output_list_lock);

    /* Set alarm as last resort - force exit after 2 seconds if cleanup hangs */
    alarm(2);

    /* Quick cleanup - don't spend too much time on this during shutdown */
    app_graphics_down(&state);

    /* Cleanup compositor backend */
    if (state.compositor_backend) {
        compositor_backend_cleanup(state.compositor_backend);
        state.compositor_backend = NULL;
    }

    /* Skip mutex/lock destruction during fast shutdown - OS will clean up */
    pthread_rwlock_destroy(&state.output_list_lock);
    pthread_mutex_destroy(&state.state_mutex);
    pthread_mutex_destroy(&state.state_file_lock);

    /* Remove PID file */
    remove_pid_file();

    /* Drop the throwaway preview config. */
    remove_preview_files();

    watch_cleanup(state.watch_fd);

    /* Cancel alarm - we finished cleanup in time */
    alarm(0);

    if (run_code == NW_EXIT_GPU_LOST) {
        /* systemd restarts us (Restart=on-failure). Otherwise start over in
         * this pid: a new process gets a new GL context. The restart counter
         * in the environment stops a GPU that never recovers from looping. */
        if (!nw_under_systemd() && !preview_shader && gpu_restart_allowed()) {
            log_info("Restarting neowall after GPU loss");
            execv("/proc/self/exe", argv);
            log_error("Restart failed: %s", strerror(errno));
        }
        return NW_EXIT_GPU_LOST;
    }

    log_info("NeoWall terminated successfully");

    return EXIT_SUCCESS;
}
