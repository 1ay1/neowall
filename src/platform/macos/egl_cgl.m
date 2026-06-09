/*
 * EGL over CGL/NSOpenGLContext — macOS backing for the EGL subset declared in
 * include/neowall/platform/egl_compat.h.
 *
 * Model:
 *   eglGetDisplay/eglInitialize  -> a process-wide singleton "display".
 *   eglChooseConfig              -> returns the singleton config token.
 *   eglCreateContext             -> creates the PRIMARY NSOpenGLContext
 *                                   (3.2 core profile; macOS promotes to the
 *                                   highest core version, 4.1).
 *   eglCreateWindowSurface(win)  -> win is an NSView*. Creates a dedicated
 *                                   NSOpenGLContext SHARING objects with the
 *                                   primary and attaches it to the view.
 *                                   GL objects (textures/programs/FBOs) are
 *                                   shared, matching EGL's one-context model
 *                                   closely enough for this engine, which
 *                                   re-binds per-output state every frame.
 *   eglMakeCurrent(d, s, s, c)   -> [surface->ctx makeCurrentContext].
 *   eglSwapBuffers               -> [surface->ctx flushBuffer].
 *
 * Threading: the engine runs all GL on the event-loop thread; AppKit view
 * attachment happens on the main thread via dispatch_sync. NSOpenGLContext
 * itself may be made current from any single thread at a time, which matches
 * the engine's usage.
 */
#ifdef __APPLE__

#define GL_SILENCE_DEPRECATION 1

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

#include "neowall/platform/egl_compat.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

extern void log_debug(const char *fmt, ...);
extern void log_info(const char *fmt, ...);
extern void log_error(const char *fmt, ...);

/* ---------------------------------------------------------------------- */

typedef struct cgl_surface {
    NSOpenGLContext *ctx;   /* per-surface context, shares objects w/ primary */
    NSView *view;           /* attached view (retained by ctx) */
    int width;
    int height;
} cgl_surface_t;

static struct {
    bool initialized;
    NSOpenGLPixelFormat *pixel_format;
    NSOpenGLContext *primary;        /* created by eglCreateContext */
    cgl_surface_t *current;          /* surface bound by eglMakeCurrent */
    EGLint last_error;
} g_cgl = {0};

#define CGL_DISPLAY_MAGIC ((EGLDisplay)0xCA11AB1E)
#define CGL_CONFIG_MAGIC ((EGLConfig)0xC0FF8E)
#define CGL_CONTEXT_MAGIC ((EGLContext)0xC0DE)

static void set_error(EGLint err) { g_cgl.last_error = err; }

