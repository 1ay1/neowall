/* Shader Core - Basic Shader Utilities
 *
 * Provides essential shader compilation and program creation utilities
 * used by transitions and basic effects.
 *
 * For live wallpaper shaders (Shadertoy format), use shader_multipass.h instead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "platform_compat.h"
#include "shader.h"

/* Global error log buffer for detailed error reporting */
#define MAX_ERROR_LOG_SIZE 16384
static char g_last_error_log[MAX_ERROR_LOG_SIZE];
static size_t g_error_log_pos = 0;

/* Maximum path length */
#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 4096
#endif

/* ============================================
 * Error Logging Functions
 * ============================================ */

/* Clear error log */
static void clear_error_log(void) {
    g_last_error_log[0] = '\0';
    g_error_log_pos = 0;
}

/* Append to error log */
static void append_to_error_log(const char *format, ...) {
    if (g_error_log_pos >= MAX_ERROR_LOG_SIZE - 1) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(g_last_error_log + g_error_log_pos,
                           MAX_ERROR_LOG_SIZE - g_error_log_pos,
                           format, args);
    va_end(args);

    if (written > 0) {
        g_error_log_pos += (size_t)written;
        if (g_error_log_pos >= MAX_ERROR_LOG_SIZE) {
            g_error_log_pos = MAX_ERROR_LOG_SIZE - 1;
        }
    }
}

/* Get last error log */
const char *shader_get_last_error_log(void) {
    return g_last_error_log;
}

/* ============================================
 * Shader Compilation
 * ============================================ */

/**
 * Compile a shader from source
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
        append_to_error_log("ERROR: Failed to create %s shader (glCreateShader returned 0)\n", type_str);
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    /* Check compilation status */
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        append_to_error_log("\n=== %s SHADER COMPILATION FAILED ===\n\n",
                           (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT");

        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc((size_t)info_len);
            if (info_log) {
                glGetShaderInfoLog(shader, info_len, NULL, info_log);
                log_error("%s shader compilation failed: %s", type_str, info_log);
                append_to_error_log("%s", info_log);
                append_to_error_log("\n\n");
                free(info_log);
            }
        } else {
            log_error("%s shader compilation failed (no log available)", type_str);
            append_to_error_log("No detailed error information available from OpenGL.\n\n");
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
 * Used by transitions and simple effects.
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

    /* Clear previous error log */
    clear_error_log();

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
        append_to_error_log("\n=== PROGRAM LINKING FAILED ===\n\n");

        GLint info_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc((size_t)info_len);
            if (info_log) {
                glGetProgramInfoLog(prog, info_len, NULL, info_log);
                log_error("Program linking failed: %s", info_log);
                append_to_error_log("%s\n", info_log);
                free(info_log);
            }
        } else {
            append_to_error_log("No detailed linking error information available.\n");
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

/* ============================================
 * Shader File Loading
 * ============================================ */

/**
 * Resolve shader path by checking multiple locations
 *
 * @param shader_name Shader filename or path
 * @param resolved_path Buffer to store resolved path
 * @param resolved_size Size of resolved_path buffer
 * @return true if shader found, false otherwise
 */
static bool shader_resolve_path(const char *shader_name, char *resolved_path, size_t resolved_size) {
    if (!shader_name || !resolved_path || resolved_size == 0) {
        return false;
    }

    /* If it's an absolute path or starts with ~, use it directly */
    if (shader_name[0] == '/' || shader_name[0] == '~') {
        strncpy(resolved_path, shader_name, resolved_size - 1);
        resolved_path[resolved_size - 1] = '\0';
        return true;
    }

    /* If it contains a path separator, treat it as a relative path */
    if (strchr(shader_name, '/') != NULL) {
        strncpy(resolved_path, shader_name, resolved_size - 1);
        resolved_path[resolved_size - 1] = '\0';
        return true;
    }

    /* Search in multiple locations for just the shader name */
    const char *home = getenv("HOME");
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");

    /* List of directories to search (in order of preference) */
    char search_paths[4][MAX_PATH_LENGTH];
    int num_paths = 0;

    /* 1. XDG_CONFIG_HOME/neowall/shaders/ */
    if (xdg_config_home && xdg_config_home[0] != '\0') {
        snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "%s/neowall/shaders/%s", xdg_config_home, shader_name);
    }

    /* 2. ~/.config/neowall/shaders/ */
    if (home && home[0] != '\0') {
        snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "%s/.config/neowall/shaders/%s", home, shader_name);
    }

    /* 3. /usr/share/neowall/shaders/ */
    snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "/usr/share/neowall/shaders/%s", shader_name);

    /* 4. /usr/local/share/neowall/shaders/ */
    snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "/usr/local/share/neowall/shaders/%s", shader_name);

    /* Check each path */
    for (int i = 0; i < num_paths; i++) {
        if (access(search_paths[i], R_OK) == 0) {
            size_t len = strlen(search_paths[i]);
            if (len >= resolved_size) {
                log_error("Resolved shader path too long: %s", search_paths[i]);
                continue;
            }
            strncpy(resolved_path, search_paths[i], resolved_size - 1);
            resolved_path[resolved_size - 1] = '\0';
            log_debug("Resolved shader '%s' to: %s", shader_name, resolved_path);
            return true;
        }
    }

    log_error("Shader not found: %s", shader_name);
    return false;
}

/**
 * Load shader source from file
 *
 * @param path Path to shader file
 * @return Shader source code (must be freed by caller), or NULL on error
 */
char *shader_load_file(const char *path) {
    if (!path) {
        log_error("Invalid shader path");
        return NULL;
    }

    /* Resolve shader path (checks config dir, then system dirs) */
    char resolved_path[MAX_PATH_LENGTH];
    if (!shader_resolve_path(path, resolved_path, sizeof(resolved_path))) {
        return NULL;
    }

    /* Expand tilde if present */
    char expanded_path[MAX_PATH_LENGTH];
    if (resolved_path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, resolved_path + 1);
        } else {
            strncpy(expanded_path, resolved_path, sizeof(expanded_path) - 1);
            expanded_path[sizeof(expanded_path) - 1] = '\0';
        }
    } else {
        strncpy(expanded_path, resolved_path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }

    /* Open file in binary mode to get accurate byte count */
    FILE *fp = fopen(expanded_path, "rb");
    if (!fp) {
        log_error("Failed to open shader file: %s", expanded_path);
        return NULL;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        log_error("Invalid shader file size: %s", expanded_path);
        fclose(fp);
        return NULL;
    }

    /* Allocate buffer */
    char *source = malloc((size_t)size + 1);
    if (!source) {
        log_error("Failed to allocate memory for shader source");
        fclose(fp);
        return NULL;
    }

    /* Read file */
    size_t bytes_read = fread(source, 1, (size_t)size, fp);
    fclose(fp);

    if (bytes_read == 0) {
        log_error("Failed to read shader file: %s", expanded_path);
        free(source);
        return NULL;
    }

    source[bytes_read] = '\0';
    log_debug("Loaded shader from %s (%zu bytes)", expanded_path, bytes_read);
    return source;
}