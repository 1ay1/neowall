/* See egl_make_surface_current in neowall/egl/egl_core.h. Kept apart from
 * egl_core.c so code that only binds surfaces (and its tests) doesn't pull
 * in EGL setup. */
#include "neowall/egl/egl_core.h"

#include <GL/gl.h>
#include <GL/glext.h>

/* What we last made current. GL runs on the loop thread only, and every
 * eglMakeCurrent and eglDestroySurface in neowall goes through here, so this
 * is the truth without asking EGL: glvnd's eglGetCurrent* each make a
 * getpid() syscall (fork check), five times per frame. */
static struct {
    EGLDisplay dpy;
    EGLSurface surf;
    EGLContext ctx;
} g_current = {EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_CONTEXT};

bool egl_is_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx) {
    return ctx != EGL_NO_CONTEXT && g_current.dpy == dpy && g_current.surf == surf &&
           g_current.ctx == ctx;
}

void egl_forget_current(void) {
    g_current.dpy = EGL_NO_DISPLAY;
    g_current.surf = EGL_NO_SURFACE;
    g_current.ctx = EGL_NO_CONTEXT;
}

bool egl_make_current_raw(EGLDisplay dpy, EGLSurface surf, EGLContext ctx) {
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        egl_forget_current();   /* EGL may have unbound the old one */
        return false;
    }
    g_current.dpy = dpy;
    g_current.surf = surf;
    g_current.ctx = ctx;
    return true;
}

/* The context is first made current with no surface (egl_core_init), so its
 * default framebuffer starts with GL_NONE as draw and read buffer. NVIDIA
 * keeps that when a window surface is bound later, and every frame then goes
 * nowhere: a black wallpaper at full fps. Mesa resets it on bind. */
bool egl_make_surface_current(EGLDisplay dpy, EGLSurface surf, EGLContext ctx) {
    if (!egl_make_current_raw(dpy, surf, ctx)) return false;
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
