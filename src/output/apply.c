/* Applying a config to an output, including staged reloads: a shader swap
 * builds in the background and its config goes live with it. */

#include <stdio.h>
#include "neowall/compositor/compositor.h"
#include "neowall/config/config_access.h"
#include "output_internal.h"
#include "neowall/config/config.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/state_file.h"

/* Find output by name */

/* Find output by model string */

/* Apply wallpaper configuration to an output */
/* Apply wallpaper configuration to an output */
/* ── staged reloads ─────────────────────────────────────────────────── */

/* Make a staged config live: its shader has just been installed. */
void output_commit_staged(struct output_state *output, struct wallpaper_config *cfg) {
    struct wallpaper_config *old = output->config;
    output->config = cfg;
    atomic_fetch_add(&output->preprocessing_generation, 1);
    output_configure_vsync(output);
    output_configure_frame_timer(output);
    output->last_cycle_time = get_time_ms();
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    log_info("Successfully applied config to output %s", output->model);
    config_free_wallpaper(old);
    free(old);
}

/* Drop a staged config whose shader won't arrive (it failed, or a newer
 * request replaced it). The live config never changed. */
void output_unstage(struct output_state *output) {
    if (!output->staged_config) return;
    config_free_wallpaper(output->staged_config);
    free(output->staged_config);
    output->staged_config = NULL;
    output->stage_failed = true;
}

bool output_reload_pending(const struct output_state *output) {
    return output && output->staged_config != NULL;
}

bool output_reload_failed(const struct output_state *output) {
    return output && output->stage_failed;
}

void output_reload_forget(struct output_state *output) {
    if (output) output->stage_failed = false;
}

/* Resume a cycle where this output left off (from the state file) and
 * publish the list for `neowall list`. */
static void restore_cycle_position(struct output_state *output, struct wallpaper_config *cfg) {
    if (cfg->cycle && cfg->cycle_count > 0) {
        /* Restore cycle index from state file for this specific output.
         * Skipped under shuffle: the saved index refers to a different
         * (previous) random permutation of the directory, so resuming at
         * that slot would just point to an arbitrary file (issue #47). */
        if (!cfg->shuffle) {
            const char *output_id = output_get_identifier(output);
            int saved_index = restore_cycle_index_from_state(output_id);
            if (saved_index >= 0 && saved_index < (int)cfg->cycle_count) {
                cfg->current_cycle_index = saved_index;
                log_info("Restored cycle position for %s: %d/%zu",
                        output_id, saved_index, cfg->cycle_count);

                /* Update the initial path to use the restored index */
                if (cfg->type == WALLPAPER_SHADER) {
                    snprintf(cfg->shader_path,
                             sizeof(cfg->shader_path), "%s",
                             cfg->cycle_paths[saved_index]);
                } else {
                    snprintf(cfg->path,
                             sizeof(cfg->path), "%s",
                             cfg->cycle_paths[saved_index]);
                }
            }
        }

        /* Write cycle list file for 'neowall list' command */
        write_cycle_list(output_get_identifier(output),
                        cfg->cycle_paths,
                        cfg->cycle_count,
                        cfg->current_cycle_index);
    }
}

