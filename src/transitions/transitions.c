/**
 * Transition System - Unified API for wallpaper transitions
 *
 * Provides a single source of truth for transition rendering with:
 * - Common OpenGL state management (VAO, viewport, clear, blend)
 * - Single-texture draws (fade, slide)
 * - Multi-texture blended draws (glitch, pixelate)
 *
 * All transitions use transition_begin() / transition_end() for setup/cleanup.
 */

#include <stddef.h>
#include <string.h>
#include <GL/gl.h>
#include "neowall.h"
#include "constants.h"
#include "transitions.h"

/**
 * Transition Registry
 * 
 * Central registry for all transition effects. New transitions can be added
 * simply by implementing the transition_render_func signature and registering
 * it in the transitions array.
 * 
 * This modular architecture makes it easy to:
 * - Add new transitions without modifying core render code
 * - Maintain transitions in separate, focused files
 * - Enable/disable transitions at compile time
 * - Test transitions independently
 */

static const struct transition transitions[] = {
    { TRANSITION_FADE,        "fade",        transition_fade_render },
    { TRANSITION_SLIDE_LEFT,  "slide_left",  transition_slide_left_render },
    { TRANSITION_SLIDE_RIGHT, "slide_right", transition_slide_right_render },
    { TRANSITION_GLITCH,      "glitch",      transition_glitch_render },
    { TRANSITION_PIXELATE,    "pixelate",    transition_pixelate_render },
};

static const size_t transition_count = sizeof(transitions) / sizeof(transitions[0]);

/**
 * Initialize transitions system
 * 
 * Currently no initialization needed, but provides hook for future
 * enhancements like dynamic registration or shader precompilation.
 */
void transitions_init(void) {
    log_debug("Transition system initialized with %zu transitions", transition_count);
}

/**
 * Render a transition effect
 * 
 * Dispatches to the appropriate transition renderer based on type.
 * If the transition type is not found, returns false.
 * 
 * @param output Output state containing images and textures
 * @param type Type of transition to render
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error or unknown transition
 */
bool transition_render(struct output_state *output, enum transition_type type, float progress) {
    if (!output) {
        log_error("Invalid output for transition render");
        return false;
    }

    /* Find and execute the transition renderer */
    for (size_t i = 0; i < transition_count; i++) {
        if (transitions[i].type == type) {
            log_debug("Rendering transition: %s (progress=%.2f)", 
                     transitions[i].name, progress);
            return transitions[i].render(output, progress);
        }
    }

    log_error("Unknown transition type: %d", type);
    return false;
}

/* ============================================================================
 * Internal Helper Functions (static - not exposed in header)
 * ============================================================================ */

static void setup_fullscreen_quad(float vertices[16]) {
    vertices[0]  = -1.0f; vertices[1]  =  1.0f; vertices[2]  = 0.0f; vertices[3]  = 0.0f;
    vertices[4]  =  1.0f; vertices[5]  =  1.0f; vertices[6]  = 1.0f; vertices[7]  = 0.0f;
    vertices[8]  = -1.0f; vertices[9]  = -1.0f; vertices[10] = 0.0f; vertices[11] = 1.0f;
    vertices[12] =  1.0f; vertices[13] = -1.0f; vertices[14] = 1.0f; vertices[15] = 1.0f;
}

