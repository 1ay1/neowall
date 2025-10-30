#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* External functions from render.c */
extern void calculate_vertex_coords_for_image(struct output_state *output, 
                                               struct image_data *image, 
                                               float vertices[16]);

/* Vertex shader for pixelate transition */
static const char *pixelate_vertex_shader_source =
    GLSL_VERSION_STRING
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Pixelate/mosaic fragment shader with dramatic effects */
static const char *pixelate_fragment_shader_source =
    GLSL_VERSION_STRING
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "uniform float progress;\n"
    "uniform float time;\n"
    "\n"
    "// Pseudo-random function\n"
    "float rand(vec2 co) {\n"
    "    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = v_texcoord;\n"
    "    \n"
    "    // Smooth easing - slow start, fast middle, slow end\n"
    "    float eased = progress < 0.5 \n"
    "        ? 2.0 * progress * progress \n"
    "        : 1.0 - 2.0 * (1.0 - progress) * (1.0 - progress);\n"
    "    \n"
    "    // Dramatic pixelation curve - gets HUGE in the middle\n"
    "    float intensity = sin(eased * 3.14159);\n"
    "    float pixelation = pow(intensity, 0.7) * 120.0 + 1.0;\n"
    "    \n"
    "    // Calculate pixel block\n"
    "    vec2 pixel_size = vec2(1.0) / pixelation;\n"
    "    vec2 block_id = floor(uv / pixel_size);\n"
    "    vec2 block_center = (block_id + 0.5) * pixel_size;\n"
    "    \n"
    "    // Add subtle rotation to blocks at peak pixelation\n"
    "    float rotation = (rand(block_id) - 0.5) * intensity * 0.15;\n"
    "    float cos_r = cos(rotation);\n"
    "    float sin_r = sin(rotation);\n"
    "    vec2 rotated_uv = block_center + mat2(cos_r, -sin_r, sin_r, cos_r) * (uv - block_center);\n"
    "    \n"
    "    // Clamp to prevent sampling outside texture\n"
    "    rotated_uv = clamp(rotated_uv, 0.0, 1.0);\n"
    "    \n"
    "    // Sample at block center for mosaic effect\n"
    "    vec4 old_color = texture2D(texture0, block_center);\n"
    "    vec4 new_color = texture2D(texture1, block_center);\n"
    "    \n"
    "    // Chromatic aberration increases with pixelation\n"
    "    float aberration = intensity * pixel_size.x * 2.0;\n"
    "    vec4 old_r = texture2D(texture0, block_center + vec2(aberration, 0.0));\n"
    "    vec4 old_b = texture2D(texture0, block_center - vec2(aberration, 0.0));\n"
    "    vec4 new_r = texture2D(texture1, block_center + vec2(aberration, 0.0));\n"
    "    vec4 new_b = texture2D(texture1, block_center - vec2(aberration, 0.0));\n"
    "    \n"
    "    old_color.r = old_r.r;\n"
    "    old_color.b = old_b.b;\n"
    "    new_color.r = new_r.r;\n"
    "    new_color.b = new_b.b;\n"
    "    \n"
    "    // Smooth transition between images\n"
    "    vec4 color = mix(old_color, new_color, eased);\n"
    "    \n"
    "    // Add color vibrance boost at peak\n"
    "    float vibrance = intensity * 0.25;\n"
    "    color.rgb = mix(color.rgb, color.rgb * 1.3, vibrance);\n"
    "    \n"
    "    // Pixel grid lines for retro effect\n"
    "    vec2 grid = fract(uv * pixelation);\n"
    "    float grid_line = step(0.92, max(grid.x, grid.y)) * intensity * 0.4;\n"
    "    color.rgb += grid_line;\n"
    "    \n"
    "    // Subtle vignette darkening at edges of each block\n"
    "    vec2 block_edge = abs(grid - 0.5) * 2.0;\n"
    "    float vignette = 1.0 - pow(max(block_edge.x, block_edge.y), 2.0) * intensity * 0.3;\n"
    "    color.rgb *= vignette;\n"
    "    \n"
    "    // Flash effect at peak transition\n"
    "    float peak_flash = 1.0 - abs(eased - 0.5) * 2.0;\n"
    "    color.rgb += vec3(peak_flash * 0.15);\n"
    "    \n"
    "    gl_FragColor = color;\n"
    "}\n";

/**
 * Create shader program for pixelate transition
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_pixelate_program(GLuint *program) {
    return shader_create_program_from_sources(
        pixelate_vertex_shader_source,
        pixelate_fragment_shader_source,
        program
    );
}

/**
 * Pixelate Transition
 * 
 * Dramatic mosaic/pixelation effect. Image progressively breaks into huge pixel
 * blocks with chromatic aberration, then smoothly transitions to the new image
 * as blocks reform. Creates a vibrant retro-digital aesthetic.
 * 
 * Features:
 * - Smooth easing with dramatic pixelation curve (up to 120x120 blocks!)
 * - RGB chromatic aberration that intensifies with pixelation
 * - Subtle block rotation for dynamic feel
 * - Pixel grid lines for authentic retro look
 * - Vignette effect on block edges
 * - Color vibrance boost at transition peak
 * - Flash effect at maximum pixelation
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error
 */
bool transition_pixelate_render(struct output_state *output, float progress) {
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

    /* Use pixelate shader program */
    glUseProgram(output->pixelate_program);

    /* Get attribute locations */
    GLint pos_attrib = glGetAttribLocation(output->pixelate_program, "position");
    GLint tex_attrib = glGetAttribLocation(output->pixelate_program, "texcoord");
    
    /* Get uniform locations */
    GLint tex0_uniform = glGetUniformLocation(output->pixelate_program, "texture0");
    GLint tex1_uniform = glGetUniformLocation(output->pixelate_program, "texture1");
    GLint progress_uniform = glGetUniformLocation(output->pixelate_program, "progress");
    GLint time_uniform = glGetUniformLocation(output->pixelate_program, "time");

    /* Setup fullscreen quad using DRY helper */
    float vertices[16];
    transition_setup_fullscreen_quad(output->vbo, vertices);
    
    /* Setup vertex attributes using DRY helper */
    transition_setup_common_attributes(output->pixelate_program, output->vbo);

    /* Bind old texture to texture unit 0 using DRY helper */
    transition_bind_texture_for_transition(output->next_texture, GL_TEXTURE0);
    if (tex0_uniform >= 0) {
        glUniform1i(tex0_uniform, 0);
    }

    /* Bind new texture to texture unit 1 using DRY helper */
    transition_bind_texture_for_transition(output->texture, GL_TEXTURE1);
    
    if (tex1_uniform >= 0) {
        glUniform1i(tex1_uniform, 1);
    }

    /* Set uniforms */
    if (progress_uniform >= 0) {
        glUniform1f(progress_uniform, progress);
    }

    if (time_uniform >= 0) {
        /* Use transition progress as time */
        float time_value = progress;
        glUniform1f(time_uniform, time_value);
    }

    /* Draw fullscreen quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Clean up */
    if (pos_attrib >= 0) {
        glDisableVertexAttribArray(pos_attrib);
    }
    if (tex_attrib >= 0) {
        glDisableVertexAttribArray(tex_attrib);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    /* Unbind textures */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glUseProgram(0);

    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during pixelate transition: 0x%x", error);
        return false;
    }

    output->needs_redraw = true;
    output->frames_rendered++;

    return true;
}
