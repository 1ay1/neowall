#ifndef NEOWALL_PLATFORM_EGL_COMPAT_H
#define NEOWALL_PLATFORM_EGL_COMPAT_H

/*
 * EGL over CGL — macOS implementation of the subset of EGL 1.5 that the
 * shared engine code uses (see src/egl/egl_core.c, src/render/render.c,
 * src/eventloop.c, src/output/output.c).
 *
 * Design: one process-wide "display". eglCreateContext creates a CGL pixel
 * format + NSOpenGLContext (3.2+ core, which on Apple silicon/AMD yields a
 * 4.1 context — superset of the 3.3 the shaders need). eglCreateWindowSurface
 * binds that context to the NSView passed as the native window; each surface
 * gets its OWN NSOpenGLContext sharing objects with the primary so
 * eglMakeCurrent(dpy, surf, surf, ctx) maps to [surface->nsctx makeCurrentContext].
 *
 * Implemented in src/platform/macos/egl_cgl.m (ObjC). The header is plain C
 * so every existing include site keeps compiling untouched.
 */

#ifdef __APPLE__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLConfig;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;
typedef void *EGLClientBuffer;

#define EGL_FALSE 0
#define EGL_TRUE 1

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

/* Errors */
#define EGL_SUCCESS 0x3000
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_ACCESS 0x3002
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_CURRENT_SURFACE 0x3007
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_PIXMAP 0x300A
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D
#define EGL_CONTEXT_LOST 0x300E

/* Attributes */
#define EGL_NONE 0x3038
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_BIT 0x0008
#define EGL_OPENGL_API 0x30A2
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A

/* Platform enums referenced by shared code (unused on macOS) */
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#define EGL_PLATFORM_X11_KHR 0x31D5

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
EGLBoolean eglBindAPI(EGLenum api);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLint eglGetError(void);
EGLContext eglGetCurrentContext(void);
EGLSurface eglGetCurrentSurface(EGLint readdraw);
EGLDisplay eglGetCurrentDisplay(void);
void (*eglGetProcAddress(const char *procname))(void);

/* PFN typedef referenced by egl_core.c's platform-display probe. The probe
 * checks eglGetProcAddress("eglGetPlatformDisplayEXT") which returns NULL on
 * macOS, falling back to eglGetDisplay — exactly what we want. */
typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum platform,
                                                      void *native_display,
                                                      const EGLint *attrib_list);

/* macOS-only surface helper: called by the AppKit backend when an NSView's
 * backing size changes so the GL drawable tracks it. */
void egl_cgl_surface_resized(EGLSurface surface);

#ifdef __cplusplus
}
#endif

#else /* !__APPLE__ — real EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#endif /* NEOWALL_PLATFORM_EGL_COMPAT_H */
