/* One wallpaper window per monitor: create, configure, commit, and the root
 * pixmap copy that gives conky-style programs a background to read. */

#include "x11_internal.h"

/* ============================================================================
 * WINDOW PROPERTY SETUP
 * ============================================================================ */

/* ============================================================================
 * SURFACE CREATION
 * ============================================================================ */

/* X11 window/pixmap dimensions are CARD16 values. Keeping all size-dependent
 * allocations behind one validator also prevents signed-to-unsigned surprises
 * and width * height * bytes-per-pixel overflow. */
static bool x11_valid_surface_size(int32_t width, int32_t height) {
    return width > 0 && height > 0 && (uint32_t)width <= UINT16_MAX &&
           (uint32_t)height <= UINT16_MAX;
}

static void x11_destroy_readback(Display *dpy, Pixmap pixmap, XImage *ximage) {
    if (ximage) {
        /* XDestroyImage owns and frees image->data when it is non-NULL. */
        XDestroyImage(ximage);
    }
    if (pixmap) {
        XFreePixmap(dpy, pixmap);
    }
}

/* Build all size-dependent readback objects without touching the live surface.
 * XCreateImage first computes the server-compatible stride; its data is then
 * allocated to that exact size and transferred to XDestroyImage ownership. */
static bool x11_allocate_readback(x11_backend_data_t *backend, int32_t width, int32_t height,
                                  Pixmap *pixmap_out, XImage **ximage_out,
                                  unsigned char **pixels_out) {
    *pixmap_out = 0;
    *ximage_out = NULL;
    *pixels_out = NULL;

    if (!x11_valid_surface_size(width, height)) {
        log_error("Invalid X11 surface size: %dx%d", width, height);
        return false;
    }

    Display *dpy = backend->x_display;
    Pixmap pixmap =
        XCreatePixmap(dpy, backend->root_window, (unsigned int)width, (unsigned int)height,
                      (unsigned int)DefaultDepth(dpy, backend->screen));
    if (!pixmap) {
        log_error("Failed to create %dx%d root pixmap", width, height);
        return false;
    }

    XImage *ximage = XCreateImage(dpy, DefaultVisual(dpy, backend->screen),
                                  (unsigned int)DefaultDepth(dpy, backend->screen), ZPixmap, 0,
                                  NULL, (unsigned int)width, (unsigned int)height, 32, 0);
    if (!ximage) {
        log_error("Failed to create %dx%d XImage", width, height);
        XFreePixmap(dpy, pixmap);
        return false;
    }

    size_t row_bytes = (size_t)width * 4u;
    if (ximage->bits_per_pixel != 32 || ximage->bytes_per_line < 0 ||
        (size_t)ximage->bytes_per_line != row_bytes ||
        (size_t)ximage->bytes_per_line > SIZE_MAX / (size_t)height) {
        log_error("Unsupported or overflowing XImage layout for %dx%d", width, height);
        XDestroyImage(ximage); /* data is still NULL */
        XFreePixmap(dpy, pixmap);
        return false;
    }

    size_t image_bytes    = (size_t)ximage->bytes_per_line * (size_t)height;
    unsigned char *pixels = malloc(image_bytes);
    if (!pixels) {
        log_error("Failed to allocate %zu-byte X11 pixel buffer", image_bytes);
        XDestroyImage(ximage); /* data is still NULL */
        XFreePixmap(dpy, pixmap);
        return false;
    }
    ximage->data = (char *)pixels;

    *pixmap_out = pixmap;
    *ximage_out = ximage;
    *pixels_out = pixels;
    return true;
}

static bool x11_replace_readback(struct compositor_surface *surface, x11_backend_data_t *backend,
                                 x11_surface_data_t *surf_data, int32_t width, int32_t height) {
    Pixmap new_pixmap;
    XImage *new_ximage;
    unsigned char *new_pixels;
    if (!x11_allocate_readback(backend, width, height, &new_pixmap, &new_ximage, &new_pixels)) {
        return false;
    }

    Pixmap old_pixmap       = surf_data->root_pixmap;
    XImage *old_ximage      = surf_data->ximage;
    surf_data->root_pixmap  = new_pixmap;
    surf_data->ximage       = new_ximage;
    surf_data->pixel_buffer = new_pixels;

    if (backend->published_root_pixmap == old_pixmap) {
        x11_unpublish_root_pixmap(backend);
    }
    x11_destroy_readback(backend->x_display, old_pixmap, old_ximage);
    surface->width     = width;
    surface->height    = height;
    surface->committed = false;
    return true;
}

