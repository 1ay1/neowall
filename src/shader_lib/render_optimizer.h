/* Render Optimizer - High-Performance Multipass Rendering Optimizations
 * 
 * This module provides aggressive GPU-side optimizations for multipass shaders:
 * 
 * 1. GPU State Manager - Tracks and caches OpenGL state to eliminate redundant calls
 * 2. Uniform Cache - Avoids setting unchanged uniform values
 * 3. Per-Buffer Smart Resolution - Analyzes shader content to right-size buffers
 * 4. Pass Culling - Skips passes that don't contribute to output
 * 5. Temporal Reuse - Detects static frames and reuses previous results
 * 6. Texture Binding Optimization - Minimizes texture unit switches
 * 7. Draw Call Batching - Consolidates operations where possible
 * 
 * Usage:
 *   render_optimizer_t opt;
 *   render_optimizer_init(&opt);
 *   
 *   // Before rendering frame:
 *   render_optimizer_begin_frame(&opt);
 *   
 *   // Use cached state functions instead of raw GL calls:
 *   opt_use_program(&opt, program);
 *   opt_bind_texture(&opt, unit, texture);
 *   opt_bind_framebuffer(&opt, fbo);
 *   opt_set_uniform_1f(&opt, program, location, value);
 *   
 *   // End of frame:
 *   render_optimizer_end_frame(&opt);
 *   
 *   // Query statistics:
 *   render_optimizer_stats_t stats = render_optimizer_get_stats(&opt);
 */

#ifndef RENDER_OPTIMIZER_H
#define RENDER_OPTIMIZER_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "platform_compat.h"

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/* Maximum number of texture units to track */
#define OPT_MAX_TEXTURE_UNITS 16

/* Maximum number of cached uniforms per program */
#define OPT_MAX_CACHED_UNIFORMS 64

/* Maximum number of shader programs to track */
#define OPT_MAX_PROGRAMS 16

/* History size for temporal analysis */
#define OPT_TEMPORAL_HISTORY 8

/* Uniform value cache size (bytes per uniform) */
#define OPT_UNIFORM_VALUE_SIZE 64

/* Buffer resolution analysis thresholds */
#define OPT_BLUR_KEYWORD_THRESHOLD 3      /* Keywords suggesting blur content */
#define OPT_NOISE_SAMPLE_THRESHOLD 5      /* Noise texture samples */
#define OPT_FEEDBACK_INDICATOR_THRESHOLD 2 /* Self-referencing patterns */

/* ============================================================================
 * Enumerations
 * ============================================================================ */

typedef enum {
    BUFFER_HINT_FULL,           /* Full resolution (sharp/precise content) */
    BUFFER_HINT_HIGH,           /* 75% resolution (moderate detail) */
    BUFFER_HINT_MEDIUM,         /* 50% resolution (blur/simulation) */
    BUFFER_HINT_LOW,            /* 25% resolution (noise/glow) */
    BUFFER_HINT_TINY,           /* 64-256px fixed (pure procedural) */
    BUFFER_HINT_AUTO            /* Let analyzer decide */
} buffer_resolution_hint_t;

typedef enum {
    PASS_CULL_NONE,             /* Never cull this pass */
    PASS_CULL_MOUSE_IDLE,       /* Cull when mouse inactive */
    PASS_CULL_TIME_STATIC,      /* Cull when time change is small */
    PASS_CULL_CONTENT_SAME,     /* Cull when content hash unchanged */
    PASS_CULL_AUTO              /* Auto-detect cull strategy */
} pass_cull_strategy_t;

typedef enum {
    TEMPORAL_MODE_NONE,         /* No temporal optimization */
    TEMPORAL_MODE_ACCUMULATE,   /* Reuse static frames */
    TEMPORAL_MODE_INTERPOLATE,  /* Motion-compensated interpolation */
    TEMPORAL_MODE_CHECKERBOARD, /* Render half pixels per frame */
    TEMPORAL_MODE_AUTO          /* Auto-detect best mode */
} temporal_mode_t;

