/* Multipass Optimizer - Smart Per-Buffer Resolution & Half-Rate Updates
 * 
 * This module provides REAL performance optimizations for multipass shaders:
 * 
 * 1. SMART PER-BUFFER RESOLUTION
 *    - Analyzes each buffer pass to determine optimal resolution
 *    - Blur/glow buffers: 25-50% resolution (blur destroys detail anyway)
 *    - Noise buffers: Fixed 256-512px (tiles seamlessly)
 *    - Feedback buffers: 50-75% resolution
 *    - Image pass: Always full resolution (user sees this)
 * 
 * 2. HALF-RATE BUFFER UPDATES
 *    - Update BufferA on even frames, BufferB on odd frames
 *    - Each buffer still gets 30 updates/sec at 60 FPS
 *    - Halves buffer rendering cost with minimal visual impact
 *    - Configurable: can do 1/3, 1/4 rate for very heavy shaders
 * 
 * 3. STATIC SCENE DETECTION
 *    - If iTime delta is tiny and mouse hasn't moved, skip buffer re-render
 *    - Reuse previous frame's buffer textures
 *    - Huge savings for paused/idle wallpapers
 * 
 * 4. CONTENT-AWARE BUFFER SIZING
 *    - Detect blur operations -> reduce resolution
 *    - Detect noise sampling -> use fixed small size
 *    - Detect edge detection -> keep high resolution
 *    - Detect raymarching -> can reduce slightly
 * 
 * 5. WORKLOAD FEEDBACK TO ADAPTIVE SCALE
 *    - Reports effective workload reduction to adaptive_scale system
 *    - Allows adaptive to be less aggressive when passes are being skipped
 *    - Prevents over-scaling when multipass optimizer is already saving work
 * 
 * Usage:
 *   multipass_optimizer_t opt;
 *   multipass_optimizer_init(&opt);
 *   multipass_optimizer_analyze_passes(&opt, shader);
 *   
 *   // Each frame:
 *   for each pass:
 *       if (multipass_optimizer_should_render_pass(&opt, pass_index, frame_num)) {
 *           int w, h;
 *           multipass_optimizer_get_pass_resolution(&opt, pass_index, base_w, base_h, &w, &h);
 *           render_pass(w, h);
 *       }
 */

#ifndef MULTIPASS_OPTIMIZER_H
#define MULTIPASS_OPTIMIZER_H

#include <stdbool.h>
#include <stdint.h>

/* Maximum passes we track */
#define MOPT_MAX_PASSES 8

/* ============================================================================
 * Buffer Content Classification
 * ============================================================================ */

typedef enum {
    BUFFER_CONTENT_UNKNOWN,         /* Can't determine, use full res */
    BUFFER_CONTENT_BLUR,            /* Blur/glow/bloom - low res OK */
    BUFFER_CONTENT_NOISE,           /* Pure noise generation - tiny fixed res */
    BUFFER_CONTENT_FEEDBACK,        /* Self-referencing temporal - medium res */
    BUFFER_CONTENT_SIMULATION,      /* Fluid/particle sim - medium res */
    BUFFER_CONTENT_RAYMARCHING,     /* SDF raymarching - can reduce slightly */
    BUFFER_CONTENT_EDGE_DETECT,     /* Edge/detail detection - high res needed */
    BUFFER_CONTENT_POSTPROCESS,     /* Color grading, etc - full res */
    BUFFER_CONTENT_IMAGE            /* Final output - always full res */
} buffer_content_t;

/* ============================================================================
 * Per-Pass Optimization Settings
 * ============================================================================ */

typedef struct {
    /* Content analysis results */
    buffer_content_t content_type;
    float recommended_scale;        /* 0.0-1.0, relative to output size */
    int min_width;                  /* Minimum useful width */
    int min_height;                 /* Minimum useful height */
    int max_width;                  /* Maximum needed width (0 = unlimited) */
    int max_height;                 /* Maximum needed height */
    
    /* Update rate control */
    int update_divisor;             /* Render every N frames (1=every, 2=half, etc) */
    int frame_offset;               /* Which frame in cycle to render on */
    bool can_skip_when_static;      /* Safe to skip if scene is static */
    
    /* Detection scores (for debugging/tuning) */
    int blur_score;
    int noise_score;
    int feedback_score;
    int edge_score;
    int raymarch_score;
    
    /* Flags */
    bool uses_previous_frame;       /* Reads from self (feedback effect) */
    bool uses_mouse;                /* Depends on iMouse */
    bool uses_time;                 /* Depends on iTime (most do) */
    bool is_image_pass;             /* Final output pass */
    bool analyzed;                  /* Analysis complete */
} pass_optimization_t;

