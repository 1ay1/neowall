#ifndef NEOWALL_H
#define NEOWALL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "egl/capability.h"
#include "../src/output/output.h"

/* Thread-safe atomic types for flags accessed from multiple threads */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

#define NEOWALL_VERSION "0.3.0"
#define MAX_PATH_LENGTH OUTPUT_MAX_PATH_LENGTH  /* Compatibility alias */
#define MAX_OUTPUTS 16
#define MAX_WALLPAPERS 256
#define CONFIG_WATCH_INTERVAL 1



/* Forward declarations */
struct neowall_state;
struct compositor_backend;

/* Global application state */
struct neowall_state {
    /* Wayland globals */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zxdg_output_manager_v1 *xdg_output_manager;  /* For getting connector names */

    /* Compositor abstraction backend */
    struct compositor_backend *compositor_backend;

    /* EGL context */
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;
    
    /* OpenGL ES capabilities */
    egl_capabilities_t gl_caps;

    /* Outputs */
    struct output_state *outputs;
    uint32_t output_count;

    /* Configuration */
    char config_path[MAX_PATH_LENGTH];
    time_t config_mtime;        /* Last modification time */
    bool watch_config;          /* Watch for config changes */

    /* Runtime state - ALL flags must be atomic for thread safety */
    atomic_bool_t running;           /* Main loop running flag - accessed from signal handlers */
    atomic_bool_t reload_requested;  /* Config reload request - set by watch thread, read by main */
    atomic_bool_t paused;            /* Pause wallpaper cycling - set by signal handlers */
    atomic_bool_t outputs_need_init; /* Flag when new outputs need initialization */
    atomic_int_t next_requested;     /* Counter for skip to next wallpaper requests */
    pthread_t watch_thread;
    pthread_mutex_t state_mutex;     /* Protects output list and config data */
    pthread_rwlock_t output_list_lock; /* Read-write lock for output linked list traversal */
    pthread_mutex_t state_file_lock; /* Mutex for state file I/O operations */
    
    /* BUG FIX #5: Condition variable for clean config watch thread shutdown */
    pthread_mutex_t watch_mutex;     /* Mutex for watch condition variable */
    pthread_cond_t watch_cond;       /* Condition variable to wake watch thread */
    
    /* BUG FIX #9: LOCK ORDERING POLICY (to prevent deadlock)
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
    
    /* Event-driven timer for wallpaper cycling */
    int timer_fd;               /* timerfd for next wallpaper cycle */
    int wakeup_fd;              /* eventfd for waking poll on internal events */
    int signal_fd;              /* signalfd for race-free signal handling */

    /* Statistics */
    uint64_t frames_rendered;
    uint64_t errors_count;
};

/* Configuration parsing */
bool config_load(struct neowall_state *state, const char *config_path);
bool config_parse_wallpaper(struct wallpaper_config *config, const char *output_name);
void config_free_wallpaper(struct wallpaper_config *config);
const char *config_get_default_path(void);
char **load_images_from_directory(const char *dir_path, size_t *count);
char **load_shaders_from_directory(const char *dir_path, size_t *count);

/* Image loading */
struct image_data *image_load(const char *path, int32_t display_width, int32_t display_height, enum wallpaper_mode mode);
void image_free(struct image_data *img);
void image_free_pixels(struct image_data *img);  /* Free pixel data only (after GPU upload) */
enum image_format image_detect_format(const char *path);

/* Image loaders for specific formats */
struct image_data *image_load_png(const char *path);
struct image_data *image_load_jpeg(const char *path);

/* Wayland/EGL initialization */
bool wayland_init(struct neowall_state *state);
void wayland_cleanup(struct neowall_state *state);
bool egl_init(struct neowall_state *state);
void egl_cleanup(struct neowall_state *state);
void detect_gl_capabilities(struct neowall_state *state);

/* Rendering */
bool render_init_output(struct output_state *output);
void render_cleanup_output(struct output_state *output);
bool render_frame(struct output_state *output);
bool render_frame_shader(struct output_state *output);
bool render_frame_transition(struct output_state *output, float progress);
GLuint render_create_texture(struct image_data *img);
void render_destroy_texture(GLuint texture);
bool render_load_channel_textures(struct output_state *output, struct wallpaper_config *config);
bool render_update_channel_texture(struct output_state *output, size_t channel_index, const char *image_path);

/* GL shader programs */
bool shader_create_program(GLuint *program);
void shader_destroy_program(GLuint program);
const char *get_glsl_version_string(struct neowall_state *state);
char *adapt_shader_for_version(struct neowall_state *state, const char *shader_code, bool is_fragment_shader);
char *adapt_vertex_shader(struct neowall_state *state, const char *shader_code);
char *adapt_fragment_shader(struct neowall_state *state, const char *shader_code);

/* Main loop */
void event_loop_run(struct neowall_state *state);
void event_loop_stop(struct neowall_state *state);

/* Config watching */
void *config_watch_thread(void *arg);
bool config_has_changed(struct neowall_state *state);
void config_reload(struct neowall_state *state);

/* Utility functions */
uint64_t get_time_ms(void);
const char *wallpaper_mode_to_string(enum wallpaper_mode mode);
enum wallpaper_mode wallpaper_mode_from_string(const char *str);
const char *transition_type_to_string(enum transition_type type);
enum transition_type transition_type_from_string(const char *str);

/* Logging */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

void log_error(const char *format, ...);
void log_info(const char *format, ...);
void log_debug(const char *format, ...);
void log_set_level(int level);
float ease_in_out_cubic(float t);

/* State file functions */
const char *get_state_file_path(void);
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path,
                           const char *mode, int cycle_index, int cycle_total,
                           const char *status);
bool read_wallpaper_state(void);
int restore_cycle_index_from_state(const char *output_name);

/* Signal handling */
void signal_handler_init(struct neowall_state *state);
void signal_handler_cleanup(void);

#endif /* NEOWALL_H */
