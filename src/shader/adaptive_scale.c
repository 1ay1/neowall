/* Adaptive Resolution Scaling - Industry-Grade Implementation
 * 
 * Techniques from AAA games and engines (Unreal, Unity, DLSS, FSR):
 * - Frame time budget model (ms-based, not FPS - more linear)
 * - Percentile tracking (P95/P99) for worst-case targeting
 * - Ring buffer with spike detection and outlier filtering
 * - Quantized scale levels to reduce texture/buffer allocation churn
 * - Asymmetric hysteresis (fast down, slow up)
 * - Cooldown periods between adjustments
 * - Headroom buffer to absorb frame spikes
 * - Velocity + acceleration prediction (PID-inspired)
 * - Stability scoring with adaptive aggressiveness
 * - Emergency mode for severe performance drops
 * - GPU timer query integration (excludes vsync wait)
 * - Thermal throttling detection (Linux sysfs)
 * - Frame pacing analysis for judder detection
 */

#include "neowall/shader/adaptive_scale.h"
#include "neowall/shader/reactive.h"
#include "neowall/shader/shader_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdio.h>

#ifdef __linux__
#include <dirent.h>
#include <unistd.h>
#endif

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/* Fast median/percentile using partial quickselect */
static int float_compare(const void *a, const void *b) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static float compute_percentile(float *data, int count, float percentile) {
    if (count <= 0) return 0.0f;
    if (count == 1) return data[0];
    
    /* Copy data for sorting */
    float *sorted = malloc(count * sizeof(float));
    if (!sorted) return data[0];
    memcpy(sorted, data, count * sizeof(float));
    qsort(sorted, count, sizeof(float), float_compare);
    
    int index = (int)(percentile * (count - 1));
    if (index >= count) index = count - 1;
    float result = sorted[index];
    free(sorted);
    return result;
}

static float compute_mean(const float *data, int count) {
    if (count <= 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        sum += data[i];
    }
    return sum / (float)count;
}

static float compute_stddev(const float *data, int count, float mean) {
    if (count <= 1) return 0.0f;
    float variance = 0.0f;
    for (int i = 0; i < count; i++) {
        float diff = data[i] - mean;
        variance += diff * diff;
    }
    return sqrtf(variance / (float)(count - 1));
}

