#ifndef CONFIG_ACCESS_H
#define CONFIG_ACCESS_H

#include "neowall.h"

/* ============================================================================
 * SAFE CONFIG ACCESS MACROS
 * ============================================================================
 * 
 * These macros provide race-free access to the double-buffered config.
 * They ensure that:
 * 1. The correct (active) config slot is read atomically
 * 2. The slot is locked during access
 * 3. The config pointer remains valid for the entire critical section
 * 
 * USAGE PATTERN:
 * 
 *   WITH_ACTIVE_CONFIG(output, cfg) {
 *       if (cfg->type == WALLPAPER_SHADER) {
 *           // Safe to use cfg->shader_path, cfg->duration, etc.
 *           shader_load(cfg->shader_path);
 *       }
 *   } END_CONFIG_ACCESS(output);
 * 
 * IMPORTANT: Do NOT return, break, continue, or goto out of the block
 * without calling END_CONFIG_ACCESS first, or you'll deadlock!
 * ============================================================================ */

/**
 * Begin safe config access for reading.
 * Loads the active slot atomically, locks it, and provides a pointer to the config.
 * 
 * @param output  The output_state to access config from
 * @param cfg_ptr Name of the pointer variable to create (struct wallpaper_config *)
 * 
 * The macro creates a scoped block where cfg_ptr points to the active config.
 * You MUST call END_CONFIG_ACCESS(output) to unlock.
 */
#define WITH_ACTIVE_CONFIG(output, cfg_ptr) \
    do { \
        int __config_slot = atomic_load_explicit(&(output)->active_slot, memory_order_acquire); \
        pthread_mutex_lock(&(output)->config_slots[__config_slot].lock); \
        struct wallpaper_config *cfg_ptr = &(output)->config_slots[__config_slot].config; \
        if ((output)->config_slots[__config_slot].valid) {

/**
 * End safe config access.
 * Unlocks the config slot mutex.
 * 
 * @param output The output_state that was passed to WITH_ACTIVE_CONFIG
 */
#define END_CONFIG_ACCESS(output) \
        } \
        pthread_mutex_unlock(&(output)->config_slots[__config_slot].lock); \
    } while(0)

/**
 * Begin safe config access for writing (used during reload).
 * Locks the INACTIVE slot for writing the new config.
 * 
 * @param output    The output_state to access config from
 * @param cfg_ptr   Name of the pointer variable to create
 * @param slot_var  Name of the variable to store inactive slot index
 * 
 * Usage:
 *   WITH_INACTIVE_CONFIG(output, cfg, slot) {
 *       parse_config_into(cfg);
 *       output->config_slots[slot].valid = true;
 *   } END_INACTIVE_CONFIG_ACCESS(output);
 */
#define WITH_INACTIVE_CONFIG(output, cfg_ptr, slot_var) \
    do { \
        int __active = atomic_load_explicit(&(output)->active_slot, memory_order_acquire); \
        int slot_var = 1 - __active; \
        pthread_mutex_lock(&(output)->config_slots[slot_var].lock); \
        struct wallpaper_config *cfg_ptr = &(output)->config_slots[slot_var].config;

/**
 * End inactive config access and unlock.
 * Does NOT swap slots - caller must do that with SWAP_CONFIG_SLOT.
 * 
 * @param output The output_state that was passed to WITH_INACTIVE_CONFIG
 */
#define END_INACTIVE_CONFIG_ACCESS(output) \
        pthread_mutex_unlock(&(output)->config_slots[slot_var].lock); \
    } while(0)

/**
 * Atomically swap the active config slot.
 * This makes the newly-written config visible to the render thread.
 * 
 * @param output      The output_state to swap
 * @param new_slot    The slot index to make active (0 or 1)
 * 
 * This is a single atomic store with release semantics, ensuring:
 * 1. All writes to the new config are visible before the swap
 * 2. The swap is indivisible (no partial writes)
 * 3. The render thread sees either old or new config, never mixed
 */
#define SWAP_CONFIG_SLOT(output, new_slot) \
    do { \
        atomic_store_explicit(&(output)->active_slot, (new_slot), memory_order_release); \
        sync_config_pointer(output); \
    } while(0)

/**
 * Get the active slot index without locking (for logging/debugging only).
 * DO NOT use this for actual config access - use WITH_ACTIVE_CONFIG instead.
 * 
 * @param output The output_state to query
 * @return The active slot index (0 or 1)
 */
#define GET_ACTIVE_SLOT(output) \
    atomic_load_explicit(&(output)->active_slot, memory_order_acquire)

/**
 * Get the inactive slot index without locking (for reload logic).
 * 
 * @param output The output_state to query
 * @return The inactive slot index (0 or 1)
 */
#define GET_INACTIVE_SLOT(output) \
    (1 - atomic_load_explicit(&(output)->active_slot, memory_order_acquire))

/* ============================================================================
 * BACKWARD COMPATIBILITY WRAPPER
 * ============================================================================
 * 
 * For gradual migration, provide a pseudo-field that looks like the old
 * output->config but actually accesses the active slot.
 * 
 * WARNING: This is NOT thread-safe and should only be used for quick fixes.
 * Proper code should use WITH_ACTIVE_CONFIG/END_CONFIG_ACCESS.
 * ============================================================================ */

/**
 * Get a pointer to the active config (UNSAFE - no locking).
 * Use only for initialization or when you already hold the lock.
 * 
 * @param output The output_state to get config from
 * @return Pointer to the active config struct
 */
static inline struct wallpaper_config* get_active_config_unsafe(struct output_state *output) {
    int slot = atomic_load_explicit(&output->active_slot, memory_order_acquire);
    return &output->config_slots[slot].config;
}

/**
 * Synchronize the config pointer to always point to the active slot.
 * Call this after swapping slots to maintain backward compatibility.
 * This allows existing code using output->config to work without changes.
 * 
 * @param output The output_state to update
 */
static inline void sync_config_pointer(struct output_state *output) {
    int slot = atomic_load_explicit(&output->active_slot, memory_order_acquire);
    output->config = &output->config_slots[slot].config;
}

/**
 * Get the inactive slot index (for writing new config during reload).
 * 
 * @param output The output_state to query
 * @return The inactive slot index (0 or 1)
 */
static inline int get_inactive_slot(struct output_state *output) {
    int active = atomic_load_explicit(&output->active_slot, memory_order_acquire);
    return 1 - active;
}

/**
 * Atomically swap the active config slot and sync the config pointer.
 * This makes the newly-written config visible to all threads.
 * 
 * @param output The output_state to swap
 * @param new_slot The slot index to make active (0 or 1)
 */
static inline void swap_config_slot(struct output_state *output, int new_slot) {
    atomic_store_explicit(&output->active_slot, new_slot, memory_order_release);
    sync_config_pointer(output);
}

/**
 * COMPATIBILITY MACRO: Redirect output->config to active slot
 * This allows existing code to work without modification.
 * 
 * Usage: Add #define config (*get_active_config_unsafe(output))
 * in functions that need backward compatibility.
 * 
 * Better approach: Use WITH_ACTIVE_CONFIG/END_CONFIG_ACCESS for thread safety.
 */
#define CONFIG_COMPAT_DEFINE(output) \
    struct wallpaper_config *__compat_config = get_active_config_unsafe(output)

#define CONFIG_COMPAT_PTR __compat_config

#endif /* CONFIG_ACCESS_H */
