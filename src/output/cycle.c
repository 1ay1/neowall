/* Cycling: on a timer, on `next`, and to an index for `set`. */

#include <stdio.h>
#include "render.h"
#include "output_internal.h"
#include "neowall/config/config.h"
#include "neowall/core/log.h"
#include "neowall/output/state_file.h"
#include "neowall/state/state.h"

/* Cycle to next wallpaper in the cycle list */
void output_cycle_wallpaper(struct output_state *output) {
    if (!output) {
        log_error("Cannot cycle wallpaper: output is NULL");
        return;
    }

    if (!output->config->cycle || output->config->cycle_count == 0) {
        /* Provide clear feedback about why cycling is not possible */
        const char *output_name = output->model[0] ? output->model : "unknown";

        if (output->config->cycle_count == 0) {
            log_info("Cannot cycle wallpaper on output '%s': No wallpapers configured for cycling",
                     output_name);
            log_info(
                "Hint: Configure multiple wallpapers using a directory path or duration setting");
        } else if (!output->config->cycle) {
            log_info("Cannot cycle wallpaper on output '%s': Cycling is disabled", output_name);
            log_info("Current wallpaper: %s", output->config->type == WALLPAPER_SHADER
                                                  ? output->config->shader_path
                                                  : output->config->path);
        }

        /* Write state file to indicate cycling is not available */
        const char *current_path = output->config->type == WALLPAPER_SHADER
                                       ? output->config->shader_path
                                       : output->config->path;
        const char *mode_str     = wallpaper_mode_to_string(output->config->mode);
        write_wallpaper_state(output_get_identifier(output), current_path, mode_str, 0, 0,
                              "cycling not enabled");

        return;
    }

    /* Move to next wallpaper/shader - protect cycle_paths access with mutex */
    pthread_mutex_lock(&output->state->state_mutex);
    size_t old_index  = output->config->current_cycle_index;
    size_t next_index = (output->config->current_cycle_index + 1) % output->config->cycle_count;

    /* End of the sequence — if shuffle is on, re-randomise so the next pass
     * is a fresh order (issue #47). keep_first_at_zero=true pins the
     * just-shown item at slot 0 so we don't immediately repeat it; we then
     * advance past it by leaving next_index at the wrap (0) and bumping it. */
    if (next_index == 0 && output->config->shuffle && output->config->cycle_paths &&
        output->config->cycle_count > 1) {
        /* Pin the currently-displayed item at index 0 so the shuffle keeps
         * it there — swap it in, then shuffle [1, n). After the shuffle we
         * step to index 1 (the first item of the freshly-randomised tail). */
        size_t cur = output->config->current_cycle_index;
        if (cur != 0) {
            char *tmp                        = output->config->cycle_paths[0];
            output->config->cycle_paths[0]   = output->config->cycle_paths[cur];
            output->config->cycle_paths[cur] = tmp;
        }
        config_shuffle_cycle_paths(output->config->cycle_paths, output->config->cycle_count, true);
        next_index = 1;
        log_debug("Cycle wrap on output %s: re-shuffled %zu-entry list",
                  output->model[0] ? output->model : "unknown", output->config->cycle_count);
    }

    output->config->current_cycle_index = next_index;

    /* Copy path before releasing lock to avoid use-after-free if config reloads */
    char next_path_copy[NW_PATH_MAX];
    const char *next_path = output->config->cycle_paths[output->config->current_cycle_index];
    snprintf(next_path_copy, sizeof(next_path_copy), "%s", next_path);
    pthread_mutex_unlock(&output->state->state_mutex);

    /* Use the copied path from here onwards */
    next_path = next_path_copy;

    /* Detect if we're in "shader + image cycling" mode:
     * - Type is WALLPAPER_SHADER (we have a shader)
     * - shader_path is set (the main shader to keep)
     * - cycle_paths contains images (not shaders)
     *
     * In this mode, we keep the same shader but cycle images through iChannel0
     */
    bool is_shader_with_image_cycling = false;
    if (output->config->type == WALLPAPER_SHADER && output->config->shader_path[0] != '\0') {
        /* Check if the first cycle path looks like an image (not a .glsl shader) */
        const char *ext = strrchr(next_path, '.');
        if (ext &&
            (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
             strcmp(ext, ".PNG") == 0 || strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0)) {
            is_shader_with_image_cycling = true;
        }
    }

    if (is_shader_with_image_cycling) {
        /* Shader + Image Cycling mode: Update iChannel0 with the next image */
        log_debug("Cycling image for shader on output %s: index %zu->%zu (%zu/%zu): %s",
                  output->model[0] ? output->model : "unknown", old_index,
                  output->config->current_cycle_index, output->config->current_cycle_index + 1,
                  output->config->cycle_count, next_path);

        /* Update iChannel0 with the new image */
        if (!render_update_channel_texture(output, 0, next_path)) {
            log_error("Failed to update iChannel0 with: %s", next_path);
            return;
        }

        /* Write state to file */
        const char *mode_str = wallpaper_mode_to_string(output->config->mode);
        write_wallpaper_state(output_get_identifier(output), output->config->shader_path, mode_str,
                              output->config->current_cycle_index, output->config->cycle_count,
                              "active");

        log_debug("Image cycled through shader successfully");
    } else {
        /* Normal cycling mode: change the wallpaper or shader entirely */
        const char *type_str = (output->config->type == WALLPAPER_SHADER) ? "shader" : "wallpaper";
        log_info("Cycling %s for output %s: index %zu->%zu (%zu/%zu): %s", type_str,
                 output->model[0] ? output->model : "unknown", old_index,
                 output->config->current_cycle_index, output->config->current_cycle_index + 1,
                 output->config->cycle_count, next_path);

        /* Apply the next item based on type */
        if (output->config->type == WALLPAPER_SHADER) {
            /* A shader that fails to load leaves the output with no program
             * bound, so skip past it to the next entry that does load. Only
             * content errors (unreadable file, bad GLSL) are worth retrying:
             * a GL/state error means this output cannot load any shader right
             * now, and every remaining candidate would fail the same way.
             * Bounded by cycle_count so an all-broken list terminates. */
            /* In the background: the current shader keeps drawing while the
             * next compiles. File errors come back here; a compile error
             * comes back to output_poll_shader_build, which moves on. */
            nw_result r = output_set_shader_async(output, next_path);

            for (size_t tried = 1; tried < output->config->cycle_count &&
                                   (r.status == NW_ERR_IO || r.status == NW_ERR_PARSE);
                 tried++) {
                log_warn("Shader '%s' failed to load on output %s (%s: %s); "
                         "skipping to next entry in the cycle",
                         next_path, output->model[0] ? output->model : "unknown",
                         nw_status_str(r.status), r.context ? r.context : "");

                pthread_mutex_lock(&output->state->state_mutex);
                output->config->current_cycle_index =
                    (output->config->current_cycle_index + 1) % output->config->cycle_count;
                snprintf(next_path_copy, sizeof(next_path_copy), "%s",
                         output->config->cycle_paths[output->config->current_cycle_index]);
                pthread_mutex_unlock(&output->state->state_mutex);
                next_path = next_path_copy;

                log_info("Cycling shader for output %s: index %zu (%zu/%zu): %s",
                         output->model[0] ? output->model : "unknown",
                         output->config->current_cycle_index,
                         output->config->current_cycle_index + 1, output->config->cycle_count,
                         next_path);

                r = output_set_shader_async(output, next_path);
            }

            if (nw_is_err(r)) {
                log_error("No shader in the cycle list loaded on output %s (last error: %s: %s)",
                          output->model[0] ? output->model : "unknown", nw_status_str(r.status),
                          r.context ? r.context : "");
            }
        } else {
            output_set_wallpaper(output, next_path);
        }

        /* Mark the output for redraw to ensure change is visible */
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    }

    /* Update cycle list file for 'neowall list' command */
    if (output->config->cycle_paths && output->config->cycle_count > 0) {
        write_cycle_list(output_get_identifier(output), output->config->cycle_paths,
                         output->config->cycle_count, output->config->current_cycle_index);
    }

    log_info("Wallpaper cycle completed successfully");
}

