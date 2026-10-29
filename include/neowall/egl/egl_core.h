#ifndef EGL_CORE_H
#define EGL_CORE_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* EGL core: one desktop OpenGL 3.3 core context (Shadertoy needs GLSL 330),
 * shared by every output's surface. */

/* Forward declarations */
struct neowall_state;
struct output_state;

/**
 * Initialize EGL with desktop OpenGL 3.3 context
 * 
 * @param state NeoWall global state
 * @return true on success, false on failure
 */
bool egl_core_init(struct neowall_state *state);

/**
 * Cleanup EGL resources
 * 
 * @param state NeoWall global state
 */
void egl_core_cleanup(struct neowall_state *state);

/**
 * Make EGL context current for an output
 * 
 * @param state NeoWall global state
 * @param output Output state (can be NULL to unbind)
 * @return true on success, false on failure
 */
bool egl_core_make_current(struct neowall_state *state, 
                           struct output_state *output);

/* eglMakeCurrent on a window surface, then point the default framebuffer at
 * GL_BACK: a context first made current without a surface keeps GL_NONE
 * there on NVIDIA, and draws go nowhere. Use this for every window bind. */
bool egl_make_surface_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx);

#endif /* EGL_CORE_H */