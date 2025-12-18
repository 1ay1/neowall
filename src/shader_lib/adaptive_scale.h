/* Adaptive Resolution Scaling - Industry-Grade Implementation
 * 
 * Features:
 * - Frame time budget model (works in ms, not FPS - more linear)
 * - Percentile tracking (P95/P99) - targets worst-case, not average
 * - Ring buffer history with spike detection and outlier filtering
 * - Quantized scale levels to reduce texture/buffer churn
 * - Asymmetric hysteresis (fast down, slow up)
 * - Cooldown periods between adjustments
 * - Headroom buffer to absorb spikes
 * - Velocity + acceleration prediction
 * - Stability scoring for adaptive aggressiveness
 * - Emergency mode for severe performance drops
 * - GPU timer query integration
 * - Thermal throttling detection (Linux)
 * - Frame pacing analysis for judder detection
 */

#ifndef ADAPTIVE_SCALE_H
#define ADAPTIVE_SCALE_H

#include <stdbool.h>
#include <stdint.h>
#include "platform_compat.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Ring buffer size for frame history - must be power of 2 */
#define ADAPTIVE_HISTORY_SIZE 64
#define ADAPTIVE_HISTORY_MASK (ADAPTIVE_HISTORY_SIZE - 1)

/* Number of quantized scale levels */
#define ADAPTIVE_SCALE_LEVELS 8

/* GPU query double-buffer count */
#define ADAPTIVE_GPU_QUERY_COUNT 3

/* Thermal monitoring update interval (seconds) */
#define ADAPTIVE_THERMAL_INTERVAL 2.0

typedef enum {
    ADAPTIVE_MODE_QUALITY,      /* Prioritize resolution, slower scaling */
    ADAPTIVE_MODE_BALANCED,     /* Default - balance quality and performance */
    ADAPTIVE_MODE_PERFORMANCE,  /* Prioritize frame rate, aggressive scaling */
    ADAPTIVE_MODE_BATTERY       /* Ultra-conservative for power saving */
} adaptive_mode_t;

typedef struct {
    /* Target frame time budget */
    float target_fps;               /* Target FPS (converted to frame budget internally) */
    float headroom_factor;          /* Target % of budget to leave as headroom (0.85-0.95) */
    
    /* Scale limits */
    float min_scale;                /* Minimum scale (e.g., 0.25 = 25%) */
    float max_scale;                /* Maximum scale (e.g., 1.0 = 100%) */
    
    /* Timing */
    float cooldown_up_ms;           /* Minimum ms between upscales */
    float cooldown_down_ms;         /* Minimum ms between downscales */
    float emergency_threshold;      /* Frame time multiple that triggers emergency mode */
    
    /* Hysteresis thresholds (as % of target frame time) */
    float threshold_up;             /* Must be this % under budget to upscale (e.g., 0.80) */
    float threshold_down;           /* Must be this % over budget to downscale (e.g., 1.05) */
    
    /* Spike filtering */
    float spike_sigma;              /* Standard deviations to consider a spike (e.g., 2.5) */
    int min_samples_for_stats;      /* Minimum samples before statistical analysis */
    
    /* Stability */
    float stability_threshold;      /* Seconds of stability before locking */
    int stable_frames_to_lock;      /* Consecutive stable frames to lock scale */
    
    /* Percentile targeting */
    float target_percentile;        /* Which percentile to target (0.95 = P95) */
    
    /* Features */
    bool use_gpu_timing;            /* Use GL timer queries */
    bool use_thermal_monitoring;    /* Monitor GPU temperature */
    bool use_quantized_levels;      /* Snap to discrete scale levels */
    bool verbose_logging;           /* Debug logging */
    
    /* Thermal limits */
    float thermal_throttle_temp;    /* Temperature to start throttling (Celsius) */
    float thermal_critical_temp;    /* Temperature to force minimum scale */
    
    /* Preset mode */
    adaptive_mode_t mode;
} adaptive_config_t;

/* ============================================================================
 * Runtime State
 * ============================================================================ */

