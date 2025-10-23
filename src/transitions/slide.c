#include <stdlib.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "transitions.h"
#include "shader.h"

/* External function from render.c for calculating vertices */
extern void calculate_vertex_coords_for_image(struct output_state *output, 
                                               struct image_data *image, 
                                               float vertices[16]);

/* Vertex shader for slide transition */
static const char *slide_vertex_shader_source =
    "#version 100\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Fragment shader for slide transition */
static const char *slide_fragment_shader_source =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform float alpha;\n"
    "void main() {\n"
    "    vec4 color = texture2D(texture0, v_texcoord);\n"
    "    gl_FragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

/**
 * Create shader program for slide transition
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_slide_program(GLuint *program) {
    return shader_create_program_from_sources(
        slide_vertex_shader_source,
        slide_fragment_shader_source,
        program
    );
}

/**
 * Helper function to render slide transition
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @param slide_left true for left slide, false for right slide
 * @return true on success, false on error
 */
static bool render_slide_transition(struct output_state *output, float progress, bool slide_left) {
    if (!output || !output->current_image || !output->next_image) {
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        return false;
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
    GLint tex_uniform = glGetUniformLocation(output->program, "texture0");
    GLint alpha_uniform = glGetUniformLocation(output->program, "alpha");

    /* Enable blending */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Calculate slide offset */
    float offset = slide_left ? progress : -progress;

    /* Render old image (sliding out) with proper display mode */
    float old_vertices[16];
    calculate_vertex_coords_for_image(output, output->next_image, old_vertices);
    
    /* Adjust position based on slide direction */
    for (int i = 0; i < 4; i++) {
        old_vertices[i * 4] = old_vertices[i * 4] - (offset * 2.0f);
    }

    glBindBuffer(GL_ARRAY_BUFFER, output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(old_vertices), old_vertices, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(pos_attrib);

    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_attrib);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, output->next_texture);
    
    /* Handle tile mode texture wrapping */
    if (output->config.mode == MODE_TILE) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    glUniform1i(tex_uniform, 0);

    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, 1.0f);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Render new image (sliding in) with proper display mode */
    float new_vertices[16];
    calculate_vertex_coords_for_image(output, output->current_image, new_vertices);
    
    /* Adjust position to slide in from opposite side */
    float slide_in_offset = slide_left ? (1.0f - progress) : -(1.0f - progress);
    
    for (int i = 0; i < 4; i++) {
        new_vertices[i * 4] = new_vertices[i * 4] + (slide_in_offset * 2.0f);
    }

    glBufferData(GL_ARRAY_BUFFER, sizeof(new_vertices), new_vertices, GL_DYNAMIC_DRAW);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, output->texture);
    
    /* Handle tile mode texture wrapping */
    if (output->config.mode == MODE_TILE) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    glUniform1i(tex_uniform, 0);

    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, 1.0f);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up */
    glDisable(GL_BLEND);
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(tex_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during slide transition: 0x%x", error);
        return false;
    }

    output->needs_redraw = true;
    output->frames_rendered++;

    return true;
}

/**
 * Slide Left Transition
 * 
 * New wallpaper slides in from right to left while old wallpaper slides out.
 * Both images maintain their display mode throughout the transition.
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error
 */
bool transition_slide_left_render(struct output_state *output, float progress) {
    return render_slide_transition(output, progress, true);
}

/**
 * Slide Right Transition
 * 
 * New wallpaper slides in from left to right while old wallpaper slides out.
 * Both images maintain their display mode throughout the transition.
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error
 */
bool transition_slide_right_render(struct output_state *output, float progress) {
    return render_slide_transition(output, progress, false);
}
