/* NeoWall Shader Library - Unified API Implementation
 * Provides clean wrapper API around shader_core functions
 */

#include "platform_compat.h"
#include "neowall_shader_api.h"
#include "shader.h"
#include <stdlib.h>
#include <string.h>

/**
 * Compile a shader from source code
 */
neowall_shader_result_t neowall_shader_compile(
    const char *shader_source,
    const neowall_shader_options_t *options)
{
    neowall_shader_result_t result = {
        .program = 0,
        .success = false,
        .error_message = NULL,
        .error_line = -1
    };

    if (!shader_source) {
        result.error_message = strdup("Shader source is NULL");
        return result;
    }

    /* Use default options if not provided */
    neowall_shader_options_t default_opts = NEOWALL_SHADER_OPTIONS_DEFAULT;
    if (!options) {
        options = &default_opts;
    }

    /* Save shader to temporary file for shader_create_live_program */
    const char *temp_path = "/tmp/neowall_tray_shader_temp.glsl";
    FILE *f = fopen(temp_path, "w");
    if (!f) {
        result.error_message = strdup("Failed to create temporary shader file");
        return result;
    }

    fprintf(f, "%s", shader_source);
    fclose(f);

    /* Use shader_create_live_program which handles everything */
    GLuint program = 0;
    bool success = shader_create_live_program(temp_path, &program, options->channel_count);

    if (success) {
        result.program = program;
        result.success = true;
    } else {
        /* Get detailed error log from shader_core */
        const char *error_log = shader_get_last_error_log();
        if (error_log && strlen(error_log) > 0) {
            result.error_message = strdup(error_log);
        } else {
            result.error_message = strdup("Shader compilation failed with no error details available");
        }
        result.success = false;
    }

    /* Clean up temp file */
    unlink(temp_path);

    return result;
}

/**
 * Compile a shader from a file path
 */
neowall_shader_result_t neowall_shader_compile_file(
    const char *shader_path,
    const neowall_shader_options_t *options)
{
    neowall_shader_result_t result = {
        .program = 0,
        .success = false,
        .error_message = NULL,
        .error_line = -1
    };

    if (!shader_path) {
        result.error_message = strdup("Shader path is NULL");
        return result;
    }

    /* Use default options if not provided */
    neowall_shader_options_t default_opts = NEOWALL_SHADER_OPTIONS_DEFAULT;
    if (!options) {
        options = &default_opts;
    }

    /* Use shader_create_live_program directly */
    GLuint program = 0;
    bool success = shader_create_live_program(shader_path, &program, options->channel_count);

    if (success) {
        result.program = program;
        result.success = true;
    } else {
        /* Get detailed error log from shader_core */
        const char *error_log = shader_get_last_error_log();
        if (error_log && strlen(error_log) > 0) {
            result.error_message = strdup(error_log);
        } else {
            result.error_message = strdup("Shader compilation failed with no error details available");
        }
        result.success = false;
    }

    return result;
}

/**
 * Destroy a compiled shader program
 */
void neowall_shader_destroy(GLuint program) {
    shader_destroy_program(program);
}

/**
 * Free shader result error message
 */
void neowall_shader_free_result(neowall_shader_result_t *result) {
    if (result && result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}

/**
 * Set shader uniforms for rendering
 */
void neowall_shader_set_uniforms(
    GLuint program,
    int width,
    int height,
    float time,
    float mouse_x,
    float mouse_y)
{
    if (!program) {
        return;
    }

    glUseProgram(program);

    /* Calculate frame count (approximate at 60fps) */
    int frame_count = (int)(time * 60.0f);
    
    /* Set mouse coordinates - iMouse expects PIXEL coordinates, not normalized
     * Default to center of screen when mouse tracking not available */
    float mx = (mouse_x < 0) ? ((float)width * 0.5f) : mouse_x;
    float my = (mouse_y < 0) ? ((float)height * 0.5f) : mouse_y;
    
    static bool logged_once = false;
    static uint64_t last_log_time = 0;
    uint64_t now = get_time_ms();
    
    if (!logged_once) {
        log_info("Setting uniforms: resolution=%dx%d, mouse=(%.1f, %.1f)", width, height, mx, my);
        logged_once = true;
    }
    
    /* Log time values every 2 seconds for debugging */
    if (now - last_log_time > 2000) {
        log_debug("Shader time: %.2f, iFrame: %d", time, frame_count);
        last_log_time = now;
    }

    /* Set NeoWall internal uniforms (exactly like gleditor) */
    GLint loc;
    
    loc = glGetUniformLocation(program, "_neowall_time");
    if (loc >= 0) {
        glUniform1f(loc, time);
    }

    loc = glGetUniformLocation(program, "_neowall_resolution");
    if (loc >= 0) {
        glUniform2f(loc, (float)width, (float)height);
    }

    loc = glGetUniformLocation(program, "_neowall_mouse");
    if (loc >= 0) {
        /* iMouse format: xy = current position, zw = click position (0,0 = no click) */
        glUniform4f(loc, mx, my, 0.0f, 0.0f);
    }

    loc = glGetUniformLocation(program, "_neowall_frame");
    if (loc >= 0) {
        glUniform1i(loc, frame_count);
    }

    /* Set Shadertoy uniforms for compatibility (exactly like gleditor) */
    loc = glGetUniformLocation(program, "iResolution");
    if (loc >= 0) {
        float aspect = (width > 0 && height > 0) ? (float)width / (float)height : 1.0f;
        glUniform3f(loc, (float)width, (float)height, aspect);
    }

    loc = glGetUniformLocation(program, "iTime");
    if (loc >= 0) {
        glUniform1f(loc, time);
    }

    loc = glGetUniformLocation(program, "iTimeDelta");
    if (loc >= 0) {
        glUniform1f(loc, 1.0f / 60.0f);
    }

    loc = glGetUniformLocation(program, "iFrame");
    if (loc >= 0) {
        glUniform1i(loc, frame_count);
    }

    loc = glGetUniformLocation(program, "iMouse");
    if (loc >= 0) {
        glUniform4f(loc, mx, my, 0.0f, 0.0f);
    }
}

/**
 * Get the vertex shader source for fullscreen quad
 */
const char *neowall_shader_get_vertex_source(bool use_es3) {
    if (use_es3) {
        return "#version 300 es\n"
               "in vec2 position;\n"
               "void main() {\n"
               "    gl_Position = vec4(position, 0.0, 1.0);\n"
               "}\n";
    } else {
        return "#version 100\n"
               "attribute vec2 position;\n"
               "void main() {\n"
               "    gl_Position = vec4(position, 0.0, 1.0);\n"
               "}\n";
    }
}
