#include <stdio.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include "staticwall.h"
#include "shader.h"

/**
 * Shader Compilation Utilities
 * 
 * Provides shared shader compilation and program creation utilities
 * for all transitions. Each transition defines its own shader sources
 * and creation functions in their respective files.
 */

/**
 * Compile a shader
 * 
 * @param type Shader type (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER)
 * @param source Shader source code
 * @return Compiled shader ID, or 0 on failure
 */
static GLuint compile_shader(GLenum type, const char *source) {
    const char *type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        log_error("Failed to create %s shader", type_str);
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    /* Check compilation status */
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetShaderInfoLog(shader, info_len, NULL, info_log);
                log_error("%s shader compilation failed: %s", type_str, info_log);
                free(info_log);
            }
        } else {
            log_error("%s shader compilation failed (no log available)", type_str);
        }
        glDeleteShader(shader);
        return 0;
    }

    log_debug("%s shader compiled successfully", type_str);
    return shader;
}

/**
 * Create a shader program from source code
 * 
 * Shared utility function that compiles shaders and links them into a program.
 * Called by each transition's shader_create_*_program() function.
 * 
 * @param vertex_src Vertex shader source code
 * @param fragment_src Fragment shader source code
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_program_from_sources(const char *vertex_src, 
                                         const char *fragment_src,
                                         GLuint *program) {
    if (!program) {
        log_error("Invalid program pointer");
        return false;
    }

    /* Compile shaders */
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (vertex_shader == 0) {
        return false;
    }

    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }

    /* Create program */
    GLuint prog = glCreateProgram();
    if (prog == 0) {
        log_error("Failed to create shader program");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    /* Attach shaders */
    glAttachShader(prog, vertex_shader);
    glAttachShader(prog, fragment_shader);

    /* Link program */
    glLinkProgram(prog);

    /* Check link status */
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetProgramInfoLog(prog, info_len, NULL, info_log);
                log_error("Program linking failed: %s", info_log);
                free(info_log);
            }
        }
        glDeleteProgram(prog);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    /* Shaders can be deleted after linking */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    *program = prog;
    log_debug("Shader program created successfully (ID: %u)", prog);
    return true;
}

/**
 * Destroy a shader program
 * 
 * @param program The program ID to destroy
 */
void shader_destroy_program(GLuint program) {
    if (program != 0) {
        glDeleteProgram(program);
        log_debug("Destroyed shader program (ID: %u)", program);
    }
}
