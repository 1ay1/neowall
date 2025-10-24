#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "../../include/staticwall.h"
#include "../../include/egl/capability.h"

/**
 * EGL 1.4 Implementation
 * 
 * Key features:
 * - Multi-context support (multiple contexts sharing resources)
 * - Improved error handling
 * - Better Wayland integration
 * - Separate draw and read surfaces
 * - Context priority hints
 * 
 * EGL 1.4 is the baseline for modern OpenGL ES 3.0+ support.
 */

/* Function pointers for EGL 1.4 features */
static PFNEGLGETCURRENTCONTEXTPROC eglGetCurrentContextProc = NULL;
static PFNEGLGETCURRENTSURFACEPROC eglGetCurrentSurfaceProc = NULL;

/* Check if EGL 1.4 is available */
bool egl_v14_available(void) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return false;
    }
    
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        return false;
    }
    
    bool available = (major > 1) || (major == 1 && minor >= 4);
    
    eglTerminate(display);
    return available;
}

/* Initialize EGL 1.4 function pointers */
bool egl_v14_init_functions(void) {
    eglGetCurrentContextProc = (PFNEGLGETCURRENTCONTEXTPROC)
        eglGetProcAddress("eglGetCurrentContext");
    eglGetCurrentSurfaceProc = (PFNEGLGETCURRENTSURFACEPROC)
        eglGetProcAddress("eglGetCurrentSurface");
    
    /* These should always be available in EGL 1.4 */
    if (!eglGetCurrentContextProc || !eglGetCurrentSurfaceProc) {
        log_debug("EGL 1.4 function pointers not available (using core functions)");
    }
    
    log_info("EGL 1.4 initialized successfully");
    return true;
}

/* Create shared context for multi-context rendering */
EGLContext egl_v14_create_shared_context(EGLDisplay display,
                                         EGLConfig config,
                                         EGLContext share_context,
                                         int gles_version) {
    if (display == EGL_NO_DISPLAY || config == NULL) {
        log_error("Invalid display or config for shared context creation");
        return EGL_NO_CONTEXT;
    }
    
    /* Context attributes based on GLES version */
    EGLint context_attribs[16];
    int idx = 0;
    
    context_attribs[idx++] = EGL_CONTEXT_CLIENT_VERSION;
    context_attribs[idx++] = gles_version;
    
    /* Add context priority hint if creating shared context */
    if (share_context != EGL_NO_CONTEXT) {
        /* Shared contexts typically have lower priority */
        context_attribs[idx++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
        context_attribs[idx++] = EGL_CONTEXT_PRIORITY_LOW_IMG;
    }
    
    context_attribs[idx++] = EGL_NONE;
    
    EGLContext context = eglCreateContext(display, config, share_context, context_attribs);
    
    if (context == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        log_error("Failed to create shared EGL context (ES %d): 0x%x", gles_version, error);
        return EGL_NO_CONTEXT;
    }
    
    if (share_context != EGL_NO_CONTEXT) {
        log_debug("Created shared EGL context for ES %d (shares with %p)", 
                  gles_version, (void*)share_context);
    } else {
        log_debug("Created primary EGL context for ES %d", gles_version);
    }
    
    return context;
}

/* Make context current with separate draw and read surfaces */
bool egl_v14_make_current_multi(EGLDisplay display,
                                EGLSurface draw_surface,
                                EGLSurface read_surface,
                                EGLContext context) {
    if (display == EGL_NO_DISPLAY) {
        log_error("Invalid display for eglMakeCurrent");
        return false;
    }
    
    /* Allow unbinding by passing EGL_NO_SURFACE and EGL_NO_CONTEXT */
    if (draw_surface == EGL_NO_SURFACE && context == EGL_NO_CONTEXT) {
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
            EGLint error = eglGetError();
            log_error("Failed to unbind EGL context: 0x%x", error);
            return false;
        }
        log_debug("Unbound EGL context");
        return true;
    }
    
    /* Validate surfaces and context */
    if ((draw_surface == EGL_NO_SURFACE && read_surface != EGL_NO_SURFACE) ||
        (draw_surface != EGL_NO_SURFACE && context == EGL_NO_CONTEXT)) {
        log_error("Invalid surface/context combination for eglMakeCurrent");
        return false;
    }
    
    /* If read_surface is not specified, use draw_surface for both */
    if (read_surface == EGL_NO_SURFACE) {
        read_surface = draw_surface;
    }
    
    if (!eglMakeCurrent(display, draw_surface, read_surface, context)) {
        EGLint error = eglGetError();
        log_error("Failed to make EGL context current: 0x%x", error);
        log_error("  Display: %p, Draw: %p, Read: %p, Context: %p",
                  (void*)display, (void*)draw_surface, 
                  (void*)read_surface, (void*)context);
        return false;
    }
    
    if (draw_surface == read_surface) {
        log_debug("Made EGL context current (surface: %p, context: %p)",
                  (void*)draw_surface, (void*)context);
    } else {
        log_debug("Made EGL context current (draw: %p, read: %p, context: %p)",
                  (void*)draw_surface, (void*)read_surface, (void*)context);
    }
    
    return true;
}

/* Get current EGL context */
EGLContext egl_v14_get_current_context(void) {
    if (eglGetCurrentContextProc) {
        return eglGetCurrentContextProc();
    }
    return eglGetCurrentContext();
}

/* Get current draw or read surface */
EGLSurface egl_v14_get_current_surface(EGLint readdraw) {
    if (eglGetCurrentSurfaceProc) {
        return eglGetCurrentSurfaceProc(readdraw);
    }
    return eglGetCurrentSurface(readdraw);
}

