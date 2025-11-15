#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "compositor.h"
#include "neowall.h"
#include "egl/egl_core.h"

/*
 * ============================================================================
 * X11 BACKEND FOR TILING WINDOW MANAGERS
 * ============================================================================
 *
 * Backend implementation for X11 tiling window managers.
 *
 * SUPPORTED WINDOW MANAGERS:
 * - i3/i3-gaps
 * - bspwm
 * - dwm
 * - awesome
 * - xmonad
 * - qtile
 * - herbstluftwm
 *
 * FEATURES:
 * - Desktop window type (_NET_WM_WINDOW_TYPE_DESKTOP)
 * - Proper stacking below all windows
 * - Multi-monitor support via XRandR
 * - EGL rendering via EGL_PLATFORM_X11_KHR
 *
 * LIMITATIONS:
 * - No layer shell (X11 doesn't have equivalent)
 * - Window stacking depends on WM respecting EWMH hints
 * - Some WMs may require additional configuration
 *
 * PRIORITY: 50 (medium - used when Wayland not available)
 */

#define BACKEND_NAME "x11-tiling-wm"
#define BACKEND_DESCRIPTION "X11 backend for tiling window managers (i3, bspwm, dwm, etc.)"
#define BACKEND_PRIORITY 50

/* Backend-specific data */
typedef struct {
    struct neowall_state *state;
    Display *x_display;
    Window root_window;
    int screen;
    
    /* EWMH Atoms */
    Atom atom_net_wm_window_type;
    Atom atom_net_wm_window_type_desktop;
    Atom atom_net_wm_state;
    Atom atom_net_wm_state_below;
    Atom atom_net_wm_state_sticky;
    Atom atom_net_wm_state_skip_taskbar;
    Atom atom_net_wm_state_skip_pager;
    
    /* XRandR support */
    bool has_xrandr;
    int xrandr_event_base;
    int xrandr_error_base;
    
    bool initialized;
} x11_backend_data_t;

/* Surface backend data */
typedef struct {
    Window x_window;
    EGLSurface egl_surface;
    EGLNativeWindowType native_window;
    bool mapped;
    Pixmap root_pixmap;  /* Pixmap set on root window for pseudo-transparency */
    GC gc;               /* Graphics context for copying to pixmap */
    XImage *ximage;      /* XImage for transferring OpenGL pixels to pixmap */
    unsigned char *pixel_buffer;  /* Buffer for glReadPixels */
} x11_surface_data_t;

/* ============================================================================
 * ATOM INITIALIZATION
 * ============================================================================ */

static bool x11_init_atoms(x11_backend_data_t *backend) {
    Display *dpy = backend->x_display;
    
    /* EWMH window type atoms */
    backend->atom_net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    backend->atom_net_wm_window_type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    
    /* EWMH state atoms */
    backend->atom_net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    backend->atom_net_wm_state_below = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
    backend->atom_net_wm_state_sticky = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    backend->atom_net_wm_state_skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    backend->atom_net_wm_state_skip_pager = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    
    return true;
}

/* ============================================================================
 * XRANDR DETECTION
 * ============================================================================ */

static bool x11_init_xrandr(x11_backend_data_t *backend) {
    Display *dpy = backend->x_display;
    
    backend->has_xrandr = XRRQueryExtension(dpy, 
                                           &backend->xrandr_event_base,
                                           &backend->xrandr_error_base);
    
    if (backend->has_xrandr) {
        int major, minor;
        if (XRRQueryVersion(dpy, &major, &minor)) {
            log_info("XRandR extension detected: version %d.%d", major, minor);
            
            /* Select for screen change events */
            XRRSelectInput(dpy, backend->root_window, RRScreenChangeNotifyMask);
            return true;
        }
    }
    
    log_info("XRandR not available - using default screen dimensions");
    return false;
}

/* Get actual screen dimensions using XRandR */
static void x11_get_screen_dimensions(x11_backend_data_t *backend, int *width, int *height) {
    Display *dpy = backend->x_display;
    
    /* Default to X11 screen dimensions */
    *width = DisplayWidth(dpy, backend->screen);
    *height = DisplayHeight(dpy, backend->screen);
    
    /* Try to get actual dimensions from XRandR if available */
    if (backend->has_xrandr) {
        XRRScreenResources *resources = XRRGetScreenResources(dpy, backend->root_window);
        if (resources) {
            /* Find the primary output or first active CRTC */
            for (int i = 0; i < resources->ncrtc; i++) {
                XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, resources, resources->crtcs[i]);
                if (crtc_info && crtc_info->mode != None && crtc_info->noutput > 0) {
                    /* Found an active CRTC - use its dimensions */
                    *width = crtc_info->width;
                    *height = crtc_info->height;
                    log_debug("Using XRandR dimensions: %dx%d", *width, *height);
                    XRRFreeCrtcInfo(crtc_info);
                    break;
                }
                if (crtc_info) {
                    XRRFreeCrtcInfo(crtc_info);
                }
            }
            XRRFreeScreenResources(resources);
        }
    }
}

