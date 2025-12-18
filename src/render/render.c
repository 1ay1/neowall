#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>

/* GPU timeout detection threshold in milliseconds.
 * If a frame takes longer than this, the shader is likely causing GPU hangs. */
#define GPU_TIMEOUT_THRESHOLD_MS 2000

/* Number of consecutive slow frames before marking shader as problematic */
#define GPU_TIMEOUT_FRAME_THRESHOLD 3
#include "neowall.h"
#include "../image/image.h"    /* Only for legacy wrapper functions */
#include "config_access.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"
#include "../shader_lib/shader_multipass.h"
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

    /* Save current GL state */
    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);

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

    /* Restore GL state */
    if (!blend_enabled) {
        glDisable(GL_BLEND);
    }
    if (current_program != 0) {
        glUseProgram(current_program);
    }
}

/* Global cache for default iChannel textures (generated once, reused forever) */
static GLuint cached_default_channel_textures[5] = {0, 0, 0, 0, 0};
static bool default_channels_initialized = false;

/* Fullscreen quad vertices (position + texcoord) for image rendering */
static const float quad_vertices[] = {
    /* positions */  /* texcoords */
    -1.0f,  1.0f,    0.0f, 0.0f,  /* top-left */
     1.0f,  1.0f,    1.0f, 0.0f,  /* top-right */
    -1.0f, -1.0f,    0.0f, 1.0f,  /* bottom-left */
     1.0f, -1.0f,    1.0f, 1.0f   /* bottom-right */
};

/* Simple fullscreen quad vertices (position only) for shader rendering - matches gleditor */
static const float shader_quad_vertices[] = {
    -1.0f, -1.0f,  /* Bottom-left */
     1.0f, -1.0f,  /* Bottom-right */
    -1.0f,  1.0f,  /* Top-left */
     1.0f,  1.0f,  /* Top-right */
};

/* Shader-specific VBO (created once, shared across outputs) */
static GLuint shader_vbo = 0;

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

    /* Create shared shader VBO (matching gleditor's format exactly) */
    if (shader_vbo == 0) {
        glGenBuffers(1, &shader_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, shader_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(shader_quad_vertices), shader_quad_vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        log_debug("Created shader VBO with gleditor-compatible format");
    }

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

    /* Clean up multipass shader */
    if (output->multipass_shader != NULL) {
        multipass_destroy(output->multipass_shader);
        output->multipass_shader = NULL;
    }

    /* Clean up legacy shader program */
    if (output->live_shader_program != 0) {
        shader_destroy_program(output->live_shader_program);
        output->live_shader_program = 0;
    }
}

/**
 * Create OpenGL texture from raw pixel data
 *
 * This is the new clean API that doesn't depend on image_data struct.
 * Render module should only handle GPU upload, not image file loading.
 *
 * @param pixels Raw pixel data (RGB or RGBA)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param channels Number of channels (3 for RGB, 4 for RGBA)
 * @return OpenGL texture ID, or 0 on failure
 */
GLuint render_create_texture_from_pixels(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels) {
    if (!pixels || width == 0 || height == 0 || (channels != 3 && channels != 4)) {
        log_error("Invalid parameters for texture creation: pixels=%p, %ux%u, %u channels",
                 (void*)pixels, width, height, channels);
        return 0;
    }

    /* Note: Caller MUST ensure EGL context is current before calling this function */

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Set texture parameters */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Upload texture data */
    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height,
                 0, format, GL_UNSIGNED_BYTE, pixels);

    glBindTexture(GL_TEXTURE_2D, 0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error creating texture: 0x%x", error);
        glDeleteTextures(1, &texture);
        return 0;
    }

    log_debug("Created texture %u from pixels (%ux%u, %u channels)",
              texture, width, height, channels);

    return texture;
}

/**
 * Create OpenGL texture from raw pixel data (vertically flipped)
 *
 * Flips the image vertically to match OpenGL texture coordinates where (0,0)
 * is at bottom-left, while image files typically have (0,0) at top-left.
 * Used for shader iChannel textures.
 *
 * @param pixels Raw pixel data (RGB or RGBA)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param channels Number of channels (3 for RGB, 4 for RGBA)
 * @return OpenGL texture ID, or 0 on failure
 */
GLuint render_create_texture_from_pixels_flipped(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels) {
    if (!pixels || width == 0 || height == 0 || (channels != 3 && channels != 4)) {
        log_error("Invalid parameters for flipped texture creation");
        return 0;
    }

    /* Allocate buffer for flipped image */
    size_t row_size = width * channels;
    size_t total_size = row_size * height;
    uint8_t *flipped_pixels = malloc(total_size);
    if (!flipped_pixels) {
        log_error("Failed to allocate memory for flipped texture");
        return 0;
    }

    /* Flip image vertically */
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src_row = pixels + (y * row_size);
        uint8_t *dst_row = flipped_pixels + ((height - 1 - y) * row_size);
        memcpy(dst_row, src_row, row_size);
    }

    /* Create texture from flipped data */
    GLuint texture = render_create_texture_from_pixels(flipped_pixels, width, height, channels);

    free(flipped_pixels);

    if (texture) {
        log_debug("Created flipped texture %u (%ux%u)", texture, width, height);
    }

    return texture;
}

