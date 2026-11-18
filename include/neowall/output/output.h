#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <GL/gl.h>
#include "neowall/core/constants.h"
#include "neowall/config/wallpaper.h"
#include "neowall/core/result.h" /* For nw_result */
#include "neowall/image/image.h" /* For struct image_data and enum image_format */
#include "neowall/shader/wallshader.h"
#include "neowall/shader/shader_clock.h" /* For shader_clock_tracker */
#include "neowall/output/frame_clock.h"
#include "neowall/output/span.h" /* For struct span_view */

/* Constants */

/* Forward declarations for external types */
struct neowall_state;
struct compositor_surface;


/* Output (monitor) state */
struct output_state {
    void
        *native_output; /* Platform-specific output handle (wl_output* for Wayland, NULL for X11) */
    void *xdg_output;   /* Extended output info (zxdg_output_v1* for Wayland) */
    struct compositor_surface *compositor_surface; /* Compositor abstraction surface */

    uint32_t name; /* Output name/ID */
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
    char span_synced_path[NW_PATH_MAX];

    char make[64];
    char model[64];
    char connector_name[64]; /* Connector name (e.g., HDMI-A-2, DP-1) from xdg-output */

    bool configured;
    atomic_bool needs_redraw; /* Atomic: written from main loop, occlusion callbacks, render */
    atomic_bool occluded;     /* Output is fully occluded by a fullscreen window */

    /* Lifetime reference count. Starts at 1 (the output-list's reference).
     * The render loop takes a transient ref on each output it snapshots so a
     * concurrent hotplug-removal (which unlinks under the write lock and then
     * unrefs) cannot free the output while GL work runs with the list lock
     * dropped. The object is freed only when this hits 0. See output_ref /
     * output_unref. */
    atomic_int refcount;

    struct neowall_state *state; /* Back-pointer to global state */

    /* Configuration for this output */
    struct wallpaper_config *config;

    struct image_data *current_image;
    struct image_data *next_image; /* For transitions */

    GLuint texture;
    GLuint next_texture; /* For transitions */

    /* Double-buffered preload for zero-stall transitions */
    GLuint preload_texture;           /* Next texture to transition to */
    struct image_data *preload_image; /* Image data for preloaded texture */
    char preload_path[NW_PATH_MAX];   /* Path of preloaded image */
    atomic_bool preload_ready;        /* Is preload_texture ready for use? */

    /* Background thread for async image loading */
    pthread_t preload_thread;          /* Background preload thread */
    atomic_bool preload_thread_active; /* Is background thread running? */
    atomic_bool
        preload_thread_join_pending; /* A completed/running joinable thread must be joined */
    atomic_bool preload_should_stop; /* Cooperative cancellation flag for preload thread */
    atomic_uint_fast64_t
        preprocessing_generation; /* Invalidates geometry/config-dependent images */
    atomic_bool
        geometry_change_pending; /* Callback-safe request; GL work is deferred to event loop */
    bool terminal_auto_cols;
    bool terminal_auto_rows;
    uint64_t preload_generation;
    int32_t preload_width;
    int32_t preload_height;
    enum wallpaper_mode preload_mode;
    pthread_mutex_t preload_mutex; /* Protects preload_image during thread handoff */
    struct image_data
        *preload_decoded_image;         /* Image decoded in background, ready for GPU upload */
    atomic_bool preload_upload_pending; /* Background thread finished, main thread should upload */

    /* the config's `channels` as iChannel0..3 (0 = the shader's own binding) */
    GLuint channel_tex[4];
    int channel_w[4], channel_h[4];

    GLuint program;
    GLuint glitch_program;   /* Shader program for glitch transition */
    GLuint pixelate_program; /* Shader program for pixelate transition */
    wallshader *shader;      /* the running shader (shader and terminal wallpapers) */
    /* a shader compiling in the background to replace it (cycling, reload) */
    struct wallshader_pending *pending_shader;
    char pending_shader_path[NW_PATH_MAX];
    size_t pending_failures; /* broken cycle entries skipped in a row */
    /* A reload whose shader is still compiling: the whole config waits here
     * and goes live with the shader. NULL when none. stage_failed records a
     * staged build that failed (or was replaced), for the reload's reply. */
    struct wallpaper_config *staged_config;
    bool stage_failed;
    GLuint vao; /* Vertex array object (required for OpenGL 3.3 Core) */
    GLuint vbo;

