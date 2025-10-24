#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <GLES2/gl2.h>
#include <ctype.h>
#include "staticwall.h"
#include "constants.h"
#include "shader.h"
#include "shadertoy_compat.h"

/**
 * Shader Compilation Utilities
 * 
 * Provides shared shader compilation and program creation utilities
 * for all transitions. Each transition defines its own shader sources
 * and creation functions in their respective files.
 */

/* Standard vertex shader for live wallpapers - ES 2.0 */
static const char *live_vertex_shader_es2 =
    "#version 100\n"
    "attribute vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

/* Standard vertex shader for live wallpapers - ES 3.0 */
static const char *live_vertex_shader_es3 =
    "#version 300 es\n"
    "in vec2 position;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

/* Shadertoy compatibility wrapper prefix - ES 2.0 version */
static const char *shadertoy_wrapper_prefix_es2 =
    "#version 100\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "// Shadertoy compatibility uniforms\n"
    "uniform float time;          // Maps to iTime\n"
    "uniform vec2 resolution;     // Maps to iResolution.xy\n"
    "uniform vec3 iResolution;    // Shadertoy iResolution (set in render)\n"
    "\n"
    "// Shadertoy uniform arrays (must come after precision specifier)\n"
    "uniform vec4 iChannelTime[4];\n"
    "uniform vec3 iChannelResolution[4];\n"
    "\n"
    "// Shadertoy uniforms - defined with default behavior\n"
    "#define iTime time\n"
    "#define iTimeDelta 0.016667\n"
    "#define iFrame 0\n"
    "#define iMouse vec4(0.0, 0.0, 0.0, 0.0)\n"
    "#define iDate vec4(2024.0, 1.0, 1.0, 0.0)\n"
    "#define iSampleRate 44100.0\n"
    "\n";

/* Shadertoy compatibility wrapper prefix - ES 3.0 version */
static const char *shadertoy_wrapper_prefix_es3 =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "// Shadertoy compatibility uniforms\n"
    "uniform float time;          // Maps to iTime\n"
    "uniform vec2 resolution;     // Maps to iResolution.xy\n"
    "uniform vec3 iResolution;    // Shadertoy iResolution (set in render)\n"
    "\n"
    "// Shadertoy uniform arrays (must come after precision specifier)\n"
    "uniform vec4 iChannelTime[4];\n"
    "uniform vec3 iChannelResolution[4];\n"
    "\n"
    "// Shadertoy uniforms - defined with default behavior\n"
    "#define iTime time\n"
    "#define iTimeDelta 0.016667\n"
    "#define iFrame 0\n"
    "#define iMouse vec4(0.0, 0.0, 0.0, 0.0)\n"
    "#define iDate vec4(2024.0, 1.0, 1.0, 0.0)\n"
    "#define iSampleRate 44100.0\n"
    "\n"
    "out vec4 fragColor;\n"
    "\n";

/* Build dynamic iChannel declarations based on channel count */
static char *build_ichannel_declarations(size_t channel_count) {
    if (channel_count == 0) {
        channel_count = 5; // Default to 5 channels
    }
    
    size_t buffer_size = channel_count * 64 + 128; // ~64 bytes per channel + header
    char *declarations = malloc(buffer_size);
    if (!declarations) {
        return NULL;
    }
    
    strcpy(declarations, "// Texture samplers for image inputs\n");
    
    for (size_t i = 0; i < channel_count; i++) {
        char line[64];
        snprintf(line, sizeof(line), "uniform sampler2D iChannel%zu;\n", i);
        strcat(declarations, line);
    }
    
    strcat(declarations, "\n");
    
    return declarations;
}

/* Shadertoy compatibility wrapper suffix - ES 2.0 version */
static const char *shadertoy_wrapper_suffix_es2 =
    "\n"
    "void main() {\n"
    "    mainImage(gl_FragColor, gl_FragCoord.xy);\n"
    "}\n";

/* Shadertoy compatibility wrapper suffix - ES 3.0 version */
static const char *shadertoy_wrapper_suffix_es3 =
    "\n"
    "void main() {\n"
    "    vec4 color;\n"
    "    mainImage(color, gl_FragCoord.xy);\n"
    "    fragColor = color;\n"
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
 * Check if shader source uses Shadertoy format (mainImage function)
 * 
 * @param source Shader source code
 * @return true if Shadertoy format detected, false otherwise
 */
