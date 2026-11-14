#ifndef NEOWALL_H
#define NEOWALL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include "version.h"
#include "neowall/core/constants.h"
#include "neowall/output/output.h"
#include "neowall/config/config.h"

/* Thread-safe atomic types for flags accessed from multiple threads */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

#define MAX_PATH_LENGTH NW_PATH_MAX
#define MAX_OUTPUTS NW_MAX_OUTPUTS
#define MAX_WALLPAPERS 256

/* Forward declarations */
struct neowall_state;
struct compositor_backend;

/* Global application state */
struct neowall_state {
    /* ===== COMPOSITOR ABSTRACTION - ONLY INTERFACE ===== */
    struct compositor_backend *compositor_backend;
    
    /* ===== EGL CONTEXT (PLATFORM-AGNOSTIC) ===== */
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;

    /* eglSwapBuffersWithDamageEXT/KHR: present only the changed screen region so
     * the compositor recomposites/re-scans just that rect instead of the whole
     * surface. Loaded at EGL init if EGL_EXT_swap_buffers_with_damage (or the
     * KHR variant) is advertised; NULL means fall back to plain eglSwapBuffers.
     * The terminal path feeds its dirty cell-row band here; other wallpapers
     * damage the full surface. */
    void *swap_with_damage;   /* PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC, NULL if unsupported */
    bool swap_damage_supported;
    bool gl_robust;           /* context created with reset notification */
    bool gpu_health_pending;  /* gpu_health_init waits for a surface (no surfaceless) */

    /* ===== OUTPUTS ===== */
    struct output_state *outputs;
    uint32_t output_count;

    /* ===== CONFIGURATION ===== */
    char config_path[MAX_PATH_LENGTH];

    /* ===== RUNTIME STATE ===== */
    /* ALL flags must be atomic for thread safety */
    atomic_bool_t running;           /* false once the runtime is stopping; read by worker
                                      * threads that must stop with it */
    /* Cycling pause, shader pause, skip/set requests and output bring-up used
     * to be atomics here, polled by the loop. They are messages now, folded
     * into the runtime's model (src/app/app.c), so nothing else can see them. */
    atomic_bool_t mouse_interaction;  /* Global: bind wl_pointer / poll X11 mouse / set cursor.
                                       * Default true. When false, the wallpaper surface receives
                                       * no pointer events and iMouse stays at center. Read by
                                       * the Wayland seat-capabilities handler and X11 backend.
                                       * Atomic so config reload (future) is safe. */
    atomic_bool_t term_raw_input;     /* Global: forward EVERY key (incl. Ctrl-C/D/\\/Z) verbatim
                                       * to a focused terminal wallpaper. Default false, so the
                                       * signal/EOF/suspend control keys are dropped and a stray
                                       * Ctrl-C can't kill the app running as your wallpaper.
                                       * Set term_raw_input true for a genuinely interactive
                                       * shell wallpaper. Read by the keyboard handler. */
    pthread_mutex_t state_mutex;     /* Protects output list and config data */
    pthread_rwlock_t output_list_lock; /* Read-write lock for output linked list traversal */
    pthread_mutex_t state_file_lock; /* Mutex for state file I/O operations */
    
    /* LOCK ORDERING POLICY (to prevent deadlock)
     * ========================================================
     * Always acquire locks in this order:
     * 1. output_list_lock (rwlock)
     * 2. state_mutex
     * 
     * NEVER acquire them in reverse order!
     * 
     * Correct example:
     *   pthread_rwlock_rdlock(&state->output_list_lock);   // 1st
     *   pthread_mutex_lock(&state->state_mutex);           // 2nd - OK
     *   // ... critical section ...
     *   pthread_mutex_unlock(&state->state_mutex);
     *   pthread_rwlock_unlock(&state->output_list_lock);
     *
     * WRONG (will cause deadlock):
     *   pthread_mutex_lock(&state->state_mutex);           // 2nd first
     *   pthread_rwlock_rdlock(&state->output_list_lock);   // 1st second - DEADLOCK!
     *
     * Rationale: output_list_lock is the coarser-grained lock (protects the
     * entire list structure), while state_mutex is fine-grained (protects
     * individual fields). Acquiring coarse-grained locks first prevents
     * deadlock scenarios.
     *========================================================*/
    
    /* ===== SHADER HOT-RELOAD (`neowall watch`) ===== */
    int  watch_fd;                          /* inotify fd, -1 when not watching */
    char watch_path[MAX_PATH_LENGTH];       /* shader being watched ('' = none) */

    /* `neowall preview` / `watch`: a transient instance beside the real
     * daemon. It must not take the pid file or the control socket. */
    bool is_preview;

    /* ===== STATISTICS ===== */
    uint64_t frames_rendered;
    uint64_t errors_count;
};

/* Note: Compositor initialization is now handled via compositor_backend_init()
 * in compositor.h. Wayland/X11 specific code has been moved to their respective backends. */

/* EGL initialization */
bool egl_init(struct neowall_state *state);
void egl_cleanup(struct neowall_state *state);
void detect_gl_capabilities(struct neowall_state *state);

/* The main loop is the runtime: see neowall/app/app.h (app_run, app_post). */

const char *wallpaper_mode_to_string(enum wallpaper_mode mode);
const char *transition_type_to_string(enum transition_type type);

#include "neowall/output/state_file.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/core/util.h"

/* Signal handling */
void signal_handler_init(struct neowall_state *state);
void signal_handler_cleanup(void);

#endif /* NEOWALL_H */
