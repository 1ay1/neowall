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
