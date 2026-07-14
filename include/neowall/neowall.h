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
#include "neowall/output/output.h"
#include "neowall/config/config.h"

/* Thread-safe atomic types for flags accessed from multiple threads */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

#define MAX_PATH_LENGTH OUTPUT_MAX_PATH_LENGTH  /* Compatibility alias */
#define MAX_OUTPUTS 16
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

    /* ===== OUTPUTS ===== */
    struct output_state *outputs;
    uint32_t output_count;

    /* ===== CONFIGURATION ===== */
    char config_path[MAX_PATH_LENGTH];

    /* ===== RUNTIME STATE ===== */
    /* ALL flags must be atomic for thread safety */
    atomic_bool_t running;           /* Main loop running flag - accessed from signal handlers */
    atomic_bool_t paused;            /* Pause wallpaper cycling - set by signal handlers */
    atomic_bool_t shader_paused;     /* Freeze shader animation (time uniform) - set by signal
                                      * handlers. Distinct from `paused`: that stops cycling
                                      * between wallpapers, this stops the shader's animation
                                      * and resumes it from the same frame. */
    atomic_bool_t outputs_need_init; /* Flag when new outputs need initialization */
    atomic_int_t next_requested;     /* Counter for skip to next wallpaper requests */
    atomic_int_t set_index_requested; /* Requested wallpaper index (-1 = no request) */
    atomic_bool_t mouse_interaction;  /* Global: bind wl_pointer / poll X11 mouse / set cursor.
                                       * Default true. When false, the wallpaper surface receives
                                       * no pointer events and iMouse stays at center. Read by
                                       * the Wayland seat-capabilities handler and X11 backend.
                                       * Atomic so config reload (future) is safe. */
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
    
    /* ===== EVENT-DRIVEN TIMERS ===== */
    int timer_fd;               /* timerfd for next wallpaper cycle */
    int wakeup_fd;              /* eventfd for waking poll on internal events */
    int signal_fd;              /* signalfd for race-free signal handling */

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

/* Main loop */
void event_loop_run(struct neowall_state *state);
void event_loop_stop(struct neowall_state *state);

/* Utility functions */
uint64_t get_time_ms(void);
uint64_t get_time_us(void);
const char *wallpaper_mode_to_string(enum wallpaper_mode mode);
enum wallpaper_mode wallpaper_mode_from_string(const char *str);
const char *transition_type_to_string(enum transition_type type);
enum transition_type transition_type_from_string(const char *str);

/* Logging */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

void log_error(const char *format, ...);
void log_warn(const char *format, ...);
void log_info(const char *format, ...);
void log_debug(const char *format, ...);
void log_set_level(int level);
float ease_in_out_cubic(float t);

/* Parse a non-negative decimal index (wallpaper index, cycle_total, ...) from a
 * string that came from the user or from disk. Rejects the empty string, any
 * non-digit character, and values that overflow long, none of which atoi() can
 * report. Returns true and writes *out on success. */
bool neowall_parse_index(const char *s, long *out);

/* State file functions */
const char *get_state_file_path(void);
const char *get_cycle_list_file_path(void);

/* Returns a process-private runtime directory for transient IPC/state files.
 * Prefers $XDG_RUNTIME_DIR/neowall, falling back to /tmp/neowall-<uid>.
 * The directory is created 0700 and validated to be a real directory we own
 * (not a symlink, not attacker-controlled) before being returned, closing the
 * symlink/TOCTOU window that a shared /tmp otherwise opens. Returns NULL if a
 * safe directory could not be established. The returned pointer is to a static
 * buffer; do not free it. */
const char *neowall_secure_runtime_dir(void);
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path,
                           const char *mode, int cycle_index, int cycle_total,
                           const char *status);
bool read_wallpaper_state(void);
int restore_cycle_index_from_state(const char *output_name);
bool write_cycle_list(const char *output_name, char **paths, size_t count, size_t current_index);
bool read_cycle_list(void);

/* Signal handling */
void signal_handler_init(struct neowall_state *state);
void signal_handler_cleanup(void);

#endif /* NEOWALL_H */
