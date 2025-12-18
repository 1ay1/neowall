#ifndef SHADER_H
#define SHADER_H

#include "platform_compat.h"
#include <stdbool.h>

/**
 * Creates a shader program from source code.
 * Shared utility function used by all transitions.
 * 
 * @param vertex_src Vertex shader source code
 * @param fragment_src Fragment shader source code
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_program_from_sources(const char *vertex_src, 
                                         const char *fragment_src,
                                         GLuint *program);

/**
 * Destroys a shader program.
 * @param program The program ID to destroy
 */
void shader_destroy_program(GLuint program);

/* Transition-specific shader creation functions (defined in transition files) */
bool shader_create_fade_program(GLuint *program);
bool shader_create_slide_program(GLuint *program);
bool shader_create_glitch_program(GLuint *program);
bool shader_create_pixelate_program(GLuint *program);

/**
 * Get the last detailed error log from shader compilation/linking
 * 
 * @return Pointer to static error log buffer (valid until next compilation)
 */
const char *shader_get_last_error_log(void);

/**
 * Load shader source from file
 * 
 * @param path Path to shader file (absolute or relative to config dir)
 * @return Shader source string (caller must free) or NULL on failure
 */
char *shader_load_file(const char *path);

#endif /* SHADER_H */