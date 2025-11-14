#ifndef CONFIG_ACCESS_H
#define CONFIG_ACCESS_H

#include "neowall.h"

/* ============================================================================
 * CONFIG ACCESS - Simplified (No Hot-Reload)
 * ============================================================================
 * 
 * With hot-reload removed, config access is now straightforward:
 * - Each output has a single config pointer allocated at creation
 * - No double-buffering, no slot swapping, no complex synchronization
 * - Config can only be changed by restarting the daemon
 * 
 * USAGE:
 *   output->config->type
 *   output->config->shader_path
 *   etc.
 * ============================================================================ */

#endif /* CONFIG_ACCESS_H */
