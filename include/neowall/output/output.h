#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <GL/gl.h>
#include "neowall/result.h"        /* For nw_result */
#include "neowall/image/image.h"   /* For struct image_data and enum image_format */
#include "neowall/shader/shader_multipass.h"  /* For multipass_shader_t */
#include "neowall/output/span.h"   /* For struct span_view */

/* Constants */
#define OUTPUT_MAX_PATH_LENGTH 4096

/* Forward declarations for external types */
struct neowall_state;
struct compositor_surface;

/* Thread-safe atomic types */
typedef atomic_bool atomic_bool_t;
typedef atomic_int atomic_int_t;

/* Wallpaper display modes */
enum wallpaper_mode {
    MODE_CENTER,    /* Center the image without scaling */
    MODE_STRETCH,   /* Stretch to fill entire screen */
    MODE_FIT,       /* Scale to fit inside screen, maintain aspect ratio */
    MODE_FILL,      /* Scale to fill screen, maintain aspect ratio, crop if needed */
    MODE_TILE,      /* Tile the image */
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

/* Wallpaper type */
enum wallpaper_type {
    WALLPAPER_IMAGE,    /* Static image file */
    WALLPAPER_SHADER,   /* Live GLSL shader */
    WALLPAPER_TERMINAL, /* Live terminal (a program rendered as the wallpaper) */
};

/* Wallpaper types that drive the multipass GLSL pipeline and need continuous
 * frame scheduling (frame timer / repeated needs_redraw). Both GLSL shaders and
 * terminal wallpapers render through render_frame_shader(); a static image does
 * not. Kept as one predicate so every animation gate in the render loop and the
 * event loop stays in agreement. */
static inline bool wallpaper_is_animated(enum wallpaper_type t) {
    return t == WALLPAPER_SHADER || t == WALLPAPER_TERMINAL;
}

/* Wallpaper configuration for a specific output */
struct wallpaper_config {
    enum wallpaper_type type;           /* Wallpaper type (image or shader) */
    char path[OUTPUT_MAX_PATH_LENGTH];  /* Path to wallpaper image */
    char shader_path[OUTPUT_MAX_PATH_LENGTH];  /* Path to GLSL shader file */
    enum wallpaper_mode mode;           /* Display mode */
    float duration;                     /* Duration in seconds (for cycling) */
    enum transition_type transition;    /* Transition effect */
    float transition_duration;          /* Transition duration in seconds */
    float shader_speed;                 /* Shader animation speed multiplier (default 1.0) */
    int shader_fps;                     /* Target FPS for shader rendering (default 60) */
    bool vsync;                         /* Enable vsync (sync to monitor refresh, ignores shader_fps) */
    bool show_fps;                      /* Show FPS watermark on screen (default false) */
    bool pause_on_fullscreen;           /* Pause rendering when output is occluded by fullscreen window */
    float pause_coverage_threshold;     /* Fraction (0.0-1.0) of wallpaper region that must be covered by tiled windows to count as occluded. Default 0.8 */
    bool cycle;                         /* Enable wallpaper cycling */
    bool shuffle;                       /* Randomise cycle order on load + on every wrap (issue #47).
                                         * Applies to both image and shader directory cycles. */
    char **cycle_paths;                 /* Array of paths for cycling */
    size_t cycle_count;                 /* Number of wallpapers to cycle */
    size_t current_cycle_index;         /* Current index in cycle */
    
    /* iChannel texture configuration */
    char **channel_paths;               /* Array of texture paths/names for iChannels */
    size_t channel_count;               /* Number of configured channels */