typedef enum {
    UNIFORM_TYPE_UNKNOWN,
    UNIFORM_TYPE_FLOAT,
    UNIFORM_TYPE_VEC2,
    UNIFORM_TYPE_VEC3,
    UNIFORM_TYPE_VEC4,
    UNIFORM_TYPE_INT,
    UNIFORM_TYPE_IVEC2,
    UNIFORM_TYPE_IVEC3,
    UNIFORM_TYPE_IVEC4,
    UNIFORM_TYPE_MAT3,
    UNIFORM_TYPE_MAT4,
    UNIFORM_TYPE_SAMPLER
} uniform_type_t;

/* ============================================================================
 * GPU State Cache Structures
 * ============================================================================ */

/* Cached uniform value */
typedef struct {
    GLint location;
    uniform_type_t type;
    bool valid;
    uint8_t value[OPT_UNIFORM_VALUE_SIZE];
} cached_uniform_t;

/* Per-program uniform cache */
typedef struct {
    GLuint program;
    cached_uniform_t uniforms[OPT_MAX_CACHED_UNIFORMS];
    int uniform_count;
    bool valid;
} program_uniform_cache_t;

/* Complete GPU state snapshot */
typedef struct {
    /* Currently bound objects */
    GLuint current_program;
    GLuint current_vao;
    GLuint current_vbo;
    GLuint current_fbo;
    GLuint current_read_fbo;
    
    /* Texture unit bindings */
    GLenum active_texture_unit;
    GLuint bound_textures[OPT_MAX_TEXTURE_UNITS];
    GLenum texture_targets[OPT_MAX_TEXTURE_UNITS];  /* GL_TEXTURE_2D, etc. */
    
    /* Render state */
    bool depth_test_enabled;
    bool blend_enabled;
    bool cull_face_enabled;
    bool scissor_test_enabled;
    bool depth_mask;
    
    /* Blend state */
    GLenum blend_src_rgb;
    GLenum blend_dst_rgb;
    GLenum blend_src_alpha;
    GLenum blend_dst_alpha;
    GLenum blend_equation_rgb;
    GLenum blend_equation_alpha;
    
    /* Viewport */
    GLint viewport[4];
    
    /* Clear color */
    GLfloat clear_color[4];
    
    /* Color mask */
    GLboolean color_mask[4];
    
    /* State validity flags */
    bool initialized;
} gpu_state_cache_t;

/* ============================================================================
 * Buffer Analysis Structures
 * ============================================================================ */

/* Analysis result for a buffer pass */
typedef struct {
    buffer_resolution_hint_t hint;
    float recommended_scale;        /* 0.0 - 1.0 */
    int min_resolution;             /* Minimum useful resolution */
    
    /* Analysis scores */
    int blur_score;                 /* Higher = more blur operations */
    int noise_score;                /* Higher = more noise sampling */
    int feedback_score;             /* Higher = more self-referencing */
    int precision_score;            /* Higher = needs more precision */
    int animation_score;            /* Higher = more time-dependent */
    
    /* Detected patterns */
    bool uses_blur;
    bool uses_noise_only;
    bool uses_self_feedback;
    bool uses_high_frequency_detail;
    bool is_time_varying;
    bool is_mouse_dependent;
    
    /* Content hash for change detection */
    uint64_t content_hash;
    uint64_t prev_content_hash;
    bool content_changed;
} buffer_analysis_t;

/* ============================================================================
 * Temporal Optimization Structures
 * ============================================================================ */

/* Frame history entry */
typedef struct {
    float time;
    float mouse_x;
    float mouse_y;
    bool mouse_click;
    uint64_t frame_hash;            /* Quick hash of frame parameters */
    double wall_time;
} frame_history_entry_t;

/* Temporal state for a pass */
typedef struct {
    temporal_mode_t mode;
    
    /* Frame history */
    frame_history_entry_t history[OPT_TEMPORAL_HISTORY];
    int history_index;
    int history_count;
    
    /* Checkerboard state */
    int checkerboard_phase;         /* 0 or 1 */
    GLuint checkerboard_stencil;    /* Stencil texture for masking */
    
    /* Temporal accumulation */
    GLuint accumulation_texture;    /* Previous frame */
    int static_frames;              /* Consecutive static frames */
    float motion_estimate;          /* Estimated scene motion */
    
    /* Skip tracking */
    int consecutive_skips;          /* How many frames skipped */
    int max_consecutive_skips;      /* Safety limit */
    bool skip_this_frame;           /* Should we skip rendering? */
    bool reuse_previous;            /* Use previous frame result */
    
    /* Interpolation */
    float interpolation_factor;     /* 0.0 = prev frame, 1.0 = current */
    GLuint prev_frame_texture;
    GLuint motion_vectors;          /* Motion vector texture */
} temporal_state_t;