/* ============================================================================
 * BACKEND INITIALIZATION
 * ============================================================================ */

static void *x11_backend_init(struct neowall_state *state) {
    log_info("Initializing X11 backend for tiling window managers");
    
    x11_backend_data_t *backend = calloc(1, sizeof(x11_backend_data_t));
    if (!backend) {
        log_error("Failed to allocate X11 backend data");
        return NULL;
    }
    
    backend->state = state;
    
    /* Open X11 display */
    backend->x_display = XOpenDisplay(NULL);
    if (!backend->x_display) {
        log_error("Failed to open X11 display");
        free(backend);
        return NULL;
    }
    
    backend->screen = DefaultScreen(backend->x_display);
    backend->root_window = RootWindow(backend->x_display, backend->screen);
    
    log_info("Connected to X11 display: screen %d", backend->screen);
    
    /* Initialize EWMH atoms */
    if (!x11_init_atoms(backend)) {
        log_error("Failed to initialize X11 atoms");
        XCloseDisplay(backend->x_display);
        free(backend);
        return NULL;
    }
    
    /* Initialize XRandR */
    x11_init_xrandr(backend);
    
    backend->initialized = true;
    
    log_info("X11 backend initialized successfully");
    return backend;
}

/* ============================================================================
 * BACKEND CLEANUP
 * ============================================================================ */

static void x11_backend_cleanup(void *backend_data) {
    if (!backend_data) return;
    
    x11_backend_data_t *backend = backend_data;
    
    log_info("Cleaning up X11 backend");
    
    if (backend->x_display) {
        XCloseDisplay(backend->x_display);
        backend->x_display = NULL;
    }
    
    free(backend);
}

/* ============================================================================
 * WINDOW PROPERTY SETUP
 * ============================================================================ */



/* ============================================================================
 * SURFACE CREATION
 * ============================================================================ */

