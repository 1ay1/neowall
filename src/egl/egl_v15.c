#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "../../include/staticwall.h"
#include "../../include/egl/capability.h"

/**
 * EGL 1.5 Implementation
 * 
 * Key features:
 * - Platform-specific display creation (eglGetPlatformDisplay)
 * - Sync objects for explicit synchronization
 * - Better error reporting with eglGetError improvements
 * - Native rendering support
 * - OpenGL ES 3.x optimizations
 * - Improved Wayland integration
 * 
 * EGL 1.5 is recommended for modern systems with full ES 3.0+ support.
 */

/* EGL 1.5 platform types */
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

/* EGL 1.5 sync object types */
#ifndef EGL_SYNC_FENCE
#define EGL_SYNC_FENCE 0x30F9
#endif

#ifndef EGL_SYNC_NATIVE_FENCE_ANDROID
#define EGL_SYNC_NATIVE_FENCE_ANDROID 0x3144
#endif

/* Function pointers for EGL 1.5 */
typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYPROC)(EGLenum platform, void *native_display, const EGLAttrib *attrib_list);
typedef EGLSurface (*PFNEGLCREATEPLATFORMWINDOWSURFACEPROC)(EGLDisplay display, EGLConfig config, void *native_window, const EGLAttrib *attrib_list);
typedef EGLSync (*PFNEGLCREATESYNCPROC)(EGLDisplay display, EGLenum type, const EGLAttrib *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYSYNCPROC)(EGLDisplay display, EGLSync sync);
typedef EGLint (*PFNEGLCLIENTWAITSYNCPROC)(EGLDisplay display, EGLSync sync, EGLint flags, EGLTime timeout);
typedef EGLBoolean (*PFNEGLGETSYNCATTRIBPROC)(EGLDisplay display, EGLSync sync, EGLint attribute, EGLAttrib *value);
typedef EGLBoolean (*PFNEGLWAITSYNCPROC)(EGLDisplay display, EGLSync sync, EGLint flags);

static PFNEGLGETPLATFORMDISPLAYPROC eglGetPlatformDisplayProc = NULL;
static PFNEGLCREATEPLATFORMWINDOWSURFACEPROC eglCreatePlatformWindowSurfaceProc = NULL;
static PFNEGLCREATESYNCPROC eglCreateSyncProc = NULL;
static PFNEGLDESTROYSYNCPROC eglDestroySyncProc = NULL;
static PFNEGLCLIENTWAITSYNCPROC eglClientWaitSyncProc = NULL;
static PFNEGLGETSYNCATTRIBPROC eglGetSyncAttribProc = NULL;
static PFNEGLWAITSYNCPROC eglWaitSyncProc = NULL;

/* Check if EGL 1.5 is available */
bool egl_v15_available(void) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return false;
    }
    
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        return false;
    }
    
    bool available = (major > 1) || (major == 1 && minor >= 5);
    
    eglTerminate(display);
    return available;
}

/* Initialize EGL 1.5 function pointers */
bool egl_v15_init_functions(void) {
    /* Load platform display functions */
    eglGetPlatformDisplayProc = (PFNEGLGETPLATFORMDISPLAYPROC)
        eglGetProcAddress("eglGetPlatformDisplay");
    
    eglCreatePlatformWindowSurfaceProc = (PFNEGLCREATEPLATFORMWINDOWSURFACEPROC)
        eglGetProcAddress("eglCreatePlatformWindowSurface");
    
    /* Load sync object functions */
    eglCreateSyncProc = (PFNEGLCREATESYNCPROC)
        eglGetProcAddress("eglCreateSync");
    
    eglDestroySyncProc = (PFNEGLDESTROYSYNCPROC)
        eglGetProcAddress("eglDestroySync");
    
    eglClientWaitSyncProc = (PFNEGLCLIENTWAITSYNCPROC)
        eglGetProcAddress("eglClientWaitSync");
    
    eglGetSyncAttribProc = (PFNEGLGETSYNCATTRIBPROC)
        eglGetProcAddress("eglGetSyncAttrib");
    
    eglWaitSyncProc = (PFNEGLWAITSYNCPROC)
        eglGetProcAddress("eglWaitSync");
    
    /* Check if core 1.5 functions are available */
    if (!eglGetPlatformDisplayProc || !eglCreateSyncProc) {
        log_error("EGL 1.5 core functions not available");
        return false;
    }
    
    log_info("EGL 1.5 initialized successfully");
    log_info("  Platform display: %s", eglGetPlatformDisplayProc ? "available" : "unavailable");
    log_info("  Sync objects: %s", eglCreateSyncProc ? "available" : "unavailable");
    
    return true;
}

