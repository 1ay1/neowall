/* NeoWall Shader Library - Unified API
 * Single source of truth for shader compilation used by both daemon and tray
 * This header provides a clean API wrapping the shader_core functions
 */

#ifndef NEOWALL_SHADER_API_H
#define NEOWALL_SHADER_API_H

#include <stdbool.h>
#include <stddef.h>
#include "platform_compat.h"

/**
 * Shader compilation result
 */
typedef struct {
    GLuint program;           /* Compiled shader program (0 if failed) */
    bool success;             /* Whether compilation succeeded */
    char *error_message;      /* Error message if failed (must be freed) */
    int error_line;           /* Line number where error occurred (-1 if unknown) */
} neowall_shader_result_t;

/**
 * Shader compilation options
 */
typedef struct {
    bool use_es3;             /* Use OpenGL ES 3.0 (true) or ES 2.0 (false) */
    int channel_count;        /* Number of texture channels (0-4, 0 = default 4) */
    bool verbose_errors;      /* Include full shader source in error messages */
} neowall_shader_options_t;

/**
 * Default shader options
 */
#define NEOWALL_SHADER_OPTIONS_DEFAULT { \
    .use_es3 = false, \
    .channel_count = 4, \
    .verbose_errors = false \
}

/**
 * Compile a shader from source code
 * Wraps shader_create_live_program for source compilation
 * 
 * @param shader_source User's shader code
 * @param options Compilation options (NULL for defaults)
 * @return Shader compilation result
 */
neowall_shader_result_t neowall_shader_compile(
    const char *shader_source,
    const neowall_shader_options_t *options
);

/**
 * Compile a shader from a file path
 * Uses shader_create_live_program from shader_core
 * 
 * @param shader_path Path to shader file
 * @param options Compilation options (NULL for defaults)
 * @return Shader compilation result
 */
neowall_shader_result_t neowall_shader_compile_file(
    const char *shader_path,
    const neowall_shader_options_t *options
);

/**
 * Destroy a compiled shader program
 * Wraps shader_destroy_program
 * 
 * @param program The shader program to destroy
 */
void neowall_shader_destroy(GLuint program);

/**
 * Free shader result error message
 * 
 * @param result The result whose error_message to free
 */
void neowall_shader_free_result(neowall_shader_result_t *result);

/**
 * Set shader uniforms for rendering
 * Sets the standard NeoWall uniforms that shaders expect
 * 
 * @param program The shader program
 * @param width Viewport width in pixels
 * @param height Viewport height in pixels
 * @param time Current time in seconds
 */
void neowall_shader_set_uniforms(
    GLuint program,
    int width,
    int height,
    float time
);

/**
 * Get the vertex shader source for fullscreen quad
 * 
 * @param use_es3 Whether to use ES3 version
 * @return Vertex shader source code (static, do not free)
 */
const char *neowall_shader_get_vertex_source(bool use_es3);

#endif /* NEOWALL_SHADER_API_H */