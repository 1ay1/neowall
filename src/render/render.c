#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <EGL/egl.h>

/* GPU timeout detection threshold in milliseconds.
 * If a frame takes longer than this, the shader is likely causing GPU hangs. */
#define GPU_TIMEOUT_THRESHOLD_MS 2000

/* Number of consecutive slow frames before marking shader as problematic */
#define GPU_TIMEOUT_FRAME_THRESHOLD 3
#include "neowall/neowall.h"
#include "neowall/image/image.h"    /* Only for legacy wrapper functions */
#include "neowall/config/config_access.h"
#include "neowall/constants.h"
#include "neowall/egl/egl_core.h"
#include "neowall/transitions.h"
#include "neowall/shader/shader.h"
#include "neowall/shader/wallshader.h"
#include "neowall/shader/shader_clock.h"
#include "neowall/textures.h"
#include "neowall/compositor/compositor.h"
#ifdef HAVE_WAYLAND_BACKEND
/* For iWindows[]: window rects come from the Hyprland IPC snapshot that the
 * occlusion path already maintains. */
#include "hyprland_coverage.h"
#endif

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

/* Make the output's EGL context/surface current, skipping the call when it
 * already is. eglMakeCurrent is NOT free even when a no-op: Mesa performs an
 * implicit flush of the outgoing context, which serializes the GPU pipeline.
 * The per-frame path hits make-current up to 3x per output (render_frame ->
 * render_frame_shader -> swap phase); this collapses the redundant ones. */
static bool ensure_egl_current(struct output_state *output) {
    EGLDisplay dpy = output->state->egl_display;
    EGLSurface surf = output->compositor_surface->egl_surface;
    EGLContext ctx = output->state->egl_context;

    if (eglGetCurrentContext() == ctx &&
        eglGetCurrentSurface(EGL_DRAW) == surf &&
        eglGetCurrentDisplay() == dpy) {
        return true;
    }
    return egl_make_surface_current(dpy, surf, ctx);
}

/* Note: Each transition manages its own shader sources in src/transitions/ */

/* Simple color shader for overlay effects */
static const char *color_vertex_shader =
    "#version 330 core\n"
    "in vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

static const char *color_fragment_shader =
    "#version 330 core\n"
    "out vec4 fragColor;\n"
    "uniform vec4 color;\n"
    "void main() {\n"
    "    fragColor = color;\n"
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

/*
 * GL object bindings belong to the shared EGL context, not to an output or
 * draw surface. Other outputs, multipass rendering, transitions, and overlays
 * may all change them between calls. Always establish the state needed by a
 * draw instead of trusting output->gl_state, whose per-output view cannot be
 * authoritative for a shared context.
 */
static inline void use_program(struct output_state *output, GLuint program) {
    glUseProgram(program);
    output->gl_state.active_program = program;
}

static inline void bind_texture(struct output_state *output, GLuint texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    output->gl_state.bound_texture = texture;
}

