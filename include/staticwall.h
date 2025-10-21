/*
 * Staticwall - A reliable Wayland wallpaper daemon
 * Copyright (C) 2024
 *
 * Main header file with core structures and definitions
 */

#ifndef STATICWALL_H
#define STATICWALL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define STATICWALL_VERSION "0.1.0"
#define MAX_PATH_LENGTH 4096
#define MAX_OUTPUTS 16
#define MAX_WALLPAPERS 256
#define CONFIG_WATCH_INTERVAL 2

/* Forward declarations */
struct staticwall_state;
struct output_state;
struct wallpaper_config;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

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

/* Wallpaper configuration for a specific output */
struct wallpaper_config {
    char path[MAX_PATH_LENGTH];         /* Path to wallpaper image */
    enum wallpaper_mode mode;           /* Display mode */
    uint32_t duration;                  /* Duration in seconds (for cycling) */
    enum transition_type transition;    /* Transition effect */
    uint32_t transition_duration;       /* Transition duration in ms */
    bool cycle;                         /* Enable wallpaper cycling */
    char **cycle_paths;                 /* Array of paths for cycling */
    size_t cycle_count;                 /* Number of wallpapers to cycle */
    size_t current_cycle_index;         /* Current index in cycle */
};

/* Output (monitor) state */
struct output_state {
    struct wl_output *output;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_surface *surface;
    struct wl_egl_window *egl_window;
    EGLSurface egl_surface;

    uint32_t name;              /* Wayland output name/ID */
    int32_t width;
    int32_t height;
    int32_t scale;
    int32_t transform;

    char make[64];
    char model[64];

    bool configured;
    bool needs_redraw;

    struct staticwall_state *state;  /* Back-pointer to global state */

    struct wallpaper_config config;
    struct image_data *current_image;
    struct image_data *next_image;      /* For transitions */

    GLuint texture;
    GLuint next_texture;                /* For transitions */
    GLuint program;
    GLuint vbo;

    uint64_t last_frame_time;
    uint64_t last_cycle_time;           /* Last time wallpaper was changed/cycled */
    uint64_t transition_start_time;
    float transition_progress;
    uint64_t frames_rendered;

    struct output_state *next;
};

/* Global application state */
struct staticwall_state {
    /* Wayland globals */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    /* EGL context */
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;

    /* Outputs */
    struct output_state *outputs;
    uint32_t output_count;

    /* Configuration */
    char config_path[MAX_PATH_LENGTH];
    time_t config_mtime;        /* Last modification time */
    bool watch_config;          /* Watch for config changes */

    /* Runtime state */
    bool running;
    bool reload_requested;
    pthread_t watch_thread;
    pthread_mutex_t state_mutex;

    /* Statistics */
    uint64_t frames_rendered;
    uint64_t errors_count;
};

/* Configuration parsing */
bool config_load(struct staticwall_state *state, const char *config_path);
bool config_parse_wallpaper(struct wallpaper_config *config, const char *output_name);
void config_free_wallpaper(struct wallpaper_config *config);
const char *config_get_default_path(void);
char **load_images_from_directory(const char *dir_path, size_t *count);

/* Image loading */
struct image_data *image_load(const char *path);
void image_free(struct image_data *img);
enum image_format image_detect_format(const char *path);

/* Image loaders for specific formats */
struct image_data *image_load_png(const char *path);
struct image_data *image_load_jpeg(const char *path);

/* Wayland/EGL initialization */
bool wayland_init(struct staticwall_state *state);
void wayland_cleanup(struct staticwall_state *state);
bool egl_init(struct staticwall_state *state);
void egl_cleanup(struct staticwall_state *state);

/* Output management */
struct output_state *output_create(struct staticwall_state *state,
                                   struct wl_output *output, uint32_t name);
void output_destroy(struct output_state *output);
bool output_configure_layer_surface(struct output_state *output);
bool output_create_egl_surface(struct output_state *output);
void output_set_wallpaper(struct output_state *output, const char *path);
bool output_apply_config(struct output_state *output, struct wallpaper_config *config);
void output_cycle_wallpaper(struct output_state *output);
bool output_should_cycle(struct output_state *output, uint64_t current_time);

/* Rendering */
bool render_init_output(struct output_state *output);
void render_cleanup_output(struct output_state *output);
bool render_frame(struct output_state *output);
bool render_frame_transition(struct output_state *output, float progress);
GLuint render_create_texture(struct image_data *img);
void render_destroy_texture(GLuint texture);

/* GL shader programs */
bool shader_create_program(GLuint *program);
void shader_destroy_program(GLuint program);

/* Main loop */
void event_loop_run(struct staticwall_state *state);
void event_loop_stop(struct staticwall_state *state);

/* Config watching */
void *config_watch_thread(void *arg);
bool config_has_changed(struct staticwall_state *state);
void config_reload(struct staticwall_state *state);

/* Utility functions */
uint64_t get_time_ms(void);
const char *wallpaper_mode_to_string(enum wallpaper_mode mode);
enum wallpaper_mode wallpaper_mode_from_string(const char *str);

/* Logging */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

void log_error(const char *format, ...);
void log_info(const char *format, ...);
void log_debug(const char *format, ...);
void log_set_level(int level);
float ease_in_out_cubic(float t);

/* Signal handling */
void signal_handler_init(struct staticwall_state *state);
void signal_handler_cleanup(void);

#endif /* STATICWALL_H */