    /* Terminal wallpaper (WALLPAPER_TERMINAL). The program named by term_cmd is
     * spawned under a PTY and rendered as the background; term_shader (optional)
     * is a GLSL file that samples it via nwTerm() for CRT/glow styling — empty
     * means the built-in crisp pass-through. */
    char term_cmd[OUTPUT_MAX_PATH_LENGTH];   /* command to run, e.g. "htop" */
    char term_font[OUTPUT_MAX_PATH_LENGTH];  /* font file path ('' = system search) */
    int  term_cols;                          /* grid width  (0 = auto from output) */
    int  term_rows;                          /* grid height (0 = auto from output) */
};

/* Output (monitor) state */
struct output_state {
    void *native_output;                /* Platform-specific output handle (wl_output* for Wayland, NULL for X11) */
    void *xdg_output;                   /* Extended output info (zxdg_output_v1* for Wayland) */
    struct compositor_surface *compositor_surface;  /* Compositor abstraction surface */

    uint32_t name;              /* Output name/ID */
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
    /* Exact fractional scale in 120ths, from wp_fractional_scale_v1's
     * preferred_scale event (e.g. 180 == 1.5x), or 0 if the compositor lacks
     * fractional-scale support or has not sent it yet. When set, the buffer is
     * sized to round(logical * fractional_scale_120 / 120) and a wp_viewport
     * sets the logical destination, so the output renders at its TRUE density
     * instead of the integer-rounded wl_output.scale. */
    int32_t fractional_scale_120;

    /* Position of this output's top-left within the global screen layout.
     * Set by the X11 backend from the RandR monitor geometry (device pixels) so
     * the wallpaper window lands on the right monitor and pointer coordinates
     * can be made output-relative, and by the Wayland backend from xdg-output's
     * logical position, which is in LOGICAL pixels and so agrees with the fields
     * above only where scale is 1. Also the origin of this output's slice of a
     * spanned wallpaper (see span.h). */
    int32_t x_offset;
    int32_t y_offset;
    /* Exact logical size from xdg-output's logical_size, or 0 until it arrives
     * (or if the compositor lacks zxdg_output_manager_v1). The span seam prefers
     * this over width/scale: dividing the device size by an integer scale is
     * exact only when the compositor rounded the logical size to a whole pixel,
     * which a fractional-scale layout does not, so the reconstructed size drifts
     * a pixel from the neighbour's and the shared box seams. This is the value
     * the compositor actually laid the output out with. */
    int32_t xdg_logical_width;
    int32_t xdg_logical_height;

    /* This output's slice of the wallpaper it shares with its span group: the
     * outputs whose config names the same wallpaper (or the same cycle list).
     * The group renders ONE continuous scene rather than a copy each, so the
     * shader is handed the group's bounding box as iResolution and this slice's
     * origin as a gl_FragCoord offset. Refreshed each main-loop pass from the
     * output snapshot (outputs_update_spans); read by the render path.
     *
     * `spanned` is false whenever the group is just this output, in which case
     * `span` is not read at all and rendering is exactly as it was. */
    struct span_view span;
    bool spanned;

    /* shader_start_time shared by the whole span group, so both halves of a
     * spanned scene animate on the same clock. 0 until the group has a shader. */
    uint64_t span_start_time;

    /* Last shader this output adopted to match its span group. Slicing a scene
     * across monitors is meaningless unless they run the SAME shader, and the
     * per-output cycle indices can start out disagreeing (a restored index, a
     * hotplug mid-cycle). Recording the path we last converged on stops a
     * shader that fails to compile here from being retried every frame. */
    char span_synced_path[OUTPUT_MAX_PATH_LENGTH];

    char make[64];
    char model[64];
    char connector_name[64];    /* Connector name (e.g., HDMI-A-2, DP-1) from xdg-output */

    bool configured;
    atomic_bool_t needs_redraw;         /* Atomic: written from main loop, occlusion callbacks, render */
    atomic_bool_t occluded;             /* Output is fully occluded by a fullscreen window */

    /* Lifetime reference count. Starts at 1 (the output-list's reference).
     * The render loop takes a transient ref on each output it snapshots so a
     * concurrent hotplug-removal (which unlinks under the write lock and then
     * unrefs) cannot free the output while GL work runs with the list lock
     * dropped. The object is freed only when this hits 0. See output_ref /
     * output_unref. */
    atomic_int refcount;