typedef struct {
    /* Frame time history (ring buffer) */
    float frame_times[ADAPTIVE_HISTORY_SIZE];   /* Frame times in ms */
    float gpu_times[ADAPTIVE_HISTORY_SIZE];     /* GPU-only times in ms */
    int history_index;                          /* Current write position */
    int history_count;                          /* Number of valid samples */
    
    /* Statistics (updated each frame) */
    float avg_frame_time;           /* Moving average frame time */
    float p50_frame_time;           /* Median (P50) */
    float p95_frame_time;           /* 95th percentile */
    float p99_frame_time;           /* 99th percentile */
    float min_frame_time;           /* Minimum in window */
    float max_frame_time;           /* Maximum in window */
    float stddev_frame_time;        /* Standard deviation */
    float frame_time_velocity;      /* Rate of change (ms/second) */
    float frame_time_accel;         /* Acceleration (ms/second²) */
    
    /* Current state */
    float current_scale;            /* Current resolution scale */
    float target_scale;             /* Target scale (interpolating towards) */
    int current_level_index;        /* Index in quantized levels array */
    float quantized_levels[ADAPTIVE_SCALE_LEVELS];  /* Precomputed scale levels */
    
    /* Timing */
    double last_frame_time;         /* Wall clock of last frame */
    double last_upscale_time;       /* Time of last upscale */
    double last_downscale_time;     /* Time of last downscale */
    double last_any_scale_time;     /* Time of any scale change */
    
    /* Stability tracking */
    int consecutive_stable_frames;  /* Frames within target range */
    int consecutive_over_budget;    /* Frames over budget */
    int consecutive_under_budget;   /* Frames under budget */
    float stability_score;          /* 0.0 = volatile, 1.0 = rock solid */
    bool is_locked;                 /* Scale locked due to stability */
    float locked_scale;             /* Scale value when locked */
    
    /* Oscillation detection */
    int oscillation_count;          /* Direction reversals */
    int last_direction;             /* -1 = down, 0 = none, 1 = up */
    float oscillation_damping;      /* Reduces adjustment magnitude */
    
    /* Emergency state */
    bool in_emergency;              /* Currently in emergency mode */
    int emergency_frames;           /* Frames spent in emergency */
    
    /* GPU timing */
    GLuint timer_queries[ADAPTIVE_GPU_QUERY_COUNT];
    int query_write_index;          /* Next query to write */
    int query_read_index;           /* Next query to read */
    int queries_in_flight;          /* Number of pending queries */
    bool gpu_timing_available;      /* Extension supported */
    float last_gpu_time_ms;         /* Most recent GPU time */
    
    /* Thermal state */
    float gpu_temperature;          /* Current GPU temp (Celsius) */
    double last_thermal_check;      /* Time of last temp reading */
    bool thermal_throttling;        /* Currently throttling due to temp */
    
    /* Frame pacing */
    float frame_time_jitter;        /* Variance in frame delivery */
    float pacing_score;             /* 0.0 = erratic, 1.0 = perfectly paced */
    int dropped_frames;             /* Detected dropped frames */
    
    /* Calibration */
    bool calibrated;                /* Initial calibration complete */
    int calibration_frames;         /* Frames during calibration */
    float calibration_sum;          /* Sum of frame times */
    double calibration_start;       /* Start time */
    
    /* Configuration */
    adaptive_config_t config;
    bool enabled;
    bool initialized;
    
    /* Debug/stats */
    uint64_t total_frames;
    uint64_t total_upscales;
    uint64_t total_downscales;
    uint64_t total_emergency_triggers;
} adaptive_state_t;

/* ============================================================================
 * Statistics Output
 * ============================================================================ */

typedef struct {
    /* Current state */
    float current_fps;
    float current_scale;
    float current_scale_percent;    /* 0-100 */
    int current_level;              /* Quantized level index */
    
    /* Frame timing */
    float avg_frame_time_ms;
    float p95_frame_time_ms;
    float gpu_frame_time_ms;
    float target_frame_time_ms;
    float headroom_ms;              /* How much budget headroom we have */
    
    /* Status */
    bool is_locked;
    bool is_emergency;
    bool is_thermal_throttling;
    bool gpu_timing_active;
    
    /* Scores */
    float stability_score;          /* 0.0-1.0 */
    float pacing_score;             /* 0.0-1.0 */
    
    /* Trends */
    float frame_time_velocity;      /* Positive = getting slower */
    float frame_time_accel;
    
    /* Lifetime stats */
    uint64_t total_frames;
    uint64_t upscale_count;
    uint64_t downscale_count;
    uint64_t emergency_count;
} adaptive_stats_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/* Lifecycle */
void adaptive_init(adaptive_state_t *state, const adaptive_config_t *config);
void adaptive_destroy(adaptive_state_t *state);
void adaptive_reset(adaptive_state_t *state);

/* Configuration */
adaptive_config_t adaptive_default_config(void);
adaptive_config_t adaptive_config_for_mode(adaptive_mode_t mode);
void adaptive_set_target_fps(adaptive_state_t *state, float fps);
void adaptive_set_mode(adaptive_state_t *state, adaptive_mode_t mode);
void adaptive_set_enabled(adaptive_state_t *state, bool enabled);
void adaptive_set_scale_range(adaptive_state_t *state, float min_scale, float max_scale);

/* GPU Timing Integration */
void adaptive_init_gpu_timing(adaptive_state_t *state);
void adaptive_begin_frame(adaptive_state_t *state);
void adaptive_end_frame(adaptive_state_t *state);

/* Per-frame update - call once per frame with wall clock time */
void adaptive_update(adaptive_state_t *state, double current_time);

/* Manual frame time submission (if not using GPU timing) */
void adaptive_submit_frame_time(adaptive_state_t *state, float frame_time_ms);

/* Query state */
float adaptive_get_scale(const adaptive_state_t *state);
float adaptive_get_target_scale(const adaptive_state_t *state);
float adaptive_get_current_fps(const adaptive_state_t *state);
bool adaptive_is_stable(const adaptive_state_t *state);
bool adaptive_needs_resize(const adaptive_state_t *state);
adaptive_stats_t adaptive_get_stats(const adaptive_state_t *state);

/* Force scale (for debugging/override) */
void adaptive_force_scale(adaptive_state_t *state, float scale);
void adaptive_unlock(adaptive_state_t *state);

/* Thermal monitoring (Linux-specific) */
float adaptive_read_gpu_temperature(void);

#endif /* ADAPTIVE_SCALE_H */