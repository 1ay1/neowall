#ifndef RELOAD_METRICS_H
#define RELOAD_METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Configuration reload metrics for monitoring and debugging */
typedef struct {
    /* Counters */
    uint64_t total_reloads_attempted;
    uint64_t total_reloads_succeeded;
    uint64_t total_reloads_failed;
    uint64_t total_reloads_throttled;
    uint64_t total_changes_detected;
    uint64_t total_changes_ignored;  /* debounce, empty file, etc. */
    
    /* Timing statistics */
    uint64_t last_reload_start_time_ms;
    uint64_t last_reload_duration_ms;
    uint64_t fastest_reload_ms;
    uint64_t slowest_reload_ms;
    uint64_t average_reload_ms;
    
    /* Error tracking */
    uint64_t file_not_found_errors;
    uint64_t permission_errors;
    uint64_t parse_errors;
    uint64_t deadlock_preventions;
    uint64_t concurrent_reload_preventions;
    
    /* Debouncing metrics */
    uint64_t debounce_hits;  /* Changes that disappeared after debounce */
    uint64_t debounce_passes; /* Changes that survived debounce */
    
    /* File system anomalies */
    uint64_t empty_file_detections;
    uint64_t oversized_file_detections;
    uint64_t invalid_file_type_detections;
    uint64_t file_disappeared_during_read;
    
    /* Recovery metrics */
    uint64_t rollbacks_performed;
    uint64_t rollbacks_succeeded;
    uint64_t rollbacks_failed;
    
    /* Timestamps */
    time_t first_reload_timestamp;
    time_t last_successful_reload_timestamp;
    time_t last_failed_reload_timestamp;
    
    /* Configuration state */
    char last_loaded_path[256];
    time_t last_loaded_mtime;
    size_t last_loaded_size;
    
} reload_metrics_t;

/* Initialize reload metrics */
void reload_metrics_init(reload_metrics_t *metrics);

/* Update metrics on reload attempt */
void reload_metrics_record_attempt(reload_metrics_t *metrics);

/* Update metrics on reload completion */
void reload_metrics_record_result(reload_metrics_t *metrics, bool success, uint64_t duration_ms);

/* Update metrics on reload throttling */
void reload_metrics_record_throttle(reload_metrics_t *metrics);

/* Update metrics on file system anomaly */
void reload_metrics_record_anomaly(reload_metrics_t *metrics, const char *anomaly_type);

/* Update metrics on debounce */
void reload_metrics_record_debounce(reload_metrics_t *metrics, bool survived);

/* Update metrics on rollback */
void reload_metrics_record_rollback(reload_metrics_t *metrics, bool success);

/* Print metrics summary (for debugging) */
void reload_metrics_print(const reload_metrics_t *metrics);

/* Reset metrics (for testing) */
void reload_metrics_reset(reload_metrics_t *metrics);

/* Check if reload performance is degrading */
bool reload_metrics_is_slow(const reload_metrics_t *metrics);

/* Check if reload is unstable (many failures) */
bool reload_metrics_is_unstable(const reload_metrics_t *metrics);

#endif /* RELOAD_METRICS_H */