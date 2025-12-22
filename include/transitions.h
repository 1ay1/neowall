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

/* Transition context for managing OpenGL state across draws */
typedef struct {
    struct output_state *output;
    GLuint program;
    GLint pos_attrib;
    GLint tex_attrib;
    float vertices[16];
    bool blend_enabled;
    bool error_occurred;
} transition_context_t;

/* High-level transition API - abstracts OpenGL state management */
bool transition_begin(transition_context_t *ctx, struct output_state *output, GLuint program);
bool transition_draw_textured_quad(transition_context_t *ctx, GLuint texture, 
                                    float alpha, const float *custom_vertices);
bool transition_draw_blended_textures(transition_context_t *ctx, 
                                       GLuint texture0, GLuint texture1,
                                       float progress, float time,
                                       const float *resolution);
void transition_end(transition_context_t *ctx);

/* Individual transition implementations */
bool transition_fade_render(struct output_state *output, float progress);
bool transition_slide_left_render(struct output_state *output, float progress);
bool transition_slide_right_render(struct output_state *output, float progress);
bool transition_glitch_render(struct output_state *output, float progress);
bool transition_pixelate_render(struct output_state *output, float progress);

#endif /* TRANSITIONS_H */
