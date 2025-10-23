#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"
#include "textures.h"

/* Note: Each transition manages its own shader sources in src/transitions/ */

/* Simple color shader for overlay effects */
static const char *color_vertex_shader =
    "#version 100\n"
    "attribute vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

static const char *color_fragment_shader =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform vec4 color;\n"
    "void main() {\n"
    "    gl_FragColor = color;\n"
    "}\n";

static GLuint color_overlay_program = 0;

/* Fullscreen quad vertices (position + texcoord) */
static const float quad_vertices[] = {
    /* positions */  /* texcoords */
    -1.0f,  1.0f,    0.0f, 0.0f,  /* top-left */
     1.0f,  1.0f,    1.0f, 0.0f,  /* top-right */
    -1.0f, -1.0f,    0.0f, 1.0f,  /* bottom-left */
     1.0f, -1.0f,    1.0f, 1.0f   /* bottom-right */
};

/* Helper: Cache uniform locations for a program */
static inline void cache_program_uniforms(struct output_state *output) {
    output->program_uniforms.position = glGetAttribLocation(output->program, "position");
    output->program_uniforms.texcoord = glGetAttribLocation(output->program, "texcoord");
    output->program_uniforms.tex_sampler = glGetUniformLocation(output->program, "texture0");
}

/* Helper: Cache uniform locations for transition shaders */
static inline void cache_transition_uniforms(GLuint program, struct output_state *output) {
    output->transition_uniforms.position = glGetAttribLocation(program, "position");
    output->transition_uniforms.texcoord = glGetAttribLocation(program, "texcoord");
    output->transition_uniforms.tex0 = glGetUniformLocation(program, "texture0");
    output->transition_uniforms.tex1 = glGetUniformLocation(program, "texture1");
    output->transition_uniforms.progress = glGetUniformLocation(program, "progress");
    output->transition_uniforms.resolution = glGetUniformLocation(program, "resolution");
}

/* Helper: Use program with state tracking to avoid redundant glUseProgram calls */
static inline void use_program_cached(struct output_state *output, GLuint program) {
    if (output->gl_state.active_program != program) {
        glUseProgram(program);
        output->gl_state.active_program = program;
    }
}

/* Helper: Bind texture with state tracking to avoid redundant glBindTexture calls */
static inline void bind_texture_cached(struct output_state *output, GLuint texture) {
    if (output->gl_state.bound_texture != texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        output->gl_state.bound_texture = texture;
    }
}

/* Helper: Enable/disable blending with state tracking */
static inline void set_blend_state(struct output_state *output, bool enable) {
    if (output->gl_state.blend_enabled != enable) {
        if (enable) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        output->gl_state.blend_enabled = enable;
    }
}

/* Initialize rendering for an output */
bool render_init_output(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for render_init_output");
        return false;
    }

    /* Context should already be current when this is called from egl.c */

    /* Initialize GL state cache */
    output->gl_state.bound_texture = 0;
    output->gl_state.active_program = 0;
    output->gl_state.blend_enabled = false;

    /* Initialize shader uniform cache to -2 (uninitialized) */
    output->shader_uniforms.position = -2;
    output->shader_uniforms.texcoord = -2;
    output->shader_uniforms.tex_sampler = -2;
    output->shader_uniforms.u_resolution = -2;
    output->shader_uniforms.u_time = -2;
    output->shader_uniforms.u_speed = -2;
    
    /* Initialize iChannel arrays to NULL (will be allocated when needed) */
    output->channel_textures = NULL;
    output->channel_count = 0;
    output->shader_uniforms.iChannel = NULL;

    /* Create simple color shader for overlays (once, shared across outputs) */
    if (color_overlay_program == 0) {
        if (!shader_create_program_from_sources(color_vertex_shader, color_fragment_shader, &color_overlay_program)) {
            log_error("Failed to create color overlay shader program");
            return false;
        }
        log_debug("Created color overlay shader program");
    }

    /* Create shader programs for transitions
     * Note: fade and slide share the same shader, so we use fade's program */
    if (!shader_create_fade_program(&output->program)) {
        log_error("Failed to create fade shader program for output");
        return false;
    }

    /* Cache uniform locations for main program */
    cache_program_uniforms(output);

    /* Create glitch shader program */
    if (!shader_create_glitch_program(&output->glitch_program)) {
        log_error("Failed to create glitch shader program for output %s", output->model);
        shader_destroy_program(output->program);
        return false;
    }

    /* Cache uniforms for glitch transitions */
    cache_transition_uniforms(output->glitch_program, output);

    /* Create pixelate shader program */
    if (!shader_create_pixelate_program(&output->pixelate_program)) {
        log_error("Failed to create pixelate shader program for output %s", output->model);
        shader_destroy_program(output->program);
        shader_destroy_program(output->glitch_program);
        return false;
    }

    /* Create persistent VBO with static data - eliminates per-frame uploads */
    glGenBuffers(1, &output->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during render init: 0x%x", error);
        return false;
    }

    log_debug("Rendering initialized for output %s", output->model);

    return true;
}