/* Check if output needs to cycle wallpaper based on duration */
/* Set wallpaper to a specific index in the cycle */
void output_set_cycle_index(struct output_state *output, size_t index) {
    if (!output) {
        log_error("Cannot set cycle index: output is NULL");
        return;
    }

    if (!output->config->cycle || output->config->cycle_count == 0) {
        const char *output_name = output->model[0] ? output->model : "unknown";
        log_error(
            "Cannot set cycle index on output '%s': Cycling is not enabled or no wallpapers configured",
            output_name);
        return;
    }

    /* Validate index is within bounds */
    if (index >= output->config->cycle_count) {
        log_error("Invalid cycle index %zu: must be between 0 and %zu", index,
                  output->config->cycle_count - 1);
        return;
    }

    /* Don't do anything if already at the requested index */
    if (output->config->current_cycle_index == index) {
        log_info("Already at wallpaper index %zu", index);
        return;
    }

    /* Set the index */
    pthread_mutex_lock(&output->state->state_mutex);
    size_t old_index                    = output->config->current_cycle_index;
    output->config->current_cycle_index = index;

    /* Copy path before releasing lock */
    char path_copy[NW_PATH_MAX];
    const char *path = output->config->cycle_paths[index];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    pthread_mutex_unlock(&output->state->state_mutex);

    const char *type_str = (output->config->type == WALLPAPER_SHADER) ? "shader" : "wallpaper";
    log_info("Setting %s index for output %s: %zu -> %zu (%zu/%zu): %s", type_str,
             output->model[0] ? output->model : "unknown", old_index, index, index + 1,
             output->config->cycle_count, path_copy);

    /* Apply the wallpaper/shader at the new index. A shader builds in the
     * background like a timed cycle, so the old one keeps drawing instead of
     * the screen freezing for the compile. */
    if (output->config->type == WALLPAPER_SHADER) {
        nw_result r = output_set_shader_async(output, path_copy);
        if (nw_is_err(r))
            log_error("set %zu: %s %s", index, nw_status_str(r.status), r.context ? r.context : "");
    } else {
        output_set_wallpaper(output, path_copy);
    }

    /* Mark for redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

    /* Update cycle list file for 'neowall list' command */
    if (output->config->cycle_paths && output->config->cycle_count > 0) {
        write_cycle_list(output_get_identifier(output), output->config->cycle_paths,
                         output->config->cycle_count, output->config->current_cycle_index);
    }

    log_info("Wallpaper index set successfully");
}

bool output_should_cycle(struct output_state *output, uint64_t current_time) {
    if (!output) {
        return false;
    }

    if (!output->config->cycle) {
        return false;
    }

    if (output->config->duration == 0.0f) {
        return false;
    }

    /* For images, check if current_image exists. For shaders, check if shader program exists */
    if (output->config->type == WALLPAPER_IMAGE && !output->current_image) {
        return false;
    }

    if (output->config->type == WALLPAPER_SHADER && output->shader == NULL) {
        return false;
    }
    /* the next one is still compiling; its landing restarts the clock */
    if (output->pending_shader) {
        return false;
    }

    if (output->config->cycle_count <= 1) {
        return false;
    }

    uint64_t elapsed_ms = current_time - output->last_cycle_time;
    uint64_t duration_ms =
        (uint64_t)(output->config->duration * 1000.0f); /* Convert seconds to milliseconds */

    bool should_cycle = elapsed_ms >= duration_ms;

    if (should_cycle) {
        log_debug("Output %s should cycle: elapsed=%lums >= duration=%lums (current_index=%zu/%zu)",
                  output->model[0] ? output->model : "unknown", elapsed_ms, duration_ms,
                  output->config->current_cycle_index, output->config->cycle_count);
    }

    return should_cycle;
}