static inline void set_blend_state(struct output_state *output, bool enable) {
    if (enable) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    output->gl_state.blend_enabled = enable;
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

    /* Create VAO - required for OpenGL 3.3 Core Profile */
    glGenVertexArrays(1, &output->vao);
    glBindVertexArray(output->vao);

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

    /* Delete VAO */
    if (output->vao) {
        glDeleteVertexArrays(1, &output->vao);
        output->vao = 0;
    }
    output->clock_fade_rebased = false;
    output->clock_fade_start = 0;
    if (output->clock_fade_tex) {
        glDeleteTextures(1, &output->clock_fade_tex);
        output->clock_fade_tex = 0;
    }

    /* Delete textures */
    if (output->texture != 0) {
        glDeleteTextures(1, &output->texture);
        output->texture = 0;
    }
    if (output->next_texture != 0) {
        glDeleteTextures(1, &output->next_texture);
        output->next_texture = 0;
    }

    for (size_t i = 0; i < 4; i++) {
        if (output->channel_tex[i]) glDeleteTextures(1, &output->channel_tex[i]);
        output->channel_tex[i] = 0;
    }

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

    wallshader_free(output->shader);
    output->shader = NULL;
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

/* Create the texture for one `channels` entry: a built-in name or an image
 * file, with its size in w and h. 0 on failure. */
static GLuint channel_texture(const char *path, int *w, int *h) {
    static const struct {
        const char *name;
        GLuint (*make)(int, int);
    } builtin[] = {
        {TEXTURE_NAME_RGBA_NOISE, texture_create_rgba_noise}, {"default", texture_create_rgba_noise},
        {TEXTURE_NAME_GRAY_NOISE, texture_create_gray_noise}, {TEXTURE_NAME_BLUE_NOISE, texture_create_blue_noise},
        {TEXTURE_NAME_WOOD, texture_create_wood},             {TEXTURE_NAME_ABSTRACT, texture_create_abstract},
        {TEXTURE_NAME_FONT, texture_create_font_atlas},
    };
    for (size_t k = 0; k < sizeof(builtin) / sizeof(builtin[0]); k++) {
        if (!strcmp(path, builtin[k].name)) {
            *w = *h = DEFAULT_TEXTURE_SIZE;
            return builtin[k].make(DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE);
        }
    }
    struct image_data *img = image_load(path, 0, 0, MODE_FILL);
    if (!img) return 0;
    /* flipped: images are top-down, GL textures bottom-up */
    GLuint texture = render_create_texture_flipped(img);
    *w = (int)img->width;
    *h = (int)img->height;
    image_free(img);
    return texture;
}

/* The config's `channels` as iChannel textures. A channel the config
 * leaves out (or sets to "_") stays 0, so the shader's own binding is used. */
bool render_load_channel_textures(struct output_state *output, struct wallpaper_config *config) {
    if (!output) {
        log_error("Invalid output for render_load_channel_textures");
        return false;
    }

    for (size_t i = 0; i < 4; i++) {
        if (output->channel_tex[i]) glDeleteTextures(1, &output->channel_tex[i]);
        output->channel_tex[i] = 0;
        output->channel_w[i] = output->channel_h[i] = 0;
    }
    if (!config || !config->channel_paths) return true;

    for (size_t i = 0; i < config->channel_count && i < 4; i++) {
        const char *path = config->channel_paths[i];
        if (!path || !path[0] || !strcmp(path, "_")) continue;
        output->channel_tex[i] = channel_texture(path, &output->channel_w[i], &output->channel_h[i]);
        if (output->channel_tex[i]) {
            log_info("iChannel%zu: %s (%dx%d)", i, path, output->channel_w[i], output->channel_h[i]);
        } else {
            log_error("iChannel%zu: can't load %s", i, path);
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

    if (channel_index >= 4) {
        log_error("Channel index %zu out of range", channel_index);
        return false;
    }

    /* CRITICAL: Ensure EGL context is current before GL operations */
    if (!output->compositor_surface || !egl_make_surface_current(output->state->egl_display, output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for texture update");
        return false;
    }

    /* Load the new image */
    struct image_data *img = image_load(image_path, 0, 0, MODE_FILL);
    if (!img) {
        log_error("Failed to load image for iChannel%zu: %s", channel_index, image_path);
        return false;
    }

    /* Create new flipped texture for shader use */
    GLuint texture = render_create_texture_flipped(img);
    if (texture == 0) {
        log_error("Failed to create texture for iChannel%zu from: %s", channel_index, image_path);
        image_free(img);
        return false;
    }

    /* swap only once the new one exists: a failed load keeps the old image */
    if (output->channel_tex[channel_index] != 0) {
        glDeleteTextures(1, &output->channel_tex[channel_index]);
    }
    output->channel_tex[channel_index] = texture;
    output->channel_w[channel_index] = (int)img->width;
    output->channel_h[channel_index] = (int)img->height;

    log_info("Updated iChannel%zu with image: %s (%ux%u) -> texture ID %u",
             channel_index, image_path, img->width, img->height, texture);

    image_free(img);

    /* Mark for redraw */
    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);

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

/* Fullscreen blit with constant alpha, for the clock-reset cross-fade. */
static const char *clock_fade_vs =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) * 2.0, (gl_VertexID >> 1) * 2.0);\n"
    "    uv = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *clock_fade_fs =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D tex;\n"
    "uniform float alpha;\n"
    "void main() { fragColor = vec4(texture(tex, uv).rgb, alpha); }\n";
static GLuint clock_fade_program = 0;
static GLuint clock_fade_vao = 0;

/* Copy what's in the back buffer now (the last frame on the old clock). */
static bool clock_fade_capture(struct output_state *output, int w, int h) {
    if (!output->clock_fade_tex) glGenTextures(1, &output->clock_fade_tex);
    glBindTexture(GL_TEXTURE_2D, output->clock_fade_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

/* Draw the captured frame over the new one, fading out. */
static void clock_fade_draw(struct output_state *output, int w, int h, float alpha) {
    if (!clock_fade_program) {
        if (!shader_create_program_from_sources(clock_fade_vs, clock_fade_fs, &clock_fade_program))
            return;
        glGenVertexArrays(1, &clock_fade_vao);
    }
    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    GLboolean blend = glIsEnabled(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(clock_fade_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, output->clock_fade_tex);
    glUniform1i(glGetUniformLocation(clock_fade_program, "tex"), 0);
    glUniform1f(glGetUniformLocation(clock_fade_program, "alpha"), alpha);
    glBindVertexArray(clock_fade_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!blend) glDisable(GL_BLEND);
    glUseProgram((GLuint)prev_prog);
}

static void clock_fade_release(struct output_state *output) {
    if (output->clock_fade_tex) {
        glDeleteTextures(1, &output->clock_fade_tex);
        output->clock_fade_tex = 0;
    }
    output->clock_fade_start = 0;
}

/* About to replace the shader on screen: draw it once more into the fade
 * texture, so the new one fades in over it instead of popping. Needs the
 * output's context current. */
void render_snapshot_for_swap(struct output_state *output) {
    if (!output || !output->shader || output->width <= 0 || output->height <= 0 ||
        output->frames_rendered == 0) {
        return;
    }
    int w = output->width, h = output->height;
    if (!output->clock_fade_tex) glGenTextures(1, &output->clock_fade_tex);
    glBindTexture(GL_TEXTURE_2D, output->clock_fade_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output->clock_fade_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, w, h);
        wallshader_frame f = {.width = w, .height = h, .time = output->last_shader_time,
                              .span_w = output->spanned ? output->span.virt_w : 0,
                              .span_h = output->spanned ? output->span.virt_h : 0,
                              .span_x = output->spanned ? output->span.off_x : 0,
                              .span_y = output->spanned ? output->span.off_y : 0};
        for (int c = 0; c < 4; c++) {
            f.channels[c] = output->channel_tex[c];
            f.channel_w[c] = output->channel_w[c];
            f.channel_h[c] = output->channel_h[c];
        }
        wallshader_draw(output->shader, &f);
        /* the new shader's first frame starts the fade */
        output->clock_fade_rebased = false;
        output->clock_fade_start = 0;
        output->swap_fade_pending = true;
    } else {
        clock_fade_release(output);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    while (glGetError() != GL_NO_ERROR) {}
}

bool render_frame_shader(struct output_state *output) {
    if (!output || !output->shader) {
        log_error("Invalid output or shader for render_frame_shader");
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

    /* Ensure EGL context is current (no-op if it already is) */
    if (!ensure_egl_current(output)) {
        log_error("Failed to make EGL context current for shader rendering");
        return false;
    }

    int width = output->width;
    int height = output->height;
    double present_at = 0;

    /* Calculate shader time at MICROSECOND precision. shader_start_time is
     * stored in ms (shared with pause/resume bookkeeping); ms*1000 == µs on
     * the same monotonic epoch. Millisecond-quantized time visibly stutters:
     * at 60 FPS the 16.67ms interval alternates 16/17ms steps (~6% jitter). */
    /* Spanned outputs run off the span group's shared start, not their own: two
     * halves of one scene that started animating even a frame apart are visibly
     * out of phase across the bezel. */
    uint64_t epoch_ms = (output->spanned && output->span_start_time > 0)
        ? output->span_start_time
        : output->shader_start_time;

    uint64_t current_time_us = get_time_us();
    /* Stamp the frame with the vblank it will be shown at, when the
     * compositor tells us the grid (Wayland presentation-time). Consecutive
     * frames then step by exactly one refresh, whatever the draw start
     * jitter. Without feedback (X11, no protocol) fall back to now. */
    output->frame_target_ns = nw_frame_clock_target(&output->frame_clock,
                                                    current_time_us * 1000ull,
                                                    &output->frame_latency_used);
    if (output->frame_target_ns) {
        current_time_us = output->frame_target_ns / 1000ull;
        present_at = (double)output->frame_target_ns / 1e9;
    }
    uint64_t start_time_us = epoch_ms > 0
        ? epoch_ms * 1000ull
        : current_time_us;
    /* Guard against start_time being (transiently) in the future — subtracting
     * unsigned would wrap to an enormous elapsed value. Can happen around a
     * shader-pause resume that races a shader reload; clamp to 0 instead. */
    double current_time = current_time_us > start_time_us
        ? (double)(current_time_us - start_time_us) / 1e6
        : 0.0;
    
    /* Apply shader speed multiplier */
    float shader_speed = output->config->shader_speed > 0.0f ? output->config->shader_speed : 1.0f;
    current_time *= shader_speed;

    /* Keep iTime small enough that float32 still resolves a frame.
     *
     * iTime is a float32 uniform and loses absolute precision as it grows:
     * ~0.24 ms granularity after an hour, ~7.8 ms after a day, against a
     * 16.7 ms frame at 60 FPS. Left alone, a wallpaper running for days
     * degrades smooth motion into a visible stairstep.
     *
     * Wrapping it would introduce a phase jump for every fractional frequency
     * a shader uses (sin(0.9*iTime) and friends), so the epoch is instead
     * pushed forward only when the shader is NOT on screen -- while occluded
     * or paused -- where a discontinuity is invisible by definition. A visible
     * shader's clock is never touched.
     *
     * needs_redraw is set when the output is actually rendering; an occluded
     * output stops here long before this point, so reaching this branch with a
     * large elapsed time means we were parked and just came back.
     *
     * Both conditions are re-checked against live state rather than trusting
     * the marker alone: shader_hidden_since is cleared on un-occlude, but if a
     * future path ever forgets to clear it, a stale marker would let the clock
     * snap to zero while the user is watching -- which is precisely the visible
     * "jumps back in time" regression this guard is meant to avoid. Requiring
     * the output to be occluded *right now* makes that impossible.
     *
     * The policy itself lives in shader_clock.c so it can be exhaustively
     * unit-tested without GL or a compositor; see tests/test_shader_clock.c. */
    shader_clock_state clock_st = {
        .elapsed_seconds = current_time,
        .hidden_since_ms = output->shader_hidden_since,
        .occluded_now = atomic_load_explicit(&output->occluded, memory_order_acquire),
        .paused_now = output->shader_paused_at > 0,
        .spanned = output->spanned,
    };
    bool did_rebase = false;

    if (shader_clock_should_rebase(&clock_st, SHADER_TIME_REBASE_SECONDS)) {
        /* Rebase to zero and drop the hidden marker; the jump lands entirely
         * inside the invisible window. Spanned outputs rebase as a group
         * elsewhere, so leave their clock alone rather than desync the bezel --
         * but still consume the marker, or the condition stays armed and is
         * retested on every frame for the rest of the process's life. */
        if (!shader_clock_defers_to_group(&clock_st)) {
            output->shader_start_time = get_time_ms();
            current_time = 0.0;
            did_rebase = true;
            log_debug("Rebased shader clock for output %s (float32 precision guard)",
                      output_get_identifier(output));
        }
        output->shader_hidden_since = 0;
    } else if (!output->clock_fade_start && output->clock_fade_rebased) {
        /* Last frame on the old clock was captured; switch now. The old
         * frame fades out on top of the new clock. */
        output->clock_fade_rebased = false;
        output->shader_start_time = get_time_ms();
        output->clock_fade_start = output->shader_start_time;
        current_time = 0.0;
        did_rebase = true;
        log_info("Reset shader clock on %s behind a cross-fade (float32 precision)",
                 output_get_identifier(output));
    }

    /* Watch the value actually handed to the shader. iTime must never move
     * backwards while the wallpaper is on screen; if it does, the user sees the
     * animation snap to an earlier state. Reporting the OUTPUT (rather than
     * auditing each site that can move the epoch) catches causes we have not
     * thought of, including ones outside this function. */
    double prev_time = output->clock_tracker.last_time;
    shader_clock_event clock_ev =
        shader_clock_track(&output->clock_tracker, current_time, epoch_ms, did_rebase);

    if (clock_ev != SHADER_CLOCK_OK) {
        /* An expected rebase is debug-level noise; anything else is a real bug
         * and is logged at warn so it shows up without -v. */
        const char *id = output_get_identifier(output);
        if (clock_ev == SHADER_CLOCK_JUMP_REBASE) {
            log_debug("shader clock %s on %s: iTime %.3f -> %.3f",
                      shader_clock_event_name(clock_ev), id,
                      prev_time, current_time);
        } else {
            log_warn("shader clock JUMPED BACKWARD on %s (%s): iTime %.3f -> %.3f, "
                     "epoch=%llu ms, occluded=%d paused=%d spanned=%d "
                     "hidden_since=%llu frames=%llu total_jumps=%llu",
                     id, shader_clock_event_name(clock_ev), prev_time, current_time,
                     (unsigned long long)epoch_ms,
                     (int)clock_st.occluded_now, (int)clock_st.paused_now,
                     (int)clock_st.spanned,
                     (unsigned long long)clock_st.hidden_since_ms,
                     (unsigned long long)output->clock_tracker.frames,
                     (unsigned long long)output->clock_tracker.jumps);
        }
    }

    /* Get mouse position (or use center if not tracked) */
    float mouse_x = output->mouse_x >= 0 ? output->mouse_x : (float)width / 2.0f;
    float mouse_y = output->mouse_y >= 0 ? output->mouse_y : (float)height / 2.0f;

    /* Publish the window layout for iWindows[]. This reads the snapshot the
     * occlusion path already refreshes on its own 500ms throttle, so it costs
     * a lock and a memcpy rather than an IPC round-trip -- important, because
     * this runs every frame on a daemon that stays up for weeks.
     *
     * Compositors that expose no geometry report zero windows, which shaders
     * are required to treat as an empty desktop rather than an error. */
    float rects[NW_SHADER_MAX_WINDOWS * 4];
    float focused[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int nwin = 0;
#ifdef HAVE_WAYLAND_BACKEND
    {
        nw_window_rect wins[NW_SHADER_MAX_WINDOWS];
        nwin = hyprland_output_windows(output, wins, NW_SHADER_MAX_WINDOWS);
        for (int i = 0; i < nwin; i++) {
            rects[i*4 + 0] = wins[i].x;
            rects[i*4 + 1] = wins[i].y;
            rects[i*4 + 2] = wins[i].w;
            rects[i*4 + 3] = wins[i].h;
            if (wins[i].focused) {
                focused[0] = wins[i].x; focused[1] = wins[i].y;
                focused[2] = wins[i].w; focused[3] = wins[i].h;
            }
        }
    }
#endif

    /* A spanned output draws its slice of the scene the span group shares;
     * every other output passes 0x0, which leaves sizing alone. */
    wallshader_frame frame = {
        .width = width,
        .height = height,
        .time = current_time,
        .present_at = present_at,
        .mouse = {mouse_x, mouse_y},
        .span_w = output->spanned ? output->span.virt_w : 0,
        .span_h = output->spanned ? output->span.virt_h : 0,
        .span_x = output->spanned ? output->span.off_x : 0,
        .span_y = output->spanned ? output->span.off_y : 0,
        .windows = rects,
        .window_count = nwin,
        .focused = focused,
    };
    for (int c = 0; c < 4; c++) {
        frame.channels[c] = output->channel_tex[c];
        frame.channel_w[c] = output->channel_w[c];
        frame.channel_h[c] = output->channel_h[c];
    }
    wallshader_draw(output->shader, &frame);
    output->last_shader_time = current_time;

    /* first frame of a new shader: fade the old one out over it */
    bool swap_fade = output->swap_fade_pending;
    if (swap_fade) {
        output->swap_fade_pending = false;
        if (output->clock_fade_tex) output->clock_fade_start = get_time_ms();
    }

    if (output->clock_fade_start) {
        float k = (float)(get_time_ms() - output->clock_fade_start) / (float)SHADER_TIME_FADE_MS;
        if (k >= 1.0f) {
            clock_fade_release(output);
        } else {
            float a = 1.0f - k;
            clock_fade_draw(output, width, height, a * a * (3.0f - 2.0f * a));
        }
    } else if (!swap_fade && !did_rebase && !output->clock_fade_rebased &&
               shader_clock_should_fade_rebase(&clock_st, SHADER_TIME_FADE_REBASE_SECONDS)) {
        /* Visible for hours with no hidden window to rebase in, so float32
         * iTime is getting coarse. Keep this frame (still on the old clock)
         * and reset on the next one, behind a cross-fade from it. */
        output->clock_fade_rebased = clock_fade_capture(output, width, height);
    }

    /* Log every 600 frames to confirm rendering is happening */
    static int frame_count = 0;
    frame_count++;
    if (frame_count % 600 == 0) {
        log_debug("Shader render frame %d (time=%.2f, passes=%d)",
                 frame_count, (float)current_time, wallshader_pass_count(output->shader));
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
        atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    } else {
        /* Frame timer controls redraw scheduling in vsync-off mode */
        atomic_store_explicit(&output->needs_redraw, false, memory_order_release);
    }
    output->frames_rendered++;

    return true;
}

/* Render a frame for an output.
 * GL context state is established explicitly because all outputs share it. */
bool render_frame(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for render_frame");
        return false;
    }

    /* CRITICAL: Ensure EGL context is current before any GL operations */
    if (output->state && output->state->egl_display != EGL_NO_DISPLAY &&
        output->compositor_surface && output->compositor_surface->egl_surface != EGL_NO_SURFACE) {
        if (!ensure_egl_current(output)) {
            log_error("Failed to make EGL context current for rendering");
            return false;
        }

        /* GL bindings are context state, so no per-output cache can remain
         * valid across a surface/output switch. The draw path below always
         * establishes its required program, texture, and blend state. */
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

    /* Terminal wallpapers render through the same multipass pipeline as GLSL
     * shaders (nwTerm() samples the terminal channel), so they take the shader
     * render path. Without this they fall through to the image path below,
     * find no texture, and draw a black screen. */
    if (output->config->type == WALLPAPER_TERMINAL) {
        if (output->shader != NULL) {
            return render_frame_shader(output);
        }
        /* Not attached yet (surface still coming up) — nothing to draw. */
        return true;
    }

    /* Check if this is a shader wallpaper */
    if (output->config->type == WALLPAPER_SHADER) {
        /* Check if shader loading has permanently failed */
        if (output->shader_load_failed) {
            /* Permanently failed - don't spam logs, just return false silently */
            return false;
        }

        /* Defensive check: ensure the shader is actually loaded */
        if (output->shader == NULL) {
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
                    if (output->shader == NULL) {
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
                            log_error("║   3. Restart daemon: neowall kill && neowall                  ║");
                            log_error("║                                                               ║");
                            log_error("║ Program will continue running with blank screen               ║");
                            log_error("║ until you fix config and restart.                             ║");
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

    if (output->config->type == WALLPAPER_SHADER && output->shader != NULL) {
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

    /* Bind VAO - required for OpenGL 3.3 Core Profile */
    glBindVertexArray(output->vao);

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);

    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Establish the program explicitly; the EGL context is shared. */
    use_program(output, output->program);

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

    bind_texture(output, output->texture);

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

    /* Establish blending explicitly (needed for images with transparency). */
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

    atomic_store_explicit(&output->needs_redraw, false, memory_order_release);
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

    log_debug("Calling transition_render: type=%d, progress=%.2f, duration=%.2fs",
             output->config->transition, progress, output->config->transition_duration);

    /* Dispatch to modular transition renderer */
    bool result = transition_render(output, output->config->transition, progress);

    if (!result) {
        log_error("transition_render failed, falling back to normal render");
        return render_frame(output);
    }

    return result;
}