/* ============================================================================
 * Static Scene Detection
 * ============================================================================ */

typedef struct {
    float last_time;                /* iTime from last frame */
    float last_mouse_x;
    float last_mouse_y;
    bool last_mouse_click;
    
    /* Thresholds */
    float time_epsilon;             /* Max time delta to consider "static" */
    float mouse_epsilon;            /* Max mouse movement to consider "static" */
    
    /* State */
    bool scene_is_static;           /* Current frame is static */
    int consecutive_static_frames;  /* How many static frames in a row */
    int max_skip_frames;            /* Max frames to skip rendering */
} static_detector_t;

/* ============================================================================
 * Main Optimizer Structure
 * ============================================================================ */

typedef struct {
    /* Per-pass settings */
    pass_optimization_t passes[MOPT_MAX_PASSES];
    int pass_count;
    int image_pass_index;
    
    /* Global settings */
    bool enabled;
    bool half_rate_enabled;         /* Use half-rate buffer updates */
    bool static_skip_enabled;       /* Skip rendering when static */
    bool smart_resolution_enabled;  /* Use per-buffer resolution */
    float global_quality;           /* 0.0-1.0, affects all scales */
    
    /* Static scene detection */
    static_detector_t static_detect;
    
    /* Frame tracking */
    uint64_t frame_number;
    
    /* Statistics */
    uint64_t passes_rendered;
    uint64_t passes_skipped;
    uint64_t pixels_saved;          /* Compared to full-res always-render */
    
    /* Workload feedback for adaptive_scale integration */
    struct {
        float effective_workload;       /* 0.0-1.0, fraction of full workload actually rendered */
        float pixel_reduction;          /* 0.0-1.0, fraction of pixels saved by smart resolution */
        float pass_skip_rate;           /* 0.0-1.0, fraction of passes skipped this frame */
        int passes_rendered_this_frame;
        int passes_skipped_this_frame;
        int64_t pixels_rendered_this_frame;
        int64_t pixels_full_resolution;  /* What we would have rendered without optimization */
    } workload;
    
    bool initialized;
} multipass_optimizer_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/* Initialize optimizer with default settings */
void multipass_optimizer_init(multipass_optimizer_t *opt);

/* Reset optimizer state (but keep settings) */
void multipass_optimizer_reset(multipass_optimizer_t *opt);

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Enable/disable optimizer entirely */
void multipass_optimizer_set_enabled(multipass_optimizer_t *opt, bool enabled);

/* Enable/disable specific features */
void multipass_optimizer_set_half_rate(multipass_optimizer_t *opt, bool enabled);
void multipass_optimizer_set_static_skip(multipass_optimizer_t *opt, bool enabled);
void multipass_optimizer_set_smart_resolution(multipass_optimizer_t *opt, bool enabled);

/* Set global quality (affects all resolution scaling) */
void multipass_optimizer_set_quality(multipass_optimizer_t *opt, float quality);

/* Override settings for a specific pass */
void multipass_optimizer_set_pass_scale(multipass_optimizer_t *opt, int pass_index, float scale);
void multipass_optimizer_set_pass_update_rate(multipass_optimizer_t *opt, int pass_index, int divisor);

/* ============================================================================
 * Analysis - Call once when shader is loaded
 * ============================================================================ */

/* Analyze a single pass source code to determine optimal settings */
pass_optimization_t multipass_optimizer_analyze_source(const char *source, bool is_image_pass);

/* Analyze all passes in a shader (pass sources array, null-terminated) */
void multipass_optimizer_analyze_shader(multipass_optimizer_t *opt,
                                        const char **pass_sources,
                                        const int *pass_types,
                                        int pass_count,
                                        int image_pass_index);

/* ============================================================================
 * Per-Frame Usage
 * ============================================================================ */

/* Call at start of each frame to update state */
void multipass_optimizer_begin_frame(multipass_optimizer_t *opt,
                                     float time,
                                     float mouse_x, float mouse_y,
                                     bool mouse_click);

/* Check if a pass should be rendered this frame */
bool multipass_optimizer_should_render_pass(multipass_optimizer_t *opt, int pass_index);

/* Get the resolution to use for a pass (applies smart scaling) */
void multipass_optimizer_get_pass_resolution(multipass_optimizer_t *opt,
                                             int pass_index,
                                             int base_width, int base_height,
                                             int *out_width, int *out_height);

/* Call after rendering a pass (updates statistics) */
void multipass_optimizer_pass_rendered(multipass_optimizer_t *opt,
                                       int pass_index,
                                       int width, int height);

/* Call when skipping a pass (updates statistics) */
void multipass_optimizer_pass_skipped(multipass_optimizer_t *opt, int pass_index);

