/* neowall/state/state.h — the daemon's shared state: the compositor backend,
 * the GL context, the output list and its locks.
 *
 * It holds only what several modules share. Each module includes what it
 * uses itself; this header pulls in nothing but libc and the EGL types. */
#ifndef NEOWALL_STATE_H
#define NEOWALL_STATE_H

#include <EGL/egl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "neowall/core/constants.h"

struct compositor_backend;
struct output_state;

struct neowall_state {
    struct compositor_backend *compositor_backend;

    /* GL: one context shared by every output. */
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;
    /* eglSwapBuffersWithDamage (EXT or KHR), NULL if unsupported: present only
     * the changed region. */
    void *swap_with_damage;
    bool swap_damage_supported;
    bool gl_robust;          /* context created with reset notification */
    bool gpu_health_pending; /* gpu_health_init waits for a surface */

    /* Outputs: a list under output_list_lock, each refcounted. */
    struct output_state *outputs;
    uint32_t output_count;

    char config_path[NW_PATH_MAX];

    /* Read by worker threads. */
    atomic_bool running;           /* false once the daemon is stopping */
    atomic_bool mouse_interaction; /* bind the pointer, feed iMouse */
    atomic_bool term_raw_input;    /* pass Ctrl-C/D/\/Z to a focused terminal */
    bool allow_exec_cli;           /* --allow-exec: exec() uniforms on, whatever the config */
    atomic_uint shader_ease_ms;    /* shader_pause_ease_ms: pause/resume ease default */

    /* Lock order: output_list_lock, then state_mutex, never the reverse.
     * state_file_lock serialises state-file writes. */
    pthread_rwlock_t output_list_lock;
    pthread_mutex_t state_mutex;
    pthread_mutex_t state_file_lock;

    /* `neowall watch` */
    int watch_fd;                 /* inotify fd, -1 when not watching */
    char watch_path[NW_PATH_MAX]; /* '' = none */

    /* preview/watch run beside the real daemon: no pid file, no socket */
    bool is_preview;

    uint64_t frames_rendered;
    uint64_t errors_count;

    /* How lower layers tell the program something happened, without
     * knowing it exists. app_run sets them; any thread may call them; NULL
     * (e.g. in tests) means nobody's listening. */
    void (*on_output_dirty)(struct output_state *output); /* needs a redraw */
    void (*on_outputs_changed)(uint32_t name);            /* added or resized */
    /* an output is up (surface + GL) and wants its wallpaper now */
    bool (*on_output_ready)(struct neowall_state *state, struct output_state *output);
};

/* Fire the hooks; a no-op when unset. */
static inline void state_output_dirty(struct neowall_state *s, struct output_state *o) {
    if (s && s->on_output_dirty) s->on_output_dirty(o);
}
static inline void state_outputs_changed(struct neowall_state *s, uint32_t name) {
    if (s && s->on_outputs_changed) s->on_outputs_changed(name);
}
static inline bool state_output_ready(struct neowall_state *s, struct output_state *o) {
    return s && s->on_output_ready && s->on_output_ready(s, o);
}

#endif /* NEOWALL_STATE_H */
