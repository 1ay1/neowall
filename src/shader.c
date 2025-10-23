#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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

/* Standard vertex shader for live wallpapers */
static const char *live_vertex_shader =
    "#version 100\n"
    "attribute vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

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
    
    /* 1. XDG_CONFIG_HOME/staticwall/shaders/ */
    if (xdg_config_home && home) {
        snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "%s/staticwall/shaders/%s", xdg_config_home, shader_name);
    }
    
    /* 2. ~/.config/staticwall/shaders/ */
    if (home) {
        snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "%s/.config/staticwall/shaders/%s", home, shader_name);
    }
    
    /* 3. /usr/share/staticwall/shaders/ */
    snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "/usr/share/staticwall/shaders/%s", shader_name);
    
    /* 4. /usr/local/share/staticwall/shaders/ */
    snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "/usr/local/share/staticwall/shaders/%s", shader_name);
    
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
        }
    } else {
        strncpy(expanded_path, resolved_path, sizeof(expanded_path) - 1);
    }
    expanded_path[sizeof(expanded_path) - 1] = '\0';

    /* Open file */
    FILE *fp = fopen(expanded_path, "r");
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
    char *source = malloc(size + 1);
    if (!source) {
        log_error("Failed to allocate memory for shader source");
        fclose(fp);
        return NULL;
    }

    /* Read file */
    size_t read = fread(source, 1, size, fp);
    fclose(fp);

    if (read != (size_t)size) {
        log_error("Failed to read shader file: %s", expanded_path);
        free(source);
        return NULL;
    }

    source[size] = '\0';
    log_debug("Loaded shader from %s (%ld bytes)", expanded_path, size);
    return source;
}

/**
 * Create live wallpaper shader program from file
 * 
 * @param shader_path Path to fragment shader file
 * @param program Pointer to store the created program ID
 * @return true on success, false on failure
 */
bool shader_create_live_program(const char *shader_path, GLuint *program) {
    if (!shader_path || !program) {
        log_error("Invalid parameters for live shader creation");
        return false;
    }

    /* Load fragment shader from file */
    char *fragment_src = shader_load_file(shader_path);
    if (!fragment_src) {
        return false;
    }

    /* Create program with standard vertex shader and loaded fragment shader */
    bool success = shader_create_program_from_sources(
        live_vertex_shader,
        fragment_src,
        program
    );

    free(fragment_src);

    if (success) {
        log_info("Created live wallpaper shader program from: %s", shader_path);
    }

    return success;
}