static bool is_shadertoy_format(const char *source) {
    if (!source) {
        return false;
    }
    
    /* Look for mainImage function signature */
    /* Common patterns:
     * - void mainImage(out vec4 fragColor, in vec2 fragCoord)
     * - void mainImage( out vec4 fragColor, in vec2 fragCoord )
     * We'll search for "mainImage" followed by "fragColor" and "fragCoord"
     */
    const char *mainImage = strstr(source, "mainImage");
    if (!mainImage) {
        return false;
    }
    
    /* Check if fragColor and fragCoord appear after mainImage */
    const char *fragColor = strstr(mainImage, "fragColor");
    const char *fragCoord = strstr(mainImage, "fragCoord");
    
    if (fragColor && fragCoord) {
        /* Make sure they appear reasonably close (within 100 chars of mainImage) */
        if ((fragColor - mainImage < 100) && (fragCoord - mainImage < 100)) {
            log_debug("Detected Shadertoy format shader (mainImage function found)");
            return true;
        }
    }
    
    return false;
}

/**
 * Strip #version directive from shader source
 * 
 * @param source Shader source code
 * @return Pointer to source after #version line, or original source if not found
 */
static const char *strip_version_directive(const char *source) {
    if (!source) {
        return source;
    }
    
    /* Skip whitespace at start */
    while (*source && isspace(*source)) {
        source++;
    }
    
    /* Check if starts with #version */
    if (strncmp(source, "#version", 8) == 0) {
        /* Find end of line */
        const char *newline = strchr(source, '\n');
        if (newline) {
            return newline + 1;
        }
    }
    
    return source;
}

/**
 * Wrap Shadertoy format shader with compatibility layer
 * 
 * @param shadertoy_source Original Shadertoy shader source
 * @param channel_count Number of iChannels to declare (0 = default 5)
 * @return Wrapped shader source (must be freed by caller), or NULL on error
 */
static char *wrap_shadertoy_shader(const char *shadertoy_source, size_t channel_count) {
    if (!shadertoy_source) {
        return NULL;
    }
    
    /* Strip #version directive from original source if present */
    const char *source_body = strip_version_directive(shadertoy_source);
    
    /* Build dynamic iChannel declarations */
    char *channel_decls = build_ichannel_declarations(channel_count);
    if (!channel_decls) {
        log_error("Failed to build iChannel declarations");
        return NULL;
    }
    
    /* Detect GL version at runtime to choose appropriate wrapper */
    const GLubyte *version_string = glGetString(GL_VERSION);
    bool use_es3 = false;
    
    if (version_string) {
        /* Check for ES 3.x in version string */
        if (strstr((const char*)version_string, "ES 3.") != NULL) {
            use_es3 = true;
            log_debug("Using ES 3.0 Shadertoy wrapper");
        } else {
            log_debug("Using ES 2.0 Shadertoy wrapper");
        }
    } else {
        log_debug("Could not detect GL version, defaulting to ES 2.0 wrapper");
    }
    
    /* Select appropriate wrapper strings */
    const char *prefix = use_es3 ? shadertoy_wrapper_prefix_es3 : shadertoy_wrapper_prefix_es2;
    const char *suffix = use_es3 ? shadertoy_wrapper_suffix_es3 : shadertoy_wrapper_suffix_es2;
    
    /* Calculate total size needed */
    size_t prefix_len = strlen(prefix);
    size_t channel_len = strlen(channel_decls);
    size_t body_len = strlen(source_body);
    size_t suffix_len = strlen(suffix);
    size_t total_len = prefix_len + channel_len + body_len + suffix_len + 1;
    
    /* Allocate buffer */
    char *wrapped = malloc(total_len);
    if (!wrapped) {
        log_error("Failed to allocate memory for wrapped Shadertoy shader");
        free(channel_decls);
        return NULL;
    }
    
    /* Concatenate parts */
    strcpy(wrapped, prefix);
    strcat(wrapped, channel_decls);
    strcat(wrapped, source_body);
    strcat(wrapped, suffix);
    
    free(channel_decls);
    
    /* Only convert texture calls if we're using ES 2.0 wrapper */
    if (!use_es3) {
        /* Use shadertoy_compat to intelligently convert GLSL 3.0 texture calls
         * to GLSL ES 1.0 texture2D calls for iChannel samplers */
        char *converted = shadertoy_convert_texture_calls(wrapped);
        free(wrapped);
        
        if (!converted) {
            log_error("Failed to convert texture calls in Shadertoy shader");
            return NULL;
        }
        
        wrapped = converted;
    }
    
    log_info("Wrapped Shadertoy format shader with compatibility layer (%zu channels)", 
             channel_count == 0 ? 5 : channel_count);
    return wrapped;
}