/* Get platform-specific display (EGL 1.5 preferred method) */
EGLDisplay egl_v15_get_platform_display(EGLenum platform,
                                        void *native_display,
                                        const EGLAttrib *attrib_list) {
    if (!eglGetPlatformDisplayProc) {
        log_error("eglGetPlatformDisplay not available");
        return EGL_NO_DISPLAY;
    }
    
    EGLDisplay display = eglGetPlatformDisplayProc(platform, native_display, attrib_list);
    
    if (display == EGL_NO_DISPLAY) {
        EGLint error = eglGetError();
        log_error("Failed to get platform display (platform 0x%x): 0x%x", platform, error);
        return EGL_NO_DISPLAY;
    }
    
    const char *platform_name = "Unknown";
    switch (platform) {
        case EGL_PLATFORM_WAYLAND_KHR: platform_name = "Wayland"; break;
        case EGL_PLATFORM_GBM_KHR: platform_name = "GBM"; break;
        case EGL_PLATFORM_X11_KHR: platform_name = "X11"; break;
    }
    
    log_debug("Got EGL 1.5 platform display for %s: %p", platform_name, (void*)display);
    return display;
}

/* Get Wayland display (convenience wrapper) */
EGLDisplay egl_v15_get_wayland_display(void *wayland_display) {
    if (!wayland_display) {
        log_error("Invalid Wayland display");
        return EGL_NO_DISPLAY;
    }
    
    return egl_v15_get_platform_display(EGL_PLATFORM_WAYLAND_KHR, wayland_display, NULL);
}

/* Create platform window surface */
EGLSurface egl_v15_create_platform_window_surface(EGLDisplay display,
                                                  EGLConfig config,
                                                  void *native_window,
                                                  const EGLAttrib *attrib_list) {
    if (!eglCreatePlatformWindowSurfaceProc) {
        log_error("eglCreatePlatformWindowSurface not available");
        return EGL_NO_SURFACE;
    }
    
    if (display == EGL_NO_DISPLAY || config == NULL || !native_window) {
        log_error("Invalid parameters for platform window surface creation");
        return EGL_NO_SURFACE;
    }
    
    EGLSurface surface = eglCreatePlatformWindowSurfaceProc(display, config, 
                                                             native_window, attrib_list);
    
    if (surface == EGL_NO_SURFACE) {
        EGLint error = eglGetError();
        log_error("Failed to create platform window surface: 0x%x", error);
        return EGL_NO_SURFACE;
    }
    
    log_debug("Created EGL 1.5 platform window surface: %p", (void*)surface);
    return surface;
}

/* Create Wayland window surface (convenience wrapper) */
EGLSurface egl_v15_create_wayland_window_surface(EGLDisplay display,
                                                 EGLConfig config,
                                                 void *wayland_window) {
    return egl_v15_create_platform_window_surface(display, config, wayland_window, NULL);
}

/* Create sync object (fence) */
EGLSync egl_v15_create_sync(EGLDisplay display, EGLenum type, const EGLAttrib *attrib_list) {
    if (!eglCreateSyncProc) {
        log_error("eglCreateSync not available");
        return EGL_NO_SYNC;
    }
    
    if (display == EGL_NO_DISPLAY) {
        log_error("Invalid display for sync object creation");
        return EGL_NO_SYNC;
    }
    
    EGLSync sync = eglCreateSyncProc(display, type, attrib_list);
    
    if (sync == EGL_NO_SYNC) {
        EGLint error = eglGetError();
        log_error("Failed to create sync object (type 0x%x): 0x%x", type, error);
        return EGL_NO_SYNC;
    }
    
    log_debug("Created EGL sync object: %p (type 0x%x)", (void*)sync, type);
    return sync;
}

/* Create fence sync (most common use case) */
EGLSync egl_v15_create_fence_sync(EGLDisplay display) {
    return egl_v15_create_sync(display, EGL_SYNC_FENCE, NULL);
}

