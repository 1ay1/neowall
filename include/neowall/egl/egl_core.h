#ifndef EGL_CORE_H
#define EGL_CORE_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* EGL core: one desktop OpenGL 3.3 core context (Shadertoy needs GLSL 330),
 * shared by every output's surface. */

struct neowall_state;

/* Make the display and the context (stored in state->egl_*), from the
 * backend's native display and EGL platform. No surfaces: each output makes
 * its own (output_create_egl_surface). */
bool egl_core_init(struct neowall_state *state, void *native_display, EGLenum platform);

/* Release the context and display. Destroy every output's surface first. */
void egl_core_cleanup(struct neowall_state *state);

/* eglMakeCurrent on a window surface, then point the default framebuffer at
 * GL_BACK: a context first made current without a surface keeps GL_NONE
 * there on NVIDIA, and draws go nowhere. Use this for every window bind. */
bool egl_make_surface_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx);

/* eglMakeCurrent that records what's current (any surface, or none). */
bool egl_make_current_raw(EGLDisplay dpy, EGLSurface surf, EGLContext ctx);

/* Whether this exact triple is current, from our own record: no EGL call.
 * Loop thread only. */
bool egl_is_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx);

/* Call before eglDestroySurface/Context: a new surface can reuse the old
 * handle, and must not look current. */
void egl_forget_current(void);

#endif /* EGL_CORE_H */