bool output_apply_config(struct output_state *output, struct wallpaper_config *config) {
    if (!output || !config) {
        log_error("Invalid parameters for output_apply_config");
        return false;
    }

    log_debug("Applying config to output %s (compositor_surface=%p, configured=%d)",
              output->model[0] ? output->model : "unknown",
              (void*)output->compositor_surface,
              output->configured);

    log_info("Config for output %s: type=%s, mode=%s, transition=%d, duration=%.2fs",
             output->model[0] ? output->model : "unknown",
             config->type == WALLPAPER_SHADER ? "shader" : "image",
             wallpaper_mode_to_string(config->mode),
             config->transition,
             config->duration);

    /* Build a complete owned copy before touching the live config. */
    struct wallpaper_config *copy = calloc(1, sizeof(*copy));
    if (!copy) {
        return false;
    }
    memcpy(copy, config, sizeof(*copy));
    copy->channel_paths = NULL;
    copy->channel_count = 0;
    copy->cycle_paths = NULL;
    copy->cycle_count = 0;
    if (config->channel_paths && config->channel_count > 0) {
        copy->channel_paths = calloc(config->channel_count, sizeof(char *));
        if (!copy->channel_paths) goto copy_failed;
        copy->channel_count = config->channel_count;
        for (size_t i = 0; i < config->channel_count; i++) {
            if (config->channel_paths[i] &&
                !(copy->channel_paths[i] = strdup(config->channel_paths[i]))) {
                goto copy_failed;
            }
        }
    }

    /* Deep copy cycle_paths array if present */
    if (config->cycle && config->cycle_paths && config->cycle_count > 0) {
        copy->cycle_paths = calloc(config->cycle_count, sizeof(char *));
        if (!copy->cycle_paths) goto copy_failed;
        copy->cycle_count = config->cycle_count;
        for (size_t i = 0; i < config->cycle_count; i++) {
            if (config->cycle_paths[i] &&
                !(copy->cycle_paths[i] = strdup(config->cycle_paths[i]))) {
                goto copy_failed;
            }
        }

        /* Restore cycle index after installing the transactional copy below. */
    }

    /* A shader already on screen and a shader coming: build the new one in
     * the background and keep drawing the old one. The whole config waits
     * in staged_config and goes live together with its shader, so the
     * output never runs the new settings on the old shader. A build that
     * fails leaves the old config and shader exactly as they were. */
    if (copy->type == WALLPAPER_SHADER && output->config &&
        output->config->type == WALLPAPER_SHADER && output->shader && output->configured &&
        output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        restore_cycle_position(output, copy);
        const char *first = copy->cycle && copy->cycle_count > 0 && copy->cycle_paths
                                ? copy->cycle_paths[copy->current_cycle_index]
                                : copy->shader_path;
        output_unstage(output);                 /* a newer reload wins */
        if (!first || !first[0] || nw_is_err(output_set_shader_async(output, first))) {
            log_error("Failed to apply shader config to output %s", output->model);
            config_free_wallpaper(copy);
            free(copy);
            return false;                       /* nothing changed */
        }
        if (!output->pending_shader) {          /* built synchronously after all */
            output_commit_staged(output, copy);
            return true;
        }
        output->staged_config = copy;
        output->stage_failed = false;
        return true;
    }
    output_unstage(output);

    struct wallpaper_config *old_config = output->config;
    output->config = copy;
    atomic_fetch_add(&output->preprocessing_generation, 1);

    restore_cycle_position(output, output->config);

    log_debug("Config applied - type=%d, cycle=%d, cycle_count=%zu, cycle_index=%zu",
              output->config->type, output->config->cycle, output->config->cycle_count,
              output->config->current_cycle_index);

    /* If we don't have a compositor surface yet, defer actual wallpaper loading */
    if (!output->compositor_surface || !output->configured) {
        log_debug("Output %s not yet configured, deferring wallpaper load",
                  output->model[0] ? output->model : "unknown");
        config_free_wallpaper(old_config);
        free(old_config);
        return true;
    }

    /* Configure vsync based on config */
    output_configure_vsync(output);
    output_configure_frame_timer(output);

    /* Load initial wallpaper based on type */
    if (output->config->type == WALLPAPER_SHADER) {
        /* Shader mode */
        const char *initial_shader = NULL;

        if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
            /* Load first shader from cycle list */
            initial_shader = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Loading initial shader from cycle: %s (index %zu/%zu)",
                     initial_shader, output->config->current_cycle_index, output->config->cycle_count);
        } else if (output->config->shader_path[0] != '\0') {
            /* Load single shader */
            initial_shader = output->config->shader_path;
            log_info("Loading single shader: %s", initial_shader);
        }

        if (initial_shader && nw_is_err(output_set_shader(output, initial_shader))) {
            log_error("Failed to apply shader config to output %s", output->model);
            goto apply_failed;
        } else if (!initial_shader) {
            log_error("No shader configured for output %s", output->model);
            goto apply_failed;
        }
    } else if (output->config->type == WALLPAPER_TERMINAL) {
        /* Terminal mode: spawn the configured command as the wallpaper. */
        if (output->config->term_cmd[0] != '\0') {
            log_info("Loading terminal wallpaper: %s", output->config->term_cmd);
            nw_result terminal_result = output_set_terminal(output, output->config->term_cmd,
                                output->config->shader_path[0] ? output->config->shader_path : NULL,
                                output->config->term_font[0] ? output->config->term_font : NULL,
                                output->config->term_cols, output->config->term_rows);
            if (nw_is_err(terminal_result)) {
                log_error("Failed to apply terminal config to output %s: %s", output->model,
                          terminal_result.context ? terminal_result.context : "unknown error");
                goto apply_failed;
            }
        } else {
            log_error("No terminal command configured for output %s", output->model);
            goto apply_failed;
        }
    } else {
        /* Image mode */
        const char *initial_path = NULL;

        if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
            /* Load first image from cycle list */
            initial_path = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Loading initial image from cycle: %s (index %zu/%zu)",
                     initial_path, output->config->current_cycle_index, output->config->cycle_count);
        } else if (output->config->path[0] != '\0') {
            /* Load single image */
            initial_path = output->config->path;
            log_info("Loading single image: %s", initial_path);
        }

        if (initial_path) {
            output_set_wallpaper(output, initial_path);
        } else {
            log_error("No image path configured for output %s", output->model);
            goto apply_failed;
        }
    }

    /* Initialize cycle time */
    output->last_cycle_time = get_time_ms();

    /* Request immediate redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    log_info("Successfully applied config to output %s", output->model);
    config_free_wallpaper(old_config);
    free(old_config);
    return true;

apply_failed:
    output->config = old_config;
    atomic_fetch_add(&output->preprocessing_generation, 1);
    output_configure_vsync(output);
    output_configure_frame_timer(output);
    config_free_wallpaper(copy);
    free(copy);
    log_error("Restored previous config for output %s after apply failure",
              output->model[0] ? output->model : "unknown");
    return false;

copy_failed:
    config_free_wallpaper(copy);
    free(copy);
    log_error("Failed to deep-copy output configuration");
    return false;
}

void output_apply_deferred_config(struct output_state *output) {
    if (!output) {
        return;
    }

    /* Ready means an EGL surface exists (see output_set_shader). */
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_debug("Output %s not ready for deferred config application",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Check if there's a deferred config to apply */
    if (output->config->type == WALLPAPER_SHADER && output->config->shader_path[0] != '\0') {
        /* Check if shader is not yet loaded */
        if (output->shader == NULL) {
            log_info("Applying deferred shader config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->shader_path);
            output_set_shader(output, output->config->shader_path);
        }
    } else if (output->config->type == WALLPAPER_TERMINAL && output->config->term_cmd[0] != '\0') {
        if (output->shader == NULL) {
            log_info("Applying deferred terminal config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->term_cmd);
            output_set_terminal(output, output->config->term_cmd,
                                output->config->shader_path[0] ? output->config->shader_path : NULL,
                                output->config->term_font[0] ? output->config->term_font : NULL,
                                output->config->term_cols, output->config->term_rows);
        }
    } else if (output->config->type == WALLPAPER_IMAGE && output->config->path[0] != '\0') {
        /* Check if wallpaper is not yet loaded */
        if (!output->current_image && output->texture == 0) {
            log_info("Applying deferred wallpaper config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config->path);
            output_set_wallpaper(output, output->config->path);
        }
    } else if (output->config->cycle && output->config->cycle_count > 0 && output->config->cycle_paths) {
        /* Handle cycling mode */
        if (!output->current_image && output->texture == 0 && output->shader == NULL) {
            const char *initial_path = output->config->cycle_paths[output->config->current_cycle_index];
            log_info("Applying deferred cycle config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     initial_path);

            /* Determine if it's a shader or image */
            const char *ext = strrchr(initial_path, '.');
            if (ext && (strcmp(ext, ".glsl") == 0 || strcmp(ext, ".frag") == 0)) {
                output_set_shader(output, initial_path);
            } else {
                output_set_wallpaper(output, initial_path);
            }
        }
    }
}