static struct compositor_surface *x11_create_surface(void *backend_data,
                                                     const compositor_surface_config_t *config) {
    x11_backend_data_t *backend = backend_data;
    
    if (!backend || !backend->initialized) {
        log_error("X11 backend not initialized");
        return NULL;
    }
    
    log_debug("Creating X11 surface");
    
    /* Allocate surface structure */
    struct compositor_surface *surface = calloc(1, sizeof(struct compositor_surface));
    if (!surface) {
        log_error("Failed to allocate compositor surface");
        return NULL;
    }
    
    /* Allocate backend data */
    x11_surface_data_t *surf_data = calloc(1, sizeof(x11_surface_data_t));
    if (!surf_data) {
        log_error("Failed to allocate X11 surface data");
        free(surface);
        return NULL;
    }
    
    /* Get screen dimensions using XRandR if available */
    int screen_width, screen_height;
    x11_get_screen_dimensions(backend, &screen_width, &screen_height);
    
    /* Determine surface dimensions from config or output */
    int width = config->width > 0 ? config->width : screen_width;
    int height = config->height > 0 ? config->height : screen_height;
    
    log_debug("Creating X11 wallpaper window: %dx%d", width, height);
    
    /* Create pixmap for root window background (for Conky pseudo-transparency) */
    surf_data->root_pixmap = XCreatePixmap(backend->x_display, backend->root_window,
                                           width, height, 
                                           DefaultDepth(backend->x_display, backend->screen));
    if (!surf_data->root_pixmap) {
        log_error("Failed to create root pixmap for wallpaper");
        free(surf_data);
        free(surface);
        return NULL;
    }
    
    /* Create graphics context for copying rendered content */
    surf_data->gc = XCreateGC(backend->x_display, backend->root_window, 0, NULL);
    
    /* Allocate pixel buffer for glReadPixels */
    surf_data->pixel_buffer = malloc(width * height * 4);  /* RGBA */
    if (!surf_data->pixel_buffer) {
        log_error("Failed to allocate pixel buffer");
        XFreePixmap(backend->x_display, surf_data->root_pixmap);
        XFreeGC(backend->x_display, surf_data->gc);
        free(surf_data);
        free(surface);
        return NULL;
    }
    
    /* Create XImage for putting pixels to pixmap */
    surf_data->ximage = XCreateImage(backend->x_display,
                                     DefaultVisual(backend->x_display, backend->screen),
                                     DefaultDepth(backend->x_display, backend->screen),
                                     ZPixmap, 0, (char *)surf_data->pixel_buffer,
                                     width, height, 32, 0);
    
    /* Create a fullscreen window at the bottom of the stack */
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;  /* Bypass WM completely */
    attrs.background_pixel = BlackPixel(backend->x_display, backend->screen);
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask;
    
    surf_data->x_window = XCreateWindow(
        backend->x_display,
        backend->root_window,
        0, 0,  /* Position at top-left */
        width, height,
        0,  /* No border */
        CopyFromParent,  /* depth */
        InputOutput,     /* class */
        CopyFromParent,  /* visual */
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs
    );
    
    if (!surf_data->x_window) {
        log_error("Failed to create X11 wallpaper window");
        free(surf_data);
        free(surface);
        return NULL;
    }
    
    /* Map and lower the window to bottom of stack */
    XMapWindow(backend->x_display, surf_data->x_window);
    XLowerWindow(backend->x_display, surf_data->x_window);
    
    /* Raise all other windows above this one */
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int nchildren = 0;
    
    if (XQueryTree(backend->x_display, backend->root_window, &root_return, 
                   &parent_return, &children, &nchildren)) {
        /* Raise all windows except our wallpaper window */
        for (unsigned int i = 0; i < nchildren; i++) {
            if (children[i] != surf_data->x_window) {
                XRaiseWindow(backend->x_display, children[i]);
            }
        }
        if (children) {
            XFree(children);
        }
    }
    
    /* Set the pixmap as root window background */
    XSetWindowBackgroundPixmap(backend->x_display, backend->root_window, surf_data->root_pixmap);
    XClearWindow(backend->x_display, backend->root_window);
    
    /* Set root window properties for pseudo-transparency apps (Conky, etc) */
    Atom prop_root = XInternAtom(backend->x_display, "_XROOTPMAP_ID", False);
    Atom prop_esetroot = XInternAtom(backend->x_display, "ESETROOT_PMAP_ID", False);
    XChangeProperty(backend->x_display, backend->root_window, prop_root, XA_PIXMAP, 32,
                   PropModeReplace, (unsigned char *)&surf_data->root_pixmap, 1);
    XChangeProperty(backend->x_display, backend->root_window, prop_esetroot, XA_PIXMAP, 32,
                   PropModeReplace, (unsigned char *)&surf_data->root_pixmap, 1);
    
    /* Send PropertyNotify event to notify apps like Conky that background changed */
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = PropertyNotify;
    event.xproperty.window = backend->root_window;
    event.xproperty.atom = prop_root;
    event.xproperty.state = PropertyNewValue;
    XSendEvent(backend->x_display, backend->root_window, False, PropertyChangeMask, &event);
    
    XFlush(backend->x_display);
    
    surf_data->mapped = true;
    surf_data->native_window = (EGLNativeWindowType)surf_data->x_window;
    
    /* Initialize surface structure */
    surface->wl_surface = NULL;  /* Not Wayland */
    surface->egl_window = NULL;  /* Not Wayland EGL */
    surface->egl_surface = EGL_NO_SURFACE;
    surface->output = NULL;
    surface->width = width;
    surface->height = height;
    surface->scale = 1;
    surface->config = *config;
    surface->configured = true;  /* X11 windows are immediately configured */
    surface->committed = false;
    surface->backend_data = surf_data;
    surface->backend = (struct compositor_backend *)backend;
    surface->tearing_control = NULL;
    
    log_info("X11 surface created successfully: window 0x%lx", surf_data->x_window);
    
    return surface;
}

/* ============================================================================
 * SURFACE DESTRUCTION
 * ============================================================================ */

