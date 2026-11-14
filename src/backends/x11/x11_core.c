/* The X11 backend: open the display, the ops table, and the event loop side
 * (fd, dispatch, RandR and property events). See x11_internal.h. */

#include "x11_internal.h"

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

    /* Desktop placement + name, for the managed path. */
    backend->atom_net_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    backend->atom_net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    backend->atom_utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

    /* Interned once, not per-probe: the probe re-runs on every root
     * PropertyNotify naming these atoms. */
    backend->atom_net_supporting_wm_check =
        XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    backend->atom_net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);

    /* Root-pixmap advertisement. Interned here, with the rest, rather than being
     * cached in a function-static: atom IDs are per-server, and the backend is
     * re-initialised against a fresh Display if the X server restarts. */
    backend->atom_xrootpmap_id = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    backend->atom_esetroot_pmap_id = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

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
void x11_get_screen_dimensions(x11_backend_data_t *backend, int *width, int *height) {
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

/* Default X error handler: log, rate-limited, and carry on. */
static int x11_log_error_handler(Display *dpy, XErrorEvent *e) {
    static uint64_t last_ms;
    static unsigned suppressed;
    uint64_t now = get_time_ms();
    if (now - last_ms < 1000) {
        suppressed++;
        return 0;
    }
    char text[128] = {0};
    XGetErrorText(dpy, e->error_code, text, sizeof(text));
    log_warn("X11 error: %s (request %u.%u, resource 0x%lx)%s", text, e->request_code,
             e->minor_code, e->resourceid, suppressed ? " (more suppressed)" : "");
    last_ms = now;
    suppressed = 0;
    return 0;
}

static void *x11_backend_init(struct neowall_state *state) {
    log_info("Initializing X11 backend for tiling window managers");

    x11_backend_data_t *backend = calloc(1, sizeof(x11_backend_data_t));
    if (!backend) {
        log_error("Failed to allocate X11 backend data");
        return NULL;
    }

    backend->state = state;

    /* Xlib's default error handler exits the process. A wallpaper daemon
     * should log a stray protocol error (a BadMatch on a resize, a window
     * that went away) and keep drawing. The WM probe and the occlusion poll
     * still swap in their own handlers and restore this one. A lost
     * connection is an I/O error, which stays fatal. */
    XSetErrorHandler(x11_log_error_handler);

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

    /* Watch the root for _NET_SUPPORTING_WM_CHECK appearing or changing, so a
     * window manager that starts *after* us is noticed. A wallpaper daemon is
     * autostarted at login and routinely wins that race; probing once at init and
     * never looking again would leave us override-redirect for the whole session,
     * painting over every window the WM later maps.
     *
     * No other code in the process selects a core event mask on the root
     * (XRRSelectInput sets the RandR extension's own mask, which is separate), so
     * this does not clobber anyone else's selection. */
    XSelectInput(backend->x_display, backend->root_window, PropertyChangeMask);

    /* Probe the WM; every per-monitor window is created the same way. Re-run on
     * the root PropertyNotify above (see x11_recheck_wm). */
    backend->override_redirect = x11_use_override_redirect(backend);

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

    /* Free the outputs we created (each unref destroys its surface, so the
     * display must still be open). The Wayland backend does the same in
     * wayland_cleanup(). */
    struct neowall_state *state = backend->state;
    if (state) output_list_clear(state, output_unref);

    if (backend->x_display) {
        /* Shutdown does not tear surfaces down one by one — main() cleans up EGL
         * and then closes the display — so this is the only place the root-pixmap
         * advertisement gets withdrawn on exit. Without it, closing the connection
         * destroys the pixmap (X protocol: a client's resources are freed at
         * connection close unless the close-down mode retains them) and leaves
         * _XROOTPMAP_ID / ESETROOT_PMAP_ID naming a dead XID for the next reader. */
        x11_unpublish_root_pixmap(backend);

        XCloseDisplay(backend->x_display);
        backend->x_display = NULL;
    }

    free(backend);
}

/* ============================================================================
 * CAPABILITIES
 * ============================================================================ */

static compositor_capabilities_t x11_get_capabilities(void *backend_data) {
    (void)backend_data;

    /* X11 capabilities are limited compared to Wayland layer-shell */
    return COMPOSITOR_CAP_MULTI_OUTPUT |  /* XRandR provides multi-monitor */
           COMPOSITOR_CAP_OCCLUSION;
}

/* ----- occlusion (EWMH + XRandR) ----- */

static bool x11_occ_init(void *backend_data, struct neowall_state *state) {
    x11_backend_data_t *b = backend_data;
    if (!b || !b->x_display) {
        return false;
    }
    return x11_occlusion_init(&b->occ, b->x_display, state);
}

static void x11_occ_update(void *backend_data, struct neowall_state *state) {
    x11_backend_data_t *b = backend_data;
    (void)state;
    if (b) x11_occlusion_update(&b->occ);
}

static void x11_occ_cleanup(void *backend_data) {
    x11_backend_data_t *b = backend_data;
    if (b) x11_occlusion_cleanup(&b->occ);
}

/* ============================================================================
 * OUTPUT MANAGEMENT
 * ============================================================================ */

static void x11_on_output_added(void *backend_data, void *output) {
    (void)backend_data;
    (void)output;

    /* X11 output management handled via XRandR events */
    log_debug("X11 output added (handled via XRandR)");
}

static void x11_on_output_removed(void *backend_data, void *output) {
    (void)backend_data;
    (void)output;

    /* X11 output management handled via XRandR events */
    log_debug("X11 output removed (handled via XRandR)");
}

static void x11_damage_surface(struct compositor_surface *surface,
                              int32_t x, int32_t y, int32_t width, int32_t height) {
    /* X11 doesn't require explicit damage marking - handled by X server */
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void x11_set_scale(struct compositor_surface *surface, int32_t scale) {
    /* X11 scaling is handled differently - store for reference but no direct API */
    if (!surface) {
        return;
    }
    /* Scale is stored in surface->scale by the caller */
    (void)scale;
}

/* ============================================================================
 * EVENT HANDLING OPERATIONS
 * ============================================================================ */

static int x11_get_fd(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return -1;
    }
    return ConnectionNumber(backend->x_display);
}

static bool x11_prepare_events(void *backend_data) {
    /* X11 doesn't require prepare step like Wayland */
    (void)backend_data;
    return true;
}

static bool x11_read_events(void *backend_data) {
    /* X11 events are processed in dispatch */
    (void)backend_data;
    return true;
}

/* True if Xlib is already holding events for us.
 *
 * The event loop polls the connection fd, but Xlib does not hand events straight
 * from the socket to poll(): it queues them in userspace, and any Xlib call that
 * round-trips (XSync in the WM probe, XFlush in every commit) will drain the
 * socket into that queue as a side effect. The fd then has nothing to report and
 * poll() sleeps for its full timeout while the events sit in the queue.
 *
 * XPending() reports the queue length, flushing and reading the socket first if
 * the queue is empty, so it is the right question to ask just before poll(). */
static bool x11_has_pending_events(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return false;
    }
    return XPending(backend->x_display) > 0;
}

