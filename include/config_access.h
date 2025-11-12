#ifndef CONFIG_ACCESS_H
#define CONFIG_ACCESS_H

#include "neowall.h"

/* ============================================================================
 * SIMPLIFIED CONFIG ACCESS
 * ============================================================================
 * 
 * Since hot-reload has been removed, we now use simple direct config access.
 * No double-buffering needed - just restart the daemon to apply config changes.
 * 
 * USAGE:
 * 
 *   if (output->config.type == WALLPAPER_SHADER) {
 *       shader_load(output->config.shader_path);
 *   }
 * 
 * For thread-safety when accessing the output list, use the output_list_lock
 * from the neowall_state:
 * 
 *   pthread_rwlock_rdlock(&state->output_list_lock);
 *   struct output_state *output = state->outputs;
 *   while (output) {
 *       // Access output->config safely here
 *       output = output->next;
 *   }
 *   pthread_rwlock_unlock(&state->output_list_lock);
 * 
 * ============================================================================ */

/**
 * Get pointer to the output's config (direct access, no locking needed)
 * 
 * @param output The output_state to get config from
 * @return Pointer to the output's config
 */
static inline struct wallpaper_config* get_config(struct output_state *output) {
    return &output->config;
}

#endif /* CONFIG_ACCESS_H */