/* ============================================================================
 * Pass Culling Structures
 * ============================================================================ */

typedef struct {
    pass_cull_strategy_t strategy;
    
    /* Timing */
    double last_render_time;        /* When this pass last rendered */
    double min_render_interval;     /* Minimum time between renders */
    
    /* Mouse tracking */
    float last_mouse_x;
    float last_mouse_y;
    double mouse_idle_time;         /* Seconds since mouse moved */
    float mouse_idle_threshold;     /* Seconds before considered idle */
    
    /* Content tracking */
    uint64_t prev_input_hash;       /* Hash of inputs last frame */
    uint64_t curr_input_hash;       /* Hash of inputs this frame */
    
    /* Time tracking */
    float prev_time;
    float time_delta_threshold;     /* Min time delta to re-render */
    
    /* Results */
    bool should_render;             /* Final decision */
    bool was_culled;                /* Was this pass culled? */
    int cull_reason;                /* Why was it culled? */
    
    /* Statistics */
    uint64_t render_count;          /* Total renders */
    uint64_t cull_count;            /* Total culls */
} pass_cull_state_t;

/* ============================================================================
 * Main Optimizer Structure
 * ============================================================================ */

typedef struct {
    /* GPU state cache */
    gpu_state_cache_t state;
    
    /* Uniform caches per program */
    program_uniform_cache_t uniform_caches[OPT_MAX_PROGRAMS];
    int uniform_cache_count;
    
    /* Buffer analysis results (indexed by pass type) */
    buffer_analysis_t buffer_analysis[4];  /* BufferA-D */
    bool analysis_complete;
    
    /* Temporal state per pass */
    temporal_state_t temporal[5];   /* BufferA-D + Image */
    temporal_mode_t global_temporal_mode;
    
    /* Pass culling state */
    pass_cull_state_t cull_state[5];
    
    /* Global settings */
    bool enabled;
    bool aggressive_mode;           /* More aggressive optimization */
    float quality_bias;             /* 0.0 = performance, 1.0 = quality */
    
    /* Frame tracking */
    uint64_t frame_number;
    double frame_start_time;
    double last_frame_time;
    float frame_time_ms;
    
    /* Mouse tracking (global) */
    float mouse_x;
    float mouse_y;
    bool mouse_click;
    double mouse_last_move_time;
    float mouse_idle_seconds;
    
    /* Performance statistics */
    struct {
        /* Call counts */
        uint64_t gl_calls_total;
        uint64_t gl_calls_avoided;
        uint64_t uniform_updates_total;
        uint64_t uniform_updates_avoided;
        uint64_t texture_binds_total;
        uint64_t texture_binds_avoided;
        uint64_t fbo_binds_total;
        uint64_t fbo_binds_avoided;
        uint64_t program_switches_total;
        uint64_t program_switches_avoided;
        
        /* Pass statistics */
        uint64_t passes_rendered;
        uint64_t passes_culled;
        uint64_t passes_reused;
        
        /* Temporal statistics */
        uint64_t frames_interpolated;
        uint64_t frames_skipped;
        uint64_t checkerboard_frames;
        
        /* Resolution statistics */
        float avg_buffer_scale;
        int total_pixels_saved;
    } stats;
    
    bool initialized;
} render_optimizer_t;

/* Statistics snapshot for reporting */
typedef struct {
    /* Efficiency metrics */
    float gl_call_efficiency;       /* % of calls avoided */
    float uniform_efficiency;       /* % of uniforms cached */
    float texture_bind_efficiency;
    float fbo_bind_efficiency;
    float program_switch_efficiency;
    
    /* Pass metrics */
    float pass_cull_rate;           /* % of passes culled */
    float temporal_reuse_rate;      /* % of frames reused */
    
    /* Performance impact */
    float estimated_speedup;        /* Multiplier vs. unoptimized */
    int64_t estimated_gpu_cycles_saved;
    
    /* Current state */
    uint64_t frame_number;
    float frame_time_ms;
    float mouse_idle_seconds;
} render_optimizer_stats_t;

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