void render_cleanup_output(struct output_state *output) {
    if (!output) {
        return;
    }

    log_debug("Cleaning up rendering for output %s", output->model);

    /* Delete textures */
    if (output->texture != 0) {
        glDeleteTextures(1, &output->texture);
        output->texture = 0;
    }
    if (output->next_texture != 0) {
        glDeleteTextures(1, &output->next_texture);
        output->next_texture = 0;
    }
    
    /* Delete iChannel textures */
    if (output->channel_textures) {
        for (size_t i = 0; i < output->channel_count; i++) {
            if (output->channel_textures[i] != 0) {
                glDeleteTextures(1, &output->channel_textures[i]);
            }
        }
        free(output->channel_textures);
        output->channel_textures = NULL;
    }
    
    /* Free iChannel uniform array */
    if (output->shader_uniforms.iChannel) {
        free(output->shader_uniforms.iChannel);
        output->shader_uniforms.iChannel = NULL;
    }
    
    output->channel_count = 0;

    /* Delete VBO */
    if (output->vbo != 0) {
        glDeleteBuffers(1, &output->vbo);
        output->vbo = 0;
    }

    /* Delete programs */
    if (output->program != 0) {
        shader_destroy_program(output->program);
        output->program = 0;
    }

    if (output->glitch_program != 0) {
        shader_destroy_program(output->glitch_program);
        output->glitch_program = 0;
    }

    if (output->pixelate_program != 0) {
        shader_destroy_program(output->pixelate_program);
        output->pixelate_program = 0;
    }

    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }
}

/* Create texture from image data
 * Optimized: Set immutable texture parameters only once at creation
 * Memory optimization: Frees pixel data after GPU upload to save RAM */
GLuint render_create_texture(struct image_data *img) {
    if (!img || !img->pixels) {
        log_error("Invalid image data for texture creation");
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Set texture parameters once at creation - these rarely change
     * Most textures use LINEAR filtering and CLAMP_TO_EDGE wrapping
     * TILE mode textures update wrapping in render_frame as needed */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Upload texture data */
    GLenum format = (img->channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, img->width, img->height,
                 0, format, GL_UNSIGNED_BYTE, img->pixels);

    glBindTexture(GL_TEXTURE_2D, 0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error creating texture: 0x%x", error);
        glDeleteTextures(1, &texture);
        return 0;
    }

    log_debug("Created texture %u (%ux%u, %d channels)",
              texture, img->width, img->height, img->channels);

    /* Free pixel data after successful GPU upload - saves massive amounts of RAM!
     * For 4K display: 3840x2160x4 = 33MB saved per image
     * We keep the image_data struct for metadata (width, height, etc.) */
    image_free_pixels(img);
    log_debug("Freed pixel data for texture %u (memory optimization)", texture);

    return texture;
}

/**
 * Create texture from image for use in shaders (iChannel)
 * 
 * This version flips the image vertically to match OpenGL texture coordinates
 * where (0,0) is at bottom-left, while image files have (0,0) at top-left.
 * 
 * @param img Image data to create texture from
 * @return OpenGL texture ID, or 0 on failure
 */
GLuint render_create_texture_flipped(struct image_data *img) {
    if (!img || !img->pixels) {
        log_error("Invalid image data for texture creation");
        return 0;
    }

    /* Flip the image vertically for OpenGL texture coordinates */
    size_t row_size = img->width * img->channels;
    unsigned char *temp_row = malloc(row_size);
    if (!temp_row) {
        log_error("Failed to allocate memory for image flip");
        return 0;
    }

    unsigned char *pixels = img->pixels;
    for (uint32_t y = 0; y < img->height / 2; y++) {
        unsigned char *row_top = pixels + y * row_size;
        unsigned char *row_bottom = pixels + (img->height - 1 - y) * row_size;
        
        /* Swap rows */
        memcpy(temp_row, row_top, row_size);
        memcpy(row_top, row_bottom, row_size);
        memcpy(row_bottom, temp_row, row_size);
    }
    
    free(temp_row);
    log_debug("Flipped image vertically for OpenGL texture coordinates");

    /* Now create texture normally */
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum format = (img->channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, img->width, img->height,
                 0, format, GL_UNSIGNED_BYTE, img->pixels);

    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error creating texture: 0x%x", error);
        glDeleteTextures(1, &texture);
        return 0;
    }

    log_debug("Created flipped texture %u (%ux%u, %d channels) for shader use",
              texture, img->width, img->height, img->channels);

    image_free_pixels(img);
    log_debug("Freed pixel data for texture %u (memory optimization)", texture);

    return texture;
}

