/* Small GL program helpers for transitions and overlays, and finding a
 * shader file by name. Wallpaper shaders go through wallshader.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "neowall/log.h"
#include "neowall/shader/platform_compat.h"
#include "neowall/shader/shader.h"

/* utils.c; declared here so this file doesn't pull in all of neowall.h. */
bool neowall_find_data(const char *rel, char *out, size_t size);

#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 4096
#endif

static GLuint compile_shader(GLenum type, const char *source) {
    const char *type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        log_error("Failed to create %s shader", type_str);
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[2048] = "";
        glGetShaderInfoLog(shader, sizeof(info), NULL, info);
        log_error("%s shader compilation failed: %s", type_str, info[0] ? info : "(no log)");
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool shader_create_program_from_sources(const char *vertex_src,
                                         const char *fragment_src,
                                         GLuint *program) {
    if (!program) return false;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vs) return false;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    /* shaders can go once linked */
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[2048] = "";
        glGetProgramInfoLog(prog, sizeof(info), NULL, info);
        log_error("Program linking failed: %s", info[0] ? info : "(no log)");
        glDeleteProgram(prog);
        return false;
    }
    *program = prog;
    return true;
}

void shader_destroy_program(GLuint program) {
    if (program != 0) glDeleteProgram(program);
}

/* A path is used as given (~ is expanded later, by the loader); a bare name
 * is looked up in the user's shader dirs first ($XDG_CONFIG_HOME,
 * ~/.config), then the data dirs. */
bool shader_resolve_path(const char *shader_name, char *resolved_path, size_t resolved_size) {
    if (!shader_name || !resolved_path || resolved_size == 0) {
        return false;
    }
    if (shader_name[0] == '/' || shader_name[0] == '~' || strchr(shader_name, '/') != NULL) {
        snprintf(resolved_path, resolved_size, "%s", shader_name);
        return true;
    }

    const char *home = getenv("HOME");
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    char cand[MAX_PATH_LENGTH];
    int n = -1;
    if (xdg_config_home && xdg_config_home[0] == '/') {
        n = snprintf(cand, sizeof(cand), "%s/neowall/shaders/%s", xdg_config_home, shader_name);
        if (n > 0 && (size_t)n < sizeof(cand) && access(cand, R_OK) == 0) goto found;
    }
    if (home && home[0]) {
        n = snprintf(cand, sizeof(cand), "%s/.config/neowall/shaders/%s", home, shader_name);
        if (n > 0 && (size_t)n < sizeof(cand) && access(cand, R_OK) == 0) goto found;
    }
    char rel[MAX_PATH_LENGTH];
    n = snprintf(rel, sizeof(rel), "shaders/%s", shader_name);
    if (n > 0 && (size_t)n < sizeof(rel) && neowall_find_data(rel, cand, sizeof(cand))) goto found;

    log_error("Shader not found: %s", shader_name);
    return false;

found:
    if (strlen(cand) >= resolved_size) {
        log_error("Resolved shader path too long: %s", cand);
        return false;
    }
    snprintf(resolved_path, resolved_size, "%s", cand);
    log_debug("Resolved shader '%s' to: %s", shader_name, resolved_path);
    return true;
}
