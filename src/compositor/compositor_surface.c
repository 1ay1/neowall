#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "compositor.h"
#include "neowall.h"

/*
 * ============================================================================
 * COMPOSITOR SURFACE MANAGEMENT
 * ============================================================================
 *
 * This module provides the public API for managing compositor surfaces.
 * It delegates to backend-specific implementations while providing a
 * unified interface.
 *
 * SURFACE LIFECYCLE:
 * 1. compositor_surface_create() - Create surface via backend
 * 2. compositor_surface_configure() - Set size, layer, anchors
 * 3. compositor_surface_create_egl() - Create EGL rendering context
 * 4. compositor_surface_commit() - Commit changes to compositor
 * 5. [Render loop...]
 * 6. compositor_surface_destroy_egl() - Destroy EGL context
 * 7. compositor_surface_destroy() - Destroy surface
 *
 * THREAD SAFETY:
 * Surface operations are NOT thread-safe. They must be called from the
 * same thread that owns the Wayland display connection (typically the
 * main event loop thread).
 */

/* ============================================================================
 * SURFACE CREATION
 * ============================================================================ */

struct compositor_surface *compositor_surface_create(struct compositor_backend *backend,
                                                     const compositor_surface_config_t *config) {
    if (!backend) {
        log_error("Cannot create surface: backend is NULL");
        return NULL;
    }
    
    if (!config) {
        log_error("Cannot create surface: config is NULL");
        return NULL;
    }
    
    if (!backend->ops || !backend->ops->create_surface) {
        log_error("Backend '%s' does not implement create_surface", backend->name);
        return NULL;
    }
    
    log_debug("Creating compositor surface via backend '%s'", backend->name);
    
    /* Delegate to backend implementation */
    struct compositor_surface *surface = backend->ops->create_surface(backend->data, config);
    
    if (!surface) {
        log_error("Backend '%s' failed to create surface", backend->name);
        return NULL;
    }
    
    /* Set back-pointer to backend */
    surface->backend = backend;
    
    log_debug("Surface created successfully: %p", (void*)surface);
    
    return surface;
}

/* ============================================================================
 * SURFACE DESTRUCTION
 * ============================================================================ */

void compositor_surface_destroy(struct compositor_surface *surface) {
    if (!surface) {
        return;
    }
    
    log_debug("Destroying compositor surface: %p", (void*)surface);
    
    struct compositor_backend *backend = surface->backend;
    
    if (!backend) {
        log_error("Cannot destroy surface: backend is NULL");
        return;
    }
    
    if (!backend->ops || !backend->ops->destroy_surface) {
        log_error("Backend '%s' does not implement destroy_surface", backend->name);
        return;
    }
    
    /* Delegate to backend implementation */
    backend->ops->destroy_surface(surface);
    
    log_debug("Surface destroyed: %p", (void*)surface);
}

/* ============================================================================
 * SURFACE CONFIGURATION
 * ============================================================================ */

bool compositor_surface_configure(struct compositor_surface *surface,
                                  const compositor_surface_config_t *config) {
    if (!surface) {
        log_error("Cannot configure surface: surface is NULL");
        return false;
    }
    
    if (!config) {
        log_error("Cannot configure surface: config is NULL");
        return false;
    }
    
    struct compositor_backend *backend = surface->backend;
    
    if (!backend) {
        log_error("Cannot configure surface: backend is NULL");
        return false;
    }
    
    if (!backend->ops || !backend->ops->configure_surface) {
        log_error("Backend '%s' does not implement configure_surface", backend->name);
        return false;
    }
    
    log_debug("Configuring surface: layer=%d, anchor=0x%x, size=%dx%d",
              config->layer, config->anchor, config->width, config->height);
    
    /* Delegate to backend implementation */
    bool result = backend->ops->configure_surface(surface, config);
    
    if (!result) {
        log_error("Backend '%s' failed to configure surface", backend->name);
        return false;
    }
    
    /* Update surface config cache */
    surface->config = *config;
    surface->configured = true;
    
    log_debug("Surface configured successfully");
    
    return true;
}

/* ============================================================================
 * SURFACE COMMIT
 * ============================================================================ */

void compositor_surface_commit(struct compositor_surface *surface) {
    if (!surface) {
        log_error("Cannot commit surface: surface is NULL");
        return;
    }
    
    struct compositor_backend *backend = surface->backend;
    
    if (!backend) {
        log_error("Cannot commit surface: backend is NULL");
        return;
    }
    
    if (!backend->ops || !backend->ops->commit_surface) {
        log_error("Backend '%s' does not implement commit_surface", backend->name);
        return;
    }
    
    /* Delegate to backend implementation */
    backend->ops->commit_surface(surface);
    
    surface->committed = true;
}

/* ============================================================================
 * EGL INTEGRATION
 * ============================================================================ */

