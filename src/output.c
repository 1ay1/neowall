#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "staticwall.h"
#include "constants.h"
#include "shader.h"
#include "../protocols/wlr-layer-shell-unstable-v1-client-protocol.h"





struct output_state *output_create(struct staticwall_state *state,
                                   struct wl_output *output, uint32_t name) {
    if (!state || !output) {
        log_error("Invalid parameters for output_create");
        return NULL;
    }

    struct output_state *out = calloc(1, sizeof(struct output_state));
    if (!out) {
        log_error("Failed to allocate output state: %s", strerror(errno));
        return NULL;
    }

    /* Initialize output state */
    out->output = output;
    out->name = name;
    out->scale = 1;
    out->transform = WL_OUTPUT_TRANSFORM_NORMAL;
    out->configured = false;
    out->needs_redraw = true;
    out->state = state;

    /* Create Wayland surface */
    out->surface = wl_compositor_create_surface(state->compositor);
    if (!out->surface) {
        log_error("Failed to create Wayland surface");
        free(out);
        return NULL;
    }

    /* Initialize wallpaper config with defaults */
    out->config.mode = MODE_FILL;
    out->config.duration = 0;
    out->config.transition = TRANSITION_NONE;
    out->config.transition_duration = 300;
    out->config.cycle = false;
    out->config.cycle_paths = NULL;
    out->config.cycle_count = 0;
    out->config.current_cycle_index = 0;
    out->shader_fade_start_time = 0;
    out->pending_shader_path[0] = '\0';

    /* Add to linked list */
    out->next = state->outputs;
    state->outputs = out;
    state->output_count++;

    log_debug("Created output state (name=%u)", name);

    return out;
}

void output_destroy(struct output_state *output) {
    if (!output) {
        return;
    }

    log_debug("Destroying output %s (name=%u)",
              output->model[0] ? output->model : "unknown", output->name);

    /* Clean up rendering resources */
    render_cleanup_output(output);

    /* Clean up shader programs */
    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }

    /* Free wallpaper config */
    config_free_wallpaper(&output->config);

    /* Free image data */
    if (output->current_image) {
        image_free(output->current_image);
        output->current_image = NULL;
    }

    if (output->next_image) {
        image_free(output->next_image);
        output->next_image = NULL;
    }

    /* Destroy EGL surface */
    if (output->egl_surface != EGL_NO_SURFACE && output->state && output->state->egl_display != EGL_NO_DISPLAY) {
        eglDestroySurface(output->state->egl_display, output->egl_surface);
        output->egl_surface = EGL_NO_SURFACE;
    }
    
    if (output->egl_window) {
        wl_egl_window_destroy(output->egl_window);
        output->egl_window = NULL;
    }

    /* Destroy layer surface */
    if (output->layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
        output->layer_surface = NULL;
    }

    /* Destroy Wayland surface */
    if (output->surface) {
        wl_surface_destroy(output->surface);
        output->surface = NULL;
    }

    /* Note: We don't destroy output->output as it's managed by Wayland */

    free(output);
}

bool output_create_egl_surface(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for EGL surface creation (NULL)");
        return false;
    }

    if (!output->surface) {
        log_error("Invalid Wayland surface for output %s (NULL)",
                  output->model[0] ? output->model : "unknown");
        return false;
    }

    if (output->width <= 0 || output->height <= 0) {
        log_debug("Output %s dimensions not ready yet: %dx%d (deferring surface creation)",
                  output->model[0] ? output->model : "unknown",
                  output->width, output->height);
        return false;
    }

    /* Check if EGL window already exists */
    if (output->egl_window) {
        log_debug("EGL window already exists for output %s, skipping creation",
                  output->model[0] ? output->model : "unknown");
        return true;
    }

    log_debug("Creating EGL window for output %s: %dx%d",
              output->model[0] ? output->model : "unknown",
              output->width, output->height);

    /* Create EGL window */
    output->egl_window = wl_egl_window_create(output->surface,
                                              output->width,
                                              output->height);
    if (!output->egl_window) {
        log_error("Failed to create EGL window for output %s",
                  output->model[0] ? output->model : "unknown");
        return false;
    }

    log_info("Created EGL surface for output %s: %dx%d",
             output->model[0] ? output->model : "unknown",
             output->width, output->height);

    return true;
}

