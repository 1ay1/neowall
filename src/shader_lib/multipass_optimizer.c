/* Multipass Optimizer - Smart Per-Buffer Resolution & Half-Rate Updates
 * 
 * Implementation of content-aware buffer optimization for multipass shaders.
 */

#include "multipass_optimizer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Forward declarations for logging */
extern void log_info(const char *fmt, ...);
extern void log_debug(const char *fmt, ...);

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/* Count occurrences of pattern in source */
static int count_pattern(const char *source, const char *pattern) {
    if (!source || !pattern) return 0;
    
    int count = 0;
    const char *p = source;
    size_t len = strlen(pattern);
    
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += len;
    }
    return count;
}

/* Check if source contains pattern */
static int contains(const char *source, const char *pattern) {
    return source && pattern && strstr(source, pattern) != NULL;
}

/* Clamp integer to range */
static int clampi(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* Clamp float to range */
static float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* ============================================================================
 * Content Type Names
 * ============================================================================ */

const char *buffer_content_type_name(buffer_content_t type) {
    switch (type) {
        case BUFFER_CONTENT_UNKNOWN:     return "unknown";
        case BUFFER_CONTENT_BLUR:        return "blur";
        case BUFFER_CONTENT_NOISE:       return "noise";
        case BUFFER_CONTENT_FEEDBACK:    return "feedback";
        case BUFFER_CONTENT_SIMULATION:  return "simulation";
        case BUFFER_CONTENT_RAYMARCHING: return "raymarching";
        case BUFFER_CONTENT_EDGE_DETECT: return "edge-detect";
        case BUFFER_CONTENT_POSTPROCESS: return "postprocess";
        case BUFFER_CONTENT_IMAGE:       return "image";
        default:                         return "unknown";
    }
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void multipass_optimizer_init(multipass_optimizer_t *opt) {
    if (!opt) return;
    
    memset(opt, 0, sizeof(*opt));
    
    /* Default settings - conservative to ensure visual quality */
    opt->enabled = true;
    opt->half_rate_enabled = false;       /* Disabled by default - can cause artifacts */
    opt->static_skip_enabled = true;      /* Safe - skip when nothing changes */
    opt->smart_resolution_enabled = true; /* Main optimization */
    opt->global_quality = 0.8f;           /* 80% quality bias */
    
    /* Static detection defaults */
    opt->static_detect.time_epsilon = 0.0001f;    /* Very small time change */
    opt->static_detect.mouse_epsilon = 1.0f;      /* 1 pixel movement */
    opt->static_detect.max_skip_frames = 10;      /* Max 10 frames (~166ms at 60fps) */
    
    /* Initialize all passes to defaults */
    for (int i = 0; i < MOPT_MAX_PASSES; i++) {
        opt->passes[i].content_type = BUFFER_CONTENT_UNKNOWN;
        opt->passes[i].recommended_scale = 1.0f;
        opt->passes[i].update_divisor = 1;
        opt->passes[i].frame_offset = 0;
        opt->passes[i].can_skip_when_static = true;
        opt->passes[i].uses_time = true;  /* Assume most shaders use time */
    }
    
    opt->image_pass_index = -1;
    opt->initialized = true;
}

void multipass_optimizer_reset(multipass_optimizer_t *opt) {
    if (!opt) return;
    
    /* Reset statistics */
    opt->passes_rendered = 0;
    opt->passes_skipped = 0;
    opt->pixels_saved = 0;
    opt->frame_number = 0;
    
    /* Reset static detection state */
    opt->static_detect.scene_is_static = false;
    opt->static_detect.consecutive_static_frames = 0;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void multipass_optimizer_set_enabled(multipass_optimizer_t *opt, bool enabled) {
    if (opt) opt->enabled = enabled;
}

void multipass_optimizer_set_half_rate(multipass_optimizer_t *opt, bool enabled) {
    if (opt) opt->half_rate_enabled = enabled;
}

void multipass_optimizer_set_static_skip(multipass_optimizer_t *opt, bool enabled) {
    if (opt) opt->static_skip_enabled = enabled;
}

void multipass_optimizer_set_smart_resolution(multipass_optimizer_t *opt, bool enabled) {
    if (opt) opt->smart_resolution_enabled = enabled;
}

void multipass_optimizer_set_quality(multipass_optimizer_t *opt, float quality) {
    if (opt) opt->global_quality = clampf(quality, 0.0f, 1.0f);
}

void multipass_optimizer_set_pass_scale(multipass_optimizer_t *opt, int pass_index, float scale) {
    if (opt && pass_index >= 0 && pass_index < MOPT_MAX_PASSES) {
        opt->passes[pass_index].recommended_scale = clampf(scale, 0.1f, 1.0f);
    }
}

void multipass_optimizer_set_pass_update_rate(multipass_optimizer_t *opt, int pass_index, int divisor) {
    if (opt && pass_index >= 0 && pass_index < MOPT_MAX_PASSES) {
        opt->passes[pass_index].update_divisor = clampi(divisor, 1, 8);
    }
}

/* ============================================================================
 * Source Code Analysis
 * ============================================================================ */

pass_optimization_t multipass_optimizer_analyze_source(const char *source, bool is_image_pass) {
    pass_optimization_t result;
    memset(&result, 0, sizeof(result));
    
    /* Image pass is always full resolution, every frame */
    if (is_image_pass) {
        result.content_type = BUFFER_CONTENT_IMAGE;
        result.recommended_scale = 1.0f;
        result.update_divisor = 1;
        result.is_image_pass = true;
        result.can_skip_when_static = false;  /* Always render output */
        result.analyzed = true;
        return result;
    }
    
    if (!source) {
        result.content_type = BUFFER_CONTENT_UNKNOWN;
        result.recommended_scale = 0.75f;
        result.update_divisor = 1;
        result.analyzed = true;
        return result;
    }
    
    /* Score different content types based on source analysis */
    
    /* BLUR indicators */
    result.blur_score += count_pattern(source, "blur") * 25;
    result.blur_score += count_pattern(source, "Blur") * 25;
    result.blur_score += count_pattern(source, "gaussian") * 30;
    result.blur_score += count_pattern(source, "Gaussian") * 30;
    result.blur_score += count_pattern(source, "glow") * 20;
    result.blur_score += count_pattern(source, "bloom") * 20;
    result.blur_score += count_pattern(source, "smooth") * 10;
    result.blur_score += count_pattern(source, "average") * 10;
    /* Blur typically samples multiple nearby texels */
    if (count_pattern(source, "texture") > 8) result.blur_score += 15;
    
    /* NOISE indicators */
    result.noise_score += count_pattern(source, "noise") * 20;
    result.noise_score += count_pattern(source, "Noise") * 20;
    result.noise_score += count_pattern(source, "hash") * 15;
    result.noise_score += count_pattern(source, "rand") * 15;
    result.noise_score += count_pattern(source, "random") * 15;
    result.noise_score += count_pattern(source, "fract(sin") * 30;
    result.noise_score += count_pattern(source, "fbm") * 25;
    result.noise_score += count_pattern(source, "FBM") * 25;
    result.noise_score += count_pattern(source, "perlin") * 25;
    result.noise_score += count_pattern(source, "simplex") * 25;
    result.noise_score += count_pattern(source, "worley") * 20;
    result.noise_score += count_pattern(source, "voronoi") * 20;
    
    /* FEEDBACK indicators (self-referencing) */
    result.feedback_score += count_pattern(source, "iChannel0") * 10;
    result.feedback_score += count_pattern(source, "previous") * 20;
    result.feedback_score += count_pattern(source, "feedback") * 30;
    result.feedback_score += count_pattern(source, "accumulate") * 20;
    result.feedback_score += count_pattern(source, "temporal") * 15;
    if (contains(source, "mix") && contains(source, "iChannel0")) {
        result.feedback_score += 25;  /* Temporal blending pattern */
    }
    
    /* EDGE DETECTION indicators (need high precision) */
    result.edge_score += count_pattern(source, "edge") * 20;
    result.edge_score += count_pattern(source, "Edge") * 20;
    result.edge_score += count_pattern(source, "sobel") * 30;
    result.edge_score += count_pattern(source, "Sobel") * 30;
    result.edge_score += count_pattern(source, "laplacian") * 25;
    result.edge_score += count_pattern(source, "gradient") * 15;
    result.edge_score += count_pattern(source, "sharpen") * 20;
    result.edge_score += count_pattern(source, "detail") * 10;
    
    /* RAYMARCHING indicators */
    result.raymarch_score += count_pattern(source, "raymarch") * 30;
    result.raymarch_score += count_pattern(source, "raytrace") * 25;
    result.raymarch_score += count_pattern(source, "sdf") * 20;
    result.raymarch_score += count_pattern(source, "SDF") * 20;
    result.raymarch_score += count_pattern(source, "distance") * 5;
    result.raymarch_score += count_pattern(source, "march") * 15;
    result.raymarch_score += count_pattern(source, "sphere") * 5;
    result.raymarch_score += count_pattern(source, "box") * 5;
    /* Heavy for loops suggest raymarching */
    if (count_pattern(source, "for") > 2) result.raymarch_score += 10;
    
    /* Detect dependency flags */
    result.uses_mouse = contains(source, "iMouse");
    result.uses_time = contains(source, "iTime") || contains(source, "iFrame");
    result.uses_previous_frame = result.feedback_score > 20;
    
    /* Determine content type based on highest score */
    int max_score = 0;
    result.content_type = BUFFER_CONTENT_UNKNOWN;
    
    if (result.noise_score > max_score && result.noise_score >= 40) {
        max_score = result.noise_score;
        result.content_type = BUFFER_CONTENT_NOISE;
    }
    if (result.blur_score > max_score && result.blur_score >= 30) {
        max_score = result.blur_score;
        result.content_type = BUFFER_CONTENT_BLUR;
    }
    if (result.feedback_score > max_score && result.feedback_score >= 30) {
        max_score = result.feedback_score;
        result.content_type = BUFFER_CONTENT_FEEDBACK;
    }
    if (result.edge_score > max_score && result.edge_score >= 30) {
        max_score = result.edge_score;
        result.content_type = BUFFER_CONTENT_EDGE_DETECT;
    }
    if (result.raymarch_score > max_score && result.raymarch_score >= 40) {
        max_score = result.raymarch_score;
        result.content_type = BUFFER_CONTENT_RAYMARCHING;
    }
    
    /* Set recommended scale based on content type */
    result.recommended_scale = buffer_content_default_scale(result.content_type);
    result.update_divisor = buffer_content_default_update_rate(result.content_type);
    
    /* Set resolution limits */
    switch (result.content_type) {
        case BUFFER_CONTENT_NOISE:
            result.min_width = 64;
            result.min_height = 64;
            result.max_width = 512;
            result.max_height = 512;
            break;
        case BUFFER_CONTENT_BLUR:
            result.min_width = 128;
            result.min_height = 128;
            result.max_width = 1024;
            result.max_height = 1024;
            break;
        default:
            result.min_width = 256;
            result.min_height = 256;
            result.max_width = 0;  /* No limit */
            result.max_height = 0;
            break;
    }
    
    /* Feedback buffers should NOT be skipped when static (state accumulates) */
    result.can_skip_when_static = (result.content_type != BUFFER_CONTENT_FEEDBACK &&
                                   result.content_type != BUFFER_CONTENT_SIMULATION);
    
    result.analyzed = true;
    return result;
}

void multipass_optimizer_analyze_shader(multipass_optimizer_t *opt,
                                        const char **pass_sources,
                                        const int *pass_types,
                                        int pass_count,
                                        int image_pass_index) {
    if (!opt || !pass_sources) return;
    
    opt->pass_count = clampi(pass_count, 0, MOPT_MAX_PASSES);
    opt->image_pass_index = image_pass_index;
    
    for (int i = 0; i < opt->pass_count; i++) {
        bool is_image = (i == image_pass_index);
        opt->passes[i] = multipass_optimizer_analyze_source(pass_sources[i], is_image);
        
        /* Stagger update frames for half-rate rendering */
        opt->passes[i].frame_offset = i % 4;
        
        (void)pass_types;  /* Reserved for future use */
    }
    
    log_info("Multipass optimizer: analyzed %d passes", opt->pass_count);
    for (int i = 0; i < opt->pass_count; i++) {
        log_info("  Pass %d: %s (scale=%.0f%%, rate=1/%d)",
                 i, buffer_content_type_name(opt->passes[i].content_type),
                 opt->passes[i].recommended_scale * 100.0f,
                 opt->passes[i].update_divisor);
    }
}

/* ============================================================================
 * Per-Frame Usage
 * ============================================================================ */

void multipass_optimizer_begin_frame(multipass_optimizer_t *opt,
                                     float time,
                                     float mouse_x, float mouse_y,
                                     bool mouse_click) {
    if (!opt) return;
    
    static_detector_t *sd = &opt->static_detect;
    
    /* Check for static scene */
    float time_delta = fabsf(time - sd->last_time);
    float mouse_delta = fabsf(mouse_x - sd->last_mouse_x) + 
                        fabsf(mouse_y - sd->last_mouse_y);
    bool click_changed = (mouse_click != sd->last_mouse_click);
    
    bool is_static = (time_delta < sd->time_epsilon &&
                      mouse_delta < sd->mouse_epsilon &&
                      !click_changed);
    
    if (is_static) {
        sd->consecutive_static_frames++;
        /* Only consider static after a few frames to avoid flicker */
        sd->scene_is_static = (sd->consecutive_static_frames >= 3);
    } else {
        sd->consecutive_static_frames = 0;
        sd->scene_is_static = false;
    }
    
    /* Cap static frames to avoid never rendering */
    if (sd->consecutive_static_frames > sd->max_skip_frames) {
        sd->scene_is_static = false;  /* Force render periodically */
    }
    
    /* Save current state for next frame */
    sd->last_time = time;
    sd->last_mouse_x = mouse_x;
    sd->last_mouse_y = mouse_y;
    sd->last_mouse_click = mouse_click;
}

bool multipass_optimizer_should_render_pass(multipass_optimizer_t *opt, int pass_index) {
    if (!opt || !opt->enabled) return true;
    if (pass_index < 0 || pass_index >= opt->pass_count) return true;
    
    pass_optimization_t *pass = &opt->passes[pass_index];
    
    /* Image pass always renders */
    if (pass->is_image_pass) return true;
    
    /* Check static scene skip */
    if (opt->static_skip_enabled && 
        opt->static_detect.scene_is_static && 
        pass->can_skip_when_static) {
        return false;  /* Skip - scene hasn't changed */
    }
    
    /* Check half-rate update */
    if (opt->half_rate_enabled && pass->update_divisor > 1) {
        int cycle_frame = (int)(opt->frame_number + pass->frame_offset) % pass->update_divisor;
        if (cycle_frame != 0) {
            return false;  /* Not this pass's turn */
        }
    }
    
    return true;
}

void multipass_optimizer_get_pass_resolution(multipass_optimizer_t *opt,
                                             int pass_index,
                                             int base_width, int base_height,
                                             int *out_width, int *out_height) {
    /* Default to base resolution */
    int w = base_width;
    int h = base_height;
    
    if (opt && opt->enabled && opt->smart_resolution_enabled &&
        pass_index >= 0 && pass_index < opt->pass_count) {
        
        pass_optimization_t *pass = &opt->passes[pass_index];
        
        /* Apply scale factor, adjusted by global quality */
        float scale = pass->recommended_scale;
        
        /* Blend towards 1.0 based on quality setting */
        /* quality=0 -> use recommended_scale, quality=1 -> use 1.0 */
        scale = scale + (1.0f - scale) * opt->global_quality * 0.5f;
        
        w = (int)(base_width * scale);
        h = (int)(base_height * scale);
        
        /* Apply min/max constraints */
        if (pass->min_width > 0) w = (w < pass->min_width) ? pass->min_width : w;
        if (pass->min_height > 0) h = (h < pass->min_height) ? pass->min_height : h;
        if (pass->max_width > 0) w = (w > pass->max_width) ? pass->max_width : w;
        if (pass->max_height > 0) h = (h > pass->max_height) ? pass->max_height : h;
        
        /* Ensure dimensions are at least 1 and even (better GPU alignment) */
        w = (w < 2) ? 2 : (w & ~1);
        h = (h < 2) ? 2 : (h & ~1);
    }
    
    if (out_width) *out_width = w;
    if (out_height) *out_height = h;
}

void multipass_optimizer_pass_rendered(multipass_optimizer_t *opt,
                                       int pass_index,
                                       int width, int height) {
    if (!opt) return;
    (void)pass_index;
    
    opt->passes_rendered++;
    
    /* Calculate pixels saved compared to full resolution */
    /* This would need base resolution to calculate properly */
    (void)width;
    (void)height;
}

void multipass_optimizer_pass_skipped(multipass_optimizer_t *opt, int pass_index) {
    if (!opt) return;
    (void)pass_index;
    
    opt->passes_skipped++;
}

void multipass_optimizer_end_frame(multipass_optimizer_t *opt) {
    if (!opt) return;
    
    opt->frame_number++;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

multipass_optimizer_stats_t multipass_optimizer_get_stats(const multipass_optimizer_t *opt) {
    multipass_optimizer_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    if (!opt) return stats;
    
    stats.total_passes_rendered = opt->passes_rendered;
    stats.total_passes_skipped = opt->passes_skipped;
    
    uint64_t total = opt->passes_rendered + opt->passes_skipped;
    if (total > 0) {
        stats.skip_rate_percent = (float)opt->passes_skipped / (float)total * 100.0f;
    }
    
    stats.total_pixels_saved = opt->pixels_saved;
    
    /* Estimate speedup based on skip rate */
    /* Rough: each skipped pass saves ~25% of a frame's work for 4-pass shader */
    float skip_factor = 1.0f - (stats.skip_rate_percent / 100.0f * 0.25f);
    stats.estimated_speedup = 1.0f / skip_factor;
    
    stats.pass_count = opt->pass_count;
    for (int i = 0; i < opt->pass_count && i < MOPT_MAX_PASSES; i++) {
        stats.pass_stats[i].content_type = opt->passes[i].content_type;
        stats.pass_stats[i].scale_used = opt->passes[i].recommended_scale;
        stats.pass_stats[i].update_divisor = opt->passes[i].update_divisor;
    }
    
    return stats;
}

void multipass_optimizer_log_stats(const multipass_optimizer_t *opt) {
    if (!opt) return;
    
    multipass_optimizer_stats_t stats = multipass_optimizer_get_stats(opt);
    
    log_info("=== Multipass Optimizer Stats ===");
    log_info("  Passes rendered: %lu", (unsigned long)stats.total_passes_rendered);
    log_info("  Passes skipped:  %lu (%.1f%%)", 
             (unsigned long)stats.total_passes_skipped, 
             stats.skip_rate_percent);
    log_info("  Estimated speedup: %.2fx", stats.estimated_speedup);
    
    for (int i = 0; i < stats.pass_count; i++) {
        log_info("  Pass %d: %s @ %.0f%% (rate 1/%d)",
                 i, 
                 buffer_content_type_name(stats.pass_stats[i].content_type),
                 stats.pass_stats[i].scale_used * 100.0f,
                 stats.pass_stats[i].update_divisor);
    }
}

/* ============================================================================
 * Workload Feedback Functions (for adaptive_scale integration)
 * ============================================================================ */

void multipass_optimizer_reset_frame_workload(multipass_optimizer_t *opt) {
    if (!opt) return;
    
    opt->workload.passes_rendered_this_frame = 0;
    opt->workload.passes_skipped_this_frame = 0;
    opt->workload.pixels_rendered_this_frame = 0;
    opt->workload.pixels_full_resolution = 0;
}

void multipass_optimizer_record_pass(multipass_optimizer_t *opt,
                                     int pass_index,
                                     int width, int height,
                                     int full_width, int full_height,
                                     bool was_rendered) {
    if (!opt) return;
    (void)pass_index;  /* Reserved for per-pass tracking */
    
    int64_t full_pixels = (int64_t)full_width * full_height;
    int64_t actual_pixels = (int64_t)width * height;
    
    opt->workload.pixels_full_resolution += full_pixels;
    
    if (was_rendered) {
        opt->workload.passes_rendered_this_frame++;
        opt->workload.pixels_rendered_this_frame += actual_pixels;
    } else {
        opt->workload.passes_skipped_this_frame++;
        /* Skipped passes contribute 0 pixels */
    }
    
    /* Update running totals */
    if (was_rendered) {
        opt->pixels_saved += (uint64_t)(full_pixels - actual_pixels);
    } else {
        opt->pixels_saved += (uint64_t)full_pixels;
    }
}

float multipass_optimizer_get_effective_workload(const multipass_optimizer_t *opt) {
    if (!opt) return 1.0f;
    
    /* If no passes recorded, assume full workload */
    if (opt->workload.pixels_full_resolution <= 0) {
        return 1.0f;
    }
    
    /* Effective workload = pixels actually rendered / pixels at full resolution */
    float workload = (float)opt->workload.pixels_rendered_this_frame / 
                     (float)opt->workload.pixels_full_resolution;
    
    /* Clamp to valid range */
    if (workload < 0.0f) workload = 0.0f;
    if (workload > 1.0f) workload = 1.0f;
    
    return workload;
}

float multipass_optimizer_get_pixel_reduction(const multipass_optimizer_t *opt) {
    if (!opt) return 0.0f;
    
    /* If no passes recorded, no reduction */
    if (opt->workload.pixels_full_resolution <= 0) {
        return 0.0f;
    }
    
    /* Reduction = 1 - (actual / full) */
    float reduction = 1.0f - (float)opt->workload.pixels_rendered_this_frame / 
                             (float)opt->workload.pixels_full_resolution;
    
    /* Clamp to valid range */
    if (reduction < 0.0f) reduction = 0.0f;
    if (reduction > 1.0f) reduction = 1.0f;
    
    return reduction;
}