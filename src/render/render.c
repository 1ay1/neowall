#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "neowall.h"
#include "config_access.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"
#include "textures.h"
#include "compositor.h"

/* Helper function to get the preferred output identifier
 * Prefers connector_name (e.g., "HDMI-A-2", "DP-1") over model name
 * for consistent identification across reboots/reconnections */
static inline const char *output_get_identifier(const struct output_state *output) {
    if (output->connector_name[0] != '\0') {
        return output->connector_name;
    }
    return output->model;
}

/* Forward declarations */
static bool render_frame_transition(struct output_state *output, float progress);

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

/* Simple 5x7 bitmap font for FPS display (digits 0-9, dot, space, and 'FPS') */
static const uint8_t font_5x7[][7] = {
    /* 0 */ {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    /* 1 */ {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    /* 2 */ {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    /* 3 */ {0x0E, 0x11, 0x01, 0x0E, 0x01, 0x11, 0x0E},
    /* 4 */ {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    /* 5 */ {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    /* 6 */ {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    /* 7 */ {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    /* 8 */ {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    /* 9 */ {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    /* . */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    /* space */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* F */ {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    /* P */ {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    /* S */ {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
};

/* Draw a single character at screen position */
static void draw_char_at(int char_index, float x, float y, float char_width, float char_height,
                         int screen_width, int screen_height) {
    if (char_index < 0 || char_index >= 15) return;
    
    const uint8_t *bitmap = font_5x7[char_index];
    float pixel_width = char_width / 5.0f;
    float pixel_height = char_height / 7.0f;
    
    /* Draw each pixel of the character */
    for (int row = 0; row < 7; row++) {
        uint8_t line = bitmap[row];
        for (int col = 0; col < 5; col++) {
            if (line & (1 << (4 - col))) {
                /* Convert screen coords to NDC */
                float px = x + col * pixel_width;
                float py = y + row * pixel_height;
                
                float left = (px / screen_width) * 2.0f - 1.0f;
                float right = ((px + pixel_width) / screen_width) * 2.0f - 1.0f;
                float top = 1.0f - (py / screen_height) * 2.0f;
                float bottom = 1.0f - ((py + pixel_height) / screen_height) * 2.0f;
                
                /* Draw pixel as quad */
                float quad[8] = {
                    left, top,
                    right, top,
                    left, bottom,
                    right, bottom
                };
                
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }
    }
}

/* Render FPS watermark overlay */
static void render_fps_watermark(struct output_state *output) {
    if (!output || !output->config->show_fps) return;
    if (output->fps_current <= 0.0f) return;
    
    /* Format FPS text */
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "%.1f FPS", output->fps_current);
    
    /* Enable blending for semi-transparent background */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* Use color shader */
    glUseProgram(color_overlay_program);
    GLint pos_attrib = glGetAttribLocation(color_overlay_program, "position");
    GLint color_uniform = glGetUniformLocation(color_overlay_program, "color");
    
    if (pos_attrib < 0 || color_uniform < 0) return;
    
    /* Create temporary VBO for text rendering */
    GLuint text_vbo;
    glGenBuffers(1, &text_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    
    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(pos_attrib);
    
    /* Position at bottom-right corner to avoid taskbar/waybar */
    float char_width = 12.0f;
    float char_height = 18.0f;
    float text_width = strlen(fps_text) * char_width;
    float text_x = output->width - text_width - 10.0f;
    float text_y = output->height - char_height - 10.0f;
    
    /* Draw black shadow/outline for visibility on any background */
    glUniform4f(color_uniform, 0.0f, 0.0f, 0.0f, 1.0f);
    
    float cursor_x = text_x;
    float cursor_y = text_y;
    
    /* Draw shadow at 1px offset */
    for (size_t i = 0; i < strlen(fps_text); i++) {
        char c = fps_text[i];
        int char_idx = -1;
        
        if (c >= '0' && c <= '9') {
            char_idx = c - '0';
        } else if (c == '.') {
            char_idx = 10;
        } else if (c == ' ') {
            char_idx = 11;
        } else if (c == 'F') {
            char_idx = 12;
        } else if (c == 'P') {
            char_idx = 13;
        } else if (c == 'S') {
            char_idx = 14;
        }
        
        if (char_idx >= 0) {
            draw_char_at(char_idx, cursor_x + 1, cursor_y + 1, char_width, char_height,
                        output->width, output->height);
        }
        
        cursor_x += char_width;
    }
    
    /* Draw text in bright green */
    glUniform4f(color_uniform, 0.0f, 1.0f, 0.0f, 1.0f);
    
    cursor_x = text_x;
    cursor_y = text_y;
    
    for (size_t i = 0; i < strlen(fps_text); i++) {
        char c = fps_text[i];
        int char_idx = -1;
        
        if (c >= '0' && c <= '9') {
            char_idx = c - '0';
        } else if (c == '.') {
            char_idx = 10;
        } else if (c == ' ') {
            char_idx = 11;
        } else if (c == 'F') {
            char_idx = 12;
        } else if (c == 'P') {
            char_idx = 13;
        } else if (c == 'S') {
            char_idx = 14;
        }
        
        if (char_idx >= 0) {
            draw_char_at(char_idx, cursor_x, cursor_y, char_width, char_height,
                        output->width, output->height);
        }
        
        cursor_x += char_width;
    }
    
    glDisableVertexAttribArray(pos_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &text_vbo);
}

/* Global cache for default iChannel textures (generated once, reused forever) */
static GLuint cached_default_channel_textures[5] = {0, 0, 0, 0, 0};
static bool default_channels_initialized = false;

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
    
    /* Note: Caller MUST ensure EGL context is current before calling this function */

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
    
    /* Note: Caller MUST ensure EGL context is current before calling this function */

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
            } else if (strcmp(path, TEXTURE_NAME_GRAY_NOISE) == 0) {
                texture = texture_create_gray_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
            } else if (strcmp(path, TEXTURE_NAME_BLUE_NOISE) == 0) {
                texture = texture_create_blue_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
            } else if (strcmp(path, TEXTURE_NAME_WOOD) == 0) {
                texture = texture_create_wood(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
            } else if (strcmp(path, TEXTURE_NAME_ABSTRACT) == 0) {
                texture = texture_create_abstract(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
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
            /* Use cached default textures (generate once, reuse forever) */
            if (!default_channels_initialized) {
                cached_default_channel_textures[0] = texture_create_rgba_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                cached_default_channel_textures[1] = texture_create_gray_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                cached_default_channel_textures[2] = texture_create_blue_noise(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                cached_default_channel_textures[3] = texture_create_wood(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                cached_default_channel_textures[4] = texture_create_abstract(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
                default_channels_initialized = true;
            }
            
            /* Reuse cached texture */
            if (i < 5) {
                texture = cached_default_channel_textures[i];
            } else {
                /* For channels beyond 5, use channel 0's texture */
                texture = cached_default_channel_textures[0];
            }
        }
        
        output->channel_textures[i] = texture;
        
        if (texture == 0) {
            log_error("iChannel%zu: failed to create texture, will be empty/black", i);
        }
    }
    
    return true;
}

/**
 * Update a single iChannel texture with a new image
 * 
 * This is used for cycling images through a shader effect - the shader stays
 * the same but we update iChannel0 with each new image from the cycle.
 * 
 * @param output Output state
 * @param channel_index Index of the channel to update (typically 0)
 * @param image_path Path to the image file
 * @return true on success, false on failure
 */
bool render_update_channel_texture(struct output_state *output, size_t channel_index, const char *image_path) {
    if (!output || !image_path) {
        log_error("Invalid parameters for render_update_channel_texture");
        return false;
    }
    
    if (channel_index >= output->channel_count) {
        log_error("Channel index %zu out of bounds (max %zu)", channel_index, output->channel_count);
        return false;
    }
    
    if (!output->channel_textures) {
        log_error("Channel textures not initialized");
        return false;
    }
    
    /* CRITICAL: Ensure EGL context is current before GL operations */
    if (!output->compositor_surface || !eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for texture update");
        return false;
    }
    
    /* Load the new image */
    struct image_data *img = image_load(image_path, 0, 0, MODE_FILL);
    if (!img) {
        log_error("Failed to load image for iChannel%zu: %s", channel_index, image_path);
        return false;
    }
    
    /* Delete old texture if it exists */
    if (output->channel_textures[channel_index] != 0) {
        glDeleteTextures(1, &output->channel_textures[channel_index]);
    }
    
    /* Create new flipped texture for shader use */
    GLuint texture = render_create_texture_flipped(img);
    if (texture == 0) {
        log_error("Failed to create texture for iChannel%zu from: %s", channel_index, image_path);
        image_free(img);
        return false;
    }
    
    /* Update the channel texture */
    output->channel_textures[channel_index] = texture;
    
    log_info("Updated iChannel%zu with image: %s (%ux%u) -> texture ID %u", 
             channel_index, image_path, img->width, img->height, texture);
    
    image_free(img);
    
    /* Mark for redraw */
    output->needs_redraw = true;
    
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
    
    /* All display modes now pre-process images to exact display size during load time:
     * - MODE_FILL: scaled and center-cropped to exact size
     * - MODE_FIT: scaled and padded with black borders to exact size
     * - MODE_CENTER: kept at 1:1 pixels, cropped or padded to exact size
     * - MODE_STRETCH: scaled to exact size
     * - MODE_TILE: physically tiled to exact size
     * 
     * This means we can use a simple fullscreen quad for all modes,
     * ensuring consistent rendering during both transitions and normal display.
     * No special vertex or texture coordinate calculations needed! */
    
    (void)output; /* Unused but kept for API consistency */
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
    
    /* Validate EGL context is still valid */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for shader rendering (display may be disconnected)");
        return false;
    }
    
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for shader rendering (display may be disconnected)");
        return false;
    }
    
    /* CRITICAL: Ensure EGL context is current on this thread before any GL operations */
    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for shader rendering");
        return false;
    }

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);
    
    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* CRITICAL: Always use glUseProgram for shader rendering (not cached) 
     * GL state is per-context, not per-output. When switching between outputs,
     * the cached state can be stale, causing GL_INVALID_OPERATION errors.
     * Shaders are performance-critical, so we can't afford stale state. */
    glUseProgram(output->live_shader_program);
    
    /* Validate program is actually linked and ready */
    GLint link_status = 0;
    glGetProgramiv(output->live_shader_program, GL_LINK_STATUS, &link_status);
    if (link_status == GL_FALSE) {
        log_error("Shader program %u not linked properly, skipping frame", output->live_shader_program);
        return false;
    }

    /* Calculate elapsed time for animation (continuous time since shader loaded) */
    uint64_t current_time = get_time_ms();
    uint64_t start_time = output->shader_start_time > 0 ? output->shader_start_time : output->last_frame_time;
    float time = (current_time - start_time) / (float)MS_PER_SECOND;
    
    /* Apply shader speed multiplier */
    float shader_speed = output->config->shader_speed > 0.0f ? output->config->shader_speed : 1.0f;
    time *= shader_speed;

    /* Cache shader uniform locations on first use to eliminate per-frame lookups */
    if (output->shader_uniforms.position == -2) {
        /* -2 means uninitialized, -1 means not found, >= 0 is valid location */
        output->shader_uniforms.position = glGetAttribLocation(output->live_shader_program, "position");
        output->shader_uniforms.u_time = glGetUniformLocation(output->live_shader_program, "_neowall_time");
        output->shader_uniforms.u_resolution = glGetUniformLocation(output->live_shader_program, "_neowall_resolution");
        
        /* Also get iResolution uniform location (Shadertoy vec3) */
        GLint iResolution_loc = glGetUniformLocation(output->live_shader_program, "iResolution");
        if (iResolution_loc >= 0) {
            /* Set iResolution as vec3(width, height, aspect_ratio) */
            float aspect = (float)output->width / (float)output->height;
            glUniform3f(iResolution_loc, (float)output->width, (float)output->height, aspect);
        }
        
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
    
    /* BUGFIX: Re-cache iChannel uniforms if they were reset (e.g., after cycle)
     * This can happen when channel textures are reloaded but position uniform is already cached */
    if (output->shader_uniforms.iChannel && output->channel_count > 0 && 
        output->shader_uniforms.iChannel[0] == -2) {
        for (size_t i = 0; i < output->channel_count; i++) {
            char sampler_name[32];
            snprintf(sampler_name, sizeof(sampler_name), "iChannel%zu", i);
            output->shader_uniforms.iChannel[i] = glGetUniformLocation(output->live_shader_program, sampler_name);
        }
        log_debug("Re-cached %zu iChannel uniform locations after reset", output->channel_count);
    }

    /* Set uniforms using cached locations */
    if (output->shader_uniforms.u_time >= 0) {
        glUniform1f(output->shader_uniforms.u_time, time);
    }

    if (output->shader_uniforms.u_resolution >= 0) {
        glUniform2f(output->shader_uniforms.u_resolution, (float)output->width, (float)output->height);
    }
    
    /* Also try direct uniform names for non-Shadertoy shaders (cached on first use) */
    static GLint time_loc_cached = -2;
    static GLint resolution_loc_cached = -2;
    static GLuint cached_program_id = 0;
    
    /* Re-cache if program changed */
    if (cached_program_id != output->live_shader_program) {
        time_loc_cached = glGetUniformLocation(output->live_shader_program, "time");
        resolution_loc_cached = glGetUniformLocation(output->live_shader_program, "resolution");
        cached_program_id = output->live_shader_program;
    }
    
    if (time_loc_cached >= 0) {
        glUniform1f(time_loc_cached, time);
    }
    
    if (resolution_loc_cached >= 0) {
        glUniform2f(resolution_loc_cached, (float)output->width, (float)output->height);
    }
    
    /* Update iResolution uniform (Shadertoy vec3) - cache location */
    static GLint iResolution_loc_cached = -2;
    if (cached_program_id != output->live_shader_program || iResolution_loc_cached == -2) {
        iResolution_loc_cached = glGetUniformLocation(output->live_shader_program, "iResolution");
    }
    
    if (iResolution_loc_cached >= 0) {
        float aspect = (float)output->width / (float)output->height;
        glUniform3f(iResolution_loc_cached, (float)output->width, (float)output->height, aspect);
    }

    /* Bind iChannel textures if they exist */
    if (output->channel_textures && output->shader_uniforms.iChannel) {
        /* Query max texture units to avoid GL_INVALID_OPERATION */
        static GLint max_texture_units = -1;
        if (max_texture_units == -1) {
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_texture_units);
            log_debug("GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: %d", max_texture_units);
        }
        
        /* Clear any pre-existing GL errors before texture binding */
        while (glGetError() != GL_NO_ERROR) {
            /* Drain error queue */
        }
        
        for (size_t i = 0; i < output->channel_count; i++) {
            /* Skip if texture doesn't exist */
            if (output->channel_textures[i] == 0) {
                continue;
            }
            
            /* Skip if uniform location is invalid */
            if (output->shader_uniforms.iChannel[i] < 0) {
                continue;
            }
            
            /* Validate texture unit index */
            if ((GLint)i >= max_texture_units) {
                log_error("iChannel%zu exceeds max texture units (%d), skipping", i, max_texture_units);
                break;
            }
            
            glActiveTexture(GL_TEXTURE0 + (GLenum)i);
            glBindTexture(GL_TEXTURE_2D, output->channel_textures[i]);
            glUniform1i(output->shader_uniforms.iChannel[i], (GLint)i);
        }
        
        /* Check for errors after all texture bindings */
        GLenum tex_error = glGetError();
        if (tex_error != GL_NO_ERROR) {
            static uint64_t last_error_log = 0;
            uint64_t now = get_time_ms();
            /* Throttle error logging to once per second */
            if (now - last_error_log > 1000) {
                log_error("GL error after binding iChannel textures: 0x%x", tex_error);
                last_error_log = now;
            }
        }
        
        /* Reset to texture unit 0 */
        glActiveTexture(GL_TEXTURE0);
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

    /* Disable alpha channel writes - force opaque output (prevents transparent shaders from showing white) */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    /* Draw fullscreen quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    /* Re-enable alpha channel writes */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

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
                if (output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE && output->state) {
                    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                                       output->compositor_surface->egl_surface, output->state->egl_context)) {
                        log_error("Failed to make EGL context current during shader swap: 0x%x", eglGetError());
                        output->shader_fade_start_time = 0;
                        output->pending_shader_path[0] = '\0';
                        /* Continue with current shader */
                        return true;
                    }
                }
                
                /* Reload iChannel textures for the new shader (config may have changed) */
                if (!render_load_channel_textures(output, output->config)) {
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
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wstringop-truncation"
                    strncpy(output->config->shader_path, output->pending_shader_path, 
                            sizeof(output->config->shader_path) - 1);
                    #pragma GCC diagnostic pop
                    output->config->shader_path[sizeof(output->config->shader_path) - 1] = '\0';
                    
                    /* Write state to file */
                    const char *mode_str = wallpaper_mode_to_string(output->config->mode);
                    write_wallpaper_state(output_get_identifier(output), output->pending_shader_path, mode_str,
                                         output->config->current_cycle_index,
                                         output->config->cycle_count,
                                         "active");
                    
                    log_info("Shader switched during cross-fade: %s", output->pending_shader_path);
                    
                    /* Clear pending path */
                    output->pending_shader_path[0] = '\0';
                } else {
                    log_error("Failed to load pending shader: %s", output->pending_shader_path);
                    
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

    /* Render FPS watermark if enabled */
    render_fps_watermark(output);

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
    
    /* CRITICAL: Ensure EGL context is current before any GL operations */
    if (output->state && output->state->egl_display != EGL_NO_DISPLAY &&
        output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                           output->compositor_surface->egl_surface, output->state->egl_context)) {
            log_error("Failed to make EGL context current for rendering");
            return false;
        }
        
        /* CRITICAL: Invalidate GL state cache when switching contexts
         * All outputs share the same EGL context but have different surfaces.
         * When we switch surfaces, the GL state (bound textures, programs, etc.) 
         * persists from the previous surface, but our cache is per-output.
         * We must invalidate the cache to force rebinding. */
        output->gl_state.bound_texture = 0;
        output->gl_state.active_program = 0;
        output->gl_state.blend_enabled = false;
    }

    /* Handle shader wallpapers */
    /* Validate EGL context is still valid */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for rendering (display may be disconnected)");
        return false;
    }
    
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for rendering (display may be disconnected)");
        return false;
    }

    /* Check if this is a shader wallpaper */
    if (output->config->type == WALLPAPER_SHADER) {
        /* Check if shader loading has permanently failed */
        if (output->shader_load_failed) {
            /* Permanently failed - don't spam logs, just return false silently */
            return false;
        }
        
        /* Defensive check: ensure shader program is actually loaded */
        if (output->live_shader_program == 0) {
            /* Track reload attempts to prevent infinite spam */
            static uint64_t last_reload_attempt_time = 0;
            static int consecutive_failures = 0;
            uint64_t current_time = get_time_ms();
            
            /* Only attempt reload once per second max, and give up after 3 failures */
            if (current_time - last_reload_attempt_time >= 1000 && consecutive_failures < 3) {
                log_error("Config type is SHADER but shader program not loaded for output %s", 
                         output->model[0] ? output->model : "unknown");
                log_error("This may happen after config reload. Attempting to reload shader (attempt %d/3)...", 
                         consecutive_failures + 1);
                
                last_reload_attempt_time = current_time;
                
                /* Try to load the shader if we have a path */
                if (output->config->shader_path[0] != '\0') {
                    output_set_shader(output, output->config->shader_path);
                    if (output->live_shader_program == 0) {
                        consecutive_failures++;
                        log_error("Failed to reload shader (attempt %d/3), skipping frame", consecutive_failures);
                        
                        if (consecutive_failures >= 3) {
                            log_error("╔═══════════════════════════════════════════════════════════════╗");
                            log_error("║ CRITICAL: Shader failed to load after 3 attempts             ║");
                            log_error("╠═══════════════════════════════════════════════════════════════╣");
                            log_error("║ Config has bad shader path: '%s'", output->config->shader_path);
                            log_error("║                                                               ║");
                            log_error("║ FIX YOUR CONFIG:                                              ║");
                            log_error("║   1. Edit: ~/.config/neowall/config.vibe                      ║");
                            log_error("║   2. Fix shader path (check spelling, file exists)            ║");
                            log_error("║   3. Save - hot-reload will detect change automatically       ║");
                            log_error("║                                                               ║");
                            log_error("║ Program will continue running with blank screen               ║");
                            log_error("║ until you fix config and it reloads.                          ║");
                            log_error("╚═══════════════════════════════════════════════════════════════╝");
                            output->shader_load_failed = true; /* Mark as permanently failed */
                        }
                        return false;
                    } else {
                        /* Success! Reset failure counter and clear failed flag */
                        consecutive_failures = 0;
                        output->shader_load_failed = false;
                        log_info("Shader successfully reloaded after failure");
                    }
                } else {
                    log_error("No shader path configured, skipping frame");
                    consecutive_failures = 3; /* Don't retry if no path */
                    output->shader_load_failed = true; /* Mark as permanently failed */
                    return false;
                }
            } else {
                /* Silently skip frame - already logged error */
                return false;
            }
        }
        return render_frame_shader(output);
    }

    if (!output->current_image || output->texture == 0) {
        /* No wallpaper loaded yet */
        return true;
    }

    /* Check if we're in a transition */
    if (output->transition_start_time > 0 && 
        output->config->transition != TRANSITION_NONE &&
        output->next_image && output->next_texture) {
        log_debug("Using transition render: start_time=%llu, progress=%.2f, type=%d",
                 (unsigned long long)output->transition_start_time,
                 output->transition_progress,
                 output->config->transition);
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
    
    /* Validate texture before binding */
    if (output->texture == 0) {
        log_error("Invalid texture ID (0) - cannot render");
        return false;
    }
    
    /* DEBUG: Log which texture is being used for this output */
    static uint64_t last_log_time = 0;
    uint64_t now = get_time_ms();
    if (now - last_log_time > 2000) {
        log_info("Rendering output %s with texture %u (image: %s)", 
                 output->model[0] ? output->model : "unknown",
                 output->texture,
                 output->config->path);
        last_log_time = now;
    }
    
    bind_texture_cached(output, output->texture);
    
    /* Check if bind succeeded */
    GLenum bind_error = glGetError();
    if (bind_error != GL_NO_ERROR) {
        log_error("OpenGL error binding texture %u: 0x%x", output->texture, bind_error);
        return false;
    }

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
    if (output->config->mode == MODE_TILE) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
 
    /* Enable blending with state tracking (needed for images with transparency) */
    set_blend_state(output, true);

    /* Disable alpha channel writes - force opaque output */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
 
    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
     
    /* Re-enable alpha channel writes */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    
    /* Check for GL errors */
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        log_error("OpenGL error after draw: 0x%x (display may be disconnected)", gl_error);
        return false;
    }

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

    /* Render FPS watermark if enabled */
    render_fps_watermark(output);

    output->needs_redraw = false;
    output->frames_rendered++;

    return true;
}

/* Render frame with transition effect
 * Dispatches to modular transition implementations */
bool render_frame_transition(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_debug("Transition fallback: output=%p, current_image=%p, next_image=%p",
                 (void*)output, 
                 output ? (void*)output->current_image : NULL,
                 output ? (void*)output->next_image : NULL);
        return render_frame(output);
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_debug("Transition fallback: texture=%u, next_texture=%u",
                 output->texture, output->next_texture);
        return render_frame(output);
    }

    log_debug("Calling transition_render: type=%d, progress=%.2f, duration=%ums",
             output->config->transition, progress, output->config->transition_duration);

    /* Dispatch to modular transition renderer */
    bool result = transition_render(output, output->config->transition, progress);
    
    if (!result) {
        log_error("transition_render failed, falling back to normal render");
        return render_frame(output);
    }
    
    return result;
}
