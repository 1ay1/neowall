#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <wayland-client.h>
#include <GLES2/gl2.h>

/* Forward declarations */
struct neowall_state;
struct wallpaper_config;
struct image_data;
struct compositor_surface;
struct zxdg_output_v1;

/* Thread-safe atomic types */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

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
    /* width/height represent the current physical buffer in pixels */
    int32_t width;
    int32_t height;
    /* cached logical + mode sizes to handle HiDPI resizes */
    int32_t logical_width;
    int32_t logical_height;
    int32_t pixel_width;
    int32_t pixel_height;
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
    char preload_path[4096];            /* Path of preloaded image */
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
    char current_shader_path[4096];
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
    char pending_shader_path[4096];     /* Next shader to load after fade-out */
    float transition_progress;
    uint64_t frames_rendered;
    bool shader_load_failed;            /* Set to true after 3 failed shader load attempts */
    
    /* FPS measurement */
    uint64_t fps_last_log_time;         /* Last time we logged FPS */
    uint64_t fps_frame_count;           /* Frames rendered since last FPS log */
    float fps_current;                  /* Current measured FPS */

    struct output_state *next;
};

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

#endif /* OUTPUT_H */
