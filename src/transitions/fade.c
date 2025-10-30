#include <stdlib.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* External function from render.c for calculating vertices */
extern void calculate_vertex_coords_for_image(struct output_state *output, 
                                               struct image_data *image, 
                                               float vertices[16]);

/* Vertex shader for fade transition */
static const char *fade_vertex_shader_source =
    GLSL_VERSION_STRING
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Fragment shader for fade transition */
static const char *fade_fragment_shader_source =
    GLSL_VERSION_STRING
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform float alpha;\n"
    "void main() {\n"
    "    vec4 color = texture2D(texture0, v_texcoord);\n"
    "    gl_FragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

/**
 * Create shader program for fade transition
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_fade_program(GLuint *program) {
    return shader_create_program_from_sources(
        fade_vertex_shader_source,
        fade_fragment_shader_source,
        program
    );
}

/**
 * Fade Transition
 * 
 * Smoothly crossfades between old and new wallpapers using alpha blending.
 * Renders old image at full opacity, then renders new image on top with
 * increasing alpha based on transition progress.
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error
 */
bool transition_fade_render(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_error("Fade transition: invalid parameters (output=%p)", (void*)output);
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_error("Fade transition: missing textures (texture=%u, next_texture=%u)",
                 output->texture, output->next_texture);
        return false;
    }

    if (output->program == 0) {
        log_error("Fade transition: program not initialized");
        return false;
    }

    log_debug("Fade transition rendering: progress=%.2f, program=%u", 
             progress, output->program);

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);

    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Use shader program */
    glUseProgram(output->program);

    /* Get uniform locations */
    GLint tex_uniform = glGetUniformLocation(output->program, "texture0");
    GLint alpha_uniform = glGetUniformLocation(output->program, "alpha");

    /* Enable blending */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Setup fullscreen quad using DRY helper */
    float vertices[16];
    transition_setup_fullscreen_quad(output->vbo, vertices);
    
    /* Setup vertex attributes using DRY helper */
    transition_setup_common_attributes(output->program, output->vbo);

    /* Bind old texture (transitioning from) using DRY helper */
    transition_bind_texture_for_transition(output->next_texture, GL_TEXTURE0);
    if (tex_uniform >= 0) {
        glUniform1i(tex_uniform, 0);
    }

    /* Set old image to full opacity */
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, 1.0f);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Bind new texture (transitioning to) using DRY helper */
    transition_bind_texture_for_transition(output->texture, GL_TEXTURE0);
    
    if (tex_uniform >= 0) {
        glUniform1i(tex_uniform, 0);
    }

    /* Set new image alpha based on transition progress */
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, progress);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up */
    glDisable(GL_BLEND);
    
    /* Disable vertex attributes */
    GLint pos_attrib_cleanup = glGetAttribLocation(output->program, "position");
    GLint tex_attrib_cleanup = glGetAttribLocation(output->program, "texcoord");
    
    if (pos_attrib_cleanup >= 0) {
        glDisableVertexAttribArray(pos_attrib_cleanup);
    }
    if (tex_attrib_cleanup >= 0) {
        glDisableVertexAttribArray(tex_attrib_cleanup);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during fade transition: 0x%x", error);
        return false;
    }

    log_debug("Fade transition frame rendered successfully");
    output->needs_redraw = true;
    output->frames_rendered++;

    return true;
}