/* Query context for attributes */
bool egl_v14_query_context(EGLDisplay display, EGLContext context,
                           EGLint attribute, EGLint *value) {
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT || !value) {
        log_error("Invalid parameters for eglQueryContext");
        return false;
    }
    
    if (!eglQueryContext(display, context, attribute, value)) {
        EGLint error = eglGetError();
        log_error("Failed to query EGL context attribute 0x%x: 0x%x", 
                  attribute, error);
        return false;
    }
    
    return true;
}

/* Create context with specific GLES version and fallback */
EGLContext egl_v14_create_context_with_fallback(EGLDisplay display,
                                                 EGLConfig config,
                                                 int *gles_version) {
    if (!gles_version) {
        log_error("Invalid gles_version pointer");
        return EGL_NO_CONTEXT;
    }
    
    /* Try ES 3.2, 3.1, 3.0, 2.0 in order */
    const int versions[] = {3, 3, 3, 2};
    const int minor_versions[] = {2, 1, 0, 0};
    
    for (int i = 0; i < 4; i++) {
        int major = versions[i];
        int minor = minor_versions[i];
        int version = (minor > 0) ? major : major; /* ES 3.x uses major only in context attribs */
        
        EGLint context_attribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, version,
            EGL_NONE
        };
        
        /* For ES 2.0, use CLIENT_VERSION instead */
        if (major == 2) {
            context_attribs[0] = EGL_CONTEXT_CLIENT_VERSION;
            context_attribs[1] = 2;
        }
        
        EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
        
        if (context != EGL_NO_CONTEXT) {
            *gles_version = major;
            log_info("Created EGL context with OpenGL ES %d.%d", major, minor);
            return context;
        }
        
        /* Clear error and try next version */
        eglGetError();
    }
    
    log_error("Failed to create EGL context with any supported GLES version");
    return EGL_NO_CONTEXT;
}

/* Destroy context safely */
void egl_v14_destroy_context(EGLDisplay display, EGLContext context) {
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        return;
    }
    
    /* Unbind if it's current */
    EGLContext current = egl_v14_get_current_context();
    if (current == context) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    
    if (!eglDestroyContext(display, context)) {
        EGLint error = eglGetError();
        log_error("Failed to destroy EGL context: 0x%x", error);
    } else {
        log_debug("Destroyed EGL context %p", (void*)context);
    }
}

/* Verify context is valid and current */
bool egl_v14_verify_context(EGLDisplay display, EGLContext context) {
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        return false;
    }
    
    /* Check if context is current */
    EGLContext current = egl_v14_get_current_context();
    if (current != context) {
        log_debug("Context %p is not current (current: %p)", 
                  (void*)context, (void*)current);
        return false;
    }
    
    /* Query context attributes to verify it's valid */
    EGLint client_version;
    if (!egl_v14_query_context(display, context, EGL_CONTEXT_CLIENT_VERSION, &client_version)) {
        return false;
    }
    
    log_debug("Context %p is valid (ES %d)", (void*)context, client_version);
    return true;
}

/* Get context info string */
const char *egl_v14_get_context_info(EGLDisplay display, EGLContext context) {
    static char info[256];
    
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        snprintf(info, sizeof(info), "Invalid context");
        return info;
    }
    
    EGLint client_version = 0;
    EGLint config_id = 0;
    EGLint render_buffer = 0;
    
    egl_v14_query_context(display, context, EGL_CONTEXT_CLIENT_VERSION, &client_version);
    egl_v14_query_context(display, context, EGL_CONFIG_ID, &config_id);
    egl_v14_query_context(display, context, EGL_RENDER_BUFFER, &render_buffer);
    
    const char *buffer_str = (render_buffer == EGL_BACK_BUFFER) ? "back" :
                             (render_buffer == EGL_SINGLE_BUFFER) ? "single" : "unknown";
    
    snprintf(info, sizeof(info), 
             "ES %d, Config %d, Buffer: %s",
             client_version, config_id, buffer_str);
    
    return info;
}

/* Print EGL 1.4 capabilities */
void egl_v14_print_info(EGLDisplay display) {
    if (display == EGL_NO_DISPLAY) {
        log_info("EGL 1.4: No display available");
        return;
    }
    
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        log_info("EGL 1.4: Failed to initialize");
        return;
    }
    
    const char *vendor = eglQueryString(display, EGL_VENDOR);
    const char *version = eglQueryString(display, EGL_VERSION);
    const char *apis = eglQueryString(display, EGL_CLIENT_APIS);
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    
    log_info("EGL 1.4 Information:");
    log_info("  Vendor: %s", vendor ? vendor : "Unknown");
    log_info("  Version: %s", version ? version : "Unknown");
    log_info("  Client APIs: %s", apis ? apis : "Unknown");
    log_info("  Extensions: %s", extensions ? extensions : "None");
    
    /* Query implementation limits */
    EGLint max_pbuffer_width = 0, max_pbuffer_height = 0;
    
    eglGetConfigAttrib(display, NULL, EGL_MAX_PBUFFER_WIDTH, &max_pbuffer_width);
    eglGetConfigAttrib(display, NULL, EGL_MAX_PBUFFER_HEIGHT, &max_pbuffer_height);
    
    /* These may not be queryable directly, but we can try */
    log_info("  Max PBuffer: %dx%d", max_pbuffer_width, max_pbuffer_height);
    
    eglTerminate(display);
}