EGLSurface compositor_surface_create_egl(struct compositor_surface *surface,
                                         EGLDisplay egl_display,
                                         EGLConfig egl_config,
                                         int32_t width, int32_t height) {
    if (!surface) {
        log_error("Cannot create EGL surface: surface is NULL");
        return EGL_NO_SURFACE;
    }
    
    if (egl_display == EGL_NO_DISPLAY) {
        log_error("Cannot create EGL surface: invalid EGL display");
        return EGL_NO_SURFACE;
    }
    
    struct compositor_backend *backend = surface->backend;
    
    if (!backend) {
        log_error("Cannot create EGL surface: backend is NULL");
        return EGL_NO_SURFACE;
    }
    
    if (!backend->ops || !backend->ops->create_egl_window) {
        log_error("Backend '%s' does not implement create_egl_window", backend->name);
        return EGL_NO_SURFACE;
    }
    
    log_debug("Creating EGL window: %dx%d", width, height);
    
    /* Create EGL window via backend */
    bool result = backend->ops->create_egl_window(surface, width, height);
    
    if (!result) {
        log_error("Backend '%s' failed to create EGL window", backend->name);
        return EGL_NO_SURFACE;
    }
    
    if (!surface->egl_window) {
        log_error("Backend created EGL window but egl_window is NULL");
        return EGL_NO_SURFACE;
    }
    
    /* Create EGL surface from EGL window */
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config,
                                                    (EGLNativeWindowType)surface->egl_window,
                                                    NULL);
    
    if (egl_surface == EGL_NO_SURFACE) {
        EGLint error = eglGetError();
        log_error("Failed to create EGL surface: 0x%x", error);
        
        /* Clean up EGL window */
        if (backend->ops->destroy_egl_window) {
            backend->ops->destroy_egl_window(surface);
        }
        
        return EGL_NO_SURFACE;
    }
    
    /* Store EGL surface in compositor surface */
    surface->egl_surface = egl_surface;
    surface->width = width;
    surface->height = height;
    
    log_debug("EGL surface created successfully: %p", (void*)egl_surface);
    
    return egl_surface;
}

void compositor_surface_destroy_egl(struct compositor_surface *surface,
                                    EGLDisplay egl_display) {
    if (!surface) {
        return;
    }
    
    log_debug("Destroying EGL surface: %p", (void*)surface->egl_surface);
    
    /* Destroy EGL surface */
    if (surface->egl_surface != EGL_NO_SURFACE && egl_display != EGL_NO_DISPLAY) {
        eglDestroySurface(egl_display, surface->egl_surface);
        surface->egl_surface = EGL_NO_SURFACE;
    }
    
    /* Destroy EGL window via backend */
    struct compositor_backend *backend = surface->backend;
    
    if (backend && backend->ops && backend->ops->destroy_egl_window) {
        backend->ops->destroy_egl_window(surface);
    }
    
    log_debug("EGL surface destroyed");
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* Get default surface configuration */
compositor_surface_config_t compositor_surface_config_default(struct wl_output *output) {
    compositor_surface_config_t config = {
        .layer = COMPOSITOR_LAYER_BACKGROUND,
        .anchor = COMPOSITOR_ANCHOR_FILL,
        .exclusive_zone = -1,
        .keyboard_interactivity = false,
        .width = 0,  /* Auto */
        .height = 0, /* Auto */
        .output = output,
    };
    
    return config;
}

/* Check if surface is ready for rendering */
bool compositor_surface_is_ready(struct compositor_surface *surface) {
    if (!surface) {
        return false;
    }
    
    return surface->configured && 
           surface->committed && 
           surface->egl_surface != EGL_NO_SURFACE;
}

/* Get surface dimensions */
void compositor_surface_get_size(struct compositor_surface *surface,
                                int32_t *width, int32_t *height) {
    if (!surface) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    if (width) *width = surface->width;
    if (height) *height = surface->height;
}

/* Resize EGL window */
bool compositor_surface_resize_egl(struct compositor_surface *surface,
                                   int32_t width, int32_t height) {
    if (!surface || !surface->egl_window) {
        log_error("Cannot resize: invalid surface or EGL window");
        return false;
    }
    
    log_debug("Resizing EGL window: %dx%d -> %dx%d",
              surface->width, surface->height, width, height);
    
    wl_egl_window_resize(surface->egl_window, width, height, 0, 0);
    
    surface->width = width;
    surface->height = height;
    
    return true;
}

/* Set surface scale factor */
void compositor_surface_set_scale(struct compositor_surface *surface, int32_t scale) {
    if (!surface || !surface->wl_surface) {
        return;
    }
    
    if (scale <= 0) {
        scale = 1;
    }
    
    log_debug("Setting surface scale: %d", scale);
    
    wl_surface_set_buffer_scale(surface->wl_surface, scale);
    surface->scale = scale;
}

/* Set surface callbacks */
void compositor_surface_set_callbacks(struct compositor_surface *surface,
                                     void (*on_configure)(struct compositor_surface*, int32_t, int32_t),
                                     void (*on_closed)(struct compositor_surface*),
                                     void *user_data) {
    if (!surface) {
        return;
    }
    
    surface->on_configure = on_configure;
    surface->on_closed = on_closed;
    surface->user_data = user_data;
}