/* Initialize the optimizer */
void render_optimizer_init(render_optimizer_t *opt);

/* Destroy optimizer and free resources */
void render_optimizer_destroy(render_optimizer_t *opt);

/* Reset all caches and statistics */
void render_optimizer_reset(render_optimizer_t *opt);

/* Enable/disable optimizer */
void render_optimizer_set_enabled(render_optimizer_t *opt, bool enabled);

/* Set quality/performance bias (0.0 = performance, 1.0 = quality) */
void render_optimizer_set_quality_bias(render_optimizer_t *opt, float bias);

/* ============================================================================
 * Frame Lifecycle
 * ============================================================================ */

/* Call at start of each frame */
void render_optimizer_begin_frame(render_optimizer_t *opt, 
                                   float time, 
                                   float mouse_x, float mouse_y, 
                                   bool mouse_click);

/* Call at end of each frame */
void render_optimizer_end_frame(render_optimizer_t *opt);

/* ============================================================================
 * Optimized GL State Functions
 * ============================================================================ */

/* Program binding (caches current program) */
void opt_use_program(render_optimizer_t *opt, GLuint program);

/* VAO binding */
void opt_bind_vao(render_optimizer_t *opt, GLuint vao);

/* VBO binding */
void opt_bind_buffer(render_optimizer_t *opt, GLenum target, GLuint buffer);

/* Framebuffer binding */
void opt_bind_framebuffer(render_optimizer_t *opt, GLenum target, GLuint fbo);

/* Texture binding with unit management */
void opt_bind_texture(render_optimizer_t *opt, int unit, GLenum target, GLuint texture);

/* Activate texture unit only if needed */
void opt_active_texture(render_optimizer_t *opt, int unit);

/* ============================================================================
 * Optimized Render State Functions
 * ============================================================================ */

/* Enable/disable with caching */
void opt_enable(render_optimizer_t *opt, GLenum cap);
void opt_disable(render_optimizer_t *opt, GLenum cap);

/* Depth mask with caching */
void opt_depth_mask(render_optimizer_t *opt, GLboolean flag);

/* Color mask with caching */
void opt_color_mask(render_optimizer_t *opt, GLboolean r, GLboolean g, GLboolean b, GLboolean a);

/* Blend function with caching */
void opt_blend_func(render_optimizer_t *opt, GLenum sfactor, GLenum dfactor);
void opt_blend_func_separate(render_optimizer_t *opt, 
                              GLenum srcRGB, GLenum dstRGB, 
                              GLenum srcAlpha, GLenum dstAlpha);

/* Viewport with caching */
void opt_viewport(render_optimizer_t *opt, GLint x, GLint y, GLsizei width, GLsizei height);

/* Clear color with caching */
void opt_clear_color(render_optimizer_t *opt, GLfloat r, GLfloat g, GLfloat b, GLfloat a);

/* ============================================================================
 * Optimized Uniform Functions
 * ============================================================================ */

/* Uniform setters with value caching - returns true if value was changed */
bool opt_uniform_1f(render_optimizer_t *opt, GLuint program, GLint location, float v);
bool opt_uniform_2f(render_optimizer_t *opt, GLuint program, GLint location, float v0, float v1);
bool opt_uniform_3f(render_optimizer_t *opt, GLuint program, GLint location, float v0, float v1, float v2);
bool opt_uniform_4f(render_optimizer_t *opt, GLuint program, GLint location, float v0, float v1, float v2, float v3);

bool opt_uniform_1i(render_optimizer_t *opt, GLuint program, GLint location, int v);
bool opt_uniform_2i(render_optimizer_t *opt, GLuint program, GLint location, int v0, int v1);
bool opt_uniform_3i(render_optimizer_t *opt, GLuint program, GLint location, int v0, int v1, int v2);
bool opt_uniform_4i(render_optimizer_t *opt, GLuint program, GLint location, int v0, int v1, int v2, int v3);

bool opt_uniform_3fv(render_optimizer_t *opt, GLuint program, GLint location, int count, const float *value);
bool opt_uniform_4fv(render_optimizer_t *opt, GLuint program, GLint location, int count, const float *value);

bool opt_uniform_matrix3fv(render_optimizer_t *opt, GLuint program, GLint location, 
                            int count, bool transpose, const float *value);
