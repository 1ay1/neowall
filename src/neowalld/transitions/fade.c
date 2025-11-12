#include <stdlib.h>
#include <GLES2/gl2.h>
#include "neowall.h"
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
 * Classic crossfade effect where the new wallpaper gradually appears
 * over the old wallpaper. Both images maintain their display mode
 * throughout the transition.
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

    /* Initialize transition context - handles all OpenGL state setup */
    transition_context_t ctx;
    if (!transition_begin(&ctx, output, output->program)) {
        return false;
    }

    /* Draw old image at full opacity */
    if (!transition_draw_textured_quad(&ctx, output->next_texture, 1.0f, NULL)) {
        transition_end(&ctx);
        return false;
    }

    /* Draw new image with alpha based on progress (crossfade effect) */
    if (!transition_draw_textured_quad(&ctx, output->texture, progress, NULL)) {
        transition_end(&ctx);
        return false;
    }

    /* Disable alpha channel writes - force opaque output */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    /* Re-enable alpha channel writes after transition */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* Clean up and return - handles all OpenGL state cleanup */
    transition_end(&ctx);
    return true;
}