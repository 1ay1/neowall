#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"
#include "shader.h"

/* External functions from render.c */
extern void calculate_vertex_coords_for_image(struct output_state *output, 
                                               struct image_data *image, 
                                               float vertices[16]);

/* Vertex shader for glitch transition */
static const char *glitch_vertex_shader_source =
    GLSL_VERSION_STRING
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Glitch transition fragment shader */
static const char *glitch_fragment_shader_source =
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
    "    float glitch_strength = progress * (1.0 - progress) * 4.0; // Peak at 0.5\n"
    "    \n"
    "    // Horizontal glitch lines\n"
    "    float line = floor(uv.y * 80.0 + time * 10.0);\n"
    "    float glitch_line = step(0.95, rand(vec2(line, time)));\n"
    "    float offset = (rand(vec2(line, time + 0.1)) - 0.5) * glitch_strength * 0.1;\n"
    "    uv.x += offset * glitch_line;\n"
    "    \n"
    "    // RGB channel separation\n"
    "    float separation = glitch_strength * 0.02;\n"
    "    vec4 old_img = texture2D(texture0, uv);\n"
    "    vec4 new_img = texture2D(texture1, uv);\n"
    "    \n"
    "    // Chromatic aberration on new image\n"
    "    float r = texture2D(texture1, uv + vec2(separation, 0.0)).r;\n"
    "    float g = texture2D(texture1, uv).g;\n"
    "    float b = texture2D(texture1, uv - vec2(separation, 0.0)).b;\n"
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
    "    // Mix based on block corruption\n"
    "    if (block_glitch > 0.5) {\n"
    "        new_img = texture2D(texture1, block_uv);\n"
    "    }\n"
    "    \n"
    "    // Mix old and new based on progress with glitch\n"
    "    vec4 color = mix(old_img, new_img, progress);\n"
    "    color.rgb += scanline;\n"
    "    \n"
    "    // Digital noise\n"
    "    float noise = rand(uv + time) * 0.05 * glitch_strength;\n"
    "    color.rgb += noise;\n"
    "    \n"
    "    gl_FragColor = color;\n"
    "}\n";

/**
 * Create shader program for glitch transition
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_glitch_program(GLuint *program) {
    return shader_create_program_from_sources(
        glitch_vertex_shader_source,
        glitch_fragment_shader_source,
        program
    );
}

/**
 * Glitch Transition
 * 
 * Digital glitch effect with RGB channel separation, scan lines, horizontal
 * glitches, block corruption, and digital noise. Creates a cyberpunk aesthetic
 * transition between wallpapers.
 * 
 * The glitch intensity peaks at 50% progress for maximum visual impact.
 * 
 * @param output Output state containing images and textures
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error
 */
bool transition_glitch_render(struct output_state *output, float progress) {
    if (!output || !output->current_image || !output->next_image) {
        log_error("Glitch transition: invalid parameters (output=%p)", (void*)output);
        return false;
    }

    if (output->texture == 0 || output->next_texture == 0) {
        log_error("Glitch transition: missing textures (texture=%u, next_texture=%u)",
                 output->texture, output->next_texture);
        return false;
    }

    if (output->glitch_program == 0) {
        log_error("Glitch transition: glitch_program not initialized");
        return false;
    }

    log_debug("Glitch transition rendering: progress=%.2f, program=%u", 
             progress, output->glitch_program);

    /* Set viewport */
    glViewport(0, 0, output->width, output->height);

    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Use glitch shader program */
    glUseProgram(output->glitch_program);

    /* Get attribute locations */
    GLint pos_attrib = glGetAttribLocation(output->glitch_program, "position");
    GLint tex_attrib = glGetAttribLocation(output->glitch_program, "texcoord");
    
    /* Get uniform locations */
    GLint tex0_uniform = glGetUniformLocation(output->glitch_program, "texture0");
    GLint tex1_uniform = glGetUniformLocation(output->glitch_program, "texture1");
    GLint progress_uniform = glGetUniformLocation(output->glitch_program, "progress");
    GLint time_uniform = glGetUniformLocation(output->glitch_program, "time");

    /* Setup fullscreen quad using DRY helper */
    float vertices[16];
    transition_setup_fullscreen_quad(output->vbo, vertices);
    
    /* Setup vertex attributes using DRY helper */
    transition_setup_common_attributes(output->glitch_program, output->vbo);

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
        /* Use transition progress as time for deterministic glitches */
        float time_value = progress * 10.0f; /* Scale for variety */
        glUniform1f(time_uniform, time_value);
    }

    /* Disable alpha channel writes - force opaque output */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    /* Draw fullscreen quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Re-enable alpha channel writes */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

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
        log_error("OpenGL error during glitch transition: 0x%x", error);
        return false;
    }

    log_debug("Glitch transition frame rendered successfully");
    output->needs_redraw = true;
    output->frames_rendered++;

    return true;
}
