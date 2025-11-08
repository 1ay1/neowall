#include <stddef.h>
#include <GLES2/gl2.h>
#include <string.h>
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

/**
 * Common Transition Helper Functions (DRY Principle)
 * 
 * These functions provide shared functionality across all transitions
 * to avoid code duplication and ensure consistency.
 */

/**
 * Setup fullscreen quad vertices for transitions
 * 
 * Creates a simple fullscreen quad with standard texture coordinates.
 * This provides consistent rendering during transitions regardless of
 * image aspect ratios or display modes.
 * 
 * @param vbo VBO to bind and upload data to
 * @param vertices Output array to populate (must be 16 floats)
 */
void transition_setup_fullscreen_quad(GLuint vbo, float vertices[16]) {
    /* Fullscreen quad: position (x,y) and texcoord (u,v) interleaved */
    vertices[0]  = -1.0f; vertices[1]  =  1.0f; vertices[2]  = 0.0f; vertices[3]  = 0.0f;  /* top-left */
    vertices[4]  =  1.0f; vertices[5]  =  1.0f; vertices[6]  = 1.0f; vertices[7]  = 0.0f;  /* top-right */
    vertices[8]  = -1.0f; vertices[9]  = -1.0f; vertices[10] = 0.0f; vertices[11] = 1.0f;  /* bottom-left */
    vertices[12] =  1.0f; vertices[13] = -1.0f; vertices[14] = 1.0f; vertices[15] = 1.0f;  /* bottom-right */
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, vertices, GL_DYNAMIC_DRAW);
}

/**
 * Bind texture for transition rendering with consistent settings
 * 
 * @param texture Texture to bind
 * @param texture_unit GL_TEXTURE0, GL_TEXTURE1, etc.
 */