void output_set_wallpaper(struct output_state *output, const char *path) {
    if (!output || !path) {
        log_error("Invalid parameters for output_set_wallpaper");
        return;
    }

    log_info("Setting wallpaper for output %s: %s",
             output->model[0] ? output->model : "unknown", path);

    /* Load new image with display-aware scaling */
    struct image_data *new_image = image_load(path, output->width, output->height, output->config.mode);
    if (!new_image) {
        log_error("Failed to load wallpaper image: %s", path);
        return;
    }

    /* Defensive checks before any EGL/GL operations */
    if (!output->state) {
        log_error("Output state is NULL, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    if (output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not initialized, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    if (output->state->egl_context == EGL_NO_CONTEXT) {
        log_error("EGL context not initialized, cannot set wallpaper");
        image_free(new_image);
        return;
    }

    if (output->egl_surface == EGL_NO_SURFACE) {
        log_debug("EGL surface not ready for output %s, deferring wallpaper load",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }
    
    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        image_free(new_image);
        return;
    }

    /* Make EGL context current before creating textures */
    if (!eglMakeCurrent(output->state->egl_display, output->egl_surface,
                       output->egl_surface, output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error("Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
                  output->model[0] ? output->model : "unknown", egl_error);
        image_free(new_image);
        return;
    }

    log_debug("EGL context made current for wallpaper load on output %s",
              output->model[0] ? output->model : "unknown");

    /* Handle transition */
    if (output->config.transition != TRANSITION_NONE && output->current_image && output->texture) {
        /* Store current image as "next_image" for transition */
        if (output->next_image) {
            image_free(output->next_image);
        }
        output->next_image = output->current_image;
        output->current_image = new_image;

        /* Start transition */
        output->transition_start_time = get_time_ms();
        output->transition_progress = 0.0f;

        /* Destroy and recreate next texture */
        if (output->next_texture) {
            render_destroy_texture(output->next_texture);
        }
        output->next_texture = output->texture;
        output->texture = render_create_texture(new_image);
        
        log_info("Transition started: %s -> %s (type=%d '%s', duration=%.2fs)",
                  output->config.path, path,
                  output->config.transition, 
                  transition_type_to_string(output->config.transition),
                  output->config.transition_duration);
    } else {
        /* No transition, just replace */
        if (output->current_image) {
            image_free(output->current_image);
        }
        output->current_image = new_image;

        /* Recreate texture */
        if (output->texture) {
            render_destroy_texture(output->texture);
        }
        output->texture = render_create_texture(new_image);
        log_info("Wallpaper texture created successfully (texture=%u) for output %s", 
                 output->texture, output->model[0] ? output->model : "unknown");
    }

    /* Update config path */
    strncpy(output->config.path, path, sizeof(output->config.path) - 1);
    output->config.path[sizeof(output->config.path) - 1] = '\0';

    /* Initialize frame time for cycling */
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Write current state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config.mode);
    write_wallpaper_state(output->model, path, mode_str, 
                         output->config.current_cycle_index,
                         output->config.cycle_count,
                         "active");

    /* Mark for redraw */
    output->needs_redraw = true;
}

/* Set live shader wallpaper */
void output_set_shader(struct output_state *output, const char *shader_path) {
    if (!output || !shader_path) {
        log_error("Invalid parameters for output_set_shader");
        return;
    }

    log_info("Setting shader wallpaper for output %s: %s",
             output->model[0] ? output->model : "unknown", shader_path);

    /* Defensive checks before any EGL/GL operations */
    if (!output->state) {
        log_error("Output state is NULL, cannot set shader");
        return;
    }

    if (output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not initialized, cannot set shader");
        return;
    }

    if (output->state->egl_context == EGL_NO_CONTEXT) {
        log_error("EGL context not initialized, cannot set shader");
        return;
    }

    if (output->egl_surface == EGL_NO_SURFACE) {
        log_debug("EGL surface not ready for output %s, deferring shader load: %s",
                  output->model[0] ? output->model : "unknown", shader_path);
        /* Store shader path in config for later application when surface is ready */
        strncpy(output->config.shader_path, shader_path, sizeof(output->config.shader_path) - 1);
        output->config.shader_path[sizeof(output->config.shader_path) - 1] = '\0';
        output->config.type = WALLPAPER_SHADER;
        return;
    }

    if (!output->egl_window) {
        log_error("EGL window not created for output %s, cannot set shader",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Validate EGL display and surface before operations */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return;
    }
    
    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for output %s (display may be disconnected)",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Make EGL context current before creating shader program */
    if (!eglMakeCurrent(output->state->egl_display, output->egl_surface,
                       output->egl_surface, output->state->egl_context)) {
        EGLint egl_error = eglGetError();
        log_error("Failed to make EGL context current for output %s: 0x%x (display may be disconnected)",
                  output->model[0] ? output->model : "unknown", egl_error);
        return;
    }

    log_debug("EGL context made current for output %s",
              output->model[0] ? output->model : "unknown");

    /* If there's an existing shader, compile new shader and switch immediately */
    if (output->live_shader_program != 0) {
        /* Prevent re-entrant shader changes */
        if (output->shader_fade_start_time > 0 && output->pending_shader_path[0] != '\0') {
            log_debug("Shader change already in progress, ignoring new request for: %s", shader_path);
            return;
        }
        
        log_info("Compiling new shader: %s", shader_path);
        
        /* Compile new shader immediately (before switching) to avoid stutter */
        GLuint new_shader_program = 0;
        if (!shader_create_live_program(shader_path, &new_shader_program, output->channel_count)) {
            log_error("Failed to create shader program from: %s", shader_path);
            return;
        }
        
        if (new_shader_program == 0) {
            log_error("Invalid shader program created for: %s", shader_path);
            return;
        }
        
        /* Successfully compiled - now switch immediately */
        log_info("Switching to new shader: %s", shader_path);
        
        /* Destroy old shader */
        shader_destroy_program(output->live_shader_program);
        
        /* Switch to new shader */
        output->live_shader_program = new_shader_program;
        output->shader_start_time = get_time_ms();
        
        /* Reset shader uniform cache for new program */
        output->shader_uniforms.position = -2;
        output->shader_uniforms.texcoord = -2;
        output->shader_uniforms.tex_sampler = -2;
        output->shader_uniforms.u_resolution = -2;
        output->shader_uniforms.u_time = -2;
        output->shader_uniforms.u_speed = -2;
        
        /* Reset iChannel uniform locations */
        if (output->shader_uniforms.iChannel && output->channel_count > 0) {
            for (size_t i = 0; i < output->channel_count; i++) {
                output->shader_uniforms.iChannel[i] = -2;
            }
        }
        
        /* Update config with new shader path */
        strncpy(output->config.shader_path, shader_path, sizeof(output->config.shader_path) - 1);
        output->config.shader_path[sizeof(output->config.shader_path) - 1] = '\0';
        
        /* Write state to file */
        const char *mode_str = wallpaper_mode_to_string(output->config.mode);
        write_wallpaper_state(output->model, shader_path, mode_str,
                             output->config.current_cycle_index,
                             output->config.cycle_count,
                             "active");
        
        /* Mark for immediate redraw with new shader */
        output->needs_redraw = true;
        output->last_cycle_time = get_time_ms();
        
        log_info("Shader switched successfully: %s", shader_path);
        return;
    }
    
    /* First shader load - no fade needed, load and compile immediately */
    
    /* Load iChannel textures based on config */
    if (!render_load_channel_textures(output, &output->config)) {
        log_error("Failed to load iChannel textures for shader: %s", shader_path);
        /* Continue anyway - shader may work without textures */
    }
    
    GLuint new_shader_program = 0;
    if (!shader_create_live_program(shader_path, &new_shader_program, output->channel_count)) {
        log_error("Failed to create shader program from: %s", shader_path);
        
        /* Clean up iChannel textures that were loaded but can't be used */
        if (output->channel_textures) {
            for (size_t i = 0; i < output->channel_count; i++) {
                if (output->channel_textures[i] != 0) {
                    render_destroy_texture(output->channel_textures[i]);
                    output->channel_textures[i] = 0;
                }
            }
            free(output->channel_textures);
            output->channel_textures = NULL;
        }
        if (output->shader_uniforms.iChannel) {
            free(output->shader_uniforms.iChannel);
            output->shader_uniforms.iChannel = NULL;
        }
        output->channel_count = 0;
        
        return;
    }
    
    output->live_shader_program = new_shader_program;
    output->shader_start_time = get_time_ms();
    
    /* Reset shader uniform cache for new program */
    output->shader_uniforms.position = -2;
    output->shader_uniforms.texcoord = -2;
    output->shader_uniforms.tex_sampler = -2;
    output->shader_uniforms.u_resolution = -2;
    output->shader_uniforms.u_time = -2;
    output->shader_uniforms.u_speed = -2;
    
    /* Reset iChannel uniform locations to uninitialized */
    if (output->shader_uniforms.iChannel && output->channel_count > 0) {
        for (size_t i = 0; i < output->channel_count; i++) {
            output->shader_uniforms.iChannel[i] = -2;
        }
    }
    
    log_debug("Shader loaded (first): %s", shader_path);

    /* Free any existing image data (shaders don't use images) */
    if (output->current_image) {
        image_free(output->current_image);
        output->current_image = NULL;
    }
    if (output->next_image) {
        image_free(output->next_image);
        output->next_image = NULL;
    }
    if (output->texture) {
        render_destroy_texture(output->texture);
        output->texture = 0;
    }
    if (output->next_texture) {
        render_destroy_texture(output->next_texture);
        output->next_texture = 0;
    }

    /* Update config */
    strncpy(output->config.shader_path, shader_path, sizeof(output->config.shader_path) - 1);
    output->config.shader_path[sizeof(output->config.shader_path) - 1] = '\0';
    output->config.type = WALLPAPER_SHADER;

    /* Initialize frame time for animation */
    uint64_t now = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;

    /* Mark for redraw */
    output->needs_redraw = true;

    /* Write current state to file */
    const char *mode_str = wallpaper_mode_to_string(output->config.mode);
    write_wallpaper_state(output->model, shader_path, mode_str, 
                         output->config.current_cycle_index,
                         output->config.cycle_count,
                         "active");

    log_info("Live shader wallpaper loaded successfully");
}

/* Cycle to next wallpaper in the cycle list */
void output_cycle_wallpaper(struct output_state *output) {
    if (!output) {
        log_error("Cannot cycle wallpaper: output is NULL");
        return;
    }
    
    if (!output->config.cycle || output->config.cycle_count == 0) {
        /* Provide clear feedback about why cycling is not possible */
        const char *output_name = output->model[0] ? output->model : "unknown";
        
        if (output->config.cycle_count == 0) {
            log_info("Cannot cycle wallpaper on output '%s': No wallpapers configured for cycling", 
                     output_name);
            log_info("Hint: Configure multiple wallpapers using a directory path or duration setting");
        } else if (!output->config.cycle) {
            log_info("Cannot cycle wallpaper on output '%s': Cycling is disabled", 
                     output_name);
            log_info("Current wallpaper: %s", 
                     output->config.type == WALLPAPER_SHADER ? 
                     output->config.shader_path : output->config.path);
        }
        
        /* Write state file to indicate cycling is not available */
        const char *current_path = output->config.type == WALLPAPER_SHADER ? 
                                   output->config.shader_path : output->config.path;
        const char *mode_str = wallpaper_mode_to_string(output->config.mode);
        write_wallpaper_state(output->model, current_path, mode_str, 0, 0,
                             "cycling not enabled");
        
        return;
    }

    /* Don't cycle if a shader cross-fade is in progress */
    if (output->config.type == WALLPAPER_SHADER && 
        output->shader_fade_start_time > 0 && 
        output->pending_shader_path[0] != '\0') {
        const char *output_name = output->model[0] ? output->model : "unknown";
        log_info("Shader transition in progress on output '%s', deferring cycle request", 
                 output_name);
        return;
    }

    /* Move to next wallpaper/shader */
    size_t old_index = output->config.current_cycle_index;
    output->config.current_cycle_index =
        (output->config.current_cycle_index + 1) % output->config.cycle_count;

    const char *next_path = output->config.cycle_paths[output->config.current_cycle_index];

    /* Detect if we're in "shader + image cycling" mode:
     * - Type is WALLPAPER_SHADER (we have a shader)
     * - shader_path is set (the main shader to keep)
     * - cycle_paths contains images (not shaders)
     * 
     * In this mode, we keep the same shader but cycle images through iChannel0
     */
    bool is_shader_with_image_cycling = false;
    if (output->config.type == WALLPAPER_SHADER && 
        output->config.shader_path[0] != '\0') {
        /* Check if the first cycle path looks like an image (not a .glsl shader) */
        const char *ext = strrchr(next_path, '.');
        if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || 
                   strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".PNG") == 0 || 
                   strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0)) {
            is_shader_with_image_cycling = true;
        }
    }

    if (is_shader_with_image_cycling) {
        /* Shader + Image Cycling mode: Update iChannel0 with the next image */
        log_info("Cycling image for shader on output %s: index %zu->%zu (%zu/%zu): %s",
                 output->model[0] ? output->model : "unknown",
                 old_index,
                 output->config.current_cycle_index,
                 output->config.current_cycle_index + 1,
                 output->config.cycle_count,
                 next_path);
        
        /* Update iChannel0 with the new image */
        if (!render_update_channel_texture(output, 0, next_path)) {
            log_error("Failed to update iChannel0 with: %s", next_path);
            return;
        }
        
        /* Write state to file */
        const char *mode_str = wallpaper_mode_to_string(output->config.mode);
        write_wallpaper_state(output->model, output->config.shader_path, mode_str,
                             output->config.current_cycle_index,
                             output->config.cycle_count,
                             "active");
        
        log_info("Image cycled through shader successfully");
    } else {
        /* Normal cycling mode: change the wallpaper or shader entirely */
        const char *type_str = (output->config.type == WALLPAPER_SHADER) ? "shader" : "wallpaper";
        log_info("Cycling %s for output %s: index %zu->%zu (%zu/%zu): %s",
                 type_str,
                 output->model[0] ? output->model : "unknown",
                 old_index,
                 output->config.current_cycle_index,
                 output->config.current_cycle_index + 1,
                 output->config.cycle_count,
                 next_path);

        /* Apply the next item based on type */
        if (output->config.type == WALLPAPER_SHADER) {
            output_set_shader(output, next_path);
        } else {
            output_set_wallpaper(output, next_path);
        }
        
        /* Mark the output for redraw to ensure change is visible */
        output->needs_redraw = true;
    }
    
    log_info("Wallpaper cycle completed successfully");
}