struct compositor_surface *x11_create_surface(void *backend_data,
                                              const compositor_surface_config_t *config) {
    x11_backend_data_t *backend = backend_data;

    if (!backend || !backend->initialized || !config) {
        log_error("X11 backend not initialized or surface config missing");
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

    /* Determine surface geometry. With a per-monitor layout the caller passes
     * the monitor's exact size and position; when those are unset we fall back
     * to the whole screen at the origin (single-output case). */
    int width  = config->width > 0 ? config->width : screen_width;
    int height = config->height > 0 ? config->height : screen_height;
    int pos_x  = config->x;
    int pos_y  = config->y;

    if (!x11_valid_surface_size(width, height)) {
        log_error("Invalid X11 wallpaper geometry: %dx%d", width, height);
        free(surf_data);
        free(surface);
        return NULL;
    }

    log_debug("Creating X11 wallpaper window: %dx%d at +%d+%d", width, height, pos_x, pos_y);

    if (!x11_allocate_readback(backend, width, height, &surf_data->root_pixmap, &surf_data->ximage,
                               &surf_data->pixel_buffer)) {
        free(surf_data);
        free(surface);
        return NULL;
    }

    /* Create graphics context for copying rendered content. */
    surf_data->gc = XCreateGC(backend->x_display, surf_data->root_pixmap, 0, NULL);
    if (!surf_data->gc) {
        log_error("Failed to create X11 graphics context");
        x11_destroy_readback(backend->x_display, surf_data->root_pixmap, surf_data->ximage);
        free(surf_data);
        free(surface);
        return NULL;
    }

    /* Create a fullscreen window at the bottom of the stack */
    XSetWindowAttributes attrs;
    attrs.override_redirect = backend->override_redirect ? True : False;
    attrs.background_pixel  = BlackPixel(backend->x_display, backend->screen);
    attrs.border_pixel      = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask;

    surf_data->x_window =
        XCreateWindow(backend->x_display, backend->root_window, pos_x,
                      pos_y,            /* Position at the monitor's top-left within the screen */
                      width, height, 0, /* No border */
                      CopyFromParent,   /* depth */
                      InputOutput,      /* class */
                      CopyFromParent,   /* visual */
                      CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);

    if (!surf_data->x_window) {
        log_error("Failed to create X11 wallpaper window");
        XFreeGC(backend->x_display, surf_data->gc);
        x11_destroy_readback(backend->x_display, surf_data->root_pixmap, surf_data->ximage);
        free(surf_data);
        free(surface);
        return NULL;
    }

    /* Must precede XMapWindow: EWMH hints on an already-mapped window are not
     * read by the WM at map time. */
    x11_set_wallpaper_properties(backend, surf_data->x_window, pos_x, pos_y, width, height);

    /* Map and lower the window to bottom of stack */
    XMapWindow(backend->x_display, surf_data->x_window);
    XLowerWindow(backend->x_display, surf_data->x_window);

    /* Only meaningful without a WM. A managed window may be reparented into a
     * frame, so the root's children are the WM's frames - raising them all would
     * fight the WM's own stacking of the desktop window. */
    if (backend->override_redirect) {
        /* Raise all other windows above this one */
        Window root_return, parent_return;
        Window *children       = NULL;
        unsigned int nchildren = 0;

        if (XQueryTree(backend->x_display, backend->root_window, &root_return, &parent_return,
                       &children, &nchildren)) {
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
    }

    /* The root-window background pixmap (for Conky-style pseudo-transparency)
     * is a single screen-wide concept; only the surface anchored at the origin
     * owns it. Secondary monitors render their own window and skip this so they
     * don't paint just their slice onto the whole root. */
    surf_data->owns_root_pixmap = (pos_x == 0 && pos_y == 0);

    /* The pixmap is deliberately NOT made the root's background here. The X
     * protocol leaves the initial contents of a pixmap undefined (XCreatePixmap
     * does not clear it), so publishing it now and clearing the root would put
     * undefined server memory on the screen.
     *
     * That matters because the root's background is exactly what is visible while
     * the wallpaper window is not: on the managed path the window is mapped by the
     * window manager, in another process, so there is a gap between creating the
     * window and it becoming viewable. Publishing is therefore deferred to the
     * first x11_commit_surface that has actually rendered into the pixmap
     * (x11_publish_root_pixmap). Until then the root keeps the background it
     * already had. */

    XFlush(backend->x_display);

    surf_data->mapped        = true;
    surf_data->native_window = (EGLNativeWindowType)surf_data->x_window;

    /* Initialize surface structure */
    surface->native_surface =
        (void *)(uintptr_t)surf_data->x_window; /* X11 Window as opaque handle */
    surface->egl_window      = NULL;            /* X11 uses window directly, not wl_egl_window */
    surface->egl_surface     = EGL_NO_SURFACE;
    surface->native_output   = NULL;
    surface->x               = pos_x;
    surface->y               = pos_y;
    surface->width           = width;
    surface->height          = height;
    surface->scale           = 1;
    surface->config          = *config;
    surface->configured      = true; /* X11 windows are immediately configured */
    surface->committed       = false;
    surface->backend_data    = surf_data;
    surface->backend         = (struct compositor_backend *)backend;
    surface->tearing_control = NULL;

    log_info("X11 surface created successfully: window 0x%lx", surf_data->x_window);

    return surface;
}

/* ============================================================================
 * SURFACE DESTRUCTION
 * ============================================================================ */

void x11_destroy_surface(struct compositor_surface *surface) {
    if (!surface) return;

    x11_surface_data_t *surf_data = surface->backend_data;
    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;

    log_debug("Destroying X11 surface");

    if (surf_data) {
        if (surf_data->egl_surface != EGL_NO_SURFACE) {
            /* EGL surface cleanup handled by caller */
            surf_data->egl_surface = EGL_NO_SURFACE;
        }

        /* Destroy the wallpaper window first, then its client/server-side
         * support objects in strict reverse creation order. */
        if (surf_data->x_window && backend && backend->x_display) {
            XDestroyWindow(backend->x_display, surf_data->x_window);
        }

        if (backend && backend->x_display) {
            if (surf_data->gc) {
                XFreeGC(backend->x_display, surf_data->gc);
            }
            if (surf_data->root_pixmap &&
                backend->published_root_pixmap == surf_data->root_pixmap) {
                x11_unpublish_root_pixmap(backend);
            }
            x11_destroy_readback(backend->x_display, surf_data->root_pixmap, surf_data->ximage);
            XFlush(backend->x_display);
        } else if (surf_data->ximage) {
            /* XDestroyImage is local and remains responsible for image data. */
            XDestroyImage(surf_data->ximage);
        }

        free(surf_data);
    }

    free(surface);

    log_debug("X11 surface destroyed");
}

/* ============================================================================
 * SURFACE CONFIGURATION
 * ============================================================================ */

bool x11_configure_surface(struct compositor_surface *surface,
                           const compositor_surface_config_t *config) {
    if (!surface || !config) return false;

    x11_surface_data_t *surf_data = surface->backend_data;
    /* surface->backend is the compositor_backend wrapper (compositor_surface.c
     * re-points it after create_surface returns); the X11 state is its ->data. */
    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;

    if (!surf_data || !backend || !backend->x_display) return false;

    log_debug("Configuring X11 surface");

    /* Move/resize the monitor window as one X request so RandR geometry
     * reconciliation cannot expose an intermediate position/size pair. */
    int32_t target_x = config->x;
    int32_t target_y = config->y;
    int32_t target_w = config->width > 0 ? config->width : surface->width;
    int32_t target_h = config->height > 0 ? config->height : surface->height;
    if (!x11_valid_surface_size(target_w, target_h)) {
        log_error("Invalid X11 configure size: %dx%d", target_w, target_h);
        return false;
    }

    bool size_changed = surface->width != target_w || surface->height != target_h;
    if (size_changed && !x11_replace_readback(surface, backend, surf_data, target_w, target_h)) {
        return false;
    }

    if (surface->x != target_x || surface->y != target_y || size_changed) {
        XMoveResizeWindow(backend->x_display, surf_data->x_window, target_x, target_y,
                          (unsigned int)target_w, (unsigned int)target_h);
        surface->x = target_x;
        surface->y = target_y;
        log_debug("Moved/resized X11 window to %dx%d at +%d+%d", target_w, target_h, target_x,
                  target_y);
    }

    /* Commit configuration only after every replacement allocation succeeds. */
    surface->config = *config;

    /* Ensure window stays at bottom of stack */
    XLowerWindow(backend->x_display, surf_data->x_window);
    XFlush(backend->x_display);

    return true;
}

/* ============================================================================
 * MOUSE TRACKING
 * ============================================================================ */

/* Distribute a root-space pointer position to every output, converting to
 * coordinates relative to each output's own origin. An output that the pointer
 * is not currently over gets (-1, -1), which render.c maps to that monitor's
 * center — so iMouse only tracks the real cursor on the monitor it's actually
 * on, and the others sit centered instead of all sharing one global coordinate.
 * Caller must NOT hold output_list_lock. */
void x11_distribute_mouse(x11_backend_data_t *backend, int root_x, int root_y) {
    pthread_rwlock_rdlock(&backend->state->output_list_lock);
    for (struct output_state *o = backend->state->outputs; o; o = o->next) {
        x11_rect_t r = {o->x_offset, o->y_offset, o->width, o->height};
        int lx, ly;
        x11_mouse_to_output(r, root_x, root_y, &lx, &ly);
        o->mouse_x = (float)lx;
        o->mouse_y = (float)ly;
    }
    pthread_rwlock_unlock(&backend->state->output_list_lock);
}

/* Update mouse position for all outputs by querying X11 pointer. One query
 * covers every output, and XQueryPointer is a round trip, so at most one per
 * frame interval even when several outputs commit. */
static void x11_update_mouse_position(x11_backend_data_t *backend) {
    if (!backend || !backend->x_display || !backend->state) return;

    static uint64_t last_query_us;
    uint64_t now = get_time_us();
    if (now - last_query_us < 4000) return; /* a 240 Hz frame */
    last_query_us = now;

    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask_return;

    if (!XQueryPointer(backend->x_display, backend->root_window, &root_return, &child_return,
                       &root_x, &root_y, &win_x, &win_y, &mask_return)) {
        return;
    }

    x11_distribute_mouse(backend, root_x, root_y);
}

/* ============================================================================
 * X11 EVENT HANDLING
 * ============================================================================ */

/* Get X11 connection file descriptor for event polling */

/* Handle X11 events (mouse, keyboard, etc.) */

/* ============================================================================
 * COMMIT SURFACE
 * ============================================================================ */

/* Does the root property `prop` still name the pixmap we put there? */
static bool x11_root_prop_names(x11_backend_data_t *backend, Atom prop, Pixmap pixmap) {
    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    if (XGetWindowProperty(backend->x_display, backend->root_window, prop, 0, 1, False, XA_PIXMAP,
                           &type, &format, &n, &rem, &data) != Success ||
        !data) {
        return false;
    }
    bool match = (type == XA_PIXMAP && format == 32 && n == 1 && *(Pixmap *)(void *)data == pixmap);
    XFree(data);
    return match;
}

/* Withdraw the root-pixmap advertisement before the pixmap it names is freed.
 *
 * XFreePixmap invalidates the XID at the protocol level immediately, so any
 * client that reads _XROOTPMAP_ID / ESETROOT_PMAP_ID after the free and before
 * the replacement surface republishes gets a dangling drawable. The window in
 * which that can happen is new: this backend now destroys and rebuilds its
 * windows mid-session (WM appears, WM dies), where before it created them once.
 *
 * Deleting the properties is the right shape rather than deferring the free
 * until the replacement has published, because:
 *   - "no root pixmap" is a state every pseudo-transparency client already
 *     handles: it is what the root looks like before any wallpaper setter has
 *     run. A dangling XID is not — it is a BadDrawable on the client's next
 *     XCopyArea, which is a class of bug the client cannot even see coming.
 *   - deferring the free has no safe end: if the replacement surface never
 *     commits (recreate failed, or we are shutting down) the pixmap is leaked in
 *     the server for the life of the connection, and the properties still name
 *     it. It trades a short-lived dangling XID for a permanent one plus a leak.
 *
 * The root's *background* is unaffected: the server holds its own reference to a
 * window's background-pixmap, so freeing the pixmap does not blank the root. (X
 * protocol, ChangeWindowAttributes: the background pixmap may be freed
 * immediately if no further explicit references to it are to be made.)
 *
 * Only deletes properties that still name our own pixmap, so a wallpaper setter
 * that took the root over while we were running keeps its advertisement. */
void x11_unpublish_root_pixmap(x11_backend_data_t *backend) {
    if (!backend->x_display || !backend->published_root_pixmap) {
        return;
    }

    Display *dpy = backend->x_display;
    Pixmap ours  = backend->published_root_pixmap;

    if (x11_root_prop_names(backend, backend->atom_xrootpmap_id, ours)) {
        XDeleteProperty(dpy, backend->root_window, backend->atom_xrootpmap_id);
    }
    if (x11_root_prop_names(backend, backend->atom_esetroot_pmap_id, ours)) {
        XDeleteProperty(dpy, backend->root_window, backend->atom_esetroot_pmap_id);
    }
    backend->published_root_pixmap = 0;

    /* Requests are processed in order on this connection, so the deletes (and
     * the PropertyNotify they generate) reach the server before the caller's
     * XFreePixmap. Flush so that holds even if we are on our way out. */
    XFlush(dpy);
}

/* Make the (now filled) root pixmap the root window's background and tell
 * pseudo-transparency clients about it.
 *
 * _XROOTPMAP_ID / ESETROOT_PMAP_ID name the pixmap for clients like Conky. They
 * are (re)written whenever the advertised pixmap ID changes — which it does on
 * every surface recreation, since each new surface gets a fresh XCreatePixmap —
 * so a client that reads them does not end up holding a stale ID. */
static void x11_publish_root_pixmap(x11_backend_data_t *backend, x11_surface_data_t *surf_data) {
    Display *dpy = backend->x_display;

    XSetWindowBackgroundPixmap(dpy, backend->root_window, surf_data->root_pixmap);
    XClearWindow(dpy, backend->root_window);

    if (backend->published_root_pixmap != surf_data->root_pixmap) {
        XChangeProperty(dpy, backend->root_window, backend->atom_xrootpmap_id, XA_PIXMAP, 32,
                        PropModeReplace, (unsigned char *)&surf_data->root_pixmap, 1);
        XChangeProperty(dpy, backend->root_window, backend->atom_esetroot_pmap_id, XA_PIXMAP, 32,
                        PropModeReplace, (unsigned char *)&surf_data->root_pixmap, 1);
        backend->published_root_pixmap = surf_data->root_pixmap;
    }

    /* Poke Conky and friends: the background changed. */
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type             = PropertyNotify;
    event.xproperty.window = backend->root_window;
    event.xproperty.atom   = backend->atom_xrootpmap_id;
    event.xproperty.state  = PropertyNewValue;
    XSendEvent(dpy, backend->root_window, False, PropertyChangeMask, &event);
}

void x11_commit_surface(struct compositor_surface *surface) {
    if (!surface) return;

    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;

    if (!backend || !backend->x_display) return;

    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return;

    /* Update mouse position for shader uniforms (only if state is initialized) */
    if (backend->state && backend->state->outputs &&
        atomic_load_explicit(&backend->state->mouse_interaction, memory_order_acquire)) {
        x11_update_mouse_position(backend);
    }

    /* Copy the OpenGL rendered content to the root pixmap for Conky pseudo-transparency.
     * Only the origin surface owns the screen-wide root pixmap; secondary monitors
     * just present their own window. This glReadPixels stalls the GPU pipeline, so
     * throttle to 1 FPS for live shader wallpapers. Static images update immediately. */
    if (surf_data->owns_root_pixmap && surf_data->root_pixmap && surf_data->gc &&
        surf_data->pixel_buffer && surf_data->ximage) {
        static uint64_t last_pixmap_update = 0;
        uint64_t now                       = get_time_ms();
        bool is_shader = surface->config.layer == COMPOSITOR_LAYER_BACKGROUND && backend->state &&
                         backend->state->outputs &&
                         backend->state->outputs->config->type == WALLPAPER_SHADER;
        bool should_update = !is_shader || (now - last_pixmap_update >= 1000);

        if (should_update) {
            last_pixmap_update = now;
            int row_size       = surface->width * 4;

            /* Read pixels in GL_BGRA format — matches X11's native byte order,
             * eliminating the per-pixel RGBA→BGRA channel swap loop entirely */
            glReadPixels(0, 0, surface->width, surface->height, GL_BGRA, GL_UNSIGNED_BYTE,
                         surf_data->pixel_buffer);

            /* Flip image vertically in-place (OpenGL origin is bottom-left, X11 is top-left) */
            {
                unsigned char temp_row[32768]; /* Covers up to 8K displays */
                int chunk = row_size <= (int)sizeof(temp_row) ? row_size : (int)sizeof(temp_row);
                for (int y = 0; y < surface->height / 2; y++) {
                    unsigned char *top = surf_data->pixel_buffer + (y * row_size);
                    unsigned char *bottom =
                        surf_data->pixel_buffer + ((surface->height - 1 - y) * row_size);
                    for (int off = 0; off < row_size; off += chunk) {
                        int len = (row_size - off < chunk) ? (row_size - off) : chunk;
                        memcpy(temp_row, top + off, len);
                        memcpy(top + off, bottom + off, len);
                        memcpy(bottom + off, temp_row, len);
                    }
                }
            }

            /* Put image data to pixmap */
            XPutImage(backend->x_display, surf_data->root_pixmap, surf_data->gc, surf_data->ximage,
                      0, 0, 0, 0, surface->width, surface->height);

            /* The pixmap now holds real rendered content, so it is safe to be the
             * root's background. On the first commit after a surface is created this
             * is what publishes it (x11_create_surface deliberately does not). */
            x11_publish_root_pixmap(backend, surf_data);
        }
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

bool x11_create_egl_window(struct compositor_surface *surface, int32_t width, int32_t height) {
    if (!surface) return false;

    x11_surface_data_t *surf_data = surface->backend_data;

    if (!surf_data || !surface->backend) return false;
    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;
    if (!backend || !backend->x_display || !x11_valid_surface_size(width, height)) {
        log_error("Invalid X11 EGL window size: %dx%d", width, height);
        return false;
    }

    if ((surface->width != width || surface->height != height) &&
        !x11_replace_readback(surface, backend, surf_data, width, height)) {
        return false;
    }

    log_debug("Creating EGL surface for X11 window");

    /* X11 windows are used directly with EGL - no separate EGL window object */
    /* The native window handle is already set in surf_data->native_window */

    /* EGL surface creation is handled by the EGL subsystem using the native window */
    /* This function just prepares the surface for EGL usage */

    surface->width  = width;
    surface->height = height;

    log_debug("X11 EGL window prepared: native handle 0x%lx",
              (unsigned long)surf_data->native_window);

    return true;
}

/* ============================================================================
 * EGL WINDOW DESTRUCTION
 * ============================================================================ */

void x11_destroy_egl_window(struct compositor_surface *surface) {
    if (!surface) return;

    log_debug("Destroying X11 EGL window");

    /* X11 doesn't have separate EGL window objects - cleanup handled elsewhere */
}

/* ============================================================================
 * EGL WINDOW RESIZE
 * ============================================================================ */

bool x11_resize_egl_window(struct compositor_surface *surface, int32_t width, int32_t height) {
    if (!surface) return false;

    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return false;

    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;
    if (!backend || !backend->x_display) return false;

    if (!x11_valid_surface_size(width, height)) {
        log_error("Invalid X11 resize size: %dx%d", width, height);
        return false;
    }

    if ((surface->width != width || surface->height != height) &&
        !x11_replace_readback(surface, backend, surf_data, width, height)) {
        return false;
    }

    XResizeWindow(backend->x_display, surf_data->x_window, (unsigned int)width,
                  (unsigned int)height);
    XFlush(backend->x_display);

    return true;
}

/* ============================================================================
 * GET NATIVE WINDOW
 * ============================================================================ */

EGLNativeWindowType x11_get_native_window(struct compositor_surface *surface) {
    if (!surface) return (EGLNativeWindowType)0;

    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return (EGLNativeWindowType)0;

    return surf_data->native_window;
}