/* Destroy sync object */
bool egl_v15_destroy_sync(EGLDisplay display, EGLSync sync) {
    if (!eglDestroySyncProc) {
        log_error("eglDestroySync not available");
        return false;
    }
    
    if (display == EGL_NO_DISPLAY || sync == EGL_NO_SYNC) {
        return false;
    }
    
    if (!eglDestroySyncProc(display, sync)) {
        EGLint error = eglGetError();
        log_error("Failed to destroy sync object: 0x%x", error);
        return false;
    }
    
    log_debug("Destroyed EGL sync object: %p", (void*)sync);
    return true;
}

/* Wait for sync object on client side (blocks CPU) */
bool egl_v15_client_wait_sync(EGLDisplay display, EGLSync sync, EGLint flags, EGLTime timeout) {
    if (!eglClientWaitSyncProc) {
        log_error("eglClientWaitSync not available");
        return false;
    }
    
    if (display == EGL_NO_DISPLAY || sync == EGL_NO_SYNC) {
        log_error("Invalid display or sync for client wait");
        return false;
    }
    
    EGLint result = eglClientWaitSyncProc(display, sync, flags, timeout);
    
    switch (result) {
        case EGL_CONDITION_SATISFIED:
            log_debug("Sync condition satisfied");
            return true;
            
        case EGL_TIMEOUT_EXPIRED:
            log_debug("Sync timeout expired (not an error)");
            return false;
            
        case EGL_FALSE:
        default: {
            EGLint error = eglGetError();
            log_error("Client wait sync failed: 0x%x", error);
            return false;
        }
    }
}

/* Wait for sync object on GPU side (non-blocking) */
bool egl_v15_wait_sync(EGLDisplay display, EGLSync sync, EGLint flags) {
    if (!eglWaitSyncProc) {
        log_error("eglWaitSync not available");
        return false;
    }
    
    if (display == EGL_NO_DISPLAY || sync == EGL_NO_SYNC) {
        log_error("Invalid display or sync for GPU wait");
        return false;
    }
    
    if (!eglWaitSyncProc(display, sync, flags)) {
        EGLint error = eglGetError();
        log_error("GPU wait sync failed: 0x%x", error);
        return false;
    }
    
    log_debug("GPU wait sync initiated for %p", (void*)sync);
    return true;
}

/* Query sync object attributes */
bool egl_v15_get_sync_attrib(EGLDisplay display, EGLSync sync, EGLint attribute, EGLAttrib *value) {
    if (!eglGetSyncAttribProc) {
        log_error("eglGetSyncAttrib not available");
        return false;
    }
    
    if (display == EGL_NO_DISPLAY || sync == EGL_NO_SYNC || !value) {
        log_error("Invalid parameters for sync attribute query");
        return false;
    }
    
    if (!eglGetSyncAttribProc(display, sync, attribute, value)) {
        EGLint error = eglGetError();
        log_error("Failed to get sync attribute 0x%x: 0x%x", attribute, error);
        return false;
    }
    
    return true;
}

/* Check if sync is signaled */
bool egl_v15_is_sync_signaled(EGLDisplay display, EGLSync sync) {
    EGLAttrib status;
    if (!egl_v15_get_sync_attrib(display, sync, EGL_SYNC_STATUS, &status)) {
        return false;
    }
    
    return (status == EGL_SIGNALED);
}

/* Wait for sync with timeout (convenience function) */
bool egl_v15_wait_sync_timeout(EGLDisplay display, EGLSync sync, uint64_t timeout_ns) {
    /* First try non-blocking check */
    if (egl_v15_is_sync_signaled(display, sync)) {
        return true;
    }
    
    /* If not signaled, wait with timeout */
    return egl_v15_client_wait_sync(display, sync, 0, timeout_ns);
}

/* Create and wait for fence (common pattern for frame synchronization) */
bool egl_v15_fence_and_wait(EGLDisplay display, uint64_t timeout_ns) {
    EGLSync fence = egl_v15_create_fence_sync(display);
    if (fence == EGL_NO_SYNC) {
        return false;
    }
    
    /* Flush to ensure fence is submitted */
    glFlush();
    
    /* Wait for fence */
    bool result = egl_v15_wait_sync_timeout(display, fence, timeout_ns);
    
    /* Clean up */
    egl_v15_destroy_sync(display, fence);
    
    return result;
}