    /* Cached uniform locations for performance */
    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
        GLint u_resolution;
        GLint u_time;
        GLint u_speed;
    } shader_uniforms;

    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
        GLint alpha;
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
    uint64_t last_cycle_time; /* Last time wallpaper was changed/cycled */
    uint64_t transition_start_time;
    uint64_t shader_start_time;         /* Time when shader was loaded (for animation) */
    shader_clock_ramp shader_ramp;      /* pause/resume speed ramp; zero = real time.
                                         * Main-thread only. */
    bool shader_pause_final_draw;       /* the pending buffer holds the frozen frame */
    uint64_t shader_hidden_since;       /* Wall-clock ms when this output stopped being
                                         * visible (occluded/paused), 0 while visible. Marks
                                         * a window in which the shader clock can be rebased
                                         * without anyone seeing the discontinuity — iTime is
                                         * a float32 and loses frame resolution after ~a day
                                         * of uptime. Main-thread only. */
    shader_clock_tracker clock_tracker; /* Watches iTime for backward steps so a
                                         * visible "jumps back in time" glitch is
                                         * reported with its cause. Main-thread only. */
    GLuint clock_fade_tex;              /* Last frame before a clock reset or shader swap */
    uint64_t clock_fade_start;          /* ms; >0 while it fades out over the new clock */
    bool clock_fade_rebased;            /* Tell the tracker the next jump is a rebase */
    bool swap_fade_pending;             /* a shader swap's fade starts on the next frame */
    double last_shader_time;            /* iTime of the last drawn frame */
    float transition_progress;
    uint64_t frames_rendered;
    bool shader_load_failed;   /* Set to true after 3 failed shader load attempts */
    uint32_t swap_fail_streak; /* consecutive failed swaps (gpu_health) */

    /* FPS measurement */
    uint64_t fps_last_log_time; /* Last time we logged FPS */
    uint64_t fps_last_frame;    /* ms, the previous frame (for the worst gap) */
    uint64_t fps_worst_gap;     /* longest ms between frames this window */
    uint64_t fps_frame_count;   /* Frames rendered since last FPS log */
    float fps_current;          /* Current measured FPS */

    /* Mouse tracking (for shader iMouse uniform) */
    float mouse_x; /* Mouse X position in pixels (or -1 for center) */
    float mouse_y; /* Mouse Y position in pixels (or -1 for center) */

    /* High-precision frame pacing for vsync-off mode */
    int frame_timer_fd; /* timerfd for precise frame timing when vsync is disabled */

    /* Phase-locked pacer state (vsync-off animated wallpapers). The frame timer
     * is armed as a ONE-SHOT ABSOLUTE deadline instead of a free-running
     * recurring interval: each cycle we advance the deadline by exactly one
     * period and re-arm to that absolute CLOCK_MONOTONIC time. This anchors the
     * schedule to real present-completion times so it can't drift out of phase
     * with the display's vblank cadence (the source of periodic judder that a
     * fixed-interval timer produces). If a frame overruns its slot the deadline
     * is snapped forward (drop, don't pile up catch-up frames). */
    uint64_t pace_period_ns;        /* target frame period; 0 = pacer inactive */
    uint64_t pace_next_deadline_ns; /* absolute CLOCK_MONOTONIC target for next present */

    /* Real presentation-time feedback (wp_presentation, Wayland only). When the
     * compositor reports an actual present timestamp + hardware refresh period
     * these carry the ground truth the pacer phase-locks to; 0 = none yet. */
    uint64_t pace_last_present_ns; /* last real present time (pacer clock) */
    uint64_t pace_hw_refresh_ns;   /* measured display refresh period, 0 = unknown */
    nw_frame_clock frame_clock;    /* stamps iTime with the vblank it shows at */
    uint64_t frame_target_ns;      /* stamp of the frame being drawn, 0 = wall clock */
    int frame_latency_used;        /* latency the stamp assumed (fed back on present) */

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

/* A new output with default config and refcount 1, not in any list yet. */
struct output_state *output_new(struct neowall_state *state, void *native_output, uint32_t name);
void output_destroy(struct output_state *output);

/* The output list (state->outputs). Each takes output_list_lock as writer;
 * don't call them holding it. The list owns one reference per output: add
 * gives it the new output's, take/take_name hand it back to the caller, who
 * output_unref()s it when done. clear hands every output to `drop`. */
void output_list_add(struct neowall_state *state, struct output_state *output);
bool output_list_take(struct neowall_state *state, struct output_state *output);
struct output_state *output_list_take_name(struct neowall_state *state, uint32_t name);
void output_list_clear(struct neowall_state *state, void (*drop)(struct output_state *));

