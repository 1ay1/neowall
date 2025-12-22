/**
 * Slide Transition Effect
 *
 * New wallpaper slides in from one side while old wallpaper slides out.
 *
 * Uses the unified transition context API for DRY code.
 */

#include <GL/gl.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* Vertex shader for slide transition */
static const char *slide_vertex_shader_source =
    GLSL_VERSION_STRING
    "in vec2 position;\n"
    "in vec2 texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Fragment shader for slide transition */
static const char *slide_fragment_shader_source =
    GLSL_VERSION_STRING
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform float alpha;\n"
    "void main() {\n"
    "    vec4 color = texture(texture0, v_texcoord);\n"
    "    fragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

bool shader_create_slide_program(GLuint *program) {
    return shader_create_program_from_sources(
        slide_vertex_shader_source,
        slide_fragment_shader_source,
        program
    );
}

static bool render_slide_transition(struct output_state *output, float progress, bool slide_left) {
    if (!output || !output->current_image || !output->next_image) {
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        return false;
    }

    /* Use unified transition context API */
    transition_context_t ctx;
    if (!transition_begin(&ctx, output, output->program)) {
        return false;
    }

    float offset = slide_left ? progress : -progress;

    /* Create vertices for old image sliding out */
    float old_vertices[16];
    for (int i = 0; i < 16; i++) {
        old_vertices[i] = ctx.vertices[i];
    }
    for (int i = 0; i < 4; i++) {
        old_vertices[i * 4] -= offset * 2.0f;
    }

    /* Draw old image */
    if (!transition_draw_textured_quad(&ctx, output->next_texture, 1.0f, old_vertices)) {
        transition_end(&ctx);
        return false;
    }

    /* Create vertices for new image sliding in */
    float new_vertices[16];
    for (int i = 0; i < 16; i++) {
        new_vertices[i] = ctx.vertices[i];
    }
    float slide_in_offset = slide_left ? (1.0f - progress) : -(1.0f - progress);
    for (int i = 0; i < 4; i++) {
        new_vertices[i * 4] += slide_in_offset * 2.0f;
    }

    /* Draw new image */
    if (!transition_draw_textured_quad(&ctx, output->texture, 1.0f, new_vertices)) {
        transition_end(&ctx);
        return false;
    }

    transition_end(&ctx);
    return true;
}

bool transition_slide_left_render(struct output_state *output, float progress) {
    return render_slide_transition(output, progress, true);
}

bool transition_slide_right_render(struct output_state *output, float progress) {
    return render_slide_transition(output, progress, false);
}