void render_destroy_texture(GLuint texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
}

/**
 * Load iChannel textures based on configuration
 * 
 * @param output Output state
 * @param config Wallpaper configuration with channel paths
 * @return true on success, false on failure
 */
bool render_load_channel_textures(struct output_state *output, struct wallpaper_config *config) {
    if (!output) {
        log_error("Invalid output for render_load_channel_textures");
        return false;
    }
    
    /* Clean up existing channels */
    if (output->channel_textures) {
        for (size_t i = 0; i < output->channel_count; i++) {
            if (output->channel_textures[i] != 0) {
                glDeleteTextures(1, &output->channel_textures[i]);
            }
        }
        free(output->channel_textures);
        output->channel_textures = NULL;
    }
    
    if (output->shader_uniforms.iChannel) {
        free(output->shader_uniforms.iChannel);
        output->shader_uniforms.iChannel = NULL;
    }
    
    /* Determine channel count - always at least 5 for default textures */
    size_t channel_count = 5;  /* Minimum 5 channels for default textures */
    if (config && config->channel_paths && config->channel_count > 5) {
        /* If config specifies MORE than 5 channels, use that count */
        channel_count = config->channel_count;
    }
    /* Note: If config specifies fewer than 5 channels, we still allocate 5
     * and fill the remaining with default textures */
    
    /* Allocate arrays */
    output->channel_textures = calloc(channel_count, sizeof(GLuint));
    output->shader_uniforms.iChannel = malloc(channel_count * sizeof(GLint));
    
    if (!output->channel_textures || !output->shader_uniforms.iChannel) {
        log_error("Failed to allocate memory for iChannel arrays");
        free(output->channel_textures);
        free(output->shader_uniforms.iChannel);
        output->channel_textures = NULL;
        output->shader_uniforms.iChannel = NULL;
        return false;
    }
    
    output->channel_count = channel_count;
    
    /* Initialize uniform locations to -2 (uninitialized) */
    for (size_t i = 0; i < channel_count; i++) {
        output->shader_uniforms.iChannel[i] = -2;
    }
    
    /* Load textures */
    for (size_t i = 0; i < channel_count; i++) {
        const char *path = NULL;
        bool is_default = true;
        
        /* Get path from config if available */
        if (config && config->channel_paths && i < config->channel_count) {
            path = config->channel_paths[i];
            /* Check if it's "_" (skip this channel) */
            if (path && strcmp(path, "_") == 0) {
                output->channel_textures[i] = 0;
                log_debug("iChannel%zu: skipped (placeholder)", i);
                continue;
            }
            is_default = false;
        }
        
        GLuint texture = 0;
        
        /* Try to load texture */
        if (path && !is_default) {
            /* Check if it's a named default texture */
            if (strcmp(path, TEXTURE_NAME_RGBA_NOISE) == 0 || strcmp(path, "default") == 0) {
                texture = texture_create_rgba_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                log_debug("iChannel%zu: %s", i, TEXTURE_NAME_RGBA_NOISE);
            } else if (strcmp(path, TEXTURE_NAME_GRAY_NOISE) == 0) {
                texture = texture_create_gray_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                log_debug("iChannel%zu: %s", i, TEXTURE_NAME_GRAY_NOISE);
            } else if (strcmp(path, TEXTURE_NAME_BLUE_NOISE) == 0) {
                texture = texture_create_blue_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                log_debug("iChannel%zu: %s", i, TEXTURE_NAME_BLUE_NOISE);
            } else if (strcmp(path, TEXTURE_NAME_WOOD) == 0) {
                texture = texture_create_wood(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                log_debug("iChannel%zu: %s", i, TEXTURE_NAME_WOOD);
            } else if (strcmp(path, TEXTURE_NAME_ABSTRACT) == 0) {
                texture = texture_create_abstract(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                log_debug("iChannel%zu: %s", i, TEXTURE_NAME_ABSTRACT);
            } else {
                /* Try to load as image file */
                struct image_data *img = image_load(path, 0, 0, MODE_FILL);
                if (img) {
                    /* Use flipped version for shader textures (OpenGL coordinates) */
                    texture = render_create_texture_flipped(img);
                    log_info("iChannel%zu: loaded from %s (%ux%u)", i, path, img->width, img->height);
                    image_free(img);
                } else {
                    log_error("Failed to load iChannel%zu texture from: %s", i, path);
                }
            }
        } else {
            /* Use default textures */
            switch (i) {
                case 0:
                    texture = texture_create_rgba_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (default)", i, TEXTURE_NAME_RGBA_NOISE);
                    break;
                case 1:
                    texture = texture_create_gray_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (default)", i, TEXTURE_NAME_GRAY_NOISE);
                    break;
                case 2:
                    texture = texture_create_blue_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (default)", i, TEXTURE_NAME_BLUE_NOISE);
                    break;
                case 3:
                    texture = texture_create_wood(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (default)", i, TEXTURE_NAME_WOOD);
                    break;
                case 4:
                    texture = texture_create_abstract(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (default)", i, TEXTURE_NAME_ABSTRACT);
                    break;
                default:
                    /* For additional channels beyond 5, use RGBA noise */
                    texture = texture_create_rgba_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                    log_debug("iChannel%zu: %s (fallback)", i, TEXTURE_NAME_RGBA_NOISE);
                    break;
            }
        }
        
        output->channel_textures[i] = texture;
        
        if (texture == 0) {
            log_error("iChannel%zu: failed to create texture, will be empty/black", i);
        }
    }
    
    log_info("Loaded %zu iChannel textures for output %s", channel_count, output->model);
    for (size_t i = 0; i < channel_count; i++) {
        log_info("  iChannel%zu: texture ID %u (from config: %s)", 
                 i, output->channel_textures[i], 
                 (config && config->channel_paths && i < config->channel_count) ? config->channel_paths[i] : "default");
    }
    return true;
}



/* Calculate vertex coordinates based on display mode for a specific image
 * This function is used by transition modules to properly size images during transitions */
void calculate_vertex_coords_for_image(struct output_state *output, 
                                        struct image_data *image, 
                                        float vertices[16]) {
    /* Default: fullscreen quad */
    memcpy(vertices, quad_vertices, sizeof(quad_vertices));
    
    if (!image) {
        return;
    }
    
    float img_width = (float)image->width;
    float img_height = (float)image->height;
    float disp_width = (float)output->width;
    float disp_height = (float)output->height;
    
    switch (output->config.mode) {
        case MODE_CENTER: {
            /* Center image at actual size (1:1 pixels) */
            if (img_width > disp_width || img_height > disp_height) {
                /* Image is larger than display - crop to show center portion */
                if (img_width > disp_width) {
                    /* Crop horizontally */
                    float crop_ratio = disp_width / img_width;
                    float crop_offset = (1.0f - crop_ratio) / 2.0f;
                    vertices[2] = crop_offset;        /* top-left texcoord */
                    vertices[6] = 1.0f - crop_offset; /* top-right texcoord */
                    vertices[10] = crop_offset;       /* bottom-left texcoord */
                    vertices[14] = 1.0f - crop_offset; /* bottom-right texcoord */
                }
                if (img_height > disp_height) {
                    /* Crop vertically */
                    float crop_ratio = disp_height / img_height;
                    float crop_offset = (1.0f - crop_ratio) / 2.0f;
                    vertices[3] = crop_offset;        /* top-left texcoord */
                    vertices[7] = crop_offset;        /* top-right texcoord */
                    vertices[11] = 1.0f - crop_offset; /* bottom-left texcoord */
                    vertices[15] = 1.0f - crop_offset; /* bottom-right texcoord */
                }
                /* Vertices stay fullscreen to fill display */
            } else {
                /* Image is smaller than display - center it */
                float scale_x = img_width / disp_width;
                float scale_y = img_height / disp_height;
                
                vertices[0] = -scale_x; vertices[1] = scale_y;   /* top-left position */
                vertices[4] = scale_x;  vertices[5] = scale_y;   /* top-right position */
                vertices[8] = -scale_x; vertices[9] = -scale_y;  /* bottom-left position */
                vertices[12] = scale_x; vertices[13] = -scale_y; /* bottom-right position */
            }
            break;
        }
        
        case MODE_FIT: {
            /* Scale to fit inside display (image was pre-scaled) */
            float scale_x = img_width / disp_width;
            float scale_y = img_height / disp_height;
            
            vertices[0] = -scale_x; vertices[1] = scale_y;   /* top-left position */
            vertices[4] = scale_x;  vertices[5] = scale_y;   /* top-right position */
            vertices[8] = -scale_x; vertices[9] = -scale_y;  /* bottom-left position */
            vertices[12] = scale_x; vertices[13] = -scale_y; /* bottom-right position */
            break;
        }
        
        case MODE_FILL: {
            /* Image was pre-scaled to fill display, crop excess to center */
            /* The scaled image is larger than display in one dimension */
            if (img_width > disp_width) {
                /* Image is wider - crop horizontal */
                float crop_ratio = disp_width / img_width;
                float crop_offset = (1.0f - crop_ratio) / 2.0f;
                vertices[2] = crop_offset;        /* top-left texcoord */
                vertices[6] = 1.0f - crop_offset; /* top-right texcoord */
                vertices[10] = crop_offset;       /* bottom-left texcoord */
                vertices[14] = 1.0f - crop_offset; /* bottom-right texcoord */
            } else if (img_height > disp_height) {
                /* Image is taller - crop vertical */
                float crop_ratio = disp_height / img_height;
                float crop_offset = (1.0f - crop_ratio) / 2.0f;
                vertices[3] = crop_offset;        /* top-left texcoord */
                vertices[7] = crop_offset;        /* top-right texcoord */
                vertices[11] = 1.0f - crop_offset; /* bottom-left texcoord */
                vertices[15] = 1.0f - crop_offset; /* bottom-right texcoord */
            }
            /* else: image fits exactly, no cropping needed */
            break;
        }
        
        case MODE_STRETCH:
            /* Stretch to fill entire screen - use default fullscreen quad */
            break;
            
        case MODE_TILE: {
            /* Tile image across screen - adjust texture coordinates */
            float tile_x = disp_width / img_width;
            float tile_y = disp_height / img_height;
            
            vertices[2] = 0.0f;    vertices[3] = 0.0f;      /* top-left texcoord */
            vertices[6] = tile_x;  vertices[7] = 0.0f;      /* top-right texcoord */
            vertices[10] = 0.0f;   vertices[11] = tile_y;   /* bottom-left texcoord */
            vertices[14] = tile_x; vertices[15] = tile_y;   /* bottom-right texcoord */
            break;
        }
    }
}

/* Calculate vertex coordinates based on display mode (uses current_image) */
static void calculate_vertex_coords(struct output_state *output, float vertices[16]) {
    calculate_vertex_coords_for_image(output, output->current_image, vertices);
}



/* Render shader wallpaper frame
 * Optimized: Uses state tracking and eliminates redundant GL calls */
bool render_frame_shader(struct output_state *output) {
    if (!output || output->live_shader_program == 0) {
        log_error("Invalid output or shader program for render_frame_shader");
        return false;
    }

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);

    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Use live shader program with state tracking */
    use_program_cached(output, output->live_shader_program);

    /* Calculate elapsed time for animation (continuous time since shader loaded) */
    uint64_t current_time = get_time_ms();
    uint64_t start_time = output->shader_start_time > 0 ? output->shader_start_time : output->last_frame_time;
    float time = (current_time - start_time) / (float)MS_PER_SECOND;
    
    /* Apply shader speed multiplier */
    float shader_speed = output->config.shader_speed > 0.0f ? output->config.shader_speed : 1.0f;
    time *= shader_speed;

    /* Cache shader uniform locations on first use to eliminate per-frame lookups */
    if (output->shader_uniforms.position == -2) {
        /* -2 means uninitialized, -1 means not found, >= 0 is valid location */
        output->shader_uniforms.position = glGetAttribLocation(output->live_shader_program, "position");
        output->shader_uniforms.u_time = glGetUniformLocation(output->live_shader_program, "time");
        output->shader_uniforms.u_resolution = glGetUniformLocation(output->live_shader_program, "resolution");
        
        /* Cache iChannel sampler locations */
        if (output->shader_uniforms.iChannel && output->channel_count > 0) {
            for (size_t i = 0; i < output->channel_count; i++) {
                char sampler_name[32];
                snprintf(sampler_name, sizeof(sampler_name), "iChannel%zu", i);
                output->shader_uniforms.iChannel[i] = glGetUniformLocation(output->live_shader_program, sampler_name);
            }
            log_debug("Cached %zu iChannel uniform locations", output->channel_count);
        }
    }

    /* Set uniforms using cached locations */
    if (output->shader_uniforms.u_time >= 0) {
        glUniform1f(output->shader_uniforms.u_time, time);
    }

    if (output->shader_uniforms.u_resolution >= 0) {
        glUniform2f(output->shader_uniforms.u_resolution, (float)output->width, (float)output->height);
    }

    /* Bind iChannel textures if they exist */
    if (output->channel_textures && output->shader_uniforms.iChannel) {
        static bool logged_once = false;
        for (size_t i = 0; i < output->channel_count; i++) {
            if (output->channel_textures[i] != 0 && output->shader_uniforms.iChannel[i] >= 0) {
                glActiveTexture(GL_TEXTURE0 + (GLenum)i);
                glBindTexture(GL_TEXTURE_2D, output->channel_textures[i]);
                glUniform1i(output->shader_uniforms.iChannel[i], (GLint)i);
                if (!logged_once) {
                    log_info("BINDING: iChannel%zu -> texture ID %u -> texture unit %d -> uniform location %d", 
                             i, output->channel_textures[i], (int)i, output->shader_uniforms.iChannel[i]);
                }
            }
        }
        if (!logged_once) {
            logged_once = true;
        }
    }

    /* Use cached position attribute location */
    GLint pos_attrib = output->shader_uniforms.position;
    if (pos_attrib < 0) {
        log_error("Failed to get 'position' attribute location in shader");
        return false;
    }

    /* Bind persistent VBO - no need to upload data every frame!
     * The fullscreen quad data is already in the VBO from render_init_output.
     * We just reinterpret it: first 2 floats of each vertex are positions. */
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);

    /* Set up vertex attributes - stride of 4 floats to skip texcoords */
    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(pos_attrib);

    /* Draw fullscreen quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up */
    glDisableVertexAttribArray(pos_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Handle cross-fade transition when switching shaders */
    const uint64_t FADE_OUT_MS = SHADER_FADE_OUT_MS;  /* Fade to black duration */
    const uint64_t FADE_IN_MS = SHADER_FADE_IN_MS;    /* Fade from black duration */
    const uint64_t TOTAL_FADE_MS = FADE_OUT_MS + FADE_IN_MS;
    
    if (output->shader_fade_start_time > 0) {
        uint64_t fade_elapsed = current_time - output->shader_fade_start_time;
        
        /* Keep redrawing during fade animation */
        output->needs_redraw = true;
        
        if (fade_elapsed < FADE_OUT_MS) {
            /* Phase 1: Fade out to black (0ms -> 400ms) */
            float fade_out_progress = (float)fade_elapsed / (float)FADE_OUT_MS;
            /* Ease-in cubic for smooth acceleration */
            float eased = fade_out_progress * fade_out_progress * fade_out_progress;
            float fade_alpha = eased; /* 0.0 -> 1.0 (transparent to black) */
            
            log_debug("Cross-fade phase 1: fade_out %.2f, alpha %.2f", fade_out_progress, fade_alpha);
            
            /* Draw black overlay with state tracking */
            set_blend_state(output, true);
            use_program_cached(output, color_overlay_program);
            
            GLint color_uniform = glGetUniformLocation(color_overlay_program, "color");
            if (color_uniform >= 0) {
                glUniform4f(color_uniform, 0.0f, 0.0f, 0.0f, fade_alpha);
            }
            
            /* Use persistent VBO - no upload needed */
            glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
            
            GLint fade_pos_attrib = glGetAttribLocation(color_overlay_program, "position");
            if (fade_pos_attrib >= 0) {
                glVertexAttribPointer(fade_pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(fade_pos_attrib);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glDisableVertexAttribArray(fade_pos_attrib);
            }
            
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        } 
        else if (fade_elapsed >= FADE_OUT_MS && fade_elapsed < TOTAL_FADE_MS) {
            /* Phase 2: Switch shader at blackout point, then fade in */
            if (output->pending_shader_path[0] != '\0') {
                /* Ensure EGL context is current before compiling shader */
                if (output->egl_surface != EGL_NO_SURFACE && output->state) {
                    if (!eglMakeCurrent(output->state->egl_display, output->egl_surface,
                                       output->egl_surface, output->state->egl_context)) {
                        log_error("Failed to make EGL context current during shader swap: 0x%x", eglGetError());
                        output->shader_fade_start_time = 0;
                        output->pending_shader_path[0] = '\0';
                        /* Continue with current shader */
                        return true;
                    }
                }
                
                /* Reload iChannel textures for the new shader (config may have changed) */
                if (!render_load_channel_textures(output, &output->config)) {
                    log_error("Failed to reload iChannel textures for new shader: %s", output->pending_shader_path);
                    /* Continue anyway - shader may work without textures */
                }
                
                /* Load the new shader */
                GLuint new_shader_program = 0;
                if (shader_create_live_program(output->pending_shader_path, &new_shader_program, output->channel_count)) {
                    /* Validate the new shader program before destroying old one */
                    if (new_shader_program == 0) {
                        log_error("Invalid shader program created for: %s", output->pending_shader_path);
                        output->shader_fade_start_time = 0;
                        output->pending_shader_path[0] = '\0';
                        return true;
                    }
                    
                    /* Destroy old shader and switch to new one */
                    shader_destroy_program(output->live_shader_program);
                    output->live_shader_program = new_shader_program;
                    output->shader_start_time = current_time;
                    
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
                    
                    /* Update config with new shader path */
                    strncpy(output->config.shader_path, output->pending_shader_path, 
                            sizeof(output->config.shader_path) - 1);
                    output->config.shader_path[sizeof(output->config.shader_path) - 1] = '\0';
                    
                    /* Write state to file */
                    const char *mode_str = wallpaper_mode_to_string(output->config.mode);
                    write_wallpaper_state(output->model, output->pending_shader_path, mode_str,
                                         output->config.current_cycle_index,
                                         output->config.cycle_count);
                    
                    log_info("Shader switched during cross-fade: %s", output->pending_shader_path);
                    
                    /* Clear pending path */
                    output->pending_shader_path[0] = '\0';
                } else {
                    log_error("Failed to load pending shader: %s", output->pending_shader_path);
                    output->shader_fade_start_time = 0;
                    output->pending_shader_path[0] = '\0';
                }
            }
            
            /* Fade in from black after cross-fade completes */
            float fade_in_progress = (float)(fade_elapsed - FADE_OUT_MS) / (float)FADE_IN_MS;
            /* Ease-out cubic for smooth deceleration */
            float eased = 1.0f - powf(1.0f - fade_in_progress, 3.0f);
            float fade_alpha = 1.0f - eased; /* 1.0 -> 0.0 (black to transparent) */
            
            log_debug("Cross-fade phase 3: fade_in %.2f, alpha %.2f", fade_in_progress, fade_alpha);
            
            /* Draw black overlay with state tracking */
            set_blend_state(output, true);
            use_program_cached(output, color_overlay_program);
            
            GLint color_uniform = glGetUniformLocation(color_overlay_program, "color");
            if (color_uniform >= 0) {
                glUniform4f(color_uniform, 0.0f, 0.0f, 0.0f, fade_alpha);
            }
            
            /* Use persistent VBO - no upload needed */
            glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
            
            GLint fade_pos_attrib = glGetAttribLocation(color_overlay_program, "position");
            if (fade_pos_attrib >= 0) {
                glVertexAttribPointer(fade_pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(fade_pos_attrib);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glDisableVertexAttribArray(fade_pos_attrib);
            }
            
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        else {
            /* Phase 3: Fade complete, reset fade state */
            log_info("Cross-fade complete");
            output->shader_fade_start_time = 0;
        }
    }

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during shader rendering: 0x%x", error);
        return false;
    }

    /* Shader wallpapers need continuous redraw for animation */
    output->needs_redraw = true;
    output->frames_rendered++;

    return true;
}

/* Render a frame for an output
 * Optimized: Uses cached uniforms, state tracking, and persistent VBO */
bool render_frame(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for render_frame");
        return false;
    }

    /* Check if this is a shader wallpaper */
    if (output->config.type == WALLPAPER_SHADER) {
        return render_frame_shader(output);
    }

    if (!output->current_image || output->texture == 0) {
        /* No wallpaper loaded yet */
        return true;
    }

    /* Check if we're in a transition */
    if (output->transition_start_time > 0 && 
        output->config.transition != TRANSITION_NONE &&
        output->next_image && output->next_texture) {
        /* Use transition rendering */
        return render_frame_transition(output, output->transition_progress);
    }

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);

    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Use shader program with state tracking */
    use_program_cached(output, output->program);

    /* Use cached attribute locations - no glGetAttribLocation calls */
    GLint pos_attrib = output->program_uniforms.position;
    GLint tex_attrib = output->program_uniforms.texcoord;

    /* Calculate mode-aware vertex coordinates */
    float mode_vertices[16];
    calculate_vertex_coords(output, mode_vertices);

    /* Bind VBO and update with mode-specific vertices
     * NOTE: This still uses DYNAMIC_DRAW because vertices change per mode
     * For most modes (stretch/fill), we could use the static quad, but
     * CENTER/FIT/TILE need per-frame vertex adjustments */
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(mode_vertices), mode_vertices, GL_DYNAMIC_DRAW);

    /* Set up vertex attributes */
    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(pos_attrib);

    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_attrib);

    /* Bind texture with state tracking */
    glActiveTexture(GL_TEXTURE0);
    bind_texture_cached(output, output->texture);

    /* Set texture unit uniform - use cached location */
    if (output->program_uniforms.tex_sampler >= 0) {
        glUniform1i(output->program_uniforms.tex_sampler, 0);
    }

    /* Set alpha uniform (for transitions) - lookup once per frame is acceptable */
    GLint alpha_uniform = glGetUniformLocation(output->program, "alpha");
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, 1.0f);
    }

    /* Handle tile mode texture wrapping - only change when needed */
    if (output->config.mode == MODE_TILE) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Enable blending with state tracking */
    set_blend_state(output, true);

    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up - disable attributes but leave state cached */
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(tex_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during rendering: 0x%x", error);
        return false;
    }

    output->needs_redraw = false;
    output->frames_rendered++;

    return true;
}

/* Render frame with transition effect
 * Dispatches to modular transition implementations */
bool render_frame_transition(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        return render_frame(output);
    }

    if (output->texture == 0 || output->next_texture == 0) {
        return render_frame(output);
    }

    /* Dispatch to modular transition renderer */
    return transition_render(output, output->config.transition, progress);
}