static void x11_destroy_surface(struct compositor_surface *surface) {
    if (!surface) return;
    
    x11_surface_data_t *surf_data = surface->backend_data;
    x11_backend_data_t *backend = surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;
    
    log_debug("Destroying X11 surface");
    
    if (surf_data) {
        if (surf_data->egl_surface != EGL_NO_SURFACE) {
            /* EGL surface cleanup handled by caller */
            surf_data->egl_surface = EGL_NO_SURFACE;
        }
        
        /* Clean up graphics context and pixmap */
        if (backend && backend->x_display) {
            if (surf_data->gc) {
                XFreeGC(backend->x_display, surf_data->gc);
            }
            if (surf_data->root_pixmap) {
                XFreePixmap(backend->x_display, surf_data->root_pixmap);
            }
            if (surf_data->ximage) {
                surf_data->ximage->data = NULL;  /* Don't let XDestroyImage free our buffer */
                XDestroyImage(surf_data->ximage);
            }
        }
        
        if (surf_data->pixel_buffer) {
            free(surf_data->pixel_buffer);
        }
        
        /* Destroy the wallpaper window */
        if (surf_data->x_window && backend && backend->x_display) {
            XDestroyWindow(backend->x_display, surf_data->x_window);
            XFlush(backend->x_display);
        }
        
        free(surf_data);
    }
    
    free(surface);
    
    log_debug("X11 surface destroyed");
}

/* ============================================================================
 * SURFACE CONFIGURATION
 * ============================================================================ */

static bool x11_configure_surface(struct compositor_surface *surface,
                                  const compositor_surface_config_t *config) {
    if (!surface || !config) return false;
    
    x11_surface_data_t *surf_data = surface->backend_data;
    x11_backend_data_t *backend = (x11_backend_data_t *)surface->backend;
    
    if (!surf_data || !backend) return false;
    
    log_debug("Configuring X11 surface");
    
    /* Update configuration */
    surface->config = *config;
    
    /* Resize window if dimensions changed */
    if (config->width > 0 && config->height > 0) {
        if (surface->width != config->width || surface->height != config->height) {
            XResizeWindow(backend->x_display, surf_data->x_window, 
                         config->width, config->height);
            surface->width = config->width;
            surface->height = config->height;
            
            log_debug("Resized X11 window to %dx%d", config->width, config->height);
        }
    }
    
    /* Ensure window stays at bottom of stack */
    XLowerWindow(backend->x_display, surf_data->x_window);
    XFlush(backend->x_display);
    
    return true;
}

/* ============================================================================
 * SURFACE COMMIT
 * ============================================================================ */

static void x11_commit_surface(struct compositor_surface *surface) {
    if (!surface) return;
    
    x11_backend_data_t *backend = surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;
    
    if (!backend || !backend->x_display) return;
    
    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return;
    
    /* Copy the OpenGL rendered content to the root pixmap for Conky pseudo-transparency */
    if (surf_data->root_pixmap && surf_data->gc && surf_data->pixel_buffer && surf_data->ximage) {
        /* Read pixels from OpenGL framebuffer */
        glReadPixels(0, 0, surface->width, surface->height, GL_RGBA, GL_UNSIGNED_BYTE, 
                    surf_data->pixel_buffer);
        
        /* Debug: Log first time we copy pixels */
        static bool first_copy = true;
        if (first_copy) {
            log_debug("Copying OpenGL framebuffer to root pixmap (%dx%d)", surface->width, surface->height);
            first_copy = false;
        }
        
        /* Flip image vertically (OpenGL origin is bottom-left, X11 is top-left) 
         * and swap R and B channels (RGBA -> BGRA for X11) */
        int row_size = surface->width * 4;
        unsigned char *temp_row = malloc(row_size);
        if (temp_row) {
            for (int y = 0; y < surface->height / 2; y++) {
                unsigned char *top = surf_data->pixel_buffer + (y * row_size);
                unsigned char *bottom = surf_data->pixel_buffer + ((surface->height - 1 - y) * row_size);
                memcpy(temp_row, top, row_size);
                memcpy(top, bottom, row_size);
                memcpy(bottom, temp_row, row_size);
            }
            free(temp_row);
        }
        
        /* Swap R and B channels for X11 (RGBA -> BGRA) */
        for (int i = 0; i < surface->width * surface->height * 4; i += 4) {
            unsigned char temp = surf_data->pixel_buffer[i];
            surf_data->pixel_buffer[i] = surf_data->pixel_buffer[i + 2];
            surf_data->pixel_buffer[i + 2] = temp;
        }
        
        /* Put image data to pixmap */
        XPutImage(backend->x_display, surf_data->root_pixmap, surf_data->gc,
                 surf_data->ximage, 0, 0, 0, 0, surface->width, surface->height);
        
        /* Update root window background */
        XSetWindowBackgroundPixmap(backend->x_display, backend->root_window, surf_data->root_pixmap);
        XClearWindow(backend->x_display, backend->root_window);
        
        /* Notify apps like Conky that background changed */
        Atom prop_root = XInternAtom(backend->x_display, "_XROOTPMAP_ID", False);
        XEvent event;
        memset(&event, 0, sizeof(event));
        event.type = PropertyNotify;
        event.xproperty.window = backend->root_window;
        event.xproperty.atom = prop_root;
        event.xproperty.state = PropertyNewValue;
        XSendEvent(backend->x_display, backend->root_window, False, PropertyChangeMask, &event);
    }
    
    /* Keep wallpaper window at the bottom of the stack */
    if (surf_data->x_window) {
        XLowerWindow(backend->x_display, surf_data->x_window);
    }
    
    /* Flush X11 commands to ensure rendering is visible */
    XFlush(backend->x_display);
    
    surface->committed = true;
}