/* ============================================================================
 * LEGACY API - Deprecated, wraps new pixel-based API
 * ============================================================================ */

/* Create texture from image data
 * Optimized: Set immutable texture parameters only once at creation
 * Memory optimization: Frees pixel data after GPU upload to save RAM */
GLuint render_create_texture(struct image_data *img) {
    if (!img || !img->pixels) {
        log_error("Invalid image data for texture creation");
        return 0;
    }

    /* Create texture using new pixel-based API */
    GLuint texture = render_create_texture_from_pixels(
        img->pixels, img->width, img->height, img->channels
    );

    if (texture) {
        /* Free pixel data after successful GPU upload - saves massive amounts of RAM!
         * For 4K display: 3840x2160x4 = 33MB saved per image
         * We keep the image_data struct for metadata (width, height, etc.) */
        image_free_pixels(img);
        log_debug("Freed pixel data for texture %u (memory optimization)", texture);
    }

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

    /* Create texture using new pixel-based flipped API */
    GLuint texture = render_create_texture_from_pixels_flipped(
        img->pixels, img->width, img->height, img->channels
    );

    if (texture) {
        /* Free pixel data after successful GPU upload */
        image_free_pixels(img);
        log_debug("Freed pixel data for texture %u (memory optimization)", texture);
    }

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



/* Render shader wallpaper frame using multipass system
 * Matches gleditor's on_gl_render exactly for consistent behavior */

bool render_frame_shader(struct output_state *output) {
    if (!output || !output->multipass_shader) {
        log_error("Invalid output or multipass shader for render_frame_shader");
        return false;
    }

    /* Check if shader was previously marked as causing GPU timeouts */
    if (output->shader_load_failed) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return true;
    }

    /* Validate EGL context is still valid */
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        log_error("EGL display not available for shader rendering");
        return false;
    }

    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        log_error("EGL surface not available for shader rendering");
        return false;
    }

    /* Ensure EGL context is current */
    if (!eglMakeCurrent(output->state->egl_display, output->compositor_surface->egl_surface,
                       output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for shader rendering");
        return false;
    }

    int width = output->width;
    int height = output->height;

    /* Resize multipass buffers if needed */
    multipass_resize(output->multipass_shader, width, height);

    /* Calculate shader time */
    uint64_t current_time_ms = get_time_ms();
    uint64_t start_time = output->shader_start_time > 0 ? output->shader_start_time : current_time_ms;
    double current_time = (current_time_ms - start_time) / 1000.0;
    
    /* Apply shader speed multiplier */
    float shader_speed = output->config->shader_speed > 0.0f ? output->config->shader_speed : 1.0f;
    current_time *= shader_speed;

    /* Get mouse position (or use center if not tracked) */
    float mouse_x = output->mouse_x >= 0 ? output->mouse_x : (float)width / 2.0f;
    float mouse_y = output->mouse_y >= 0 ? output->mouse_y : (float)height / 2.0f;

    /* Render all passes using multipass system */
    multipass_render(output->multipass_shader,
                     (float)current_time,
                     mouse_x, mouse_y,
                     false);  /* mouse_click */

    /* Log every 60 frames to confirm rendering is happening */
    static int frame_count = 0;
    frame_count++;
    if (frame_count % 60 == 0) {
        log_info("Multipass shader render frame %d (time=%.2f, passes=%d)", 
                 frame_count, (float)current_time, output->multipass_shader->pass_count);
    }

    /* Handle cross-fade transition when switching shaders - simplified for multipass */
    if (output->shader_fade_start_time > 0) {
        /* For now, just clear the fade state - multipass handles transitions differently */
        output->shader_fade_start_time = 0;
        output->pending_shader_path[0] = '\0';
    }

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during shader rendering: 0x%x", error);
        return false;
    }

    /* Render FPS watermark if enabled */
    render_fps_watermark(output);

    /* Shader wallpapers need continuous redraw for animation
     * - vsync mode: always set needs_redraw (monitor refresh drives rendering)
     * - vsync off: only set if frame timer hasn't expired (timer drives rendering) */
    if (output->config->vsync) {
        output->needs_redraw = true;
    } else {
        /* Frame timer controls redraw scheduling in vsync-off mode */
        output->needs_redraw = false;
    }
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

        /* Defensive check: ensure multipass shader is actually loaded */
        if (output->multipass_shader == NULL && output->live_shader_program == 0) {
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
                    if (output->multipass_shader == NULL && output->live_shader_program == 0) {
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

    /* Check for multipass shader (new system) */
    if (output->config->type == WALLPAPER_SHADER && output->multipass_shader != NULL) {
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
