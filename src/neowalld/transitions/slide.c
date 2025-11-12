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

/* Vertex shader for slide transition */
static const char *slide_vertex_shader_source =
    GLSL_VERSION_STRING
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Fragment shader for slide transition */
static const char *slide_fragment_shader_source =
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

    /* Initialize transition context */
    transition_context_t ctx;
    if (!transition_begin(&ctx, output, output->program)) {
        return false;
    }

    /* Calculate slide offset */
    float offset = slide_left ? progress : -progress;

    /* === DRAW OLD IMAGE (sliding out) === */
    
    /* Create custom vertices for old image sliding out */
    float old_vertices[16];
    for (int i = 0; i < 16; i++) {
        old_vertices[i] = ctx.vertices[i];
    }
    
    /* Adjust X positions based on slide direction */
    for (int i = 0; i < 4; i++) {
        old_vertices[i * 4] = old_vertices[i * 4] - (offset * 2.0f);
    }
    
    /* Disable alpha channel writes - force opaque output for old image */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    
    /* Draw old image */
    if (!transition_draw_textured_quad(&ctx, output->next_texture, 1.0f, old_vertices)) {
        transition_end(&ctx);
        return false;
    }

    /* === DRAW NEW IMAGE (sliding in) === */
    
    /* Create custom vertices for new image sliding in */
    float new_vertices[16];
    for (int i = 0; i < 16; i++) {
        new_vertices[i] = ctx.vertices[i];
    }
    
    /* Adjust position to slide in from opposite side */
    float slide_in_offset = slide_left ? (1.0f - progress) : -(1.0f - progress);
    
    for (int i = 0; i < 4; i++) {
        new_vertices[i * 4] = new_vertices[i * 4] + (slide_in_offset * 2.0f);
    }
    
    /* Draw new image */
    if (!transition_draw_textured_quad(&ctx, output->texture, 1.0f, new_vertices)) {
        transition_end(&ctx);
        return false;
    }

    /* Re-enable alpha channel writes */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* Clean up and return */
    transition_end(&ctx);
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