/* ============================================================================
 * EGL WINDOW CREATION
 * ============================================================================ */

static bool x11_create_egl_window(struct compositor_surface *surface,
                                  int32_t width, int32_t height) {
    if (!surface) return false;
    
    x11_surface_data_t *surf_data = surface->backend_data;
    x11_backend_data_t *backend = (x11_backend_data_t *)surface->backend;
    
    if (!surf_data || !backend) return false;
    
    log_debug("Creating EGL surface for X11 window");
    
    /* X11 windows are used directly with EGL - no separate EGL window object */
    /* The native window handle is already set in surf_data->native_window */
    
    /* EGL surface creation is handled by the EGL subsystem using the native window */
    /* This function just prepares the surface for EGL usage */
    
    surface->width = width;
    surface->height = height;
    
    log_debug("X11 EGL window prepared: native handle 0x%lx", 
             (unsigned long)surf_data->native_window);
    
    return true;
}

/* ============================================================================
 * EGL WINDOW DESTRUCTION
 * ============================================================================ */

static void x11_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface) return;
    
    log_debug("Destroying X11 EGL window");
    
    /* X11 doesn't have separate EGL window objects - cleanup handled elsewhere */
}

/* ============================================================================
 * CAPABILITIES
 * ============================================================================ */

static compositor_capabilities_t x11_get_capabilities(void *backend_data) {
    (void)backend_data;
    
    /* X11 capabilities are limited compared to Wayland layer-shell */
    return COMPOSITOR_CAP_MULTI_OUTPUT;  /* XRandR provides multi-monitor */
}

/* ============================================================================
 * OUTPUT MANAGEMENT
 * ============================================================================ */

static void x11_on_output_added(void *backend_data, struct wl_output *output) {
    (void)backend_data;
    (void)output;
    
    /* X11 output management handled via XRandR events */
    log_debug("X11 output added (handled via XRandR)");
}

static void x11_on_output_removed(void *backend_data, struct wl_output *output) {
    (void)backend_data;
    (void)output;
    
    /* X11 output management handled via XRandR events */
    log_debug("X11 output removed (handled via XRandR)");
}

/* ============================================================================
 * BACKEND OPERATIONS TABLE
 * ============================================================================ */

static const compositor_backend_ops_t x11_backend_ops = {
    .init = x11_backend_init,
    .cleanup = x11_backend_cleanup,
    .create_surface = x11_create_surface,
    .destroy_surface = x11_destroy_surface,
    .configure_surface = x11_configure_surface,
    .commit_surface = x11_commit_surface,
    .create_egl_window = x11_create_egl_window,
    .destroy_egl_window = x11_destroy_egl_window,
    .get_capabilities = x11_get_capabilities,
    .on_output_added = x11_on_output_added,
    .on_output_removed = x11_on_output_removed,
};

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

struct compositor_backend *compositor_backend_x11_init(struct neowall_state *state) {
    /* Check if X11 is available */
    Display *test_display = XOpenDisplay(NULL);
    if (!test_display) {
        log_debug("X11 display not available - skipping X11 backend");
        return NULL;
    }
    XCloseDisplay(test_display);
    
    log_info("X11 backend available - registering");
    
    struct compositor_backend *backend = calloc(1, sizeof(struct compositor_backend));
    if (!backend) {
        log_error("Failed to allocate X11 backend");
        return NULL;
    }
    
    backend->name = BACKEND_NAME;
    backend->description = BACKEND_DESCRIPTION;
    backend->priority = BACKEND_PRIORITY;
    backend->ops = &x11_backend_ops;
    backend->data = x11_backend_init(state);
    
    if (!backend->data) {
        log_error("Failed to initialize X11 backend");
        free(backend);
        return NULL;
    }
    
    return backend;
}