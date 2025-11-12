#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <GLES2/gl2.h>

/* Forward declarations */
struct output_state;
struct wallpaper_config;
struct neowall_state;

/* Texture creation from raw pixel data - render module doesn't need to know about image_data */
GLuint render_create_texture_from_pixels(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels);
GLuint render_create_texture_from_pixels_flipped(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels);
void render_destroy_texture(GLuint texture);

/* Legacy API - deprecated, use render_create_texture_from_pixels() instead */
struct image_data;  /* Only for legacy functions */
GLuint render_create_texture(struct image_data *img);

/* Rendering */
bool render_init_output(struct output_state *output);
void render_cleanup_output(struct output_state *output);
bool render_frame(struct output_state *output);
bool render_frame_shader(struct output_state *output);
bool render_frame_transition(struct output_state *output, float progress);
GLuint render_create_texture(struct image_data *img);
void render_destroy_texture(GLuint texture);
bool render_load_channel_textures(struct output_state *output, struct wallpaper_config *config);
bool render_update_channel_texture(struct output_state *output, size_t channel_index, const char *image_path);

/* GL shader programs */
bool shader_create_program(GLuint *program);
void shader_destroy_program(GLuint program);
const char *get_glsl_version_string(struct neowall_state *state);
char *adapt_shader_for_version(struct neowall_state *state, const char *shader_code, bool is_fragment_shader);
char *adapt_vertex_shader(struct neowall_state *state, const char *shader_code);
char *adapt_fragment_shader(struct neowall_state *state, const char *shader_code);

#endif /* RENDER_H */