/* Check if output needs to cycle wallpaper based on duration */
bool output_should_cycle(struct output_state *output, uint64_t current_time) {
    if (!output) {
        return false;
    }

    if (!output->config.cycle) {
        return false;
    }

    if (output->config.duration == 0.0f) {
        return false;
    }

    /* For images, check if current_image exists. For shaders, check if shader program exists */
    if (output->config.type == WALLPAPER_IMAGE && !output->current_image) {
        return false;
    }
    
    if (output->config.type == WALLPAPER_SHADER && output->live_shader_program == 0) {
        return false;
    }

    if (output->config.cycle_count <= 1) {
        return false;
    }

    uint64_t elapsed_ms = current_time - output->last_cycle_time;
    uint64_t duration_ms = (uint64_t)(output->config.duration * 1000.0f);  /* Convert seconds to milliseconds */

    bool should_cycle = elapsed_ms >= duration_ms;

    if (should_cycle) {
        log_debug("Output %s should cycle: elapsed=%lums >= duration=%lums (current_index=%zu/%zu)",
                  output->model[0] ? output->model : "unknown",
                  elapsed_ms, duration_ms,
                  output->config.current_cycle_index,
                  output->config.cycle_count);
    }

    return should_cycle;
}

/* Find output by name */
struct output_state *output_find_by_name(struct staticwall_state *state, uint32_t name) {
    if (!state) {
        return NULL;
    }

