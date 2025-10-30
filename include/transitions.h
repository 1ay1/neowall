#ifndef TRANSITIONS_H
#define TRANSITIONS_H

#include "neowall.h"

/* Transition rendering function signature */
typedef bool (*transition_render_func)(struct output_state *output, float progress);

/* Transition interface - each transition implements this */
struct transition {
    enum transition_type type;
    const char *name;
    transition_render_func render;
};

/* Transition registry functions */
void transitions_init(void);
bool transition_render(struct output_state *output, enum transition_type type, float progress);

/* Common transition helper functions (DRY) */
void transition_setup_fullscreen_quad(GLuint vbo, float vertices[16]);
void transition_bind_texture_for_transition(GLuint texture, GLenum texture_unit);
void transition_setup_common_attributes(GLuint program, GLuint vbo);

/* Individual transition implementations */
bool transition_fade_render(struct output_state *output, float progress);
bool transition_slide_left_render(struct output_state *output, float progress);
bool transition_slide_right_render(struct output_state *output, float progress);
bool transition_glitch_render(struct output_state *output, float progress);
bool transition_pixelate_render(struct output_state *output, float progress);

#endif /* TRANSITIONS_H */
