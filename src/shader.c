#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <GLES2/gl2.h>
#include <ctype.h>
#include "neowall.h"
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
    "// Shadertoy compatibility uniforms (prefixed to avoid conflicts)\n"
    "uniform float _neowall_time;          // Maps to iTime\n"
    "uniform vec2 _neowall_resolution;     // Maps to iResolution.xy\n"
    "uniform vec3 iResolution;    // Shadertoy iResolution (set in render)\n"
    "\n"
    "// Non-Shadertoy uniforms (for plain shaders)\n"
    "#define time _neowall_time\n"
    "#define resolution _neowall_resolution\n"
    "\n"
    "// Shadertoy uniform arrays (must come after precision specifier)\n"
    "uniform vec4 iChannelTime[4];\n"
    "uniform vec3 iChannelResolution[4];\n"
    "\n"
    "// Shadertoy uniforms - defined with default behavior\n"
    "#define iTime _neowall_time\n"
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
    "// Shadertoy compatibility uniforms (prefixed to avoid conflicts)\n"
    "uniform float _neowall_time;          // Maps to iTime\n"
    "uniform vec2 _neowall_resolution;     // Maps to iResolution.xy\n"
    "uniform vec3 iResolution;    // Shadertoy iResolution (set in render)\n"
    "\n"
    "// Non-Shadertoy uniforms (for plain shaders)\n"
    "#define time _neowall_time\n"
    "#define resolution _neowall_resolution\n"
    "\n"
    "// Shadertoy uniform arrays (must come after precision specifier)\n"
    "uniform vec4 iChannelTime[4];\n"
    "uniform vec3 iChannelResolution[4];\n"
    "\n"
    "// Shadertoy uniforms - defined with default behavior\n"
    "#define iTime _neowall_time\n"
    "#define iTimeDelta 0.016667\n"
    "#define iFrame 0\n"
    "#define iMouse vec4(0.0, 0.0, 0.0, 0.0)\n"
    "#define iDate vec4(2024.0, 1.0, 1.0, 0.0)\n"
    "#define iSampleRate 44100.0\n"
    "\n"
    "// GLSL ES 3.0 output\n"
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
 * Print shader source with line numbers for debugging
 */
static void print_shader_with_line_numbers(const char *source, const char *type) {
    if (!source) return;
    
    log_debug("========== %s SHADER SOURCE (with line numbers) ==========", type);
    
    const char *line_start = source;
    const char *line_end;
    int line_num = 1;
    
    while (*line_start) {
        line_end = strchr(line_start, '\n');
        if (line_end) {
            // Print line with number
            log_debug("%4d: %.*s", line_num, (int)(line_end - line_start), line_start);
            line_start = line_end + 1;
        } else {
            // Last line without newline
            log_debug("%4d: %s", line_num, line_start);
            break;
        }
        line_num++;
    }
    
    log_debug("========== END %s SHADER SOURCE ==========", type);
}

/**
 * Compile a shader
 * 
 * @param type Shader type (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER)
 * @param source Shader source code
 * @return Compiled shader ID, or 0 on failure
 */
