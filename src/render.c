#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "transitions.h"
#include "shader.h"

/* Note: Each transition manages its own shader sources in src/transitions/ */

/* Fullscreen quad vertices (position + texcoord) */
static const float quad_vertices[] = {
    /* positions */  /* texcoords */
    -1.0f,  1.0f,    0.0f, 0.0f,  /* top-left */
     1.0f,  1.0f,    1.0f, 0.0f,  /* top-right */
    -1.0f, -1.0f,    0.0f, 1.0f,  /* bottom-left */
     1.0f, -1.0f,    1.0f, 1.0f   /* bottom-right */
};

/* Initialize rendering for an output */
bool render_init_output(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for render_init_output");
        return false;
    }

    /* Context should already be current when this is called from egl.c */

    /* Create shader programs for transitions
     * Note: fade and slide share the same shader, so we use fade's program */
    if (!shader_create_fade_program(&output->program)) {
        log_error("Failed to create fade/slide shader program for output %s", output->model);
        return false;
    }

    /* Create glitch shader program */
    if (!shader_create_glitch_program(&output->glitch_program)) {
        log_error("Failed to create glitch shader program for output %s", output->model);
        shader_destroy_program(output->program);
        return false;
    }

    /* Create pixelate shader program */
    if (!shader_create_pixelate_program(&output->pixelate_program)) {
        log_error("Failed to create pixelate shader program for output %s", output->model);
        shader_destroy_program(output->program);
        shader_destroy_program(output->glitch_program);
        return false;
    }

    /* Create VBO */
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