bool x11_dispatch_events(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return false;
    }

    bool layout_changed = false;
    bool wm_check_changed = false;
    bool mouse_on = backend->state &&
        atomic_load_explicit(&backend->state->mouse_interaction, memory_order_acquire);

    /* Process all pending X11 events. This runs on the event-loop thread, so it
     * is safe to mutate the output list / EGL state from here. */
    while (XPending(backend->x_display) > 0) {
        XEvent event;
        XNextEvent(backend->x_display, &event);

        switch (event.type) {
            case ButtonPress:
            case ButtonRelease:
                if (mouse_on) {
                    x11_distribute_mouse(backend, event.xbutton.x_root, event.xbutton.y_root);
                }
                break;

            case MotionNotify:
                if (mouse_on) {
                    x11_distribute_mouse(backend, event.xmotion.x_root, event.xmotion.y_root);
                }
                break;

            case Expose:
                /* The window's contents were discarded and must be redrawn. Only
                 * the last event of a burst matters (count == 0 = no more to come). */
                if (event.xexpose.count == 0) {
                    x11_mark_window_dirty(backend, event.xexpose.window);
                }
                break;

            case PropertyNotify:
                /* A window manager starting (or cleanly exiting) rewrites
                 * _NET_SUPPORTING_WM_CHECK on the root. Coalesce a burst — a WM
                 * start can touch the property more than once — into one re-probe
                 * after the queue drains.
                 *
                 * The atom test is load-bearing, not just an optimisation:
                 * x11_commit_surface XSendEvents a synthetic PropertyNotify for
                 * _XROOTPMAP_ID to the root (to poke Conky) once a second, and now
                 * that we select PropertyChangeMask on the root we receive our own
                 * event back.
                 *
                 * _NET_SUPPORTED re-probes too: the managed choice requires
                 * _NET_WM_WINDOW_TYPE_DESKTOP to be advertised there, and a
                 * starting WM's write order between the two root properties is
                 * not specified — keying on the handshake alone could read
                 * _NET_SUPPORTED before the WM has set it and latch
                 * override-redirect for the session. */
                if (event.xproperty.window == backend->root_window &&
                    (event.xproperty.atom == backend->atom_net_supporting_wm_check ||
                     event.xproperty.atom == backend->atom_net_supported)) {
                    wm_check_changed = true;
                }
                break;

            case DestroyNotify:
                /* The WM's _NET_SUPPORTING_WM_CHECK window has been destroyed. A
                 * WM that is killed outright (kill -9, crash) does not clean up the
                 * root property, so this is the only notification we get that it
                 * has gone; waiting for a PropertyNotify would wait for ever. */
                if (backend->wm_check_window != None &&
                    event.xdestroywindow.window == backend->wm_check_window) {
                    wm_check_changed = true;
                }
                break;

            default:
                /* RRScreenChangeNotify: monitor layout changed. Coalesce a burst
                 * of notifies (mode set fires several) into one reconcile after
                 * the queue drains. */
                if (backend->has_xrandr &&
                    event.type == backend->xrandr_event_base + RRScreenChangeNotify) {
                    XRRUpdateConfiguration(&event);
                    layout_changed = true;
                }
                break;
        }
    }

    if (wm_check_changed) {
        x11_recheck_wm(backend);
    }

    /* Deferred WM-went-away rebuild. Checked every iteration (the event loop polls
     * with at most a 1s timeout), not on a busy poll of the X server. */
    x11_check_wm_gone_deadline(backend);

    if (layout_changed) {
        log_info("X11: monitor layout changed, reconciling outputs");
        x11_reconcile_outputs(backend);
    }
    return true;
}