/* Call at end of frame */
void multipass_optimizer_end_frame(multipass_optimizer_t *opt);

/* ============================================================================
 * Workload Feedback (for adaptive_scale integration)
 * ============================================================================ */

/* Get effective workload factor for this frame (0.0-1.0)
 * Returns the fraction of "full" work that was actually done.
 * adaptive_scale can use this to adjust its scaling decisions. */
float multipass_optimizer_get_effective_workload(const multipass_optimizer_t *opt);

/* Get pixel reduction factor (0.0-1.0)
 * Returns fraction of pixels saved by smart per-buffer resolution */
float multipass_optimizer_get_pixel_reduction(const multipass_optimizer_t *opt);

/* Reset per-frame workload tracking (call at start of frame) */
void multipass_optimizer_reset_frame_workload(multipass_optimizer_t *opt);

/* Record rendered pass for workload tracking */
void multipass_optimizer_record_pass(multipass_optimizer_t *opt,
                                     int pass_index,
                                     int width, int height,
                                     int full_width, int full_height,
                                     bool was_rendered);

/* ============================================================================
 * Statistics & Debugging
 * ============================================================================ */

typedef struct {
    uint64_t total_passes_rendered;
    uint64_t total_passes_skipped;
    float skip_rate_percent;
    
    uint64_t total_pixels_rendered;
    uint64_t total_pixels_saved;
    float pixel_savings_percent;
    
    float estimated_speedup;
    
    /* Per-pass info */
    struct {
        buffer_content_t content_type;
        float scale_used;
        int update_divisor;
        uint64_t times_rendered;
        uint64_t times_skipped;
    } pass_stats[MOPT_MAX_PASSES];
    int pass_count;
} multipass_optimizer_stats_t;

/* Get current statistics */
multipass_optimizer_stats_t multipass_optimizer_get_stats(const multipass_optimizer_t *opt);

/* Log statistics */
void multipass_optimizer_log_stats(const multipass_optimizer_t *opt);

/* Get human-readable name for content type */
const char *buffer_content_type_name(buffer_content_t type);

/* ============================================================================
 * Inline Helpers
 * ============================================================================ */

/* Quick check if optimizer is active and might skip passes */
static inline bool multipass_optimizer_may_skip(const multipass_optimizer_t *opt) {
    return opt && opt->enabled && (opt->half_rate_enabled || opt->static_skip_enabled);
}

/* Quick check if optimizer might reduce resolution */
static inline bool multipass_optimizer_may_scale(const multipass_optimizer_t *opt) {
    return opt && opt->enabled && opt->smart_resolution_enabled;
}

/* Calculate scale factor for a content type */
static inline float buffer_content_default_scale(buffer_content_t type) {
    switch (type) {
        case BUFFER_CONTENT_NOISE:       return 0.125f;  /* 12.5% - very small */
        case BUFFER_CONTENT_BLUR:        return 0.25f;   /* 25% */
        case BUFFER_CONTENT_SIMULATION:  return 0.5f;    /* 50% */
        case BUFFER_CONTENT_FEEDBACK:    return 0.5f;    /* 50% */
        case BUFFER_CONTENT_RAYMARCHING: return 0.75f;   /* 75% */
        case BUFFER_CONTENT_EDGE_DETECT: return 1.0f;    /* 100% - needs precision */
        case BUFFER_CONTENT_POSTPROCESS: return 1.0f;    /* 100% */
        case BUFFER_CONTENT_IMAGE:       return 1.0f;    /* 100% - always full */
        case BUFFER_CONTENT_UNKNOWN:     
        default:                         return 0.75f;   /* 75% - safe default */
    }
}

/* Calculate recommended update divisor for content type */
static inline int buffer_content_default_update_rate(buffer_content_t type) {
    switch (type) {
        case BUFFER_CONTENT_NOISE:       return 4;  /* Update every 4 frames */
        case BUFFER_CONTENT_BLUR:        return 2;  /* Every other frame */
        case BUFFER_CONTENT_SIMULATION:  return 1;  /* Every frame (state-dependent) */
        case BUFFER_CONTENT_FEEDBACK:    return 1;  /* Every frame (temporal) */
        case BUFFER_CONTENT_RAYMARCHING: return 2;  /* Every other frame */
        case BUFFER_CONTENT_EDGE_DETECT: return 2;  /* Every other frame */
        case BUFFER_CONTENT_POSTPROCESS: return 1;  /* Every frame */
        case BUFFER_CONTENT_IMAGE:       return 1;  /* Every frame (output) */
        case BUFFER_CONTENT_UNKNOWN:     
        default:                         return 1;  /* Safe: every frame */
    }
}

#endif /* MULTIPASS_OPTIMIZER_H */