/* Create texture from image data */
GLuint render_create_texture(struct image_data *img) {
    if (!img || !img->pixels) {
        log_error("Invalid image data for texture creation");
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Set texture parameters */
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

    return texture;
}

void render_destroy_texture(GLuint texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
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

/* Render a frame with live shader wallpaper */
/* Render shader transition by blending between two shaders */
bool render_frame_shader_transition(struct output_state *output, float progress) {
    if (!output || output->live_shader_program == 0 || output->prev_shader_program == 0) {
        return render_frame_shader(output);
    }

    glViewport(0, 0, output->width, output->height);
    glClear(GL_COLOR_BUFFER_BIT);

    float current_time = (float)(get_time_ms() - output->shader_start_time) / 1000.0f;

    /* Render previous shader to a temporary texture would be complex,
     * so we'll just do a simple cross-fade by rendering both and blending */
    
    /* First render the old shader with reduced alpha */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Render old shader */
    glUseProgram(output->prev_shader_program);
    GLint time_loc = glGetUniformLocation(output->prev_shader_program, "time");
    GLint res_loc = glGetUniformLocation(output->prev_shader_program, "resolution");
    
    if (time_loc != -1) {
        glUniform1f(time_loc, current_time);
    }
    if (res_loc != -1) {
        glUniform2f(res_loc, (float)output->width, (float)output->height);
    }

    /* Draw with fading out alpha */
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    GLint pos_attr = glGetAttribLocation(output->prev_shader_program, "position");
    if (pos_attr != -1) {
        glEnableVertexAttribArray(pos_attr);
        glVertexAttribPointer(pos_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
        
        /* Modulate alpha - fade out the old shader */
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBlendColor(1.0f, 1.0f, 1.0f, 1.0f - progress);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(pos_attr);
    }

    /* Now render new shader on top with fading in alpha */
    glUseProgram(output->live_shader_program);
    time_loc = glGetUniformLocation(output->live_shader_program, "time");
    res_loc = glGetUniformLocation(output->live_shader_program, "resolution");
    
    if (time_loc != -1) {
        glUniform1f(time_loc, current_time);
    }
    if (res_loc != -1) {
        glUniform2f(res_loc, (float)output->width, (float)output->height);
    }

    pos_attr = glGetAttribLocation(output->live_shader_program, "position");
    if (pos_attr != -1) {
        glEnableVertexAttribArray(pos_attr);
        glVertexAttribPointer(pos_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
        
        /* Fade in the new shader */
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBlendColor(1.0f, 1.0f, 1.0f, progress);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(pos_attr);
    }

    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Update transition progress */
    uint64_t current_time_ms = get_time_ms();
    uint64_t elapsed = current_time_ms - output->transition_start_time;
    output->transition_progress = (float)elapsed / (float)output->config.transition_duration;

    /* Clean up old shader when transition is complete */
    if (output->transition_progress >= 1.0f) {
        if (output->prev_shader_program != 0) {
            shader_destroy_program(output->prev_shader_program);
            output->prev_shader_program = 0;
        }
        output->transition_progress = 1.0f;
    }

    return true;
}

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

    /* Use live shader program */
    glUseProgram(output->live_shader_program);

    /* Calculate elapsed time for animation (continuous time since shader loaded) */
    uint64_t current_time = get_time_ms();
    uint64_t start_time = output->shader_start_time > 0 ? output->shader_start_time : output->last_frame_time;
    float time = (current_time - start_time) / 1000.0f;

    /* Set uniforms */
    GLint time_uniform = glGetUniformLocation(output->live_shader_program, "time");
    if (time_uniform >= 0) {
        glUniform1f(time_uniform, time);
    }

    GLint resolution_uniform = glGetUniformLocation(output->live_shader_program, "resolution");
    if (resolution_uniform >= 0) {
        glUniform2f(resolution_uniform, (float)output->width, (float)output->height);
    }

    /* Get position attribute location */
    GLint pos_attrib = glGetAttribLocation(output->live_shader_program, "position");
    if (pos_attrib < 0) {
        log_error("Failed to get 'position' attribute location in shader");
        return false;
    }

    /* Simple fullscreen quad vertices (position only, no texcoords) */
    static const float shader_quad[] = {
        -1.0f,  1.0f,  /* top-left */
         1.0f,  1.0f,  /* top-right */
        -1.0f, -1.0f,  /* bottom-left */
         1.0f, -1.0f   /* bottom-right */
    };

    /* Bind VBO and upload shader quad */
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(shader_quad), shader_quad, GL_DYNAMIC_DRAW);

    /* Set up vertex attributes */
    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(pos_attrib);

    /* Draw fullscreen quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up */
    glDisableVertexAttribArray(pos_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

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

/* Render a frame for an output */
bool render_frame(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for render_frame");
        return false;
    }

    /* Check if this is a shader wallpaper */
    if (output->config.type == WALLPAPER_SHADER) {
        /* Check if we're in a shader transition */
        if (output->config.transition != TRANSITION_NONE &&
            output->prev_shader_program != 0 &&
            output->transition_progress < 1.0f) {
            /* Render shader transition */
            return render_frame_shader_transition(output, output->transition_progress);
        }
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

    /* Use shader program */
    glUseProgram(output->program);

    /* Get attribute locations */
    GLint pos_attrib = glGetAttribLocation(output->program, "position");
    GLint tex_attrib = glGetAttribLocation(output->program, "texcoord");

    /* Calculate mode-aware vertex coordinates */
    float mode_vertices[16];
    calculate_vertex_coords(output, mode_vertices);

    /* Bind VBO and update with mode-specific vertices */
    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(mode_vertices), mode_vertices, GL_DYNAMIC_DRAW);

    /* Set up vertex attributes */
    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(pos_attrib);

    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_attrib);

    /* Bind texture */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, output->texture);

    /* Set texture unit uniform */
    GLint tex_uniform = glGetUniformLocation(output->program, "texture0");
    glUniform1i(tex_uniform, 0);

    /* Set alpha uniform (for transitions) */
    GLint alpha_uniform = glGetUniformLocation(output->program, "alpha");
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, 1.0f);
    }

    /* Handle tile mode texture wrapping */
    if (output->config.mode == MODE_TILE) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Enable blending for transparency */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Disable blending */
    glDisable(GL_BLEND);

    /* Clean up */
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(tex_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

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