    struct output_state *output = state->outputs;
    while (output) {
        if (output->name == name) {
            return output;
        }
        output = output->next;
    }

    return NULL;
}

/* Find output by model string */
struct output_state *output_find_by_model(struct staticwall_state *state, const char *model) {
    if (!state || !model) {
        return NULL;
    }

    struct output_state *output = state->outputs;
    while (output) {
        if (strcmp(output->model, model) == 0) {
            return output;
        }
        output = output->next;
    }

    return NULL;
}

/* Apply wallpaper configuration to an output */
bool output_apply_config(struct output_state *output, struct wallpaper_config *config) {
    if (!output || !config) {
        log_error("Invalid parameters for output_apply_config");
        return false;
    }

    log_debug("Applying config to output %s (egl_surface=%p, egl_window=%p, configured=%d)",
              output->model[0] ? output->model : "unknown",
              (void*)output->egl_surface,
              (void*)output->egl_window,
              output->configured);
    
    log_info("Config for output %s: type=%s, mode=%s, transition=%d, duration=%.2fs",
             output->model[0] ? output->model : "unknown",
             config->type == WALLPAPER_SHADER ? "shader" : "image",
             wallpaper_mode_to_string(config->mode),
             config->transition,
             config->duration);

    /* Clean up old resources when switching modes */
    if (output->config.type != config->type) {
        log_info("Switching mode from %s to %s, cleaning up old resources",
                 output->config.type == WALLPAPER_SHADER ? "shader" : "image",
                 config->type == WALLPAPER_SHADER ? "shader" : "image");
        
        if (output->config.type == WALLPAPER_IMAGE) {
            /* Switching from IMAGE to SHADER - clean up image resources */
            if (output->current_image) {
                image_free(output->current_image);
                output->current_image = NULL;
            }
            if (output->next_image) {
                image_free(output->next_image);
                output->next_image = NULL;
            }
            if (output->texture) {
                render_destroy_texture(output->texture);
                output->texture = 0;
            }
            if (output->next_texture) {
                render_destroy_texture(output->next_texture);
                output->next_texture = 0;
            }
            /* Reset transition state */
            output->transition_start_time = 0;
            output->transition_progress = 0.0f;
            
        } else if (output->config.type == WALLPAPER_SHADER) {
            /* Switching from SHADER to IMAGE - clean up shader resources */
            if (output->live_shader_program) {
                shader_destroy_program(output->live_shader_program);
                output->live_shader_program = 0;
            }
            /* Clean up channel textures */
            if (output->channel_textures) {
                for (size_t i = 0; i < output->channel_count; i++) {
                    if (output->channel_textures[i]) {
                        render_destroy_texture(output->channel_textures[i]);
                    }
                }
                free(output->channel_textures);
                output->channel_textures = NULL;
                output->channel_count = 0;
            }
            /* Reset shader state */
            output->shader_start_time = 0;
            output->shader_fade_start_time = 0;
            output->pending_shader_path[0] = '\0';
        }
    }

    /* Free old channel_paths if they exist */
    if (output->config.channel_paths) {
        for (size_t i = 0; i < output->config.channel_count; i++) {
            if (output->config.channel_paths[i]) {
                free(output->config.channel_paths[i]);
            }
        }
        free(output->config.channel_paths);
        output->config.channel_paths = NULL;
        output->config.channel_count = 0;
    }
    
    /* Free old cycle_paths if they exist */
    if (output->config.cycle_paths) {
        for (size_t i = 0; i < output->config.cycle_count; i++) {
            if (output->config.cycle_paths[i]) {
                free(output->config.cycle_paths[i]);
            }
        }
        free(output->config.cycle_paths);
        output->config.cycle_paths = NULL;
        output->config.cycle_count = 0;
    }

    /* Copy configuration (shallow copy for now, will handle cycle_paths and channel_paths separately) */
    memcpy(&output->config, config, sizeof(struct wallpaper_config));
    
    log_debug("After memcpy - output->config.transition=%d, output->config.transition_duration=%u",
              output->config.transition, output->config.transition_duration);
    
    /* Deep copy channel_paths array if present */
    output->config.channel_paths = NULL;
    output->config.channel_count = 0;
    if (config->channel_paths && config->channel_count > 0) {
        output->config.channel_paths = calloc(config->channel_count, sizeof(char *));
        if (output->config.channel_paths) {
            output->config.channel_count = config->channel_count;
            for (size_t i = 0; i < config->channel_count; i++) {
                if (config->channel_paths[i]) {
                    output->config.channel_paths[i] = strdup(config->channel_paths[i]);
                    if (!output->config.channel_paths[i]) {
                        log_error("Failed to duplicate channel path %zu", i);
                        /* Clean up already allocated paths */
                        for (size_t j = 0; j < i; j++) {
                            free(output->config.channel_paths[j]);
                        }
                        free(output->config.channel_paths);
                        output->config.channel_paths = NULL;
                        output->config.channel_count = 0;
                        break;
                    }
                }
            }
        } else {
            log_error("Failed to allocate memory for channel_paths array");
        }
    }
    
    /* Deep copy cycle_paths array if present */
    output->config.cycle_paths = NULL;
    output->config.cycle_count = 0;
    if (config->cycle && config->cycle_paths && config->cycle_count > 0) {
        output->config.cycle_paths = calloc(config->cycle_count, sizeof(char *));
        if (output->config.cycle_paths) {
            output->config.cycle_count = config->cycle_count;
            for (size_t i = 0; i < config->cycle_count; i++) {
                if (config->cycle_paths[i]) {
                    output->config.cycle_paths[i] = strdup(config->cycle_paths[i]);
                    if (!output->config.cycle_paths[i]) {
                        log_error("Failed to duplicate cycle path %zu", i);
                        /* Clean up already allocated paths */
                        for (size_t j = 0; j < i; j++) {
                            free(output->config.cycle_paths[j]);
                        }
                        free(output->config.cycle_paths);
                        output->config.cycle_paths = NULL;
                        output->config.cycle_count = 0;
                        output->config.cycle = false;
                        break;
                    }
                }
            }
        } else {
            log_error("Failed to allocate memory for cycle_paths array");
            output->config.cycle = false;
        }
    }

    /* Check if path is a directory - if so, auto-enable cycling */
    if (config->path[0] != '\0' && !config->cycle) {
        /* Expand path if needed */
        char expanded_path[MAX_PATH_LENGTH];
        if (config->path[0] == '~') {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, config->path + 1);
            } else {
                strncpy(expanded_path, config->path, sizeof(expanded_path) - 1);
            }
        } else {
            strncpy(expanded_path, config->path, sizeof(expanded_path) - 1);
        }
        expanded_path[sizeof(expanded_path) - 1] = '\0';

        /* Check if it's a directory */
        struct stat st;
        if (stat(expanded_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            log_info("Path is a directory, auto-enabling cycling: %s", config->path);

            /* Load images from directory */
            size_t dir_count = 0;
            char **dir_paths = load_images_from_directory(config->path, &dir_count);

            if (dir_paths && dir_count > 0) {
                output->config.cycle = true;
                output->config.cycle_count = dir_count;
                output->config.cycle_paths = dir_paths;
                log_info("Auto-loaded %zu images from directory", dir_count);
            }
        }
    }

    /* Allocate and copy cycle paths if present */
    if (config->cycle && config->cycle_count > 0 && config->cycle_paths) {
        output->config.cycle_paths = calloc(config->cycle_count, sizeof(char *));
        if (!output->config.cycle_paths) {
            log_error("Failed to allocate cycle paths array");
            return false;
        }

        for (size_t i = 0; i < config->cycle_count; i++) {
            output->config.cycle_paths[i] = strdup(config->cycle_paths[i]);
            if (!output->config.cycle_paths[i]) {
                log_error("Failed to duplicate cycle path");
                /* Clean up already allocated paths */
                for (size_t j = 0; j < i; j++) {
                    free(output->config.cycle_paths[j]);
                }
                free(output->config.cycle_paths);
                output->config.cycle_paths = NULL;
                return false;
            }
        }
    }

    /* Restore cycle index from previous state if cycling */
    if (output->config.cycle && output->config.cycle_count > 0) {
        int restored_index = restore_cycle_index_from_state(output->model[0] ? output->model : "unknown");
        if (restored_index >= 0 && restored_index < (int)output->config.cycle_count) {
            output->config.current_cycle_index = (size_t)restored_index;
            log_info("Restored cycle index %d for output %s", restored_index, 
                     output->model[0] ? output->model : "unknown");
        } else {
            output->config.current_cycle_index = 0;
        }
    }

    /* Set initial wallpaper/shader based on type */
    if (config->type == WALLPAPER_SHADER) {
        /* Load shader wallpaper */
        const char *initial_shader = config->shader_path;
        bool is_shader_with_image_cycling = false;
        
        if (output->config.cycle && output->config.cycle_count > 0 && output->config.cycle_paths) {
            /* Check if this is shader + image cycling mode */
            const char *first_cycle_path = output->config.cycle_paths[0];
            const char *ext = strrchr(first_cycle_path, '.');
            
            if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || 
                       strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".PNG") == 0 || 
                       strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0)) {
                /* Cycle paths contain images - this is shader + image cycling mode */
                is_shader_with_image_cycling = true;
                log_info("Detected shader + image cycling mode: shader='%s', cycling through %zu images",
                         initial_shader, output->config.cycle_count);
            } else {
                /* Cycle paths contain shaders - use the shader at restored index */
                initial_shader = output->config.cycle_paths[output->config.current_cycle_index];
            }
        }
        
        if (initial_shader[0] != '\0') {
            /* Check if output is ready for shader loading */
            if (output->egl_surface != EGL_NO_SURFACE && output->egl_window) {
                output_set_shader(output, initial_shader);
                
                /* If shader + image cycling mode, load the first image into iChannel0 */
                if (is_shader_with_image_cycling) {
                    const char *initial_image = output->config.cycle_paths[output->config.current_cycle_index];
                    log_info("Loading initial image into iChannel0: %s", initial_image);
                    
                    if (!render_update_channel_texture(output, 0, initial_image)) {
                        log_error("Failed to load initial image into iChannel0: %s", initial_image);
                    }
                }
            } else {
                log_debug("Output %s not ready for shader load, storing config for later",
                          output->model[0] ? output->model : "unknown");
                /* Config is already stored in output->config, will be applied when surface is ready */
            }
        }
    } else {
        /* Load image wallpaper */
        const char *initial_path = config->path;
        if (output->config.cycle && output->config.cycle_count > 0 && output->config.cycle_paths) {
            /* Use the image at restored index */
            initial_path = output->config.cycle_paths[output->config.current_cycle_index];
        }

        if (initial_path[0] != '\0') {
            /* Check if output is ready for wallpaper loading */
            if (output->egl_surface != EGL_NO_SURFACE && output->egl_window) {
                log_info("Loading wallpaper for output %s: %s", 
                         output->model[0] ? output->model : "unknown", initial_path);
                output_set_wallpaper(output, initial_path);
                log_info("Wallpaper load completed for output %s", 
                         output->model[0] ? output->model : "unknown");
            } else {
                log_error("Output %s not ready for wallpaper load (surface=%p, window=%p)",
                          output->model[0] ? output->model : "unknown",
                          (void*)output->egl_surface, (void*)output->egl_window);
                log_error("This may indicate a problem after config reload cleanup");
                /* Config is already stored in output->config, will be applied when surface is ready */
            }
        } else {
            log_error("No wallpaper path configured for output %s - cannot load image",
                     output->model[0] ? output->model : "unknown");
            log_error("Built-in defaults failed to find a wallpaper. Output will show black screen.");
        }
    }

    return true;
}