static float clampf(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

static float lerpf(float a, float b, float t) {
    return a + t * (b - a);
}

/* Compute quantized scale levels between min and max */
static void compute_quantized_levels(adaptive_state_t *state) {
    float min_s = state->config.min_scale;
    float max_s = state->config.max_scale;
    
    /* Use perceptually uniform spacing (based on area, so sqrt) */
    for (int i = 0; i < ADAPTIVE_SCALE_LEVELS; i++) {
        float t = (float)i / (float)(ADAPTIVE_SCALE_LEVELS - 1);
        /* Square root spacing: more levels at lower scales where it matters more */
        float scale = min_s + (max_s - min_s) * sqrtf(t);
        state->quantized_levels[ADAPTIVE_SCALE_LEVELS - 1 - i] = scale;
    }
    
    /* Ensure exact min/max at endpoints */
    state->quantized_levels[0] = max_s;
    state->quantized_levels[ADAPTIVE_SCALE_LEVELS - 1] = min_s;
}

/* Find closest quantized level */
static int find_closest_level(const adaptive_state_t *state, float scale) {
    int best = 0;
    float best_dist = fabsf(state->quantized_levels[0] - scale);
    
    for (int i = 1; i < ADAPTIVE_SCALE_LEVELS; i++) {
        float dist = fabsf(state->quantized_levels[i] - scale);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

/* Find next level up (higher quality) */
static int find_level_up(int current) {
    if (current <= 0) return 0;
    return current - 1;
}

/* Find next level down (lower quality) */
static int find_level_down(int current) {
    if (current >= ADAPTIVE_SCALE_LEVELS - 1) return ADAPTIVE_SCALE_LEVELS - 1;
    return current + 1;
}

/* ============================================================================
 * Configuration Presets
 * ============================================================================ */

adaptive_config_t adaptive_default_config(void) {
    return adaptive_config_for_mode(ADAPTIVE_MODE_BALANCED);
}

adaptive_config_t adaptive_config_for_mode(adaptive_mode_t mode) {
    adaptive_config_t cfg = {0};
    
    /* Common defaults */
    cfg.target_fps = 60.0f;
    cfg.min_scale = 0.25f;
    cfg.max_scale = 1.0f;
    cfg.spike_sigma = 2.5f;
    cfg.min_samples_for_stats = 8;
    cfg.target_percentile = 0.95f;
    cfg.use_gpu_timing = true;
    cfg.use_thermal_monitoring = true;
    cfg.use_quantized_levels = true;
    cfg.verbose_logging = false;
    cfg.thermal_throttle_temp = 80.0f;
    cfg.thermal_critical_temp = 95.0f;
    cfg.mode = mode;
    
    switch (mode) {
    case ADAPTIVE_MODE_QUALITY:
        /* Prioritize resolution - slow to drop, quick to recover */
        cfg.headroom_factor = 0.92f;
        cfg.cooldown_up_ms = 200.0f;
        cfg.cooldown_down_ms = 500.0f;
        cfg.emergency_threshold = 2.0f;
        cfg.threshold_up = 0.75f;
        cfg.threshold_down = 1.15f;
        cfg.stability_threshold = 2.0f;
        cfg.stable_frames_to_lock = 90;
        break;
        
    case ADAPTIVE_MODE_BALANCED:
        /* Default - balanced response */
        cfg.headroom_factor = 0.88f;
        cfg.cooldown_up_ms = 300.0f;
        cfg.cooldown_down_ms = 150.0f;
        cfg.emergency_threshold = 1.5f;
        cfg.threshold_up = 0.80f;
        cfg.threshold_down = 1.08f;
        cfg.stability_threshold = 1.5f;
        cfg.stable_frames_to_lock = 60;
        break;
        
    case ADAPTIVE_MODE_PERFORMANCE:
        /* Prioritize frame rate - aggressive scaling */
        cfg.headroom_factor = 0.82f;
        cfg.cooldown_up_ms = 500.0f;
        cfg.cooldown_down_ms = 80.0f;
        cfg.emergency_threshold = 1.25f;
        cfg.threshold_up = 0.70f;
        cfg.threshold_down = 1.03f;
        cfg.stability_threshold = 1.0f;
        cfg.stable_frames_to_lock = 45;
        break;
        
    case ADAPTIVE_MODE_BATTERY:
        /* Power saving - very conservative, minimize GPU work */
        cfg.headroom_factor = 0.75f;
        cfg.cooldown_up_ms = 1000.0f;
        cfg.cooldown_down_ms = 50.0f;
        cfg.emergency_threshold = 1.1f;
        cfg.threshold_up = 0.60f;
        cfg.threshold_down = 1.02f;
        cfg.stability_threshold = 3.0f;
        cfg.stable_frames_to_lock = 120;
        cfg.target_percentile = 0.99f;  /* More conservative */
        break;
    }
    
    return cfg;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

void adaptive_init(adaptive_state_t *state, const adaptive_config_t *config) {
    if (!state) return;
    
    memset(state, 0, sizeof(adaptive_state_t));
    
    if (config) {
        state->config = *config;
    } else {
        state->config = adaptive_default_config();
    }
    
    /* Initialize state */
    state->current_scale = state->config.max_scale;
    state->target_scale = state->config.max_scale;
    state->stability_score = 0.5f;
    state->pacing_score = 1.0f;
    state->oscillation_damping = 1.0f;
    state->enabled = true;
    
    /* Compute quantized levels */
    compute_quantized_levels(state);
    state->current_level_index = 0;  /* Start at max quality */
    
    /* Initialize frame time estimate */
    float target_ms = 1000.0f / state->config.target_fps;
    for (int i = 0; i < ADAPTIVE_HISTORY_SIZE; i++) {
        state->frame_times[i] = target_ms;
        state->gpu_times[i] = target_ms * 0.8f;  /* Assume 80% is GPU */
    }
    state->avg_frame_time = target_ms;
    state->p50_frame_time = target_ms;
    state->p95_frame_time = target_ms;
    state->p99_frame_time = target_ms;
    
    state->initialized = true;
}

void adaptive_destroy(adaptive_state_t *state) {
    if (!state) return;
    
    /* Delete GPU timer queries */
    if (state->gpu_timing_available && state->timer_queries[0]) {
        glDeleteQueries(ADAPTIVE_GPU_QUERY_COUNT, state->timer_queries);
    }
    
    memset(state, 0, sizeof(adaptive_state_t));
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void adaptive_set_target_fps(adaptive_state_t *state, float fps) {
    if (!state || fps <= 0) return;
    state->config.target_fps = fps;
    
    /* Reset calibration when target changes */
    state->calibrated = false;
    state->calibration_frames = 0;
    state->calibration_sum = 0.0f;
}

void adaptive_set_mode(adaptive_state_t *state, adaptive_mode_t mode) {
    if (!state) return;
    
    float saved_fps = state->config.target_fps;
    float saved_min = state->config.min_scale;
    float saved_max = state->config.max_scale;
    bool saved_verbose = state->config.verbose_logging;
    
    state->config = adaptive_config_for_mode(mode);
    
    /* Preserve user settings */
    state->config.target_fps = saved_fps;
    state->config.min_scale = saved_min;
    state->config.max_scale = saved_max;
    state->config.verbose_logging = saved_verbose;
    
    compute_quantized_levels(state);
}

void adaptive_set_enabled(adaptive_state_t *state, bool enabled) {
    if (!state) return;
    state->enabled = enabled;
    if (!enabled) {
        state->current_scale = state->config.max_scale;
        state->target_scale = state->config.max_scale;
    }
}

void adaptive_set_scale_range(adaptive_state_t *state, float min_scale, float max_scale) {
    if (!state) return;
    
    state->config.min_scale = clampf(min_scale, 0.1f, 1.0f);
    state->config.max_scale = clampf(max_scale, state->config.min_scale, 2.0f);
    
    compute_quantized_levels(state);
    
    /* Clamp current scale to new range */
    state->current_scale = clampf(state->current_scale, 
                                   state->config.min_scale, 
                                   state->config.max_scale);
    state->target_scale = clampf(state->target_scale,
                                  state->config.min_scale,
                                  state->config.max_scale);
    state->current_level_index = find_closest_level(state, state->current_scale);
}

/* ============================================================================
 * GPU Timing
 * ============================================================================ */

void adaptive_init_gpu_timing(adaptive_state_t *state) {
    if (!state || !state->config.use_gpu_timing) return;
    
    /* GL 3.3 core has timer queries built-in */
    state->gpu_timing_available = true;
    glGenQueries(ADAPTIVE_GPU_QUERY_COUNT, state->timer_queries);
    state->query_write_index = 0;
    state->query_read_index = 0;
    state->queries_in_flight = 0;
    
    if (state->config.verbose_logging) {
        log_info("Adaptive: GPU timing initialized with %d queries", ADAPTIVE_GPU_QUERY_COUNT);
    }
}

void adaptive_begin_frame(adaptive_state_t *state) {
    if (!state || !state->gpu_timing_available) return;
    if (!state->config.use_gpu_timing) return;
    
    /* Start timer query for this frame */
    glBeginQuery(GL_TIME_ELAPSED, state->timer_queries[state->query_write_index]);
}

void adaptive_end_frame(adaptive_state_t *state) {
    if (!state || !state->gpu_timing_available) return;
    if (!state->config.use_gpu_timing) return;
    
    glEndQuery(GL_TIME_ELAPSED);
    
    state->query_write_index = (state->query_write_index + 1) % ADAPTIVE_GPU_QUERY_COUNT;
    if (state->queries_in_flight < ADAPTIVE_GPU_QUERY_COUNT) {
        state->queries_in_flight++;
    }
    
    /* Try to read oldest query result (non-blocking) */
    if (state->queries_in_flight >= 2) {
        GLint available = 0;
        glGetQueryObjectiv(state->timer_queries[state->query_read_index],
                          GL_QUERY_RESULT_AVAILABLE, &available);
        
        if (available) {
            GLuint64 elapsed_ns = 0;
            glGetQueryObjectui64v(state->timer_queries[state->query_read_index],
                                 GL_QUERY_RESULT, &elapsed_ns);
            state->last_gpu_time_ms = (float)elapsed_ns / 1000000.0f;
            state->query_read_index = (state->query_read_index + 1) % ADAPTIVE_GPU_QUERY_COUNT;
            state->queries_in_flight--;
        }
    }
}

/* ============================================================================
 * Thermal Monitoring (Linux)
 * ============================================================================ */

float adaptive_read_gpu_temperature(void) {
    /* Read from the reactive sampler's snapshot instead of sysfs: this runs
     * on the render thread, and a hwmon read can take milliseconds. */
    reactive_snapshot_t snap;
    reactive_get(&snap);
    float t = snap.nv_active && snap.nv_temp_c > 0.0f ? snap.nv_temp_c : snap.gpu_temp_c;
    if (t <= 0.0f) t = snap.cpu_temp_c;
    return t > 0.0f ? t : -1.0f;  /* -1: unavailable */
}

static void update_thermal_state(adaptive_state_t *state, double current_time) {
    if (!state->config.use_thermal_monitoring) return;
    
    /* Only check temperature periodically */
    if (current_time - state->last_thermal_check < ADAPTIVE_THERMAL_INTERVAL) {
        return;
    }
    state->last_thermal_check = current_time;
    
    float temp = adaptive_read_gpu_temperature();
    if (temp < 0) return;
    
    state->gpu_temperature = temp;
    
    bool was_throttling = state->thermal_throttling;
    
    if (temp >= state->config.thermal_critical_temp) {
        /* Critical - force minimum scale */
        state->thermal_throttling = true;
        state->target_scale = state->config.min_scale;
        if (!was_throttling && state->config.verbose_logging) {
            log_info("Adaptive: THERMAL CRITICAL %.0f°C - forcing minimum scale", temp);
        }
    } else if (temp >= state->config.thermal_throttle_temp) {
        /* Throttling - bias towards lower scales */
        state->thermal_throttling = true;
        if (!was_throttling && state->config.verbose_logging) {
            log_info("Adaptive: Thermal throttling at %.0f°C", temp);
        }
    } else if (temp < state->config.thermal_throttle_temp - 5.0f) {
        /* Hysteresis - only stop throttling when 5°C below threshold */
        if (was_throttling && state->config.verbose_logging) {
            log_info("Adaptive: Thermal throttling ended at %.0f°C", temp);
        }
        state->thermal_throttling = false;
    }
}

/* ============================================================================
 * Statistics Computation
 * ============================================================================ */

static void update_statistics(adaptive_state_t *state) {
    int count = state->history_count;
    if (count < state->config.min_samples_for_stats) return;
    
    /* Collect valid samples into temporary array */
    float *samples = malloc(count * sizeof(float));
    if (!samples) return;
    
    for (int i = 0; i < count; i++) {
        int idx = (state->history_index - 1 - i + ADAPTIVE_HISTORY_SIZE) & ADAPTIVE_HISTORY_MASK;
        samples[i] = state->frame_times[idx];
    }
    
    /* Basic statistics */
    state->avg_frame_time = compute_mean(samples, count);
    state->stddev_frame_time = compute_stddev(samples, count, state->avg_frame_time);
    
    /* Spike filtering - remove outliers beyond N sigma */
    float spike_threshold = state->avg_frame_time + state->config.spike_sigma * state->stddev_frame_time;
    int filtered_count = 0;
    for (int i = 0; i < count; i++) {
        if (samples[i] <= spike_threshold) {
            samples[filtered_count++] = samples[i];
        }
    }
    
    if (filtered_count >= state->config.min_samples_for_stats) {
        /* Recompute with filtered data */
        state->avg_frame_time = compute_mean(samples, filtered_count);
        state->stddev_frame_time = compute_stddev(samples, filtered_count, state->avg_frame_time);
        
        /* Percentiles */
        state->p50_frame_time = compute_percentile(samples, filtered_count, 0.50f);
        state->p95_frame_time = compute_percentile(samples, filtered_count, 0.95f);
        state->p99_frame_time = compute_percentile(samples, filtered_count, 0.99f);
        state->min_frame_time = compute_percentile(samples, filtered_count, 0.0f);
        state->max_frame_time = compute_percentile(samples, filtered_count, 1.0f);
    }
    
    free(samples);
    
    /* Frame pacing analysis - compute jitter (variance in inter-frame time) */
    if (count >= 4) {
        float jitter_sum = 0.0f;
        int jitter_count = 0;
        float prev = state->frame_times[(state->history_index - 1 + ADAPTIVE_HISTORY_SIZE) & ADAPTIVE_HISTORY_MASK];
        
        for (int i = 1; i < count && i < 16; i++) {
            int idx = (state->history_index - 1 - i + ADAPTIVE_HISTORY_SIZE) & ADAPTIVE_HISTORY_MASK;
            float curr = state->frame_times[idx];
            float diff = fabsf(curr - prev);
            jitter_sum += diff;
            jitter_count++;
            prev = curr;
        }
        
        state->frame_time_jitter = jitter_count > 0 ? jitter_sum / jitter_count : 0.0f;
        
        /* Pacing score: 1.0 = perfectly consistent, 0.0 = wildly variable */
        float target_ms = 1000.0f / state->config.target_fps;
        float normalized_jitter = state->frame_time_jitter / target_ms;
        state->pacing_score = clampf(1.0f - normalized_jitter * 2.0f, 0.0f, 1.0f);
    }
}

/* ============================================================================
 * Core Update Logic
 * ============================================================================ */

void adaptive_submit_frame_time(adaptive_state_t *state, float frame_time_ms) {
    if (!state || !state->enabled) return;
    
    /* Sanity check */
    if (frame_time_ms < 0.1f || frame_time_ms > 1000.0f) return;
    
    /* Add to ring buffer */
    state->frame_times[state->history_index] = frame_time_ms;
    if (state->last_gpu_time_ms > 0.0f) {
        state->gpu_times[state->history_index] = state->last_gpu_time_ms;
    }
    
    state->history_index = (state->history_index + 1) & ADAPTIVE_HISTORY_MASK;
    if (state->history_count < ADAPTIVE_HISTORY_SIZE) {
        state->history_count++;
    }
    
    state->total_frames++;
}

/* Snap a scale to the nearest quantized level when that mode is on. */
static void set_scale_level(adaptive_state_t *state, float scale) {
    if (state->config.use_quantized_levels) {
        state->current_level_index = find_closest_level(state, scale);
        state->current_scale = state->quantized_levels[state->current_level_index];
    } else {
        state->current_scale = scale;
    }
}

/* Take this frame's time (GPU if available, else wall clock) into history.
 * Returns false when the frame was a gap (resume, stall), not a sample. */
static bool sample_frame(adaptive_state_t *state, double current_time) {
    const adaptive_config_t *cfg = &state->config;
    if (state->last_frame_time > 0.0) {
        float wall_ms = (float)(current_time - state->last_frame_time) * 1000.0f;
        /* GPU time excludes the vsync wait */
        float frame_time_ms =
            (state->gpu_timing_available && cfg->use_gpu_timing && state->last_gpu_time_ms > 0.1f)
                ? state->last_gpu_time_ms : wall_ms;

        /* Startup frames include surface and shader setup, and a parked
         * wallpaper can sit idle for minutes. Fed to the controller those read
         * as a collapse and drop quality for a shader that's actually fine. A
         * frame slower than ADAPTIVE_GAP_MS is a gap, not a slow frame. */
        if (state->warmup_frames < ADAPTIVE_WARMUP_FRAMES) {
            state->warmup_frames++;
        } else if (frame_time_ms > ADAPTIVE_GAP_MS) {
            state->last_frame_time = current_time;   /* keep the clock, drop the sample */
            return false;
        } else {
            adaptive_submit_frame_time(state, frame_time_ms);
        }
    }
    state->last_frame_time = current_time;
    return true;
}

/* First ~250 ms of real samples: jump straight to a sensible scale instead of
 * walking there. Calibrates on real samples only, never on the seeds
 * adaptive_init puts in the history. */
static void calibrate(adaptive_state_t *state, double current_time, float budget_ms) {
    const adaptive_config_t *cfg = &state->config;
    if (state->history_count == 0) return;
    if (state->calibration_start == 0.0) state->calibration_start = current_time;

    state->calibration_frames++;
    int idx = (state->history_index - 1 + ADAPTIVE_HISTORY_SIZE) & ADAPTIVE_HISTORY_MASK;
    state->calibration_sum += state->frame_times[idx];

    double elapsed = current_time - state->calibration_start;
    if (!((elapsed >= 0.25 && state->calibration_frames >= 8) || state->calibration_frames >= 15)) {
        return;
    }
    float avg_ms = state->calibration_sum / (float)state->calibration_frames;
    if (avg_ms > budget_ms) {
        /* render time ~ pixels ~ scale^2, so scale = sqrt(budget/actual), less 10% */
        float optimal = state->current_scale * sqrtf(budget_ms / avg_ms) * 0.9f;
        set_scale_level(state, clampf(optimal, cfg->min_scale, cfg->max_scale));
        state->target_scale = state->current_scale;
        if (cfg->verbose_logging) {
            log_info("Adaptive: Calibrated %.1fms avg (budget %.1fms) -> %.0f%% scale",
                     avg_ms, budget_ms, state->current_scale * 100.0f);
        }
    } else if (cfg->verbose_logging) {
        log_info("Adaptive: Calibrated %.1fms avg (budget %.1fms) -> full resolution OK",
                 avg_ms, budget_ms);
    }
    state->calibrated = true;
}

/* The frame time decisions are made on: the configured percentile. */
static float decision_frame_time(const adaptive_state_t *state) {
    float pct = state->config.target_percentile;
    if (pct >= 0.99f) return state->p99_frame_time;
    if (pct >= 0.95f) return state->p95_frame_time;
    return state->p50_frame_time;
}

/* Smoothed velocity/acceleration of the decision time, per output. Returns
 * the frame time predicted 100 ms ahead, never much better than now. */
static float predict_frame_time(adaptive_state_t *state, double current_time, float decision_ms) {
    if (state->prev_update_time > 0.0) {
        float dt = (float)(current_time - state->prev_update_time);
        if (dt > 0.001f) {
            float new_velocity = (decision_ms - state->prev_decision_ms) / dt;
            state->frame_time_velocity = lerpf(state->frame_time_velocity, new_velocity, 0.2f);
            float new_accel = (state->frame_time_velocity - state->prev_velocity) / dt;
            state->frame_time_accel = lerpf(state->frame_time_accel, new_accel, 0.15f);
            state->prev_velocity = state->frame_time_velocity;
        }
    }
    state->prev_decision_ms = decision_ms;
    state->prev_update_time = current_time;

    float predicted_ms = decision_ms + state->frame_time_velocity * 0.1f;
    return fmaxf(predicted_ms, decision_ms * 0.8f);
}

/* A severe drop skips the normal controller and cuts the scale at once.
 * Returns whether we're (still) in emergency. */
static bool handle_emergency(adaptive_state_t *state, float decision_ms,
                             float target_ms, float budget_ms) {
    const adaptive_config_t *cfg = &state->config;
    bool was_emergency = state->in_emergency;

    if (decision_ms > target_ms * cfg->emergency_threshold) {
        state->in_emergency = true;
        state->emergency_frames++;
        state->total_emergency_triggers++;
        if (!was_emergency) {
            float scale = state->current_scale * sqrtf(budget_ms / decision_ms) * 0.85f;
            scale = clampf(scale, cfg->min_scale, cfg->max_scale);
            if (cfg->use_quantized_levels) {
                /* two levels further down than the estimate */
                int level = find_closest_level(state, scale);
                level = level < ADAPTIVE_SCALE_LEVELS - 2 ? level + 2 : ADAPTIVE_SCALE_LEVELS - 1;
                state->current_level_index = level;
                state->current_scale = state->quantized_levels[level];
            } else {
                state->current_scale = scale;
            }
            state->target_scale = state->current_scale;
            state->is_locked = false;
            if (cfg->verbose_logging) {
                log_info("Adaptive: EMERGENCY! %.1fms >> %.1fms budget -> %.0f%% scale",
                         decision_ms, budget_ms, state->current_scale * 100.0f);
            }
        }
    } else if (decision_ms < target_ms * 0.9f) {
        if (was_emergency && cfg->verbose_logging) {
            log_info("Adaptive: Emergency resolved after %d frames", state->emergency_frames);
        }
        state->in_emergency = false;
        state->emergency_frames = 0;
    }
    return state->in_emergency;
}

/* Count stable / over / under frames; lock after sustained stability, unlock
 * when performance drifts well outside the band. */
static void track_stability(adaptive_state_t *state, float budget_ratio) {
    const adaptive_config_t *cfg = &state->config;
    if (budget_ratio >= cfg->threshold_up && budget_ratio <= cfg->threshold_down) {
        state->consecutive_stable_frames++;
        state->consecutive_over_budget = 0;
        state->consecutive_under_budget = 0;
        state->stability_score = fminf(1.0f, state->stability_score + 0.02f);
        if (state->consecutive_stable_frames >= cfg->stable_frames_to_lock && !state->is_locked) {
            state->is_locked = true;
            state->locked_scale = state->current_scale;
            state->oscillation_count = 0;
            state->oscillation_damping = 1.0f;
            if (cfg->verbose_logging) {
                log_info("Adaptive: LOCKED at %.0f%% (stable for %d frames, score=%.2f)",
                         state->current_scale * 100.0f, state->consecutive_stable_frames,
                         state->stability_score);
            }
        }
        return;
    }

    state->consecutive_stable_frames = 0;
    state->stability_score = fmaxf(0.0f, state->stability_score - 0.05f);
    if (budget_ratio > cfg->threshold_down) {
        state->consecutive_over_budget++;
        state->consecutive_under_budget = 0;
    } else {
        state->consecutive_under_budget++;
        state->consecutive_over_budget = 0;
    }
    if (state->is_locked && (budget_ratio > cfg->threshold_down * 1.1f ||
                             budget_ratio < cfg->threshold_up * 0.9f)) {
        state->is_locked = false;
        if (cfg->verbose_logging) log_info("Adaptive: UNLOCKED (budget_ratio=%.2f)", budget_ratio);
    }
}

/* The scale the controller wants next, and which way that moves (-1/0/+1). */
static int propose_scale(const adaptive_state_t *state, double current_time, float budget_ratio,
                         float budget_ms, float predicted_ms, float *out_scale) {
    const adaptive_config_t *cfg = &state->config;
    double since_up_ms = (current_time - state->last_upscale_time) * 1000.0;
    double since_down_ms = (current_time - state->last_downscale_time) * 1000.0;
    *out_scale = state->current_scale;

    if (budget_ratio > cfg->threshold_down && since_down_ms >= cfg->cooldown_down_ms) {
        /* down: proportional to the predicted overage, damped, harder when hot */
        float factor = sqrtf((predicted_ms - budget_ms) / budget_ms) * state->oscillation_damping;
        if (state->thermal_throttling) factor *= 1.5f;
        if (cfg->use_quantized_levels) {
            int level = find_level_down(state->current_level_index);
            if (factor > 0.3f) level = find_level_down(level);
            if (level == state->current_level_index) return 0;
            *out_scale = state->quantized_levels[level];
            return -1;
        }
        float reduction = clampf(factor * 0.15f, 0.02f, 0.20f);
        *out_scale = state->current_scale * (1.0f - reduction);
        return *out_scale < state->current_scale - 0.01f ? -1 : 0;
    }

    bool may_go_up = budget_ratio < cfg->threshold_up && since_up_ms >= cfg->cooldown_up_ms &&
                     state->current_scale < cfg->max_scale - 0.01f &&
                     !state->thermal_throttling &&
                     state->frame_time_velocity < 1.0f;   /* not getting worse */
    if (!may_go_up) return 0;

    if (cfg->use_quantized_levels) {
        float headroom_ratio = (budget_ms - predicted_ms) / budget_ms;
        if (headroom_ratio <= 0.15f || state->consecutive_under_budget < 10) return 0;
        int level = find_level_up(state->current_level_index);
        if (level == state->current_level_index) return 0;
        *out_scale = state->quantized_levels[level];
        return 1;
    }
    /* continuous: 10% of the way to the theoretical max */
    float theoretical_max = fminf(state->current_scale * sqrtf(budget_ms / predicted_ms),
                                  cfg->max_scale);
    float increase = clampf((theoretical_max - state->current_scale) * 0.10f, 0.01f, 0.05f);
    if (increase <= 0.01f || state->consecutive_under_budget < 5) return 0;
    *out_scale = state->current_scale + increase;
    return 1;
}

/* Take a proposed move, with oscillation damping: flipping direction three
 * times locks the scale where it is. */
static void commit_scale(adaptive_state_t *state, double current_time, int direction,
                         float new_scale, float decision_ms) {
    const adaptive_config_t *cfg = &state->config;
    if (state->last_direction != 0 && state->last_direction != direction) {
        state->oscillation_count++;
        state->oscillation_damping = fmaxf(state->oscillation_damping * 0.7f, 0.3f);
        if (state->oscillation_count >= 3) {
            state->is_locked = true;
            state->locked_scale = state->current_scale;
            state->oscillation_count = 0;
            if (cfg->verbose_logging) {
                log_info("Adaptive: LOCKED at %.0f%% (oscillation damping)",
                         state->current_scale * 100.0f);
            }
            return;
        }
    } else {
        if (state->oscillation_count > 0) state->oscillation_count--;
        state->oscillation_damping = fminf(1.0f, state->oscillation_damping + 0.05f);
    }
    state->last_direction = direction;

    new_scale = clampf(new_scale, cfg->min_scale, cfg->max_scale);
    if (fabsf(new_scale - state->target_scale) <= 0.005f) return;

    state->target_scale = new_scale;
    if (direction > 0) {
        state->last_upscale_time = current_time;
        state->total_upscales++;
    } else {
        state->last_downscale_time = current_time;
        state->total_downscales++;
    }
    state->last_any_scale_time = current_time;
    if (cfg->use_quantized_levels) state->current_level_index = find_closest_level(state, new_scale);
    if (cfg->verbose_logging) {
        log_info("Adaptive: %.1fms (P%d) %s -> %.0f%% (vel=%.1f, damp=%.2f)",
                 decision_ms, (int)(cfg->target_percentile * 100), direction > 0 ? "UP" : "DOWN",
                 new_scale * 100.0f, state->frame_time_velocity, state->oscillation_damping);
    }
}

/* Ease current_scale toward target_scale: fast down, slow up. */
static void approach_target(adaptive_state_t *state) {
    const adaptive_config_t *cfg = &state->config;
    float diff = state->target_scale - state->current_scale;
    float abs_diff = fabsf(diff);
    if (abs_diff > 0.001f) {
        float rate = diff < 0 ? (state->in_emergency ? 0.6f : 0.25f) : 0.12f;
        state->current_scale += diff * rate;
        if (cfg->use_quantized_levels && abs_diff < 0.02f) {
            state->current_scale = state->quantized_levels[state->current_level_index];
        }
    } else {
        state->current_scale = state->target_scale;
    }
    state->current_scale = clampf(state->current_scale, cfg->min_scale, cfg->max_scale);
}

/* One controller step per frame: sample, (calibrate), decide, ease. */
void adaptive_update(adaptive_state_t *state, double current_time) {
    if (!state || !state->initialized || !state->enabled) return;

    const adaptive_config_t *cfg = &state->config;
    float target_ms = 1000.0f / cfg->target_fps;
    float budget_ms = target_ms * cfg->headroom_factor;

    if (!sample_frame(state, current_time)) return;
    if (!state->calibrated) {
        calibrate(state, current_time, budget_ms);
        return;
    }

    update_statistics(state);
    update_thermal_state(state, current_time);
    /* No decisions until the percentiles come from real frames, not seeds. */
    if (state->history_count < cfg->min_samples_for_stats) return;

    float decision_ms = decision_frame_time(state);
    float predicted_ms = predict_frame_time(state, current_time, decision_ms);

    if (!handle_emergency(state, decision_ms, target_ms, budget_ms)) {
        float budget_ratio = decision_ms / budget_ms;
        track_stability(state, budget_ratio);
        if (state->is_locked) {
            state->target_scale = state->locked_scale;
        } else {
            float new_scale;
            int direction = propose_scale(state, current_time, budget_ratio, budget_ms,
                                          predicted_ms, &new_scale);
            if (direction != 0) commit_scale(state, current_time, direction, new_scale, decision_ms);
        }
    }
    approach_target(state);
}

/* ============================================================================
 * Query Functions
 * ============================================================================ */

float adaptive_get_scale(const adaptive_state_t *state) {
    return state ? state->current_scale : 1.0f;
}

float adaptive_get_target_scale(const adaptive_state_t *state) {
    return state ? state->target_scale : 1.0f;
}

float adaptive_get_current_fps(const adaptive_state_t *state) {
    if (!state || state->avg_frame_time <= 0.0f) return 0.0f;
    return 1000.0f / state->avg_frame_time;
}

bool adaptive_is_stable(const adaptive_state_t *state) {
    return state ? state->is_locked : false;
}

adaptive_stats_t adaptive_get_stats(const adaptive_state_t *state) {
    adaptive_stats_t stats = {0};
    if (!state) return stats;
    
    stats.current_fps = adaptive_get_current_fps(state);
    stats.current_scale = state->current_scale;
    stats.current_scale_percent = state->current_scale * 100.0f;
    stats.current_level = state->current_level_index;
    
    stats.avg_frame_time_ms = state->avg_frame_time;
    stats.p95_frame_time_ms = state->p95_frame_time;
    stats.gpu_frame_time_ms = state->last_gpu_time_ms;
    stats.target_frame_time_ms = 1000.0f / state->config.target_fps;
    stats.headroom_ms = stats.target_frame_time_ms * state->config.headroom_factor - stats.avg_frame_time_ms;
    
    stats.is_locked = state->is_locked;
    stats.is_emergency = state->in_emergency;
    stats.is_thermal_throttling = state->thermal_throttling;
    stats.gpu_timing_active = state->gpu_timing_available && state->config.use_gpu_timing;
    
    stats.stability_score = state->stability_score;
    stats.pacing_score = state->pacing_score;
    
    stats.frame_time_velocity = state->frame_time_velocity;
    stats.frame_time_accel = state->frame_time_accel;
    
    stats.total_frames = state->total_frames;
    stats.upscale_count = state->total_upscales;
    stats.downscale_count = state->total_downscales;
    stats.emergency_count = state->total_emergency_triggers;
    
    return stats;
}

void adaptive_force_scale(adaptive_state_t *state, float scale) {
    if (!state) return;
    
    scale = clampf(scale, state->config.min_scale, state->config.max_scale);
    state->current_scale = scale;
    state->target_scale = scale;
    state->is_locked = true;
    state->locked_scale = scale;
    
    if (state->config.use_quantized_levels) {
        state->current_level_index = find_closest_level(state, scale);
    }
}

