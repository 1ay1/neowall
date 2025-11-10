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

/* Thread-safe atomic types for flags accessed from multiple threads */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

#define NEOWALL_VERSION "0.3.0"
#define MAX_PATH_LENGTH 4096
#define MAX_OUTPUTS 16
#define MAX_WALLPAPERS 256
#define CONFIG_WATCH_INTERVAL 1



/* Forward declarations */
struct neowall_state;
struct output_state;
struct wallpaper_config;
struct compositor_backend;

/* Wallpaper display modes */
enum wallpaper_mode {
    MODE_CENTER,    /* Center the image without scaling */
    MODE_STRETCH,   /* Stretch to fill entire screen */
    MODE_FIT,       /* Scale to fit inside screen, maintain aspect ratio */
    MODE_FILL,      /* Scale to fill screen, maintain aspect ratio, crop if needed */
    MODE_TILE,      /* Tile the image */
};

/* Image format types */
enum image_format {
    FORMAT_PNG,
    FORMAT_JPEG,
    FORMAT_UNKNOWN,
};

/* Wallpaper transition types */
enum transition_type {
    TRANSITION_NONE,
    TRANSITION_FADE,
    TRANSITION_SLIDE_LEFT,
    TRANSITION_SLIDE_RIGHT,
    TRANSITION_GLITCH,
    TRANSITION_PIXELATE,
};

/* Image data structure */
struct image_data {
    uint8_t *pixels;        /* RGBA pixel data */
    uint32_t width;
    uint32_t height;
    uint32_t channels;      /* Number of channels (3 for RGB, 4 for RGBA) */
    enum image_format format;
    char path[MAX_PATH_LENGTH];
};

/* Wallpaper type */
enum wallpaper_type {
    WALLPAPER_IMAGE,    /* Static image file */
    WALLPAPER_SHADER,   /* Live GLSL shader */
};

/* Wallpaper configuration for a specific output */
struct wallpaper_config {
    enum wallpaper_type type;           /* Wallpaper type (image or shader) */
    char path[MAX_PATH_LENGTH];         /* Path to wallpaper image */
    char shader_path[MAX_PATH_LENGTH];  /* Path to GLSL shader file */
    enum wallpaper_mode mode;           /* Display mode */
    float duration;                     /* Duration in seconds (for cycling) */
    enum transition_type transition;    /* Transition effect */
    float transition_duration;          /* Transition duration in seconds */
    float shader_speed;                 /* Shader animation speed multiplier (default 1.0) */
    int shader_fps;                     /* Target FPS for shader rendering (default 60) */
    bool show_fps;                      /* Show FPS watermark on screen (default false) */
    bool cycle;                         /* Enable wallpaper cycling */
    char **cycle_paths;                 /* Array of paths for cycling */
    size_t cycle_count;                 /* Number of wallpapers to cycle */
    size_t current_cycle_index;         /* Current index in cycle */
    
    /* iChannel texture configuration */
    char **channel_paths;               /* Array of texture paths/names for iChannels */
    size_t channel_count;               /* Number of configured channels */
};

/* Double-buffered config slot for race-free hot-reload */
typedef struct {
    struct wallpaper_config config;
    pthread_mutex_t lock;
    bool valid;  /* Is this slot initialized? */
} config_slot_t;

/* Output (monitor) state */
struct output_state {
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;  /* For getting connector name */
    struct compositor_surface *compositor_surface;  /* Compositor abstraction surface */

    uint32_t name;              /* Wayland output name/ID */
    int32_t width;
    int32_t height;
    int32_t scale;
    int32_t transform;

    char make[64];
    char model[64];
    char connector_name[64];    /* Connector name (e.g., HDMI-A-2, DP-1) from xdg-output */

    bool configured;
    bool needs_redraw;

    struct neowall_state *state;  /* Back-pointer to global state */

