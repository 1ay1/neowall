/* NeoWall Shader Library - Unified API Implementation
 * Provides clean wrapper API around shader_core functions
 */

#include "neowall_shader_api.h"
#include "shader.h"
#include "shader_log.h"
#include "platform_compat.h"
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
#ifdef PLATFORM_WINDOWS
    char temp_path[MAX_PATH];
    char *temp_dir = getenv("TEMP");
    if (!temp_dir) temp_dir = getenv("TMP");
    if (!temp_dir) temp_dir = ".";
    snprintf(temp_path, sizeof(temp_path), "%s\\neowall_tray_shader_temp.glsl", temp_dir);
#else
    const char *temp_path = "/tmp/neowall_tray_shader_temp.glsl";
#endif
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
    float time)
{
    if (!program) {
        return;
    }

    glUseProgram(program);

    /* Set internal NeoWall uniforms */
    GLint loc_time = glGetUniformLocation(program, "_neowall_time");
    if (loc_time >= 0) {
        glUniform1f(loc_time, time);
    }

    GLint loc_resolution = glGetUniformLocation(program, "_neowall_resolution");
    if (loc_resolution >= 0) {
        glUniform2f(loc_resolution, (float)width, (float)height);
    }

    /* Set mouse uniform (default to 0,0,0,0) */
    GLint loc_mouse = glGetUniformLocation(program, "_neowall_mouse");
    if (loc_mouse >= 0) {
        glUniform4f(loc_mouse, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    /* Set frame counter uniform (increments with time) */
    GLint loc_frame = glGetUniformLocation(program, "_neowall_frame");
    if (loc_frame >= 0) {
        glUniform1i(loc_frame, (int)(time * 60.0f)); /* Approximate frame count at 60fps */
    }

    /* Set iResolution uniform (vec3) */
    GLint loc_iresolution = glGetUniformLocation(program, "iResolution");
    if (loc_iresolution >= 0) {
        float aspect = (width > 0 && height > 0) ? (float)width / (float)height : 1.0f;
        glUniform3f(loc_iresolution, (float)width, (float)height, aspect);
    }

    /* Set Shadertoy uniforms for compatibility (matching gleditor's on_gl_render) */
    GLint loc_itime = glGetUniformLocation(program, "iTime");
    if (loc_itime >= 0) {
        glUniform1f(loc_itime, time);
    }

    GLint loc_itimedelta = glGetUniformLocation(program, "iTimeDelta");
    if (loc_itimedelta >= 0) {
        glUniform1f(loc_itimedelta, 1.0f / 60.0f);
    }

    GLint loc_iframe = glGetUniformLocation(program, "iFrame");
    if (loc_iframe >= 0) {
        glUniform1i(loc_iframe, (int)(time * 60.0f));
    }

    GLint loc_imouse = glGetUniformLocation(program, "iMouse");
    if (loc_imouse >= 0) {
        glUniform4f(loc_imouse, 0.0f, 0.0f, 0.0f, 0.0f);
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