bool opt_uniform_matrix4fv(render_optimizer_t *opt, GLuint program, GLint location, 
                            int count, bool transpose, const float *value);

/* ============================================================================
 * Buffer Analysis Functions
 * ============================================================================ */

/* Analyze shader source to determine optimal buffer resolution */
buffer_analysis_t analyze_buffer_requirements(const char *shader_source, int pass_type);

/* Get recommended resolution scale for a buffer */
float get_recommended_buffer_scale(const buffer_analysis_t *analysis, float base_scale);

/* Update buffer analysis with runtime information */
void update_buffer_analysis(buffer_analysis_t *analysis, 
                            int actual_width, int actual_height,
                            float render_time_ms);

/* ============================================================================
 * Pass Culling Functions
 * ============================================================================ */

/* Initialize culling state for a pass */
void pass_cull_init(pass_cull_state_t *state, pass_cull_strategy_t strategy);

/* Update culling decision for this frame */
bool pass_should_render(pass_cull_state_t *state,
                        float time, float prev_time,
                        float mouse_x, float mouse_y,
                        float prev_mouse_x, float prev_mouse_y,
                        double current_wall_time);

/* Mark pass as rendered (updates tracking) */
void pass_rendered(pass_cull_state_t *state, double wall_time);

/* Mark pass as culled */
void pass_culled(pass_cull_state_t *state, int reason);

/* ============================================================================
 * Temporal Optimization Functions
 * ============================================================================ */

/* Initialize temporal state for a pass */
void temporal_init(temporal_state_t *state, temporal_mode_t mode);

/* Destroy temporal resources */
void temporal_destroy(temporal_state_t *state);

/* Update temporal state and decide rendering strategy */
void temporal_update(temporal_state_t *state,
                     float time, float mouse_x, float mouse_y, bool mouse_click,
                     double wall_time);

/* Check if we should skip rendering this frame */
bool temporal_should_skip(const temporal_state_t *state);

/* Check if we should reuse previous frame */
bool temporal_should_reuse(const temporal_state_t *state);

/* Get checkerboard phase (0 or 1) */
int temporal_get_checkerboard_phase(const temporal_state_t *state);

/* Get interpolation factor for motion interpolation */
float temporal_get_interpolation_factor(const temporal_state_t *state);

/* Mark frame as rendered in temporal history */
void temporal_frame_rendered(temporal_state_t *state, double wall_time);

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

/* Get current statistics */
render_optimizer_stats_t render_optimizer_get_stats(const render_optimizer_t *opt);

/* Reset statistics */
void render_optimizer_reset_stats(render_optimizer_t *opt);

/* Print statistics to log */
void render_optimizer_log_stats(const render_optimizer_t *opt);

/* Sync cached state with actual GPU state (for debugging/recovery) */
void render_optimizer_sync_state(render_optimizer_t *opt);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Hash function for content change detection */
uint64_t opt_hash_floats(const float *values, int count);
uint64_t opt_hash_combine(uint64_t h1, uint64_t h2);

/* Quick check if two float arrays are approximately equal */
bool opt_floats_equal(const float *a, const float *b, int count, float epsilon);

/* Convert buffer hint to scale factor */
float buffer_hint_to_scale(buffer_resolution_hint_t hint);

/* Convert scale factor to nearest buffer hint */
buffer_resolution_hint_t scale_to_buffer_hint(float scale);

/* ============================================================================
 * Inline Helper Implementations
 * ============================================================================ */

static inline uint64_t opt_hash_float(float f) {
    union { float f; uint32_t u; } conv;
    conv.f = f;
    /* FNV-1a inspired mixing */
    uint64_t h = 14695981039346656037ULL;
    h ^= conv.u;
    h *= 1099511628211ULL;
    return h;
}

static inline uint64_t opt_hash_int(int i) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint32_t)i;
    h *= 1099511628211ULL;
    return h;
}

static inline bool opt_float_eq(float a, float b, float eps) {
    float diff = a - b;
    return (diff > -eps) && (diff < eps);
}

/* Calculate approximate GPU cycles for a draw call */
static inline int64_t estimate_draw_cycles(int width, int height, int instruction_count) {
    /* Very rough estimate: ~2 cycles per pixel per instruction for fragment shader */
    return (int64_t)width * height * instruction_count * 2;
}

#endif /* RENDER_OPTIMIZER_H */