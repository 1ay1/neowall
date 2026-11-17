/* Small GL program helpers for transitions and overlays, and finding a
 * shader file by name. Wallpaper shaders go through wallshader.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "neowall/core/log.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include "neowall/core/log.h"
#include "neowall/shader/shader.h"
#include "neowall/core/constants.h"

/* utils.c; declared here so this file doesn't pull in all of neowall.h. */
bool neowall_find_data(const char *rel, char *out, size_t size);

#ifndef NW_PATH_MAX
#endif

static GLuint compile_shader(GLenum type, const char *source) {
    const char *type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    GLuint shader        = glCreateShader(type);
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

bool shader_create_program_from_sources(const char *vertex_src, const char *fragment_src,
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
/* One name in the shader dirs, exactly as given: user dir, then data dirs. */
static bool resolve_exact(const char *shader_name, char *cand, size_t cap) {
    const char *home            = getenv("HOME");
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    int n                       = -1;
    if (xdg_config_home && xdg_config_home[0] == '/') {
        n = snprintf(cand, cap, "%s/neowall/shaders/%s", xdg_config_home, shader_name);
        if (n > 0 && (size_t)n < cap && access(cand, R_OK) == 0) return true;
    }
    if (home && home[0]) {
        n = snprintf(cand, cap, "%s/.config/neowall/shaders/%s", home, shader_name);
        if (n > 0 && (size_t)n < cap && access(cand, R_OK) == 0) return true;
    }
    char rel[NW_PATH_MAX];
    n = snprintf(rel, sizeof(rel), "shaders/%s", shader_name);
    return n > 0 && (size_t)n < sizeof(rel) && neowall_find_data(rel, cand, cap);
}

bool shader_resolve_path(const char *shader_name, char *resolved_path, size_t resolved_size) {
    if (!shader_name || !resolved_path || resolved_size == 0) {
        return false;
    }
    if (shader_name[0] == '/' || shader_name[0] == '~' || strchr(shader_name, '/') != NULL) {
        snprintf(resolved_path, resolved_size, "%s", shader_name);
        return true;
    }

    char cand[NW_PATH_MAX];
    if (resolve_exact(shader_name, cand, sizeof(cand))) goto found;
    /* `shader garden` / `neowall render garden`: try the usual extensions,
     * the manifest first (it names the .glsl and wires the passes) */
    if (!strchr(shader_name, '.')) {
        static const char *const exts[] = {".neowall", ".glsl"};
        for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
            char with[NW_PATH_MAX];
            int n = snprintf(with, sizeof(with), "%s%s", shader_name, exts[i]);
            if (n > 0 && (size_t)n < sizeof(with) && resolve_exact(with, cand, sizeof(cand)))
                goto found;
        }
    }

    log_error("Shader not found: %s (looked in ~/.config/neowall/shaders and the data dirs)",
              shader_name);
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