static void bind_texture(GLuint texture, GLenum texture_unit) {
    glActiveTexture(texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* ============================================================================
 * Public Transition Context API
 * ============================================================================ */

bool transition_begin(transition_context_t *ctx, struct output_state *output, GLuint program) {
    if (!ctx || !output) {
        log_error("transition_begin: invalid parameters");
        return false;
    }
    if (program == 0) {
        log_error("transition_begin: invalid shader program");
        return false;
    }

    memset(ctx, 0, sizeof(transition_context_t));
    ctx->output = output;
    ctx->program = program;

    /* Clear previous GL errors */
    while (glGetError() != GL_NO_ERROR);

    /* Bind VAO - required for OpenGL 3.3 Core Profile */
    glBindVertexArray(output->vao);

    glViewport(0, 0, output->width, output->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);

    ctx->pos_attrib = glGetAttribLocation(program, "position");
    ctx->tex_attrib = glGetAttribLocation(program, "texcoord");

    setup_fullscreen_quad(ctx->vertices);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx->blend_enabled = true;

    return true;
}

bool transition_draw_textured_quad(transition_context_t *ctx, GLuint texture,
                                    float alpha, const float *custom_vertices) {
    if (!ctx || !ctx->output || ctx->error_occurred) {
        return false;
    }

    const float *verts = custom_vertices ? custom_vertices : ctx->vertices;

    glBindBuffer(GL_ARRAY_BUFFER, ctx->output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, verts, GL_DYNAMIC_DRAW);

    if (ctx->pos_attrib >= 0) {
        glVertexAttribPointer(ctx->pos_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(ctx->pos_attrib);
    }
    if (ctx->tex_attrib >= 0) {
        glVertexAttribPointer(ctx->tex_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(ctx->tex_attrib);
    }

    if (texture != 0) {
        bind_texture(texture, GL_TEXTURE0);
        GLint tex_uniform = glGetUniformLocation(ctx->program, "texture0");
        if (tex_uniform >= 0) {
            glUniform1i(tex_uniform, 0);
        }
    }

    GLint alpha_uniform = glGetUniformLocation(ctx->program, "alpha");
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, alpha);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during transition draw: 0x%x", error);
        ctx->error_occurred = true;
        return false;
    }
    return true;
}

bool transition_draw_blended_textures(transition_context_t *ctx,
                                       GLuint texture0, GLuint texture1,
                                       float progress, float time,
                                       const float *resolution) {
    if (!ctx || !ctx->output || ctx->error_occurred) {
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, ctx->output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, ctx->vertices, GL_DYNAMIC_DRAW);

    if (ctx->pos_attrib >= 0) {
        glVertexAttribPointer(ctx->pos_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(ctx->pos_attrib);
    }
    if (ctx->tex_attrib >= 0) {
        glVertexAttribPointer(ctx->tex_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(ctx->tex_attrib);
    }

    /* Bind textures */
    bind_texture(texture0, GL_TEXTURE0);
    GLint tex0_loc = glGetUniformLocation(ctx->program, "texture0");
    if (tex0_loc >= 0) glUniform1i(tex0_loc, 0);

    bind_texture(texture1, GL_TEXTURE1);
    GLint tex1_loc = glGetUniformLocation(ctx->program, "texture1");
    if (tex1_loc >= 0) glUniform1i(tex1_loc, 1);

    /* Set uniforms */
    GLint prog_loc = glGetUniformLocation(ctx->program, "progress");
    if (prog_loc >= 0) glUniform1f(prog_loc, progress);

    GLint time_loc = glGetUniformLocation(ctx->program, "time");
    if (time_loc >= 0) glUniform1f(time_loc, time);

    if (resolution) {
        GLint res_loc = glGetUniformLocation(ctx->program, "resolution");
        if (res_loc >= 0) glUniform2f(res_loc, resolution[0], resolution[1]);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during blended transition: 0x%x", error);
        ctx->error_occurred = true;
        return false;
    }
    return true;
}

void transition_end(transition_context_t *ctx) {
    if (!ctx) return;

    if (ctx->pos_attrib >= 0) glDisableVertexAttribArray(ctx->pos_attrib);
    if (ctx->tex_attrib >= 0) glDisableVertexAttribArray(ctx->tex_attrib);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (ctx->blend_enabled) glDisable(GL_BLEND);
    glUseProgram(0);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR && !ctx->error_occurred) {
        log_error("OpenGL error during transition cleanup: 0x%x", error);
    }

    if (ctx->output && !ctx->error_occurred) {
        ctx->output->needs_redraw = true;
        ctx->output->frames_rendered++;
    }
}