    struct neowall_state *state;  /* Back-pointer to global state */

    /* Configuration for this output */
    struct wallpaper_config *config;
    
    struct image_data *current_image;
    struct image_data *next_image;      /* For transitions */

    GLuint texture;
    GLuint next_texture;                /* For transitions */
    
    /* Double-buffered preload for zero-stall transitions */
    GLuint preload_texture;             /* Next texture to transition to */
    struct image_data *preload_image;   /* Image data for preloaded texture */
    char preload_path[OUTPUT_MAX_PATH_LENGTH];  /* Path of preloaded image */
    atomic_bool_t preload_ready;        /* Is preload_texture ready for use? */
    
    /* Background thread for async image loading */
    pthread_t preload_thread;           /* Background preload thread */
    atomic_bool_t preload_thread_active; /* Is background thread running? */
    atomic_bool_t preload_should_stop;   /* Cooperative cancellation flag for preload thread */
    pthread_mutex_t preload_mutex;      /* Protects preload_image during thread handoff */
    struct image_data *preload_decoded_image; /* Image decoded in background, ready for GPU upload */
    atomic_bool_t preload_upload_pending; /* Background thread finished, main thread should upload */
    
    /* iChannel textures for shader inputs (dynamic count) */
    GLuint *channel_textures;           /* Dynamic array of channel textures */
    size_t channel_count;               /* Number of allocated channels */
    
    GLuint program;
    GLuint glitch_program;              /* Shader program for glitch transition */
    GLuint pixelate_program;            /* Shader program for pixelate transition */
    GLuint live_shader_program;         /* Shader program for live wallpaper (legacy, kept for compatibility) */
    multipass_shader_t *multipass_shader; /* Multipass shader for live wallpaper (new) */
    GLuint vao;                         /* Vertex array object (required for OpenGL 3.3 Core) */
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
    uint64_t shader_start_time;         /* Time when shader was loaded (for animation) */
    uint64_t shader_paused_at;          /* Wall-clock ms when shader animation was frozen,
                                         * 0 when running. On resume, shader_start_time is
                                         * advanced by (now - shader_paused_at) so animation
                                         * continues from the same frame. Main-thread only. */
    uint64_t shader_fade_start_time;    /* Time when shader fade started (for cross-fade) */
    char pending_shader_path[OUTPUT_MAX_PATH_LENGTH];  /* Next shader to load after fade-out */
    float transition_progress;
    uint64_t frames_rendered;
    bool shader_load_failed;            /* Set to true after 3 failed shader load attempts */
    
    /* FPS measurement */
    uint64_t fps_last_log_time;         /* Last time we logged FPS */
    uint64_t fps_frame_count;           /* Frames rendered since last FPS log */
    float fps_current;                  /* Current measured FPS */
    
    /* Mouse tracking (for shader iMouse uniform) */
    float mouse_x;                      /* Mouse X position in pixels (or -1 for center) */
    float mouse_y;                      /* Mouse Y position in pixels (or -1 for center) */
    
    /* High-precision frame pacing for vsync-off mode */
    int frame_timer_fd;                 /* timerfd for precise frame timing when vsync is disabled */