static GLuint compile_shader(GLenum type, const char *source) {
    const char *type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    
    // Debug: print shader source with line numbers
    print_shader_with_line_numbers(source, type_str);
    
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
    
    /* 1. XDG_CONFIG_HOME/neowall/shaders/ */
    if (xdg_config_home && home) {
        snprintf(search_paths[num_paths++], MAX_PATH_LENGTH, "%s/neowall/shaders/%s", xdg_config_home, shader_name);
    }
    
    /* 2. ~/.config/neowall/shaders/ */
    if (home) {
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
    /* Shadertoy shaders define a mainImage function with signature:
     *   void mainImage(out vec4 <name>, in vec2 <name>)
     * Parameter names can be anything (fragColor/fragCoord, o/u, col/uv, etc.)
     * We'll just check for "void mainImage(" or "void mainImage (" patterns
     */
    const char *mainImage = strstr(source, "mainImage");
    if (!mainImage) {
        return false;
    }
    
    /* Check if it's a function definition (has opening parenthesis after mainImage) */
    const char *openParen = mainImage + strlen("mainImage");
    while (*openParen && isspace(*openParen)) {
        openParen++;
    }
    
    if (*openParen == '(') {
        /* Also check for 'void' keyword before mainImage (within reasonable distance) */
        const char *checkStart = (mainImage - source) > 20 ? (mainImage - 20) : source;
        const char *voidKeyword = strstr(checkStart, "void");
        
        /* Check if 'void' appears before 'mainImage' in the search window */
        if (voidKeyword && voidKeyword < mainImage) {
            log_debug("Detected Shadertoy format shader (mainImage function found)");
            return true;
        }
    }
    
    return false;
}

/**
 * Skip whitespace, comments, and preprocessor directives
 * @param p Current position in source
 * @return Position after skipped content
 */
static const char *skip_whitespace_and_comments(const char *p) {
    if (!p) return NULL;
    
    while (*p) {
        /* Skip whitespace */
        if (isspace(*p)) {
            p++;
            continue;
        }
        
        /* Skip single-line comments */
        if (*p == '/' && *(p+1) == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }
        
        /* Skip multi-line comments */
        if (*p == '/' && *(p+1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p+1) == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        
        /* Skip preprocessor directives */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }
        
        break;
    }
    
    return p;
}

/**
 * Check if character is a valid identifier character
 */
static inline bool is_identifier_char(char c) {
    return isalnum(c) || c == '_';
}

/**
 * Intelligent uniform declaration detector
 * Handles comments, preprocessor directives, and multi-line declarations
 * 
 * @param source Shader source code
 * @param uniform_name Name of uniform to search for
 * @return true if uniform is declared, false otherwise
 */
static bool has_uniform_declaration(const char *source, const char *uniform_name) {
    if (!source || !uniform_name) {
        return false;
    }
    
    const char *p = source;
    size_t name_len = strlen(uniform_name);
    
    while (*p) {
        p = skip_whitespace_and_comments(p);
        if (!*p) break;
        
        /* Look for "uniform" keyword */
        if (strncmp(p, "uniform", 7) == 0 && !is_identifier_char(*(p+7))) {
            p += 7;
            p = skip_whitespace_and_comments(p);
            
            /* Skip type declaration (float, vec2, vec3, sampler2D, etc.) */
            /* Could be: float, vec2, vec3, vec4, int, mat3, mat4, sampler2D, etc. */
            while (*p && is_identifier_char(*p)) p++;
            
            /* Skip array brackets if present: float[4] */
            p = skip_whitespace_and_comments(p);
            if (*p == '[') {
                while (*p && *p != ']') p++;
                if (*p) p++;
            }
            
            p = skip_whitespace_and_comments(p);
            
            /* Now we should be at the uniform name */
            if (strncmp(p, uniform_name, name_len) == 0 && 
                !is_identifier_char(*(p + name_len))) {
                /* Verify what follows is valid (semicolon, equals, or array bracket) */
                const char *after = skip_whitespace_and_comments(p + name_len);
                if (*after == ';' || *after == '=' || *after == '[' || *after == ',') {
                    log_debug("Found uniform declaration: %s at position %ld", 
                             uniform_name, (long)(p - source));
                    return true;
                }
            }
        }
        
        p++;
    }
    
    return false;
}

/**
 * Detect assignment to a uniform-like variable
 * Looks for patterns like: iTime = value; or _neowall_time += value;
 * 
 * @param source Shader source code
 * @param var_name Variable name to check
 * @return true if assignment found, false otherwise
 */
static bool has_uniform_assignment(const char *source, const char *var_name) {
    if (!source || !var_name) {
        return false;
    }
    
    const char *p = source;
    size_t name_len = strlen(var_name);
    
    while ((p = strstr(p, var_name)) != NULL) {
        /* Check if this is a complete identifier (not part of another word) */
        bool valid_start = (p == source || !is_identifier_char(*(p-1)));
        bool valid_end = !is_identifier_char(*(p + name_len));
        
        if (valid_start && valid_end) {
            /* Skip whitespace after the identifier */
            const char *after = skip_whitespace_and_comments(p + name_len);
            
            /* Check for assignment operators: =, +=, -=, *=, /= */
            if (after && (*after == '=' || 
                         (*after == '+' && *(after+1) == '=') ||
                         (*after == '-' && *(after+1) == '=') ||
                         (*after == '*' && *(after+1) == '=') ||
                         (*after == '/' && *(after+1) == '='))) {
                log_debug("Found assignment to %s at position %ld", 
                         var_name, (long)(p - source));
                return true;
            }
        }
        
        p++;
    }
    
    return false;
}

/**
 * Detect global variable declaration (non-uniform)
 * Looks for patterns like: float time = 0.0; or vec3 resolution;
 * 
 * @param source Shader source code
 * @param var_name Variable name to check
 * @return true if global variable declaration found, false otherwise
 */
static bool has_global_variable_declaration(const char *source, const char *var_name) {
    if (!source || !var_name) {
        return false;
    }
    
    const char *p = source;
    size_t name_len = strlen(var_name);
    
    /* Look for type keywords that indicate variable declarations */
    const char *types[] = {
        "float", "vec2", "vec3", "vec4", 
        "int", "ivec2", "ivec3", "ivec4",
        "bool", "bvec2", "bvec3", "bvec4",
        "mat2", "mat3", "mat4",
        NULL
    };
    
    while (*p) {
        p = skip_whitespace_and_comments(p);
        if (!*p) break;
        
        /* Check for type keywords (but not uniform or const) */
        for (int i = 0; types[i]; i++) {
            size_t type_len = strlen(types[i]);
            if (strncmp(p, types[i], type_len) == 0 && 
                !is_identifier_char(*(p + type_len))) {
                
                /* Make sure this isn't preceded by "uniform" or "const" */
                const char *check_back = p;
                while (check_back > source && isspace(*(check_back - 1))) {
                    check_back--;
                }
                
                bool is_uniform = (check_back >= source + 7 && 
                                  strncmp(check_back - 7, "uniform", 7) == 0);
                bool is_const = (check_back >= source + 5 && 
                                strncmp(check_back - 5, "const", 5) == 0);
                
                if (!is_uniform && !is_const) {
                    /* This is a regular variable declaration - check the name */
                    const char *name_pos = p + type_len;
                    name_pos = skip_whitespace_and_comments(name_pos);
                    
                    if (strncmp(name_pos, var_name, name_len) == 0 && 
                        !is_identifier_char(*(name_pos + name_len))) {
                        
                        /* Check what follows - should be =, ;, or [ */
                        const char *after = skip_whitespace_and_comments(name_pos + name_len);
                        if (*after == '=' || *after == ';' || *after == '[') {
                            log_debug("Found global variable declaration: %s at position %ld", 
                                     var_name, (long)(name_pos - source));
                            return true;
                        }
                    }
                }
                break;
            }
        }
        
        p++;
    }
    
    return false;
}

/**
 * Detect and report all conflicts in shader source
 * 
 * @param source Shader source code
 * @return Bitmask of conflicts (0 = no conflicts)
 */
static uint32_t detect_shader_conflicts(const char *source) {
    if (!source) return 0;
    
    uint32_t conflicts = 0;
    
    /* Check for reserved neowall uniform declarations */
    if (has_uniform_declaration(source, "_neowall_time")) {
        conflicts |= (1 << 0);
        log_info("Shader declares '_neowall_time' uniform (neowall provides this)");
    }
    
    if (has_uniform_declaration(source, "_neowall_resolution")) {
        conflicts |= (1 << 1);
        log_info("Shader declares '_neowall_resolution' uniform (neowall provides this)");
    }
    
    /* Check for Shadertoy standard uniforms that we provide via macros */
    if (has_uniform_declaration(source, "iTime")) {
        conflicts |= (1 << 2);
        log_info("Shader declares 'iTime' uniform (neowall provides this via macro)");
    }
    
    if (has_uniform_declaration(source, "iResolution")) {
        conflicts |= (1 << 3);
        log_info("Shader declares 'iResolution' uniform (neowall provides this)");
    }
    
    /* Check for assignments to uniforms (which are read-only) */
    if (has_uniform_assignment(source, "_neowall_time")) {
        conflicts |= (1 << 4);
        log_info("Shader attempts to assign to '_neowall_time' (uniforms are read-only)");
    }
    
    if (has_uniform_assignment(source, "iTime")) {
        conflicts |= (1 << 5);
        log_info("Shader attempts to assign to 'iTime' (expands to uniform, read-only)");
    }
    
    /* Check for global variable shadowing (variables that would conflict with our macros) */
    if (has_global_variable_declaration(source, "time")) {
        conflicts |= (1 << 6);
        log_info("Shader declares global variable 'time' (conflicts with neowall macro)");
    }
    
    if (has_global_variable_declaration(source, "resolution")) {
        conflicts |= (1 << 7);
        log_info("Shader declares global variable 'resolution' (conflicts with neowall macro)");
    }
    
    return conflicts;
}

/**
 * Remove conflicting global variable declarations
 * Removes declarations like: float time = 0.0;
 * 
 * @param source Original shader source
 * @return Cleaned shader source (caller must free), or NULL on error
 */
static char *strip_global_variables(const char *source) {
    if (!source) return NULL;
    
    /* Variables to strip */
    const char *strip_vars[] = {
        "time",
        "resolution",
        NULL
    };
    
    /* Allocate result buffer */
    size_t src_len = strlen(source);
    char *result = malloc(src_len + 1);
    if (!result) {
        log_error("Failed to allocate memory for global variable cleaning");
        return NULL;
    }
    
    const char *read_ptr = source;
    char *write_ptr = result;
    int removed_count = 0;
    
    while (*read_ptr) {
        const char *line_start = read_ptr;
        bool should_remove = false;
        const char *found_var = NULL;
        
        /* Check if this line contains a global variable declaration to remove */
        for (int i = 0; strip_vars[i]; i++) {
            if (has_global_variable_declaration(line_start, strip_vars[i])) {
                /* Verify this declaration is on the current line */
                const char *line_end = strchr(line_start, '\n');
                const char *semicolon = strchr(line_start, ';');
                
                if (semicolon && (!line_end || semicolon < line_end)) {
                    should_remove = true;
                    found_var = strip_vars[i];
                    break;
                }
            }
        }
        
        if (should_remove) {
            /* Skip this line - find the semicolon */
            const char *semicolon = strchr(read_ptr, ';');
            if (semicolon) {
                read_ptr = semicolon + 1;
                /* Also skip trailing newline if present */
                if (*read_ptr == '\n') read_ptr++;
                
                removed_count++;
                log_info("Removed conflicting global variable: %s", found_var);
            } else {
                /* No semicolon found - just skip the line */
                const char *newline = strchr(read_ptr, '\n');
                if (newline) {
                    read_ptr = newline + 1;
                } else {
                    break;
                }
            }
        } else {
            /* Keep this line */
            while (*read_ptr && *read_ptr != '\n') {
                *write_ptr++ = *read_ptr++;
            }
            if (*read_ptr == '\n') {
                *write_ptr++ = *read_ptr++;
            }
        }
    }
    
    *write_ptr = '\0';
    
    if (removed_count > 0) {
        log_info("Automatically removed %d conflicting global variable(s)", removed_count);
    }
    
    return result;
}

/**
 * Remove conflicting uniform declarations from shader source
 * Strategy: Comment out or remove lines with conflicting declarations
 * 
 * @param source Original shader source
 * @return Cleaned shader source (caller must free), or NULL on error
 */
static char *strip_conflicting_uniforms(const char *source) {
    if (!source) return NULL;
    
    /* Names to strip */
    const char *strip_uniforms[] = {
        "_neowall_time",
        "_neowall_resolution",
        "iTime",
        "iResolution",
        NULL
    };
    
    /* Allocate result buffer (same size as original) */
    size_t src_len = strlen(source);
    char *result = malloc(src_len + 1);
    if (!result) {
        log_error("Failed to allocate memory for shader cleaning");
        return NULL;
    }
    
    const char *read_ptr = source;
    char *write_ptr = result;
    int removed_count = 0;
    
    while (*read_ptr) {
        const char *line_start = read_ptr;
        
        /* Skip whitespace and comments at line start */
        const char *content_start = skip_whitespace_and_comments(read_ptr);
        
        /* Check if this line contains a uniform declaration to remove */
        bool should_remove = false;
        const char *found_uniform = NULL;
        
        if (content_start && strncmp(content_start, "uniform", 7) == 0 && 
            !is_identifier_char(*(content_start + 7))) {
            
            /* This line starts with "uniform" - check if it declares a conflicting name */
            for (int i = 0; strip_uniforms[i]; i++) {
                if (has_uniform_declaration(line_start, strip_uniforms[i])) {
                    /* Find if this uniform declaration is on this line */
                    const char *line_end = strchr(line_start, '\n');
                    const char *semicolon = strchr(line_start, ';');
                    
                    if (semicolon && (!line_end || semicolon < line_end)) {
                        should_remove = true;
                        found_uniform = strip_uniforms[i];
                        break;
                    }
                }
            }
        }
        
        if (should_remove) {
            /* Skip this line - find the semicolon */
            const char *semicolon = strchr(read_ptr, ';');
            if (semicolon) {
                read_ptr = semicolon + 1;
                /* Also skip trailing newline if present */
                if (*read_ptr == '\n') read_ptr++;
                
                removed_count++;
                log_info("Removed conflicting uniform declaration: %s", found_uniform);
            } else {
                /* No semicolon found - just skip the line */
                const char *newline = strchr(read_ptr, '\n');
                if (newline) {
                    read_ptr = newline + 1;
                } else {
                    break; /* End of file */
                }
            }
        } else {
            /* Keep this line - copy character by character until newline */
            while (*read_ptr && *read_ptr != '\n') {
                *write_ptr++ = *read_ptr++;
            }
            if (*read_ptr == '\n') {
                *write_ptr++ = *read_ptr++;
            }
        }
    }
    
    *write_ptr = '\0';
    
    if (removed_count > 0) {
        log_info("Automatically removed %d conflicting uniform declaration(s)", removed_count);
    }
    
    return result;
}

/**
 * Replace assignments to read-only uniforms with local variables
 * Strategy: Remove or comment out assignments like "time = iTime;"
 * Since these variables will be macros that expand to uniforms, assignments are illegal
 * 
 * @param source Original shader source
 * @return Fixed shader source (caller must free), or NULL on error
 */
static char *fix_uniform_assignments(const char *source) {
    if (!source) return NULL;
    
    /* Check if we need to fix anything */
    bool needs_fix = has_uniform_assignment(source, "iTime") ||
                     has_uniform_assignment(source, "_neowall_time") ||
                     has_uniform_assignment(source, "time") ||
                     has_uniform_assignment(source, "resolution");
    
    if (!needs_fix) {
        return strdup(source); /* No changes needed */
    }
    
    /* Allocate result buffer - add extra space for comments we might add */
    size_t src_len = strlen(source);
    char *result = malloc(src_len * 2); /* Double size for safety - comments add text */
    if (!result) {
        log_error("Failed to allocate memory for fixing uniform assignments");
        return NULL;
    }
    
    const char *read_ptr = source;
    char *write_ptr = result;
    int fix_count = 0;
    
    /* List of variables that map to read-only uniforms */
    const char *readonly_vars[] = {
        "iTime", "_neowall_time", "time",
        "iResolution", "_neowall_resolution", "resolution",
        NULL
    };
    
    while (*read_ptr) {
        const char *line_start = read_ptr;
        bool should_remove = false;
        const char *found_var = NULL;
        
        /* Check if this line contains an assignment to a read-only variable */
        for (int i = 0; readonly_vars[i]; i++) {
            const char *var_name = readonly_vars[i];
            size_t var_len = strlen(var_name);
            
            /* Look for the variable name in this line */
            const char *var_pos = strstr(line_start, var_name);
            if (var_pos) {
                /* Verify it's a complete identifier */
                bool valid_start = (var_pos == line_start || !is_identifier_char(*(var_pos-1)));
                bool valid_end = !is_identifier_char(*(var_pos + var_len));
                
                if (valid_start && valid_end) {
                    /* Check if followed by assignment operator */
                    const char *after = skip_whitespace_and_comments(var_pos + var_len);
                    if (after && *after == '=') {
                        /* Check it's not == (comparison) */
                        if (*(after + 1) != '=') {
                            const char *line_end = strchr(line_start, '\n');
                            const char *semicolon = strchr(var_pos, ';');
                            
                            if (semicolon && (!line_end || semicolon < line_end)) {
                                should_remove = true;
                                found_var = var_name;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        if (should_remove) {
            /* Remove this assignment line - skip to semicolon */
            const char *semicolon = strchr(read_ptr, ';');
            if (semicolon) {
                /* Add a comment explaining what was removed */
                write_ptr += sprintf(write_ptr, "// [neowall] Removed assignment to read-only uniform: %s", found_var);
                
                read_ptr = semicolon + 1;
                /* Preserve newline if present */
                if (*read_ptr == '\n') {
                    *write_ptr++ = *read_ptr++;
                }
                
                fix_count++;
                log_info("Removed assignment to read-only uniform: %s", found_var);
            } else {
                /* No semicolon - skip whole line */
                const char *newline = strchr(read_ptr, '\n');
                if (newline) {
                    read_ptr = newline + 1;
                } else {
                    break;
                }
            }
        } else {
            /* Keep this line */
            while (*read_ptr && *read_ptr != '\n') {
                *write_ptr++ = *read_ptr++;
            }
            if (*read_ptr == '\n') {
                *write_ptr++ = *read_ptr++;
            }
        }
    }
    
    *write_ptr = '\0';
    
    if (fix_count > 0) {
        log_info("Fixed %d assignment(s) to read-only uniforms", fix_count);
    }
    
    return result;
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
    
    /* INTELLIGENT CONFLICT RESOLUTION - Strategy 2 & 3 */
    
    /* Step 1: Detect conflicts before processing */
    uint32_t conflicts = detect_shader_conflicts(source_body);
    
    /* Step 2: Apply automatic fixes if conflicts detected */
    char *cleaned_source = NULL;
    bool needs_cleanup = false;
    
    if (conflicts) {
        log_info("╔══════════════════════════════════════════════════════════════╗");
        log_info("║ Shader Conflict Detected - Applying Automatic Fixes         ║");
        log_info("╚══════════════════════════════════════════════════════════════╝");
        
        /* Fix 1: Remove conflicting uniform declarations */
        if (conflicts & ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3))) {
            log_info("» Removing conflicting uniform declarations...");
            cleaned_source = strip_conflicting_uniforms(source_body);
            if (cleaned_source) {
                source_body = cleaned_source;
                needs_cleanup = true;
                log_info("✓ Successfully removed conflicting uniforms");
            } else {
                log_error("✗ Failed to clean conflicting uniforms");
                return NULL;
            }
        }
        
        /* Fix 2: Remove conflicting global variable declarations */
        if (conflicts & ((1 << 6) | (1 << 7))) {
            log_info("» Removing conflicting global variables...");
            char *cleaned_vars = strip_global_variables(source_body);
            if (cleaned_vars) {
                if (needs_cleanup) {
                    free(cleaned_source);
                }
                source_body = cleaned_vars;
                cleaned_source = cleaned_vars;
                needs_cleanup = true;
                log_info("✓ Successfully removed conflicting global variables");
                
                /* After removing global variables, check for orphaned assignments */
                /* Example: "float time = 0;" was removed, but "time = iTime;" remains */
                /* This will expand to "_neowall_time = iTime" which is an assignment to uniform */
                if (has_uniform_assignment(source_body, "time") || 
                    has_uniform_assignment(source_body, "resolution")) {
                    log_info("» Detected orphaned assignments after removing global variables");
                    conflicts |= (1 << 4);  /* Set assignment conflict flag */
                }
            } else {
                log_error("✗ Failed to clean conflicting global variables");
                if (needs_cleanup) {
                    free(cleaned_source);
                }
                return NULL;
            }
        }
        
        /* Fix 3: Replace assignments to read-only uniforms */
        if (conflicts & ((1 << 4) | (1 << 5))) {
            log_info("» Fixing assignments to read-only uniforms...");
            char *fixed_source = fix_uniform_assignments(source_body);
            if (fixed_source) {
                if (needs_cleanup) {
                    free(cleaned_source);
                }
                source_body = fixed_source;
                cleaned_source = fixed_source;
                needs_cleanup = true;
                log_info("✓ Successfully fixed uniform assignments");
            } else {
                log_error("✗ Could not fix uniform assignments automatically");
            }
        }
        
        log_info("╔══════════════════════════════════════════════════════════════╗");
        log_info("║ Conflict Resolution Complete                                 ║");
        log_info("╠══════════════════════════════════════════════════════════════╣");
        log_info("║ Your shader has been automatically modified to work with     ║");
        log_info("║ neowall. Conflicting declarations have been removed.         ║");
        log_info("║                                                               ║");
        log_info("║ If you see compilation errors, please check:                 ║");
        log_info("║   /tmp/neowall_shader_debug.glsl                              ║");
        log_info("╚══════════════════════════════════════════════════════════════╝");
    }
    
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
    
    /* Clean up the conflict-resolution allocated source */
    if (needs_cleanup && cleaned_source) {
        free(cleaned_source);
    }
    
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
    FILE *debug_fp = fopen("/tmp/neowall_shader_debug.glsl", "w");
    if (debug_fp) {
        fprintf(debug_fp, "%s", final_fragment_src);
        fclose(debug_fp);
        log_debug("Saved wrapped shader to /tmp/neowall_shader_debug.glsl for debugging");
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