    /* Double-buffered config for race-free hot-reload */
    config_slot_t config_slots[2];
    atomic_int_t active_slot;  /* 0 or 1 - which slot is currently active */
    
    /* Convenience pointer that always points to active config (for backward compatibility) */
    struct wallpaper_config *config;
    
    struct image_data *current_image;
    struct image_data *next_image;      /* For transitions */

    GLuint texture;
    GLuint next_texture;                /* For transitions */
    
    /* Double-buffered preload for zero-stall transitions */
    GLuint preload_texture;             /* Next texture to transition to */
    struct image_data *preload_image;   /* Image data for preloaded texture */
    char preload_path[MAX_PATH_LENGTH]; /* Path of preloaded image */
    atomic_bool_t preload_ready;        /* Is preload_texture ready for use? */
    
    /* Background thread for async image loading */
    pthread_t preload_thread;           /* Background preload thread */
    atomic_bool_t preload_thread_active; /* Is background thread running? */
    pthread_mutex_t preload_mutex;      /* Protects preload_image during thread handoff */
    struct image_data *preload_decoded_image; /* Image decoded in background, ready for GPU upload */
    atomic_bool_t preload_upload_pending; /* Background thread finished, main thread should upload */
    
    /* iChannel textures for shader inputs (dynamic count) */
    GLuint *channel_textures;           /* Dynamic array of channel textures */
    size_t channel_count;               /* Number of allocated channels */
    
    GLuint program;
    GLuint glitch_program;              /* Shader program for glitch transition */
    GLuint pixelate_program;            /* Shader program for pixelate transition */
    GLuint live_shader_program;         /* Shader program for live wallpaper */
    char current_shader_path[MAX_PATH_LENGTH];
    GLuint vbo;

    /* Cached uniform locations for performance */
    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
        GLint u_resolution;
        GLint u_time;
        GLint u_speed;
        GLint *iChannel;    /* Dynamic array of iChannel sampler locations */
    } shader_uniforms;

    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
    } program_uniforms;

    struct {
        GLint position;
        GLint texcoord;
        GLint tex0;
        GLint tex1;
        GLint progress;
        GLint resolution;
    } transition_uniforms;

    /* GL state cache to avoid redundant calls */
    struct {
        GLuint bound_texture;
        GLuint active_program;
        bool blend_enabled;
    } gl_state;

    uint64_t last_frame_time;
    uint64_t last_cycle_time;           /* Last time wallpaper was changed/cycled */
    uint64_t transition_start_time;
    uint64_t shader_start_time;         /* Time when shader clock last restarted */
    uint64_t shader_time_accum_ms;      /* Accumulated time preserved across reloads */
    uint64_t shader_fade_start_time;    /* Time when shader fade started (for cross-fade) */
    char pending_shader_path[MAX_PATH_LENGTH]; /* Next shader to load after fade-out */
    float transition_progress;
    uint64_t frames_rendered;
    bool shader_load_failed;            /* Set to true after 3 failed shader load attempts */
    
    /* FPS measurement */
    uint64_t fps_last_log_time;         /* Last time we logged FPS */
    uint64_t fps_frame_count;           /* Frames rendered since last FPS log */
    float fps_current;                  /* Current measured FPS */

    struct output_state *next;
};

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

/* Output management */
struct output_state *output_create(struct neowall_state *state,
                                   struct wl_output *output, uint32_t name);
void output_destroy(struct output_state *output);
bool output_configure_compositor_surface(struct output_state *output);
bool output_create_egl_surface(struct output_state *output);
void output_set_wallpaper(struct output_state *output, const char *path);
void output_set_shader(struct output_state *output, const char *shader_path);
bool output_apply_config(struct output_state *output, struct wallpaper_config *config);
void output_apply_deferred_config(struct output_state *output);
void output_cycle_wallpaper(struct output_state *output);
bool output_should_cycle(struct output_state *output, uint64_t current_time);
void output_preload_next_wallpaper(struct output_state *output);

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