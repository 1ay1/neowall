/**
 * Fade Transition Effect
 *
 * Classic crossfade effect where the new wallpaper gradually appears
 * over the old wallpaper.
 *
 * Uses the unified transition context API for DRY code.
 */

#include <GL/gl.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* Vertex shader for fade transition */
static const char *fade_vertex_shader_source =
    GLSL_VERSION_STRING
    "in vec2 position;\n"
    "in vec2 texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Fragment shader for fade transition */
static const char *fade_fragment_shader_source =
    GLSL_VERSION_STRING
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform float alpha;\n"
    "void main() {\n"
    "    vec4 color = texture(texture0, v_texcoord);\n"
    "    fragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

bool shader_create_fade_program(GLuint *program) {
    return shader_create_program_from_sources(
        fade_vertex_shader_source,
        fade_fragment_shader_source,
        program
    );
}

bool transition_fade_render(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_error("Fade transition: invalid output or images");
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_error("Fade transition: missing textures");
        return false;
    }

    if (output->program == 0) {
        log_error("Fade transition: program not initialized");
        return false;
    }

    /* Use unified transition context API */
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

    transition_end(&ctx);
    return true;
}