/* Reference counting: take a ref before using an output across a window
 * where the list lock isn't held; output_unref() when done. At zero the
 * output is destroyed. A render snapshot holds refs, so hotplug can't free
 * an output mid-frame. */
void output_ref(struct output_state *output);
void output_unref(struct output_state *output);
bool output_configure_compositor_surface(struct output_state *output);
bool output_create_egl_surface(struct output_state *output);
/* Make the shared context current on this output's surface. */
bool output_make_current(struct output_state *output);
/* Release GL objects shared by all outputs. The context must be current. */
void output_release_shared_gl(void);
void output_set_wallpaper(struct output_state *output, const char *path);
/* Notify the output core that its physical render dimensions changed. Safe in
 * compositor callbacks: only atomics are touched; image/GL replacement occurs
 * later from output_process_geometry_change() on the render thread. */
void output_notify_geometry_change(struct output_state *output);
void output_process_geometry_change(struct output_state *output);
/* Load `shader_path` and make it this output's live wallpaper.
 *
 * Returns NW_OK once the shader is running, or when the load was deferred
 * because the EGL surface is not up yet (the path is stored on the config and
 * applied when the surface arrives). On any error the previous shader is
 * still on screen and config->shader_path still names it. */
nw_result output_set_shader(struct output_state *output, const char *shader_path);
/* The same without stalling: the current shader keeps drawing while the
 * new one compiles; output_poll_shader_build swaps it in. */
nw_result output_set_shader_async(struct output_state *output, const char *shader_path);
/* Finish a background shader build if it's done. true while one is going.
 * The output's context must be current. */
bool output_poll_shader_build(struct output_state *output);

/* A reload that changes one shader for another stages the new config and
 * builds the shader in the background (output_apply_config returns true at
 * once). These tell whether it's still waiting, and whether a staged build
 * failed (the old config and shader are still live then). forget clears
 * the failure once it's been reported. */
bool output_reload_pending(const struct output_state *output);
bool output_reload_failed(const struct output_state *output);
void output_reload_forget(struct output_state *output);

/* Spawn `cmd` under a PTY and make the live terminal this output's wallpaper.
 * `shader_path` (may be NULL/"") is an optional GLSL file that samples the
 * terminal via nwTerm() for CRT/glow styling; NULL uses a built-in crisp
 * pass-through. cols/rows may be 0 to derive a grid from the output size. */
nw_result output_set_terminal(struct output_state *output, const char *cmd, const char *shader_path,
                              const char *font_path, int cols, int rows);
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

/* Rendering wrappers - hide render module from the runtime */
bool output_render_frame(struct output_state *output);

/* Forward a pointer event to this output's terminal wallpaper (pixel coords
 * relative to the output top-left). No-op unless the output runs a terminal. */
bool output_terminal_mouse(struct output_state *output, int px, int py, int button, bool pressed,
                           bool motion);

/* Forward already-encoded key bytes to this output's terminal wallpaper child.
 * No-op unless the output runs a terminal. */
bool output_terminal_key(struct output_state *output, const void *bytes, size_t len);
GLuint output_upload_preload_texture(struct output_state *output);
void output_cleanup_transition(struct output_state *output);
bool output_init_render(struct output_state *output);

/* Frame timing */
int output_get_frame_timer_fd(struct output_state *output);
/* Drawn on the compositor's frame callbacks (vsync, Wayland, no shader_fps
 * cap) rather than on a timer. */
bool output_frame_paced(const struct output_state *output);

/* Phase-locked frame pacer. Call once per output immediately after its buffer
 * swap, passing a CLOCK_MONOTONIC timestamp (ns) captured just after the swap.
 * Re-arms the frame timer as a one-shot absolute deadline exactly one period
 * ahead, keeping the schedule phase-anchored to real present times so it can't
 * drift against the display's vblank cadence. No-op / false when the pacer is
 * inactive (vsync on, static wallpaper, or no frame timer). */
bool output_pace_advance(struct output_state *output, uint64_t now_ns);

/* Feed a real present event (from wp_presentation feedback, Wayland only) into
 * the pacer: `present_ns` = compositor-reported present time in the
 * presentation clock (ns), `refresh_ns` = display refresh period (ns, 0 if
 * unknown). The next output_pace_advance() phase-locks to this instead of its
 * own predicted deadline and quantises the period to whole refreshes. */
void output_pace_note_present(struct output_state *output, uint64_t present_ns,
                              uint64_t refresh_ns);

#endif /* OUTPUT_H */