/**
 * Create live wallpaper shader program from file
 * 
 * @param shader_path Path to fragment shader file
 * @param program Pointer to store the created program ID
 * @param channel_count Number of iChannels to declare (0 = default 5)
 * @return true on success, false on failure
 */
bool shader_create_live_program(const char *shader_path, GLuint *program, size_t channel_count) {
    if (!shader_path || !program) {
        log_error("Invalid parameters for live shader creation");
        return false;
    }

    /* Load fragment shader from file */
    char *fragment_src = shader_load_file(shader_path);
    if (!fragment_src) {
        return false;
    }

    log_info("Loaded shader source: %zu bytes", strlen(fragment_src));

    /* Check if shader is in Shadertoy format and wrap if needed */
    char *final_fragment_src = fragment_src;
    bool is_shadertoy = is_shadertoy_format(fragment_src);
    
    if (is_shadertoy) {
        log_info("Detected Shadertoy format shader");
        
        /* Analyze shader complexity and features */
        shadertoy_analyze_shader(fragment_src);
        
        /* DISABLED: Preprocessing interferes with real iChannel textures
         * The preprocessor was replacing texture() calls with noise fallbacks
         * Now that we have real texture support, we don't need fallbacks */
        /*
        char *preprocessed = shadertoy_preprocess(fragment_src);
        if (preprocessed) {
            log_info("Shader preprocessed: %zu bytes -> %zu bytes", 
                     strlen(fragment_src), strlen(preprocessed));
            free(fragment_src);
            fragment_src = preprocessed;
        } else {
            log_info("Shader preprocessing skipped or failed, using original");
        }
        */
        log_info("Using original shader source (preprocessing disabled to support real iChannel textures)");
        
        /* Wrap with Shadertoy compatibility layer */
        final_fragment_src = wrap_shadertoy_shader(fragment_src, channel_count);
        log_debug("Final wrapped shader source:");
        log_debug("========================");
        
        /* Print shader with line numbers for easier debugging */
        if (final_fragment_src) {
            char *line_start = final_fragment_src;
            char *line_end;
            int line_num = 1;
            
            while (line_start && *line_start) {
                line_end = strchr(line_start, '\n');
                if (line_end) {
                    *line_end = '\0';
                    log_debug("%3d: %s", line_num, line_start);
                    *line_end = '\n';
                    line_start = line_end + 1;
                } else {
                    log_debug("%3d: %s", line_num, line_start);
                    break;
                }
                line_num++;
            }
        }
        
        log_debug("========================");
        if (!final_fragment_src) {
            log_error("Failed to wrap Shadertoy shader");
            free(fragment_src);
            return false;
        }
        
        log_info("Wrapped shader: %zu bytes final", strlen(final_fragment_src));
        
        /* Save first 500 chars for debugging */
        char preview[501];
        size_t preview_len = strlen(final_fragment_src);
        if (preview_len > 500) preview_len = 500;
        memcpy(preview, final_fragment_src, preview_len);
        preview[preview_len] = '\0';
        log_debug("Shader preview:\n%s\n...", preview);
    }

    /* Detect GL version to select appropriate vertex shader */
    const GLubyte *version_string = glGetString(GL_VERSION);
    const char *vertex_shader = live_vertex_shader_es2; // Default to ES 2.0
    
    if (version_string && strstr((const char*)version_string, "ES 3.") != NULL) {
        vertex_shader = live_vertex_shader_es3;
        log_debug("Using ES 3.0 vertex shader");
    } else {
        log_debug("Using ES 2.0 vertex shader");
    }
    
    /* Save wrapped shader for debugging on failure */
    FILE *debug_fp = fopen("/tmp/staticwall_shader_debug.glsl", "w");
    if (debug_fp) {
        fprintf(debug_fp, "%s", final_fragment_src);
        fclose(debug_fp);
        log_debug("Saved wrapped shader to /tmp/staticwall_shader_debug.glsl for debugging");
    }
    
    /* Create program with standard vertex shader and loaded fragment shader */
    log_info("Compiling shader program...");
    bool success = shader_create_program_from_sources(
        vertex_shader,
        final_fragment_src,
        program
    );

    /* Free allocated memory */
    if (is_shadertoy) {
        free(final_fragment_src);
    }
    free(fragment_src);

    if (success) {
        log_info("Successfully created live wallpaper shader program from: %s%s", 
                 shader_path, is_shadertoy ? " (Shadertoy format)" : "");
    } else {
        log_error("Failed to create shader program from: %s", shader_path);
    }

    return success;
}
