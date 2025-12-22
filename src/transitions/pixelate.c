/**
 * Pixelate Transition Effect
 *
 * Dramatic mosaic/pixelation effect with chromatic aberration, pixel grid lines,
 * color vibrance boost, and flash effect at peak. Creates a retro-digital aesthetic.
 *
 * Uses the unified transition context API for DRY code.
 */

#include <GL/gl.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* Vertex shader for pixelate transition */
static const char *pixelate_vertex_shader_source =
    GLSL_VERSION_STRING
    "in vec2 position;\n"
    "in vec2 texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Pixelate/mosaic fragment shader */
static const char *pixelate_fragment_shader_source =
    GLSL_VERSION_STRING
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "uniform float progress;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = v_texcoord;\n"
    "    \n"
    "    // Smooth easing curve\n"
    "    float eased = progress < 0.5 \n"
    "        ? 2.0 * progress * progress \n"
    "        : 1.0 - 2.0 * (1.0 - progress) * (1.0 - progress);\n"
    "    \n"
    "    // Dramatic pixelation curve - peaks in the middle\n"
    "    float intensity = sin(eased * 3.14159);\n"
    "    float pixelation = intensity * intensity * 80.0 + 1.0;\n"
    "    \n"
    "    // Calculate pixel block\n"
    "    vec2 pixel_size = vec2(1.0) / pixelation;\n"
    "    vec2 block_id = floor(uv / pixel_size);\n"
    "    vec2 block_center = (block_id + 0.5) * pixel_size;\n"
    "    \n"
    "    // Blend between pixelated and normal sampling\n"
    "    vec2 sample_uv = mix(uv, block_center, intensity);\n"
    "    \n"
    "    // Sample textures\n"
    "    vec4 old_color = texture(texture0, sample_uv);\n"
    "    vec4 new_color = texture(texture1, sample_uv);\n"
    "    \n"
    "    // Chromatic aberration\n"
    "    float aberration = intensity * pixel_size.x * 1.5;\n"
    "    old_color.r = texture(texture0, sample_uv + vec2(aberration, 0.0)).r;\n"
    "    old_color.b = texture(texture0, sample_uv - vec2(aberration, 0.0)).b;\n"
    "    new_color.r = texture(texture1, sample_uv + vec2(aberration, 0.0)).r;\n"
    "    new_color.b = texture(texture1, sample_uv - vec2(aberration, 0.0)).b;\n"
    "    \n"
    "    // Mix images\n"
    "    vec4 color = mix(old_color, new_color, eased);\n"
    "    \n"
    "    // Color vibrance boost at peak\n"
    "    color.rgb = mix(color.rgb, color.rgb * 1.2, intensity * 0.2);\n"
    "    \n"
    "    // Pixel grid lines\n"
    "    vec2 grid = fract(uv * pixelation);\n"
    "    color.rgb += vec3(step(0.9, max(grid.x, grid.y)) * intensity * 0.3);\n"
    "    \n"
    "    // Flash effect at peak\n"
    "    color.rgb += vec3((1.0 - abs(eased - 0.5) * 2.0) * 0.1);\n"
    "    \n"
    "    fragColor = color;\n"
    "}\n";

bool shader_create_pixelate_program(GLuint *program) {
    return shader_create_program_from_sources(
        pixelate_vertex_shader_source,
        pixelate_fragment_shader_source,
        program
    );
}

bool transition_pixelate_render(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_error("Pixelate transition: invalid output or images");
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_error("Pixelate transition: missing textures");
        return false;
    }

    if (output->pixelate_program == 0) {
        log_error("Pixelate transition: program not initialized");
        return false;
    }

    /* Use unified transition context API */
    transition_context_t ctx;
    if (!transition_begin(&ctx, output, output->pixelate_program)) {
        return false;
    }

    /* Resolution needed for pixelate shader */
    float resolution[2] = { (float)output->width, (float)output->height };

    /* Draw blended transition with both textures */
    if (!transition_draw_blended_textures(&ctx,
                                           output->next_texture,
                                           output->texture,
                                           progress,
                                           0.0f,  /* time not used by pixelate */
                                           resolution)) {
        transition_end(&ctx);
        return false;
    }

    transition_end(&ctx);
    return true;
}