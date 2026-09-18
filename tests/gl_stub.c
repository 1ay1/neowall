/*
 * gl_stub.c — enough GL symbols to link the gfx tests without a driver.
 *
 * The gfx layer's logic worth testing (which half of a ping-pong pair is
 * readable, format table lookups, safety on zeroed objects) does not need a
 * real context — but it does need these symbols to exist at link time. So they
 * do nothing, hand back plausible ids, and report success.
 *
 * This deliberately does NOT emulate GL. Anything whose correctness depends on
 * what the driver actually does belongs in a test with a real context, not
 * here. These stubs only exist so the pure logic is reachable in CI, which has
 * no display server.
 */

#include "neowall/shader/platform_compat.h"

static GLuint g_next_id = 1;

/* --- objects --- */
void glGenTextures(GLsizei n, GLuint *out) {
    for (GLsizei i = 0; i < n; i++) {
        out[i] = g_next_id++;
    }
}
void glDeleteTextures(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
void glGenFramebuffers(GLsizei n, GLuint *out) {
    for (GLsizei i = 0; i < n; i++) {
        out[i] = g_next_id++;
    }
}
void glDeleteFramebuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }

GLuint glCreateShader(GLenum type) { (void)type; return g_next_id++; }
GLuint glCreateProgram(void) { return g_next_id++; }
void   glDeleteShader(GLuint s) { (void)s; }
void   glDeleteProgram(GLuint p) { (void)p; }

/* --- state --- */
void glBindTexture(GLenum t, GLuint id) { (void)t; (void)id; }
void glBindFramebuffer(GLenum t, GLuint id) { (void)t; (void)id; }
void glActiveTexture(GLenum unit) { (void)unit; }
void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g,
                  GLenum h, const void *p) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)p;
}
void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
void glGenerateMipmap(GLenum t) { (void)t; }
void glFramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint d, GLint e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void glViewport(GLint a, GLint b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
void glClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    (void)a; (void)b; (void)c; (void)d;
}
void glClear(GLbitfield m) { (void)m; }

/* --- shaders --- */
void glShaderSource(GLuint s, GLsizei n, const GLchar *const *src, const GLint *len) {
    (void)s; (void)n; (void)src; (void)len;
}
void glCompileShader(GLuint s) { (void)s; }
void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
void glLinkProgram(GLuint p) { (void)p; }
void glBindAttribLocation(GLuint p, GLuint i, const GLchar *n) { (void)p; (void)i; (void)n; }

/* Report success so the build path is exercised; failure paths are driven by
 * argument validation, which happens before any of this. */
void glGetShaderiv(GLuint s, GLenum pname, GLint *out) {
    (void)s;
    *out = (pname == GL_COMPILE_STATUS) ? GL_TRUE : 0;
}
void glGetProgramiv(GLuint p, GLenum pname, GLint *out) {
    (void)p;
    *out = (pname == GL_LINK_STATUS) ? GL_TRUE : 0;
}
void glGetShaderInfoLog(GLuint s, GLsizei cap, GLsizei *len, GLchar *log) {
    (void)s; (void)cap;
    if (len) *len = 0;
    if (log && cap > 0) log[0] = '\0';
}
void glGetProgramInfoLog(GLuint p, GLsizei cap, GLsizei *len, GLchar *log) {
    (void)p; (void)cap;
    if (len) *len = 0;
    if (log && cap > 0) log[0] = '\0';
}
GLint glGetUniformLocation(GLuint p, const GLchar *name) { (void)p; (void)name; return 0; }

/* --- queries --- */
GLenum glCheckFramebufferStatus(GLenum t) { (void)t; return GL_FRAMEBUFFER_COMPLETE; }
GLenum glGetError(void) { return GL_NO_ERROR; }