    struct output_state *next;
};

/* Device pixels per logical pixel, never zero. An output carries scale 0 until
 * the compositor has told it otherwise, and X11 outputs carry 1 forever; both
 * mean device == logical. Divide a physical width by this to get the logical
 * width the compositor lays the output out with, multiply to go back. */
static inline int32_t output_normalized_scale(const struct output_state *output) {
    if (!output || output->scale <= 0) {
        return 1;
    }
    return output->scale;
}

/* Output management */
struct output_state *output_create(struct neowall_state *state,
                                   void *native_output, uint32_t name);
void output_destroy(struct output_state *output);

/* Reference counting for safe concurrent lifetime management.
 *
 * output_create() returns an output with refcount 1 (the list's reference).
 * Take a ref before using an output across a window where the list lock is not
 * held; drop it with output_unref() when done. When the count reaches zero the
 * output is torn down via output_destroy() and freed.
 *
 * Removal from the output list is: unlink under the write lock, then
 * output_unref() the list's reference. Any outstanding refs (e.g. a render
 * snapshot) keep the object alive until they too are dropped. */
void output_ref(struct output_state *output);
void output_unref(struct output_state *output);
bool output_configure_compositor_surface(struct output_state *output);
bool output_create_egl_surface(struct output_state *output);
void output_set_wallpaper(struct output_state *output, const char *path);
/* Load `shader_path` and make it this output's live wallpaper.
 *
 * Returns NW_OK once the shader is running, or when the load was deferred
 * because the EGL surface is not up yet (the path is stored on the config and
 * applied when the surface arrives). Any error status means no shader is bound
 * on this output: the previous program has already been torn down and
 * config->shader_path still names it. */
nw_result output_set_shader(struct output_state *output, const char *shader_path);

/* Spawn `cmd` under a PTY and make the live terminal this output's wallpaper.
 * `shader_path` (may be NULL/"") is an optional GLSL file that samples the
 * terminal via nwTerm() for CRT/glow styling; NULL uses a built-in crisp
 * pass-through. cols/rows may be 0 to derive a grid from the output size. */
nw_result output_set_terminal(struct output_state *output, const char *cmd,
                              const char *shader_path, const char *font_path,
                              int cols, int rows);
bool output_apply_config(struct output_state *output, struct wallpaper_config *config);
void output_apply_deferred_config(struct output_state *output);
void output_cycle_wallpaper(struct output_state *output);
void output_set_cycle_index(struct output_state *output, size_t index);
bool output_should_cycle(struct output_state *output, uint64_t current_time);
void output_preload_next_wallpaper(struct output_state *output);

/* True if `a` and `b` draw the same wallpaper and so form one spanned scene:
 * same type, and either the same cycle list or, when not cycling, the same
 * source path. Membership deliberately ignores where in the cycle each output
 * currently is — two outputs on one directory belong together even if a
 * restored index left them showing different entries; outputs_sync_span_group()
 * is what brings them back together. */
bool output_same_span_group(const struct output_state *a, const struct output_state *b);

/* Recompute every output's slice of its span group from the current layout.
 * Call from the main loop with the output snapshot; sets output->span,
 * output->spanned and output->span_start_time. */
void outputs_update_spans(struct output_state **outs, size_t count);

/* Bring `output` onto the same shader as the rest of its span group, if it has
 * drifted. No-op for an output that is alone in its group or already in step. */
void outputs_sync_span_group(struct output_state **outs, size_t count, size_t index);

/* Advance `leader` to the next wallpaper and take its whole span group with it,
 * so a spanned scene never ends up half one shader and half another. Members are
 * pushed the leader's resulting wallpaper rather than cycling themselves, which
 * would re-shuffle and re-skip broken entries independently and drift apart.
 * Returns how many outputs were cycled, the leader included. */
size_t output_cycle_group(struct output_state **outs, size_t count, struct output_state *leader);

/* Rendering wrappers - hide render module from eventloop */
bool output_render_frame(struct output_state *output);

/* Forward a pointer event to this output's terminal wallpaper (pixel coords
 * relative to the output top-left). No-op unless the output runs a terminal. */
bool output_terminal_mouse(struct output_state *output, int px, int py,
                           int button, bool pressed, bool motion);
GLuint output_upload_preload_texture(struct output_state *output);
void output_cleanup_transition(struct output_state *output);
bool output_init_render(struct output_state *output);
void output_destroy_texture(GLuint texture);

/* Frame timing */
int output_get_frame_timer_fd(struct output_state *output);

#endif /* OUTPUT_H */
