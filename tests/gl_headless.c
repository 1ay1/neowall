/* Headless OpenGL 3.3 core context for tests: EGL on Mesa's surfaceless
 * platform, so no display server is needed. llvmpipe (software) makes the
 * results the same on every machine. */
#include "gl_headless.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;

const char *gl_headless_init(void) {
    /* Force Mesa + software rendering so the test is the same everywhere
     * (an NVIDIA box would otherwise pick its own driver). */
    setenv("__EGL_VENDOR_LIBRARY_FILENAMES", "/usr/share/glvnd/egl_vendor.d/50_mesa.json", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    setenv("EGL_PLATFORM", "surfaceless", 0);

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_dpy) return "eglGetPlatformDisplayEXT missing";
    g_dpy = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (g_dpy == EGL_NO_DISPLAY) return "no surfaceless EGL display";
    if (!eglInitialize(g_dpy, NULL, NULL)) return "eglInitialize failed";
    if (!eglBindAPI(EGL_OPENGL_API)) return "eglBindAPI(OpenGL) failed";

    const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                               EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        /* surfaceless may expose no configs; contexts work without one */
        cfg = NULL;
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                               EGL_CONTEXT_OPENGL_PROFILE_MASK,
                               EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) return "no GL 3.3 core context";
    if (!eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
        return "eglMakeCurrent(surfaceless) failed";
    }
    return NULL;
}

void gl_headless_fini(void) {
    if (g_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    g_ctx = EGL_NO_CONTEXT;
}