static compositor_flush_result_t x11_flush(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return COMPOSITOR_FLUSH_FATAL;
    }

    XFlush(backend->x_display);
    return COMPOSITOR_FLUSH_OK;
}

static void x11_cancel_read(void *backend_data) {
    /* X11 doesn't need cancel_read */
    (void)backend_data;
}

static int x11_get_error(void *backend_data) {
    /* X11 errors are handled via error handlers, not return values */
    (void)backend_data;
    return 0;
}

static bool x11_sync(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return false;
    }

    /* Sync with X server - equivalent to Wayland roundtrip */
    XSync(backend->x_display, False);
    return true;
}

static void *x11_get_native_display(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend) {
        return NULL;
    }
    return backend->x_display;
}

static EGLenum x11_get_egl_platform(void *backend_data) {
    (void)backend_data;
    return EGL_PLATFORM_X11_KHR;
}

static const compositor_backend_ops_t x11_backend_ops = {
    .init = x11_backend_init,
    .cleanup = x11_backend_cleanup,
    .create_surface = x11_create_surface,
    .destroy_surface = x11_destroy_surface,
    .configure_surface = x11_configure_surface,
    .commit_surface = x11_commit_surface,
    .create_egl_window = x11_create_egl_window,
    .destroy_egl_window = x11_destroy_egl_window,
    .resize_egl_window = x11_resize_egl_window,
    .get_native_window = x11_get_native_window,
    .get_capabilities = x11_get_capabilities,
    .on_output_added = x11_on_output_added,
    .on_output_removed = x11_on_output_removed,
    .damage_surface = x11_damage_surface,
    .set_scale = x11_set_scale,
    .init_outputs = x11_init_outputs,
    /* Event handling operations */
    .get_fd = x11_get_fd,
    .prepare_events = x11_prepare_events,
    .has_pending_events = x11_has_pending_events,
    .read_events = x11_read_events,
    .dispatch_events = x11_dispatch_events,
    .flush = x11_flush,
    .cancel_read = x11_cancel_read,
    .get_error = x11_get_error,
    .sync = x11_sync,
    /* Display/EGL operations */
    .get_native_display = x11_get_native_display,
    .get_egl_platform = x11_get_egl_platform,
    /* Occlusion detection */
    .occlusion_init = x11_occ_init,
    .occlusion_update = x11_occ_update,
    .occlusion_cleanup = x11_occ_cleanup,
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
