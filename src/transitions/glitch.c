/**
 * Glitch Transition Effect
 *
 * Digital glitch effect with RGB channel separation, scan lines, horizontal
 * glitches, block corruption, and digital noise. Creates a cyberpunk aesthetic
 * transition between wallpapers.
 *
 * Uses the unified transition context API for DRY code.
 */

#include <GL/gl.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* Vertex shader for glitch transition */
static const char *glitch_vertex_shader_source =
    GLSL_VERSION_STRING
    "in vec2 position;\n"
    "in vec2 texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Glitch transition fragment shader */
static const char *glitch_fragment_shader_source =
    GLSL_VERSION_STRING
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "uniform float progress;\n"
    "uniform float time;\n"
    "\n"
    "float rand(vec2 co) {\n"
    "    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = v_texcoord;\n"
    "    float glitch_strength = progress * (1.0 - progress) * 4.0;\n"
    "    \n"
    "    // Horizontal glitch lines\n"
    "    float line = floor(uv.y * 80.0 + time * 10.0);\n"
    "    float glitch_line = step(0.95, rand(vec2(line, time)));\n"
    "    float offset = (rand(vec2(line, time + 0.1)) - 0.5) * glitch_strength * 0.1;\n"
    "    uv.x += offset * glitch_line;\n"
    "    \n"
    "    // RGB channel separation\n"
    "    float separation = glitch_strength * 0.02;\n"
    "    vec4 old_img = texture(texture0, uv);\n"
    "    vec4 new_img = texture(texture1, uv);\n"
    "    \n"
    "    // Chromatic aberration on new image\n"
    "    float r = texture(texture1, uv + vec2(separation, 0.0)).r;\n"
    "    float g = texture(texture1, uv).g;\n"
    "    float b = texture(texture1, uv - vec2(separation, 0.0)).b;\n"
    "    new_img = vec4(r, g, b, new_img.a);\n"
    "    \n"
    "    // Scan lines\n"
    "    float scanline = sin(uv.y * 800.0 + time * 20.0) * 0.03 * glitch_strength;\n"
    "    \n"
    "    // Block corruption\n"
    "    float block_y = floor(uv.y * 20.0);\n"
    "    float block_glitch = step(0.92, rand(vec2(block_y, floor(time * 5.0))));\n"
    "    float block_shift = (rand(vec2(block_y, time)) - 0.5) * block_glitch * glitch_strength * 0.15;\n"
    "    vec2 block_uv = vec2(uv.x + block_shift, uv.y);\n"
    "    \n"
    "    if (block_glitch > 0.5) {\n"
    "        new_img = texture(texture1, block_uv);\n"
    "    }\n"
    "    \n"
    "    // Mix old and new based on progress\n"
    "    vec4 color = mix(old_img, new_img, progress);\n"
    "    color.rgb += scanline;\n"
    "    \n"
    "    // Digital noise\n"
    "    float noise = rand(uv + time) * 0.05 * glitch_strength;\n"
    "    color.rgb += noise;\n"
    "    \n"
    "    fragColor = color;\n"
    "}\n";

bool shader_create_glitch_program(GLuint *program) {
    return shader_create_program_from_sources(
        glitch_vertex_shader_source,
        glitch_fragment_shader_source,
        program
    );
}

bool transition_glitch_render(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_error("Glitch transition: invalid output or images");
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_error("Glitch transition: missing textures");
        return false;
    }

    if (output->glitch_program == 0) {
        log_error("Glitch transition: program not initialized");
        return false;
    }

    /* Use unified transition context API */
    transition_context_t ctx;
    if (!transition_begin(&ctx, output, output->glitch_program)) {
        return false;
    }

    /* Use progress-based time for deterministic glitches */
    float time_value = progress * 10.0f;

    /* Draw blended transition with both textures */
    if (!transition_draw_blended_textures(&ctx,
                                           output->next_texture,
                                           output->texture,
                                           progress,
                                           time_value,
                                           NULL)) {
        transition_end(&ctx);
        return false;
    }

    transition_end(&ctx);
    return true;
}