/* Apply deferred configuration to output when surface becomes ready */
void output_apply_deferred_config(struct output_state *output) {
    if (!output) {
        return;
    }

    /* Check if output is ready for rendering */
    if (output->egl_surface == EGL_NO_SURFACE || !output->egl_window) {
        log_debug("Output %s not ready for deferred config application",
                  output->model[0] ? output->model : "unknown");
        return;
    }

    /* Check if there's a deferred config to apply */
    if (output->config.type == WALLPAPER_SHADER && output->config.shader_path[0] != '\0') {
        /* Check if shader is not yet loaded */
        if (output->live_shader_program == 0) {
            log_info("Applying deferred shader config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config.shader_path);
            output_set_shader(output, output->config.shader_path);
        }
    } else if (output->config.type == WALLPAPER_IMAGE && output->config.path[0] != '\0') {
        /* Check if wallpaper is not yet loaded */
        if (!output->current_image && output->texture == 0) {
            log_info("Applying deferred wallpaper config to output %s: %s",
                     output->model[0] ? output->model : "unknown",
                     output->config.path);
            output_set_wallpaper(output, output->config.path);
        }
    } else if (output->config.cycle && output->config.cycle_count > 0 && output->config.cycle_paths) {
        /* Handle cycling mode */
        if (!output->current_image && output->texture == 0 && output->live_shader_program == 0) {
            const char *initial_path = output->config.cycle_paths[output->config.current_cycle_index];
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

/* Get output count */
uint32_t output_get_count(struct staticwall_state *state) {
    if (!state) {
        return 0;
    }
    return state->output_count;
}

/* Iterate through all outputs and apply a function */
void output_foreach(struct staticwall_state *state,
                   void (*callback)(struct output_state *, void *),
                   void *userdata) {
    if (!state || !callback) {
        return;
    }

    struct output_state *output = state->outputs;
    while (output) {
        struct output_state *next = output->next;
        callback(output, userdata);
        output = next;
    }
}