/* Sync-based VSync implementation */
bool egl_v15_vsync_with_sync(EGLDisplay display, EGLSurface surface) {
    /* Swap buffers */
    if (!eglSwapBuffers(display, surface)) {
        EGLint error = eglGetError();
        log_error("Swap buffers failed: 0x%x", error);
        return false;
    }
    
    /* Create fence to track when swap completes */
    EGLSync fence = egl_v15_create_fence_sync(display);
    if (fence == EGL_NO_SYNC) {
        /* Not critical, just means we can't track completion */
        return true;
    }
    
    /* Wait on GPU side (non-blocking for CPU) */
    bool result = egl_v15_wait_sync(display, fence, 0);
    
    /* Clean up fence */
    egl_v15_destroy_sync(display, fence);
    
    return result;
}

/* Get human-readable sync type name */
const char *egl_v15_get_sync_type_name(EGLenum type) {
    switch (type) {
        case EGL_SYNC_FENCE: return "Fence";
        case EGL_SYNC_NATIVE_FENCE_ANDROID: return "Native Fence (Android)";
        default: return "Unknown";
    }
}

/* Print sync object info */
void egl_v15_print_sync_info(EGLDisplay display, EGLSync sync) {
    if (display == EGL_NO_DISPLAY || sync == EGL_NO_SYNC) {
        log_info("Invalid sync object");
        return;
    }
    
    EGLAttrib type, status, condition;
    
    egl_v15_get_sync_attrib(display, sync, EGL_SYNC_TYPE, &type);
    egl_v15_get_sync_attrib(display, sync, EGL_SYNC_STATUS, &status);
    egl_v15_get_sync_attrib(display, sync, EGL_SYNC_CONDITION, &condition);
    
    const char *status_str = (status == EGL_SIGNALED) ? "Signaled" : "Unsignaled";
    const char *type_str = egl_v15_get_sync_type_name((EGLenum)type);
    
    log_info("EGL Sync Object %p:", (void*)sync);
    log_info("  Type: %s (0x%lx)", type_str, (unsigned long)type);
    log_info("  Status: %s", status_str);
    log_info("  Condition: 0x%lx", (unsigned long)condition);
}

/* Print EGL 1.5 capabilities and info */
void egl_v15_print_info(EGLDisplay display) {
    if (display == EGL_NO_DISPLAY) {
        log_info("EGL 1.5: No display available");
        return;
    }
    
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        log_info("EGL 1.5: Failed to initialize");
        return;
    }
    
    const char *vendor = eglQueryString(display, EGL_VENDOR);
    const char *version = eglQueryString(display, EGL_VERSION);
    const char *apis = eglQueryString(display, EGL_CLIENT_APIS);
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    
    log_info("EGL 1.5 Information:");
    log_info("  Vendor: %s", vendor ? vendor : "Unknown");
    log_info("  Version: %s", version ? version : "Unknown");
    log_info("  Client APIs: %s", apis ? apis : "Unknown");
    
    /* Check for EGL 1.5 specific extensions */
    bool has_wayland = extensions && strstr(extensions, "EGL_EXT_platform_wayland");
    bool has_sync = extensions && strstr(extensions, "EGL_KHR_fence_sync");
    bool has_wait_sync = extensions && strstr(extensions, "EGL_KHR_wait_sync");
    
    log_info("  Platform APIs:");
    log_info("    Wayland: %s", has_wayland ? "available" : "unavailable");
    log_info("  Sync Objects:");
    log_info("    Fence sync: %s", has_sync ? "available" : "unavailable");
    log_info("    Wait sync: %s", has_wait_sync ? "available" : "unavailable");
    
    /* Print first few extensions for brevity */
    if (extensions) {
        char *ext_copy = strdup(extensions);
        char *token = strtok(ext_copy, " ");
        int count = 0;
        log_info("  Extensions (first 10):");
        while (token && count < 10) {
            log_info("    %s", token);
            token = strtok(NULL, " ");
            count++;
        }
        if (token) {
            log_info("    ... and more");
        }
        free(ext_copy);
    }
    
    eglTerminate(display);
}

/* Check if running on EGL 1.5 with all features */
bool egl_v15_is_fully_supported(EGLDisplay display) {
    if (!egl_v15_available()) {
        return false;
    }
    
    if (!eglGetPlatformDisplayProc || !eglCreateSyncProc) {
        return false;
    }
    
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions) {
        return false;
    }
    
    /* Check for key extensions */
    bool has_platform = strstr(extensions, "EGL_EXT_platform_base") ||
                        strstr(extensions, "EGL_KHR_platform_base");
    bool has_sync = strstr(extensions, "EGL_KHR_fence_sync");
    
    return has_platform && has_sync;
}