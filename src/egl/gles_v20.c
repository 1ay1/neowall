#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include "../../include/staticwall.h"
#include "../../include/egl/capability.h"

/**
 * OpenGL ES 2.0 Implementation
 * 
 * This is the baseline implementation that works on all systems.
 * Provides programmable shader support with GLSL 100.
 */

/* Initialize OpenGL ES 2.0 rendering */
bool gles20_init_rendering(struct output_state *output) {
    if (!output) {
        log_error("Invalid output for ES 2.0 initialization");
        return false;
    }
    
    log_debug("Initializing OpenGL ES 2.0 rendering for output %s", output->model);
    
    /* Set viewport */
    glViewport(0, 0, output->width, output->height);
    
    /* Clear color */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    /* Enable blending for transparency */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* Disable depth test (2D rendering) */
    glDisable(GL_DEPTH_TEST);
    
    /* Check for errors */
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_error("OpenGL ES 2.0 initialization error: 0x%x", error);
        return false;
    }
    
    log_debug("OpenGL ES 2.0 rendering initialized successfully");
    return true;
}

/* Cleanup OpenGL ES 2.0 resources */
void gles20_cleanup_rendering(struct output_state *output) {
    if (!output) {
        return;
    }
    
    log_debug("Cleaning up OpenGL ES 2.0 resources for output %s", output->model);
    
    /* Textures are cleaned up elsewhere */
    /* Shader programs are cleaned up elsewhere */
    
    /* Just reset state */
    glDisable(GL_BLEND);
}

/* Create shader program (ES 2.0 specific) */
bool gles20_create_shader_program(const char *vertex_src, const char *fragment_src,
                                   GLuint *program) {
    if (!vertex_src || !fragment_src || !program) {
        log_error("Invalid parameters for shader creation");
        return false;
    }
    
    /* Vertex shader */
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_src, NULL);
    glCompileShader(vertex_shader);
    
    GLint compiled;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetShaderInfoLog(vertex_shader, info_len, NULL, info_log);
                log_error("Vertex shader compilation failed: %s", info_log);
                free(info_log);
            }
        }
        glDeleteShader(vertex_shader);
        return false;
    }
    
    /* Fragment shader */
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_src, NULL);
    glCompileShader(fragment_shader);
    
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            if (info_log) {
                glGetShaderInfoLog(fragment_shader, info_len, NULL, info_log);
                log_error("Fragment shader compilation failed: %s", info_log);
                free(info_log);
            }
        }
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    
    /* Link program */
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vertex_shader);
    glAttachShader(prog, fragment_shader);
    glLinkProgram(prog);
    
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
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        glDeleteProgram(prog);
        return false;
    }
    
    /* Cleanup shaders (they're now in the program) */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    *program = prog;
    log_debug("OpenGL ES 2.0 shader program created successfully");
    return true;
}

/* Render frame with ES 2.0 */
bool gles20_render_frame(struct output_state *output) {
    if (!output) {
        return false;
    }
    
    /* Clear */
    glClear(GL_COLOR_BUFFER_BIT);
    
    /* Render using existing render system */
    /* The actual rendering is handled by render.c which uses ES 2.0 features */
    
    return true;
}

/* Check ES 2.0 specific capabilities */
void gles20_check_capabilities(gles_v20_caps_t *caps) {
    if (!caps) {
        return;
    }
    
    log_debug("Checking OpenGL ES 2.0 capabilities...");
    
    caps->available = true;
    caps->has_programmable_shaders = true;
    caps->has_vertex_shaders = true;
    caps->has_fragment_shaders = true;
    caps->has_glsl_100 = true;
    caps->has_framebuffer_objects = true;
    caps->has_vertex_buffer_objects = true;
    
    /* Query limits */
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &caps->max_vertex_attribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &caps->max_vertex_uniform_vectors);
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &caps->max_varying_vectors);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &caps->max_fragment_uniform_vectors);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &caps->max_texture_image_units);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &caps->max_vertex_texture_image_units);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &caps->max_combined_texture_image_units);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps->max_texture_size);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &caps->max_cube_map_texture_size);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &caps->max_renderbuffer_size);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, caps->max_viewport_dims);
    
    /* Check extensions */
    const char *extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (extensions) {
        caps->has_texture_npot = (strstr(extensions, "GL_OES_texture_npot") != NULL);
        caps->has_depth_texture = (strstr(extensions, "GL_OES_depth_texture") != NULL);
        caps->has_float_textures = (strstr(extensions, "GL_OES_texture_float") != NULL);
        caps->has_standard_derivatives = (strstr(extensions, "GL_OES_standard_derivatives") != NULL);
        caps->has_3d_textures = (strstr(extensions, "GL_OES_texture_3D") != NULL);
        caps->has_instanced_arrays = (strstr(extensions, "GL_EXT_instanced_arrays") != NULL);
        caps->has_depth24_stencil8 = (strstr(extensions, "GL_OES_packed_depth_stencil") != NULL);
    }
    
    log_debug("OpenGL ES 2.0 capabilities:");
    log_debug("  Max vertex attributes: %d", caps->max_vertex_attribs);
    log_debug("  Max texture units: %d", caps->max_texture_image_units);
    log_debug("  Max texture size: %d", caps->max_texture_size);
    log_debug("  NPOT textures: %s", caps->has_texture_npot ? "Yes" : "No");
    log_debug("  Float textures: %s", caps->has_float_textures ? "Yes" : "No");
    log_debug("  Standard derivatives: %s", caps->has_standard_derivatives ? "Yes" : "No");
}

/* Get ES 2.0 vertex shader template */
const char *gles20_get_vertex_shader_template(void) {
    static const char *template =
        "#version 100\n"
        "attribute vec2 position;\n"
        "attribute vec2 texcoord;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "    v_texcoord = texcoord;\n"
        "}\n";
    return template;
}

/* Get ES 2.0 fragment shader template */
const char *gles20_get_fragment_shader_template(void) {
    static const char *template =
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D texture0;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(texture0, v_texcoord);\n"
        "}\n";
    return template;
}

/* ES 2.0 optimization hints */
void gles20_apply_optimizations(struct output_state *output) {
    if (!output) {
        return;
    }
    
    log_debug("Applying OpenGL ES 2.0 optimizations...");
    
    /* Use lower precision where possible */
    /* This is done at shader compile time with 'mediump' and 'lowp' */
    
    /* Minimize state changes */
    /* Handled by GL state cache in render.c */
    
    /* Batch draw calls */
    /* Single fullscreen quad for wallpaper rendering */
    
    log_debug("OpenGL ES 2.0 optimizations applied");
}