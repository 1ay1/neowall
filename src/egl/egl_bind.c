/* See egl_make_surface_current in neowall/egl/egl_core.h. Kept apart from
 * egl_core.c so code that only binds surfaces (and its tests) doesn't pull
 * in EGL setup. */
#include "neowall/egl/egl_core.h"

#include <GL/gl.h>
#include <GL/glext.h>

/* The context is first made current with no surface (egl_core_init), so its
 * default framebuffer starts with GL_NONE as draw and read buffer. NVIDIA
 * keeps that when a window surface is bound later, and every frame then goes
 * nowhere: a black wallpaper at full fps. Mesa resets it on bind. */
bool egl_make_surface_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx) {
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) return false;
    if (surf != EGL_NO_SURFACE) {
        GLint fb = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    }
    return true;
}