/* ---------------------------------------------------------------------- */

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    return CGL_DISPLAY_MAGIC;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    if (dpy != CGL_DISPLAY_MAGIC) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (major) *major = 1;
    if (minor) *minor = 5;
    g_cgl.initialized = true;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    if (dpy != CGL_DISPLAY_MAGIC) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    g_cgl.primary = nil;
    g_cgl.pixel_format = nil;
    g_cgl.current = NULL;
    g_cgl.initialized = false;
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api) {
    return api == EGL_OPENGL_API ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config) {
    (void)attrib_list;
    if (dpy != CGL_DISPLAY_MAGIC || !g_cgl.initialized) {
        set_error(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    if (!g_cgl.pixel_format) {
        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
            NSOpenGLPFAColorSize, 24,
            NSOpenGLPFAAlphaSize, 8,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            0
        };
        g_cgl.pixel_format =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!g_cgl.pixel_format) {
            /* Retry without the hard accel requirement (VMs, CI). */
            NSOpenGLPixelFormatAttribute soft[] = {
                NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                NSOpenGLPFAColorSize, 24,
                NSOpenGLPFAAlphaSize, 8,
                NSOpenGLPFADoubleBuffer,
                0
            };
            g_cgl.pixel_format =
                [[NSOpenGLPixelFormat alloc] initWithAttributes:soft];
        }
        if (!g_cgl.pixel_format) {
            set_error(EGL_BAD_CONFIG);
            return EGL_FALSE;
        }
    }

    if (configs && config_size >= 1) configs[0] = CGL_CONFIG_MAGIC;
    if (num_config) *num_config = 1;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list) {
    (void)config;
    (void)share_context;
    (void)attrib_list;
    if (dpy != CGL_DISPLAY_MAGIC || !g_cgl.pixel_format) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_NO_CONTEXT;
    }

    g_cgl.primary = [[NSOpenGLContext alloc] initWithFormat:g_cgl.pixel_format
                                               shareContext:nil];
    if (!g_cgl.primary) {
        set_error(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }
    log_info("CGL: created OpenGL core-profile context (3.2+, highest available)");
    set_error(EGL_SUCCESS);
    return CGL_CONTEXT_MAGIC;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    (void)dpy;
    if (ctx != CGL_CONTEXT_MAGIC) {
        set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    g_cgl.primary = nil;
    return EGL_TRUE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list) {
    (void)config;
    (void)attrib_list;
    if (dpy != CGL_DISPLAY_MAGIC || !g_cgl.pixel_format) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    NSView *view = (__bridge NSView *)win;
    if (!view) {
        set_error(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }

    cgl_surface_t *surf = calloc(1, sizeof(*surf));
    if (!surf) {
        set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    NSOpenGLContext *ctx =
        [[NSOpenGLContext alloc] initWithFormat:g_cgl.pixel_format
                                   shareContext:g_cgl.primary];
    if (!ctx) {
        free(surf);
        set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    surf->ctx = ctx;
    surf->view = view;

    void (^attach)(void) = ^{
        [view setWantsBestResolutionOpenGLSurface:YES];
        [ctx setView:view];
        NSRect backing = [view convertRectToBacking:[view bounds]];
        surf->width = (int)backing.size.width;
        surf->height = (int)backing.size.height;
    };
    if ([NSThread isMainThread]) {
        attach();
    } else {
        dispatch_sync(dispatch_get_main_queue(), attach);
    }

    /* Opaque surface: wallpaper never needs alpha against the desktop. */
    GLint opaque = 1;
    [ctx setValues:&opaque forParameter:NSOpenGLContextParameterSurfaceOpacity];

    set_error(EGL_SUCCESS);
    return (EGLSurface)surf;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    (void)dpy;
    cgl_surface_t *surf = (cgl_surface_t *)surface;
    if (!surf) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (g_cgl.current == surf) {
        [NSOpenGLContext clearCurrentContext];
        g_cgl.current = NULL;
    }
    void (^detach)(void) = ^{ [surf->ctx clearDrawable]; };
    if ([NSThread isMainThread]) detach();
    else dispatch_sync(dispatch_get_main_queue(), detach);
    surf->ctx = nil;
    surf->view = nil;
    free(surf);
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
    (void)dpy;
    (void)read;
    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE) {
        [NSOpenGLContext clearCurrentContext];
        g_cgl.current = NULL;
        return EGL_TRUE;
    }
    cgl_surface_t *surf = (cgl_surface_t *)draw;
    if (!surf || !surf->ctx) {
        /* Context-only make-current (no surface yet): use the primary so GL
         * object creation (shader compiles, texture uploads) can proceed. */
        if (ctx == CGL_CONTEXT_MAGIC && g_cgl.primary) {
            [g_cgl.primary makeCurrentContext];
            g_cgl.current = NULL;
            return EGL_TRUE;
        }
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    [surf->ctx makeCurrentContext];
    g_cgl.current = surf;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    (void)dpy;
    cgl_surface_t *surf = (cgl_surface_t *)surface;
    if (!surf || !surf->ctx) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    [surf->ctx flushBuffer];
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    (void)dpy;
    cgl_surface_t *surf = g_cgl.current;
    NSOpenGLContext *ctx = surf ? surf->ctx : g_cgl.primary;
    if (!ctx) return EGL_FALSE;
    GLint swap = interval;
    [ctx setValues:&swap forParameter:NSOpenGLContextParameterSwapInterval];
    return EGL_TRUE;
}

EGLint eglGetError(void) {
    EGLint e = g_cgl.last_error;
    g_cgl.last_error = EGL_SUCCESS;
    return e;
}

EGLContext eglGetCurrentContext(void) {
    return [NSOpenGLContext currentContext] ? CGL_CONTEXT_MAGIC : EGL_NO_CONTEXT;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    (void)readdraw;
    return (EGLSurface)g_cgl.current;
}

EGLDisplay eglGetCurrentDisplay(void) {
    return g_cgl.initialized ? CGL_DISPLAY_MAGIC : EGL_NO_DISPLAY;
}

void (*eglGetProcAddress(const char *procname))(void) {
    /* OpenGL.framework exports core 3.2+ entry points directly; dlsym covers
     * anything the engine probes for. eglGetPlatformDisplayEXT correctly
     * resolves to NULL, steering egl_core.c onto the eglGetDisplay path. */
    static void *gl_handle = NULL;
    if (!gl_handle) {
        gl_handle = dlopen(
            "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
    }
    if (!gl_handle) return NULL;
    union { void *p; void (*f)(void); } cast;
    cast.p = dlsym(gl_handle, procname);
    return cast.f;
}

/* Called by the AppKit backend when a view resizes (display reconfig). */
void egl_cgl_surface_resized(EGLSurface surface) {
    cgl_surface_t *surf = (cgl_surface_t *)surface;
    if (!surf || !surf->ctx) return;
    void (^update)(void) = ^{ [surf->ctx update]; };
    if ([NSThread isMainThread]) update();
    else dispatch_sync(dispatch_get_main_queue(), update);
}

#endif /* __APPLE__ */