void transition_bind_texture_for_transition(GLuint texture, GLenum texture_unit) {
    glActiveTexture(texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    /* Always use CLAMP_TO_EDGE during transitions to prevent artifacts
     * This ensures edges don't wrap or repeat unexpectedly */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/**
 * Setup common vertex attributes for transitions
 * 
 * Most transitions use position and texcoord attributes with the same layout.
 * This function sets them up consistently.
 * 
 * @param program Shader program to get attribute locations from
 * @param vbo VBO that's already bound with vertex data
 */
void transition_setup_common_attributes(GLuint program, GLuint vbo) {
    GLint pos_attrib = glGetAttribLocation(program, "position");
    GLint tex_attrib = glGetAttribLocation(program, "texcoord");
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    /* Position attribute (x, y) - first 2 floats of each vertex */
    if (pos_attrib >= 0) {
        glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(pos_attrib);
    }
    
    /* Texcoord attribute (u, v) - last 2 floats of each vertex */
    if (tex_attrib >= 0) {
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(tex_attrib);
    }
}

/**
 * High-Level Transition Context API
 * 
 * This API provides automatic OpenGL state management for transitions,
 * eliminating the need for each transition to manually handle:
 * - Error clearing
 * - Viewport setup
 * - Vertex attribute management
 * - Buffer binding
 * - State cleanup
 * 
 * This ensures consistent behavior across all transitions and proper
 * multi-monitor support.
 */

/**
 * Begin a transition rendering context
 * 
 * Initializes OpenGL state for transition rendering. This function:
 * - Clears any previous OpenGL errors
 * - Sets up viewport and clears the screen
 * - Activates the shader program
 * - Caches attribute locations
 * - Sets up fullscreen quad vertices
 * - Enables blending
 * 
 * @param ctx Transition context to initialize
 * @param output Output state
 * @param program Shader program to use for rendering
 * @return true on success, false on error
 */
bool transition_begin(transition_context_t *ctx, struct output_state *output, GLuint program) {
    if (!ctx || !output) {
        log_error("transition_begin: invalid parameters");
        return false;
    }
    
    if (program == 0) {
        log_error("transition_begin: invalid shader program");
        return false;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(transition_context_t));
    ctx->output = output;
    ctx->program = program;
    ctx->blend_enabled = false;
    ctx->error_occurred = false;
    
    /* Clear any previous OpenGL errors (critical for multi-monitor) */
    while (glGetError() != GL_NO_ERROR);
    
    /* Set viewport */
    glViewport(0, 0, output->width, output->height);
    
    /* Clear screen */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    /* Use shader program */
    glUseProgram(program);
    
    /* Cache attribute locations */
    ctx->pos_attrib = glGetAttribLocation(program, "position");
    ctx->tex_attrib = glGetAttribLocation(program, "texcoord");
    
    /* Setup fullscreen quad vertices */
    ctx->vertices[0]  = -1.0f; ctx->vertices[1]  =  1.0f; ctx->vertices[2]  = 0.0f; ctx->vertices[3]  = 0.0f;
    ctx->vertices[4]  =  1.0f; ctx->vertices[5]  =  1.0f; ctx->vertices[6]  = 1.0f; ctx->vertices[7]  = 0.0f;
    ctx->vertices[8]  = -1.0f; ctx->vertices[9]  = -1.0f; ctx->vertices[10] = 0.0f; ctx->vertices[11] = 1.0f;
    ctx->vertices[12] =  1.0f; ctx->vertices[13] = -1.0f; ctx->vertices[14] = 1.0f; ctx->vertices[15] = 1.0f;
    
    /* Enable blending for transitions */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx->blend_enabled = true;
    
    return true;
}

/**
 * Draw a textured quad in transition context
 * 
 * This function handles all state management for drawing a single textured quad:
 * - Binds and uploads vertex data
 * - Sets up vertex attributes
 * - Binds texture
 * - Sets alpha uniform
 * - Draws the quad
 * - Maintains proper state between draws
 * 
 * @param ctx Transition context (must be initialized with transition_begin)
 * @param texture Texture to bind (or 0 to skip texture binding)
 * @param alpha Alpha value for the draw (set to 1.0 for opaque, or progress for fade)
 * @param custom_vertices Optional custom vertices (or NULL to use fullscreen quad)
 * @return true on success, false on error
 */
bool transition_draw_textured_quad(transition_context_t *ctx, GLuint texture, 
                                    float alpha, const float *custom_vertices) {
    if (!ctx || !ctx->output) {
        log_error("transition_draw_textured_quad: invalid context");
        return false;
    }
    
    if (ctx->error_occurred) {
        return false;  /* Don't attempt more draws if an error occurred */
    }
    
    /* Use custom vertices if provided, otherwise use cached fullscreen quad */
    const float *vertices_to_use = custom_vertices ? custom_vertices : ctx->vertices;
    
    /* Bind VBO and upload vertex data */
    glBindBuffer(GL_ARRAY_BUFFER, ctx->output->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, vertices_to_use, GL_DYNAMIC_DRAW);
    
    /* Setup vertex attributes (do this for every draw to ensure state is correct) */
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
    
    /* Bind texture if provided */
    if (texture != 0) {
        transition_bind_texture_for_transition(texture, GL_TEXTURE0);
        GLint tex_uniform = glGetUniformLocation(ctx->program, "texture0");
        if (tex_uniform >= 0) {
            glUniform1i(tex_uniform, 0);
        }
    }
    
    /* Set alpha uniform if available */
    GLint alpha_uniform = glGetUniformLocation(ctx->program, "alpha");
    if (alpha_uniform >= 0) {
        glUniform1f(alpha_uniform, alpha);
    }
    
    /* Draw the quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL error during transition draw: 0x%x", error);
        ctx->error_occurred = true;
        return false;
    }
    
    return true;
}

/**
 * End transition rendering context
 * 
 * Cleans up all OpenGL state that was set up during transition_begin:
 * - Disables vertex attributes
 * - Unbinds buffers and textures
 * - Disables blending
 * - Unbinds shader program
 * - Checks for final errors
 * 
 * @param ctx Transition context to clean up
 */
void transition_end(transition_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    /* Disable vertex attributes */
    if (ctx->pos_attrib >= 0) {
        glDisableVertexAttribArray(ctx->pos_attrib);
    }
    if (ctx->tex_attrib >= 0) {
        glDisableVertexAttribArray(ctx->tex_attrib);
    }
    
    /* Unbind buffers and textures */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    /* Disable blending if it was enabled */
    if (ctx->blend_enabled) {
        glDisable(GL_BLEND);
    }
    
    /* Unbind shader program */
    glUseProgram(0);
    
    /* Final error check */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR && !ctx->error_occurred) {
        log_error("OpenGL error during transition cleanup: 0x%x", error);
    }
    
    /* Update output state */
    if (ctx->output && !ctx->error_occurred) {
        ctx->output->needs_redraw = true;
        ctx->output->frames_rendered++;
    }
}
