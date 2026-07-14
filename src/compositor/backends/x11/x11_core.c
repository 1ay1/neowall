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

/* GL_BGRA is not in GLES3/gl3.h but is available on desktop GL (OpenGL 1.2+).
 * Used for efficient pixel readback matching X11's native BGRA byte order. */
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#include "neowall/compositor/compositor.h"
#include "neowall/neowall.h"
#include "neowall/egl/egl_core.h"
#include "neowall/config/config.h"
#include "x11_occlusion.h"
#include "x11_geometry.h"
#include "x11_wm_detect.h"
#include "neowall/output/output.h"

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
    Atom atom_net_wm_desktop;
    Atom atom_net_wm_name;
    Atom atom_utf8_string;
    Atom atom_net_supporting_wm_check;
    Atom atom_net_supported;

    /* Root-pixmap advertisement, for pseudo-transparency clients. */
    Atom atom_xrootpmap_id;
    Atom atom_esetroot_pmap_id;

    /* The pixmap XID currently advertised by _XROOTPMAP_ID / ESETROOT_PMAP_ID
     * because we put it there. 0 when we have advertised nothing (or have since
     * withdrawn it). Tracked on the backend, not the surface, because the
     * properties are a screen-wide singleton that outlives any one surface: a
     * window rebuild destroys the surface that published, and the replacement
     * must not be able to leave the root naming a pixmap that no longer exists. */
    Pixmap published_root_pixmap;

    /* XRandR support */
    bool has_xrandr;
    int xrandr_event_base;
    int xrandr_error_base;

    /* How every per-monitor window is currently created. Resolved at init, and
     * revised if a window manager appears later (see x11_recheck_wm). */
    bool override_redirect;

    /* NEOWALL_X11_OVERRIDE_REDIRECT pinned the choice: never auto-revise it. */
    bool or_env_forced;

    /* Result of the last WM probe: a live EWMH WM that advertises
     * _NET_WM_WINDOW_TYPE_DESKTOP in _NET_SUPPORTED, i.e. one we should hand a
     * managed window to. Cached so a PropertyNotify that does not change the
     * answer costs nothing. */
    bool wm_present;

    /* The last probe saw a live EWMH WM, whether or not it advertises desktop
     * support (dwm and i3 announce EWMH but tile/float DESKTOP windows). Only
     * consulted for logging, so a user can tell "no WM" from "your WM does not
     * advertise _NET_WM_WINDOW_TYPE_DESKTOP". */
    bool wm_alive;

    /* When the WM went away, the time (ms) at which we stop giving it the benefit
     * of the doubt and rebuild on the override-redirect path. 0 = nothing pending.
     * Debounces a WM restart, which flaps the property down and back up. */
    uint64_t wm_gone_deadline_ms;

    /* The window named by _NET_SUPPORTING_WM_CHECK at the last successful probe.
     * We select StructureNotifyMask on it so that its DestroyNotify tells us the
     * WM has gone: a WM killed outright (kill -9, crash) does not clean up the
     * root property, so waiting for a PropertyNotify would wait forever. */
    Window wm_check_window;

    bool occlusion_active;

    bool initialized;
} x11_backend_data_t;

/* Surface backend data */
typedef struct {
    Window x_window;
    EGLSurface egl_surface;
    EGLNativeWindowType native_window;
    bool mapped;
    bool owns_root_pixmap;  /* Only the origin (0,0) surface drives the root-window
                            * background pixmap for pseudo-transparency. Secondary
                            * monitors render their own window but must not clobber
                            * the shared root pixmap with just their slice. */
    Pixmap root_pixmap;  /* Pixmap set on root window for pseudo-transparency.
                          * Whether it is *advertised* is backend state, not surface
                          * state: see x11_backend_data_t.published_root_pixmap.
                          * Until the first commit fills it, it holds whatever
                          * XCreatePixmap handed back, which is undefined — so it is
                          * not advertised at create time. */
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

/* Defined with its publishing counterpart, below the commit path. Used by both
 * the surface-destroy path and backend cleanup. */
static void x11_unpublish_root_pixmap(x11_backend_data_t *backend);

/* ============================================================================
 * WINDOW MANAGER DETECTION
 * ============================================================================ */

/* XGetWindowProperty on the window named by a stale _NET_SUPPORTING_WM_CHECK
 * raises BadWindow, and Xlib's default error handler terminates the process.
 * Swallow errors for the duration of the probe.
 *
 * THREADING CONSTRAINT: XSetErrorHandler is process-global, not per-Display.
 * Xlib keeps exactly one handler pointer for the whole process, so the
 * save/install/restore dance below is only safe because nothing else in the
 * process can be making X requests concurrently: compositor_backend_init()
 * (and hence every call to this probe that runs at startup) completes before
 * the first pthread_create() in the codebase. The re-probe on the no-WM -> WM
 * edge runs later, but on the event-loop thread, which is the only thread that
 * touches this Display.
 *
 * If a future change spawns a thread that makes X requests, or moves backend
 * init after thread spawn, this becomes a race: the other thread's errors would
 * be swallowed for the probe's duration, and its own handler could be clobbered.
 * Either keep that invariant or move to a per-probe Display connection. */
static bool x11_probe_error;

static int x11_probe_error_handler(Display *dpy, XErrorEvent *err) {
    (void)dpy;
    (void)err;
    x11_probe_error = true;
    return 0;
}

/* Read one XA_WINDOW-typed property naming a single window. Returns true if the
 * property is present with the expected type/format/count, and writes the window
 * it names to *out. */
static bool x11_read_window_prop(Display *dpy, Window target, Atom prop_atom,
                                 unsigned long *out) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, target, prop_atom, 0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) != Success) {
        return false;
    }
    if (actual_type != XA_WINDOW || actual_format != 32 || nitems != 1 || !prop) {
        if (prop) XFree(prop);
        return false;
    }

    *out = (unsigned long)*(Window *)prop;
    XFree(prop);
    return true;
}

/* Fill obs->supported_prop_valid / obs->desktop_in_supported from the root's
 * _NET_SUPPORTED. A root property read cannot raise BadWindow, so this needs no
 * error handler. Bounded read: 4096 32-bit items is far beyond any real WM's
 * list (metacity's is under a hundred). */
static void x11_read_supported(x11_backend_data_t *backend, x11_wm_check_obs_t *obs) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(backend->x_display, backend->root_window,
                           backend->atom_net_supported, 0, 4096, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) != Success) {
        return;
    }
    if (actual_type != XA_ATOM || actual_format != 32 || nitems == 0 || !prop) {
        if (prop) XFree(prop);
        return;
    }

    obs->supported_prop_valid = true;
    Atom *atoms = (Atom *)prop;
    for (unsigned long i = 0; i < nitems; i++) {
        if (atoms[i] == backend->atom_net_wm_window_type_desktop) {
            obs->desktop_in_supported = true;
            break;
        }
    }
    XFree(prop);
}

/* True if a window manager we should hand a managed window to owns the screen:
 * a live EWMH WM (two-step _NET_SUPPORTING_WM_CHECK handshake, EWMH 1.5) that
 * advertises _NET_WM_WINDOW_TYPE_DESKTOP in _NET_SUPPORTED.
 *
 * This function gathers the facts from the X server; the decisions made from
 * them are x11_ewmh_wm_check_decide() / x11_ewmh_wm_manages_desktop(), in
 * x11_wm_detect.c, so they can be unit-tested without an X server. Also records
 * backend->wm_alive (liveness regardless of desktop support) for logging. */
static bool x11_ewmh_wm_present(x11_backend_data_t *backend) {
    Display *dpy = backend->x_display;
    Atom check = backend->atom_net_supporting_wm_check;

    x11_wm_check_obs_t obs;
    memset(&obs, 0, sizeof(obs));

    obs.root_prop_valid =
        x11_read_window_prop(dpy, backend->root_window, check, &obs.root_names);

    /* Only the second read can touch a window that may not exist, so only it
     * needs the error handler. */
    bool present = false;
    if (obs.root_prop_valid && obs.root_names != 0) {
        x11_read_supported(backend, &obs);

        x11_probe_error = false;
        XErrorHandler prev = XSetErrorHandler(x11_probe_error_handler);

        obs.child_prop_valid =
            x11_read_window_prop(dpy, (Window)obs.root_names, check, &obs.child_names);

        obs.x_error = x11_probe_error;
        present = x11_ewmh_wm_manages_desktop(&obs);

        /* Watch the WM's check window die. A WM that is killed outright leaves the
         * root property behind, so its death produces no PropertyNotify — this
         * DestroyNotify is the only event that tells us. Selecting input on
         * another client's window is fine (StructureNotify is not exclusive); it
         * races with the window being destroyed, hence the error handler, which is
         * still installed here.
         *
         * XSelectInput REPLACES this client's mask on the window. If a broken WM
         * ever named the root as its check window, a bare StructureNotifyMask here
         * would drop the PropertyChangeMask selected at init and blind the WM
         * tracker for the rest of the session — so keep it in that case. */
        if (present) {
            backend->wm_check_window = (Window)obs.root_names;
            long mask = StructureNotifyMask;
            if (backend->wm_check_window == backend->root_window) {
                mask |= PropertyChangeMask;
            }
            XSelectInput(dpy, backend->wm_check_window, mask);
        }

        XSync(dpy, False);  /* Flush any BadWindow before restoring the handler. */
        XSetErrorHandler(prev);

        /* A BadWindow at any point in the probe — including XSelectInput racing
         * the check window's destruction — means the WM is already gone. Fold it
         * into the observations and re-decide, so present and wm_alive below are
         * derived from one consistent set of facts. */
        obs.x_error = obs.x_error || x11_probe_error;
        present = x11_ewmh_wm_manages_desktop(&obs);
    }

    backend->wm_alive = x11_ewmh_wm_check_decide(&obs);
    if (!present) {
        backend->wm_check_window = None;
    }
    return present;
}

/* Decide how the per-monitor wallpaper windows are created, and record whether
 * the env var pinned that choice (which disables the later heal).
 *
 * NEOWALL_X11_OVERRIDE_REDIRECT=1 forces override-redirect, =0 forces a managed
 * window, as an escape hatch for WMs that mis-handle the auto-detected choice.
 * Any other value is a mistake and is reported rather than silently ignored. */
static bool x11_use_override_redirect(x11_backend_data_t *backend) {
    const char *env = getenv("NEOWALL_X11_OVERRIDE_REDIRECT");
    x11_or_env_t mode = x11_or_env_parse(env);

    if (mode == X11_OR_ENV_INVALID) {
        log_warn("X11: NEOWALL_X11_OVERRIDE_REDIRECT=\"%s\" is not recognised "
                 "(expected \"0\" for a managed window or \"1\" for "
                 "override-redirect) - ignoring it and auto-detecting", env);
    }

    backend->or_env_forced = x11_or_env_is_forced(mode);
    backend->wm_present = x11_ewmh_wm_present(backend);

    bool override_redirect = x11_or_decide(mode, backend->wm_present);

    if (backend->or_env_forced) {
        log_info("X11: NEOWALL_X11_OVERRIDE_REDIRECT=%s, forcing %s wallpaper window",
                 env, override_redirect ? "override-redirect" : "managed");
    } else if (backend->wm_present) {
        log_info("X11: EWMH window manager with _NET_WM_WINDOW_TYPE_DESKTOP "
                 "support detected - using managed wallpaper windows");
    } else if (backend->wm_alive) {
        log_info("X11: EWMH window manager detected, but its _NET_SUPPORTED does "
                 "not advertise _NET_WM_WINDOW_TYPE_DESKTOP - using "
                 "override-redirect wallpaper windows "
                 "(NEOWALL_X11_OVERRIDE_REDIRECT=0 forces managed)");
    } else {
        log_info("X11: no EWMH window manager - using override-redirect wallpaper "
                 "windows (will switch to managed if one appears)");
    }

    return override_redirect;
}

/* Declare the wallpaper window as the desktop, before it is mapped. EWMH requires
 * _NET_WM_WINDOW_TYPE to be set before the window is mapped, and the client may
 * only set _NET_WM_STATE directly while the window is unmapped; once mapped, state
 * changes must go through the WM via a _NET_WM_STATE client message.
 *
 * Everything here is set on both the managed and the override-redirect path. A
 * WM does not read properties off an override-redirect window, so on that path
 * they are inert — but they are also free, and setting them unconditionally
 * means the heal path (x11_recreate_surfaces) has one window-setup path to get
 * right instead of two. */
static void x11_set_wallpaper_properties(x11_backend_data_t *backend, Window window,
                                         int pos_x, int pos_y, int width, int height) {
    Display *dpy = backend->x_display;

    XChangeProperty(dpy, window, backend->atom_net_wm_window_type, XA_ATOM, 32,
                    PropModeReplace,
                    (unsigned char *)&backend->atom_net_wm_window_type_desktop, 1);

    Atom states[] = {
        backend->atom_net_wm_state_below,
        backend->atom_net_wm_state_sticky,
        backend->atom_net_wm_state_skip_taskbar,
        backend->atom_net_wm_state_skip_pager,
    };
    XChangeProperty(dpy, window, backend->atom_net_wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states,
                    (int)(sizeof(states) / sizeof(states[0])));

    /* EWMH 5.5 / 9.2: a desktop window belongs on every desktop, which is what
     * 0xFFFFFFFF means. _NET_WM_STATE_STICKY above is not the same thing — it
     * means "fixed relative to the viewport", i.e. it does not scroll with a
     * large desktop, and says nothing about which desktops the window is on. A
     * format-32 property is passed to Xlib as an array of long. */
    unsigned long all_desktops = 0xFFFFFFFFUL;
    XChangeProperty(dpy, window, backend->atom_net_wm_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&all_desktops, 1);

    /* ICCCM 4.1.2.5: WM_CLASS "must be present when the window leaves the
     * Withdrawn state". An override-redirect window is never managed, so it was
     * exempt; a managed wallpaper window is not. This is also the handle a user
     * needs to write a WM rule against us if our stacking misbehaves on their
     * WM (i3 `for_window [class="NeoWall"]`, KWin window rules, openbox
     * `<application class="NeoWall">`). */
    XClassHint class_hint = {
        .res_name = (char *)"neowall",   /* instance name */
        .res_class = (char *)"NeoWall",  /* class name */
    };
    XSetClassHint(dpy, window, &class_hint);

    /* WM_NAME (ICCCM, latin-1) and _NET_WM_NAME (EWMH, UTF-8). EWMH says a WM
     * must prefer _NET_WM_NAME where both are present; pagers and `xprop` read
     * either, so set both. */
    static const char wm_name[] = "NeoWall Wallpaper";
    XStoreName(dpy, window, wm_name);
    XChangeProperty(dpy, window, backend->atom_net_wm_name, backend->atom_utf8_string,
                    8, PropModeReplace, (const unsigned char *)wm_name,
                    (int)(sizeof(wm_name) - 1));

    /* ICCCM 4.1.2.3: for a managed window the geometry passed to XCreateWindow is
     * only a request, and without PPosition the WM is free to place the window by
     * its own policy. We create one window per monitor at that monitor's origin,
     * so being placed anywhere else puts the wrong wallpaper on the wrong screen.
     * PPosition/PSize say the position and size are ours, not the WM's.
     *
     * Deliberately no PMinSize/PMaxSize: the window is resized on RandR changes
     * (x11_configure_surface), and pinning min == max here would leave stale hints
     * fighting that. */
    XSizeHints size_hints;
    memset(&size_hints, 0, sizeof(size_hints));
    size_hints.flags = PPosition | PSize;
    size_hints.x = pos_x;
    size_hints.y = pos_y;
    size_hints.width = width;
    size_hints.height = height;
    XSetWMNormalHints(dpy, window, &size_hints);

    /* input=False: the wallpaper must never take keyboard focus from real apps. */
    XWMHints wm_hints;
    memset(&wm_hints, 0, sizeof(wm_hints));
    wm_hints.flags = InputHint;
    wm_hints.input = False;
    XSetWMHints(dpy, window, &wm_hints);
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

    /* Determine surface geometry. With a per-monitor layout the caller passes
     * the monitor's exact size and position; when those are unset we fall back
     * to the whole screen at the origin (single-output case). */
    int width = config->width > 0 ? config->width : screen_width;
    int height = config->height > 0 ? config->height : screen_height;
    int pos_x = config->x;
    int pos_y = config->y;

    log_debug("Creating X11 wallpaper window: %dx%d at +%d+%d", width, height, pos_x, pos_y);

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
    attrs.override_redirect = backend->override_redirect ? True : False;
    attrs.background_pixel = BlackPixel(backend->x_display, backend->screen);
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

    surf_data->x_window = XCreateWindow(
        backend->x_display,
        backend->root_window,
        pos_x, pos_y,  /* Position at the monitor's top-left within the screen */
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

    /* Must precede XMapWindow: EWMH hints on an already-mapped window are not
     * read by the WM at map time. */
    x11_set_wallpaper_properties(backend, surf_data->x_window,
                                 pos_x, pos_y, width, height);

    /* Map and lower the window to bottom of stack */
    XMapWindow(backend->x_display, surf_data->x_window);
    XLowerWindow(backend->x_display, surf_data->x_window);

    /* Only meaningful without a WM. A managed window may be reparented into a
     * frame, so the root's children are the WM's frames - raising them all would
     * fight the WM's own stacking of the desktop window. */
    if (backend->override_redirect) {
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

    surf_data->mapped = true;
    surf_data->native_window = (EGLNativeWindowType)surf_data->x_window;

    /* Initialize surface structure */
    surface->native_surface = (void *)(uintptr_t)surf_data->x_window;  /* X11 Window as opaque handle */
    surface->egl_window = NULL;  /* X11 uses window directly, not wl_egl_window */
    surface->egl_surface = EGL_NO_SURFACE;
    surface->native_output = NULL;
    surface->x = pos_x;
    surface->y = pos_y;
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
                /* Withdraw the advertisement first: after XFreePixmap the XID is
                 * invalid, and nothing republishes until the replacement surface's
                 * first rendered commit. */
                if (backend->published_root_pixmap == surf_data->root_pixmap) {
                    x11_unpublish_root_pixmap(backend);
                }
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
    /* surface->backend is the compositor_backend wrapper (compositor_surface.c
     * re-points it after create_surface returns); the X11 state is its ->data. */
    x11_backend_data_t *backend =
        surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;

    if (!surf_data || !backend || !backend->x_display) return false;

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
 * MOUSE TRACKING
 * ============================================================================ */

/* Distribute a root-space pointer position to every output, converting to
 * coordinates relative to each output's own origin. An output that the pointer
 * is not currently over gets (-1, -1), which render.c maps to that monitor's
 * center — so iMouse only tracks the real cursor on the monitor it's actually
 * on, and the others sit centered instead of all sharing one global coordinate.
 * Caller must NOT hold output_list_lock. */
static void x11_distribute_mouse(x11_backend_data_t *backend, int root_x, int root_y) {
    pthread_rwlock_rdlock(&backend->state->output_list_lock);
    for (struct output_state *o = backend->state->outputs; o; o = o->next) {
        x11_rect_t r = { o->x_offset, o->y_offset, o->width, o->height };
        int lx, ly;
        x11_mouse_to_output(r, root_x, root_y, &lx, &ly);
        o->mouse_x = (float)lx;
        o->mouse_y = (float)ly;
    }
    pthread_rwlock_unlock(&backend->state->output_list_lock);
}

/* Update mouse position for all outputs by querying X11 pointer */
static void x11_update_mouse_position(x11_backend_data_t *backend) {
    if (!backend || !backend->x_display || !backend->state) return;

    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask_return;

    /* Query the current pointer position relative to root window */
    if (!XQueryPointer(backend->x_display, backend->root_window,
                       &root_return, &child_return,
                       &root_x, &root_y, &win_x, &win_y,
                       &mask_return)) {
        return;  /* Query failed */
    }

    /* Debug: Log mouse position occasionally */
    static uint64_t last_mouse_log = 0;
    uint64_t now = get_time_ms();
    if (now - last_mouse_log > 2000) {
        log_debug("X11 mouse position: (%d, %d)", root_x, root_y);
        last_mouse_log = now;
    }

    x11_distribute_mouse(backend, root_x, root_y);
}

/* ============================================================================
 * X11 EVENT HANDLING
 * ============================================================================ */

/* Get X11 connection file descriptor for event polling */
static int x11_get_connection_fd(x11_backend_data_t *backend) {
    if (!backend || !backend->x_display) {
        return -1;
    }
    return ConnectionNumber(backend->x_display);
}

/* Handle X11 events (mouse, keyboard, etc.) */
static bool x11_handle_events(x11_backend_data_t *backend) {
    if (!backend || !backend->x_display) {
        return false;
    }

    /* Process all pending X11 events */
    while (XPending(backend->x_display) > 0) {
        XEvent event;
        XNextEvent(backend->x_display, &event);

        /* Skip mouse-related events entirely if the user disabled mouse
         * interaction — we still drain the event queue (we have to, the X
         * server won't unblock us otherwise) but we don't touch any output
         * state, so iMouse stays at its initial -1 (which render.c maps to
         * the screen center). */
        bool mouse_on = backend->state &&
            atomic_load_explicit(&backend->state->mouse_interaction, memory_order_acquire);

        switch (event.type) {
            case ButtonPress:
                log_debug("X11 mouse button pressed: button %d at (%d, %d)",
                         event.xbutton.button,
                         event.xbutton.x_root,
                         event.xbutton.y_root);

                if (!mouse_on) break;
                x11_distribute_mouse(backend, event.xbutton.x_root, event.xbutton.y_root);
                break;

            case ButtonRelease:
                log_debug("X11 mouse button released: button %d at (%d, %d)",
                         event.xbutton.button,
                         event.xbutton.x_root,
                         event.xbutton.y_root);

                if (!mouse_on) break;
                x11_distribute_mouse(backend, event.xbutton.x_root, event.xbutton.y_root);
                break;

            case MotionNotify: {
                if (!mouse_on) break;
                /* Update mouse position for motion events
                 * Use throttling to avoid log spam */
                static uint64_t last_motion_log = 0;
                uint64_t now = get_time_ms();
                if (now - last_motion_log > 2000) {
                    log_debug("X11 mouse motion: (%d, %d)",
                             event.xmotion.x_root,
                             event.xmotion.y_root);
                    last_motion_log = now;
                }

                x11_distribute_mouse(backend, event.xmotion.x_root, event.xmotion.y_root);
                break;
            }

            case Expose:
                log_debug("X11 Expose event received");
                break;

            case ConfigureNotify:
                log_debug("X11 ConfigureNotify event: %dx%d",
                         event.xconfigure.width,
                         event.xconfigure.height);
                break;

            case ReparentNotify:
                log_debug("X11 ReparentNotify event");
                break;

            case MapNotify:
                log_debug("X11 MapNotify event");
                break;

            case UnmapNotify:
                log_debug("X11 UnmapNotify event");
                break;

            default:
                /* XRandR layout changes are handled in x11_dispatch_events
                 * (the live event pump). This legacy handler is unused. */
                break;
        }
    }

    /* Flush any pending requests */
    XFlush(backend->x_display);

    return true;
}

/* ============================================================================
 * COMMIT SURFACE
 * ============================================================================ */

/* Does the root property `prop` still name the pixmap we put there? */
static bool x11_root_prop_names(x11_backend_data_t *backend, Atom prop, Pixmap pixmap) {
    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    if (XGetWindowProperty(backend->x_display, backend->root_window, prop, 0, 1,
                           False, XA_PIXMAP, &type, &format, &n, &rem,
                           &data) != Success || !data) {
        return false;
    }
    bool match = (type == XA_PIXMAP && format == 32 && n == 1 &&
                  *(Pixmap *)(void *)data == pixmap);
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
static void x11_unpublish_root_pixmap(x11_backend_data_t *backend) {
    if (!backend->x_display || !backend->published_root_pixmap) {
        return;
    }

    Display *dpy = backend->x_display;
    Pixmap ours = backend->published_root_pixmap;

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
static void x11_publish_root_pixmap(x11_backend_data_t *backend,
                                    x11_surface_data_t *surf_data) {
    Display *dpy = backend->x_display;

    XSetWindowBackgroundPixmap(dpy, backend->root_window, surf_data->root_pixmap);
    XClearWindow(dpy, backend->root_window);

    if (backend->published_root_pixmap != surf_data->root_pixmap) {
        XChangeProperty(dpy, backend->root_window, backend->atom_xrootpmap_id,
                        XA_PIXMAP, 32, PropModeReplace,
                        (unsigned char *)&surf_data->root_pixmap, 1);
        XChangeProperty(dpy, backend->root_window, backend->atom_esetroot_pmap_id,
                        XA_PIXMAP, 32, PropModeReplace,
                        (unsigned char *)&surf_data->root_pixmap, 1);
        backend->published_root_pixmap = surf_data->root_pixmap;
    }

    /* Poke Conky and friends: the background changed. */
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = PropertyNotify;
    event.xproperty.window = backend->root_window;
    event.xproperty.atom = backend->atom_xrootpmap_id;
    event.xproperty.state = PropertyNewValue;
    XSendEvent(dpy, backend->root_window, False, PropertyChangeMask, &event);
}

static void x11_commit_surface(struct compositor_surface *surface) {
    if (!surface) return;

    x11_backend_data_t *backend = surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;

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
    if (surf_data->owns_root_pixmap &&
        surf_data->root_pixmap && surf_data->gc && surf_data->pixel_buffer && surf_data->ximage) {
        static uint64_t last_pixmap_update = 0;
        uint64_t now = get_time_ms();
        bool is_shader = surface->config.layer == COMPOSITOR_LAYER_BACKGROUND &&
                         backend->state && backend->state->outputs &&
                         backend->state->outputs->config->type == WALLPAPER_SHADER;
        bool should_update = !is_shader || (now - last_pixmap_update >= 1000);

        if (should_update) {
            last_pixmap_update = now;
            int row_size = surface->width * 4;

            /* Read pixels in GL_BGRA format — matches X11's native byte order,
             * eliminating the per-pixel RGBA→BGRA channel swap loop entirely */
            glReadPixels(0, 0, surface->width, surface->height, GL_BGRA, GL_UNSIGNED_BYTE,
                        surf_data->pixel_buffer);

            /* Flip image vertically in-place (OpenGL origin is bottom-left, X11 is top-left) */
            {
                unsigned char temp_row[32768];  /* Covers up to 8K displays */
                int chunk = row_size <= (int)sizeof(temp_row) ? row_size : (int)sizeof(temp_row);
                for (int y = 0; y < surface->height / 2; y++) {
                    unsigned char *top = surf_data->pixel_buffer + (y * row_size);
                    unsigned char *bottom = surf_data->pixel_buffer + ((surface->height - 1 - y) * row_size);
                    for (int off = 0; off < row_size; off += chunk) {
                        int len = (row_size - off < chunk) ? (row_size - off) : chunk;
                        memcpy(temp_row, top + off, len);
                        memcpy(top + off, bottom + off, len);
                        memcpy(bottom + off, temp_row, len);
                    }
                }
            }

            /* Put image data to pixmap */
            XPutImage(backend->x_display, surf_data->root_pixmap, surf_data->gc,
                     surf_data->ximage, 0, 0, 0, 0, surface->width, surface->height);

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

static bool x11_create_egl_window(struct compositor_surface *surface,
                                  int32_t width, int32_t height) {
    if (!surface) return false;

    x11_surface_data_t *surf_data = surface->backend_data;

    if (!surf_data || !surface->backend) return false;

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
 * EGL WINDOW RESIZE
 * ============================================================================ */

static bool x11_resize_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height) {
    if (!surface) return false;

    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return false;

    x11_backend_data_t *backend = surface->backend ? (x11_backend_data_t *)surface->backend->data : NULL;
    if (!backend || !backend->x_display) return false;

    /* Resize the X11 window */
    XResizeWindow(backend->x_display, surf_data->x_window,
                 (unsigned int)width, (unsigned int)height);
    XFlush(backend->x_display);

    return true;
}

/* ============================================================================
 * GET NATIVE WINDOW
 * ============================================================================ */

static EGLNativeWindowType x11_get_native_window(struct compositor_surface *surface) {
    if (!surface) return (EGLNativeWindowType)0;

    x11_surface_data_t *surf_data = surface->backend_data;
    if (!surf_data) return (EGLNativeWindowType)0;

    return surf_data->native_window;
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
    b->occlusion_active = x11_occlusion_init(b->x_display, state);
    return b->occlusion_active;
}

static void x11_occ_update(void *backend_data, struct neowall_state *state) {
    x11_backend_data_t *b = backend_data;
    if (b && b->occlusion_active) {
        x11_occlusion_update(state);
    }
}

static void x11_occ_cleanup(void *backend_data) {
    x11_backend_data_t *b = backend_data;
    if (b && b->occlusion_active) {
        x11_occlusion_cleanup();
        b->occlusion_active = false;
    }
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
 * BACKEND OPERATIONS TABLE
 * ============================================================================ */

/* Geometry of one physical monitor within the X screen. */
typedef struct {
    char name[64];   /* RandR connector name, e.g. "DVI-D-0" */
    int x, y;        /* Top-left within the screen */
    int width, height;
} x11_monitor_t;

/* Enumerate the active monitors of the X screen.
 *
 * Prefers XRandR 1.5 monitors (XRRGetMonitors) which already merges CRTCs into
 * logical monitors and carries real connector names. Falls back to walking
 * active CRTCs (RandR < 1.5), and finally to a single whole-screen monitor when
 * RandR is unavailable. Writes up to `max` entries and returns the count. */
static int x11_enumerate_monitors(x11_backend_data_t *backend, x11_monitor_t *mons, int max) {
    Display *dpy = backend->x_display;
    int count = 0;

    if (backend->has_xrandr) {
        int n = 0;
        XRRMonitorInfo *info = XRRGetMonitors(dpy, backend->root_window, True, &n);
        if (info && n > 0) {
            for (int i = 0; i < n && count < max; i++) {
                if (info[i].width <= 0 || info[i].height <= 0) continue;
                char *cn = info[i].name ? XGetAtomName(dpy, info[i].name) : NULL;
                snprintf(mons[count].name, sizeof(mons[count].name), "%.63s",
                         cn ? cn : "X11-0");
                if (cn) XFree(cn);
                mons[count].x = info[i].x;
                mons[count].y = info[i].y;
                mons[count].width = info[i].width;
                mons[count].height = info[i].height;
                count++;
            }
            XRRFreeMonitors(info);
            if (count > 0) return count;
        } else if (info) {
            XRRFreeMonitors(info);
        }

        /* Fallback: active CRTCs with their connector names (RandR < 1.5). */
        XRRScreenResources *res = XRRGetScreenResources(dpy, backend->root_window);
        if (res) {
            for (int i = 0; i < res->ncrtc && count < max; i++) {
                XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
                if (!ci) continue;
                if (ci->mode != None && ci->noutput > 0 && ci->width > 0 && ci->height > 0) {
                    const char *name = NULL;
                    XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, ci->outputs[0]);
                    if (oi && oi->name) name = oi->name;
                    snprintf(mons[count].name, sizeof(mons[count].name), "%.63s",
                             name ? name : "X11-0");
                    if (oi) XRRFreeOutputInfo(oi);
                    mons[count].x = ci->x;
                    mons[count].y = ci->y;
                    mons[count].width = ci->width;
                    mons[count].height = ci->height;
                    count++;
                }
                XRRFreeCrtcInfo(ci);
            }
            XRRFreeScreenResources(res);
            if (count > 0) return count;
        }
    }

    /* Last resort: one monitor covering the whole screen. */
    snprintf(mons[0].name, sizeof(mons[0].name), "X11-0");
    mons[0].x = 0;
    mons[0].y = 0;
    mons[0].width = DisplayWidth(dpy, backend->screen);
    mons[0].height = DisplayHeight(dpy, backend->screen);
    return 1;
}

/* Build one output_state for a monitor and link it into the global list. The
 * compositor surface is created lazily by the caller. Returns NULL on OOM. */
static struct output_state *x11_make_output(struct neowall_state *state,
                                            const x11_monitor_t *mon) {
    struct output_state *out = calloc(1, sizeof(struct output_state));
    if (!out) {
        log_error("Failed to allocate X11 output");
        return NULL;
    }

    out->state = state;
    out->native_output = NULL;  /* X11 doesn't use Wayland outputs */
    out->name = 0;
    /* model and connector_name both carry the real RandR name so per-output
     * config blocks (matched by either) work and `neowall current` is right. */
    snprintf(out->model, sizeof(out->model), "%.63s", mon->name);
    snprintf(out->connector_name, sizeof(out->connector_name), "%.63s", mon->name);

    out->x_offset = mon->x;
    out->y_offset = mon->y;
    out->pixel_width = mon->width;
    out->pixel_height = mon->height;
    out->width = mon->width;
    out->height = mon->height;
    out->logical_width = mon->width;
    out->logical_height = mon->height;
    out->scale = 1;
    out->configured = true;
    atomic_init(&out->refcount, 1);  /* the output list's reference */

    out->config = calloc(1, sizeof(struct wallpaper_config));
    if (!out->config) {
        log_error("Failed to allocate config for X11 output");
        free(out);
        return NULL;
    }

    /* Defaults; the real config is applied by config_load via output_apply_config. */
    out->config->mode = MODE_FILL;
    out->config->duration = 0;
    out->config->transition = TRANSITION_NONE;
    out->config->transition_duration = 300;
    out->config->cycle = false;
    out->config->cycle_paths = NULL;
    out->config->cycle_count = 0;
    out->config->current_cycle_index = 0;
    out->config->type = WALLPAPER_IMAGE;
    out->config->path[0] = '\0';
    out->config->shader_path[0] = '\0';
    out->config->shader_speed = 1.0f;
    out->config->shader_fps = 60;
    out->config->show_fps = false;
    out->config->channel_paths = NULL;
    out->config->channel_count = 0;

    out->preload_texture = 0;
    out->preload_image = NULL;
    out->preload_path[0] = '\0';
    atomic_store(&out->preload_ready, false);

    pthread_mutex_init(&out->preload_mutex, NULL);
    out->preload_decoded_image = NULL;
    atomic_store(&out->preload_thread_active, false);
    atomic_store(&out->preload_upload_pending, false);

    out->fps_last_log_time = 0;
    out->fps_frame_count = 0;
    out->fps_current = 0.0f;
    out->frame_timer_fd = -1;
    out->mouse_x = -1.0f;
    out->mouse_y = -1.0f;

    return out;
}

/* Create and attach the positioned wallpaper window for an output that has a
 * geometry but no compositor surface yet. Adopts the real surface geometry the
 * backend ends up with. Returns false (and leaves the output surface-less) on
 * failure. Caller must NOT hold output_list_lock. */
static bool x11_attach_surface(struct neowall_state *state, struct output_state *out) {
    compositor_surface_config_t surface_config = {
        .output = NULL,
        .x = out->x_offset,
        .y = out->y_offset,
        .width = out->width,
        .height = out->height,
        .layer = COMPOSITOR_LAYER_BACKGROUND,
        .anchor = COMPOSITOR_ANCHOR_FILL,
        .exclusive_zone = 0,
        .keyboard_interactivity = false,
    };

    out->compositor_surface =
        compositor_surface_create(state->compositor_backend, &surface_config);
    if (!out->compositor_surface) {
        return false;
    }

    /* Adopt the real surface geometry (the backend may have clamped). */
    out->width = out->compositor_surface->width;
    out->height = out->compositor_surface->height;
    out->pixel_width = out->compositor_surface->width;
    out->pixel_height = out->compositor_surface->height;
    out->logical_width = out->compositor_surface->width;
    out->logical_height = out->compositor_surface->height;
    return true;
}

/* Initialize outputs for X11 backend: one output + one positioned wallpaper
 * window per active monitor. */
static bool x11_init_outputs(void *backend_data, struct neowall_state *state) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !state) {
        return false;
    }

    x11_monitor_t mons[MAX_OUTPUTS];
    int n = x11_enumerate_monitors(backend, mons, MAX_OUTPUTS);
    log_info("X11: detected %d monitor(s)", n);

    int created = 0;
    for (int i = 0; i < n; i++) {
        log_info("  monitor %s: %dx%d at +%d+%d",
                 mons[i].name, mons[i].width, mons[i].height, mons[i].x, mons[i].y);

        struct output_state *out = x11_make_output(state, &mons[i]);
        if (!out) continue;

        /* Link into the output list under the write lock. */
        pthread_rwlock_wrlock(&state->output_list_lock);
        out->next = state->outputs;
        state->outputs = out;
        state->output_count++;
        pthread_rwlock_unlock(&state->output_list_lock);

        if (!x11_attach_surface(state, out)) {
            log_error("Failed to create compositor surface for monitor %s", mons[i].name);
            /* Unlink and drop the output we just added. */
            pthread_rwlock_wrlock(&state->output_list_lock);
            struct output_state **pp = &state->outputs;
            while (*pp && *pp != out) pp = &(*pp)->next;
            if (*pp == out) {
                *pp = out->next;
                if (state->output_count > 0) state->output_count--;
            }
            pthread_rwlock_unlock(&state->output_list_lock);
            output_unref(out);
            continue;
        }

        log_info("X11 output ready: %s (%dx%d at +%d+%d)",
                 out->model, out->width, out->height, out->x_offset, out->y_offset);
        created++;
    }

    if (created == 0) {
        log_error("X11: failed to create any outputs");
        return false;
    }

    return true;
}

/* ============================================================================
 * HOTPLUG RECONCILE (RRScreenChangeNotify)
 * ============================================================================ */

/* True if an output's geometry matches a freshly-enumerated monitor. */
static bool x11_mon_matches(const struct output_state *o, const x11_monitor_t *m) {
    if (strncmp(o->connector_name, m->name, sizeof(o->connector_name)) != 0) {
        return false;
    }
    x11_rect_t a = { o->x_offset, o->y_offset, o->width, o->height };
    x11_rect_t b = { m->x, m->y, m->width, m->height };
    return x11_rect_equal(a, b);
}

/* Bring a freshly-added output fully online: surface, EGL surface, GL render
 * state, and its wallpaper config. Runs on the event-loop thread, which owns the
 * EGL context, so the GL bring-up is safe inline. Caller must NOT hold
 * output_list_lock. Returns true on success. */
static bool x11_bring_output_online(struct neowall_state *state, struct output_state *out) {
    if (!x11_attach_surface(state, out)) {
        log_error("Hotplug: failed to create surface for %s", out->connector_name);
        return false;
    }
    if (out->width <= 0 || out->height <= 0) {
        log_error("Hotplug: %s has no dimensions", out->connector_name);
        return false;
    }
    if (!output_create_egl_surface(out) || !egl_core_make_current(state, out) ||
        !output_init_render(out)) {
        log_error("Hotplug: GL bring-up failed for %s", out->connector_name);
        return false;
    }
    if (config_apply_to_output(state, out)) {
        atomic_store_explicit(&out->needs_redraw, true, memory_order_release);
    }
    log_info("Hotplug: output %s online (%dx%d at +%d+%d)",
             out->connector_name, out->width, out->height, out->x_offset, out->y_offset);
    return true;
}

/* Reconcile the output list against the current monitor layout after an
 * RRScreenChangeNotify. Removes outputs whose monitor vanished or changed
 * geometry, and adds outputs for newly-appeared monitors. Idempotent: a
 * spurious notify with an unchanged layout is a no-op. Runs on the event-loop
 * thread. */
static void x11_reconcile_outputs(x11_backend_data_t *backend) {
    struct neowall_state *state = backend->state;
    if (!state) return;

    x11_monitor_t mons[MAX_OUTPUTS];
    int n = x11_enumerate_monitors(backend, mons, MAX_OUTPUTS);

    /* Pass 1: collect outputs whose monitor no longer matches (removed or
     * geometry-changed). Take a transient ref so the unlink+teardown below is
     * safe even if another thread also holds the output. */
    struct output_state *stale[MAX_OUTPUTS];
    int n_stale = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n_stale < MAX_OUTPUTS; o = o->next) {
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (x11_mon_matches(o, &mons[i])) { found = true; break; }
        }
        if (!found) {
            output_ref(o);
            stale[n_stale++] = o;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    /* Unlink + drop each stale output. output_unref frees it (surface + EGL +
     * render state) once the list's reference and our transient ref are gone. */
    for (int i = 0; i < n_stale; i++) {
        struct output_state *o = stale[i];
        log_info("Hotplug: removing output %s (%dx%d at +%d+%d)",
                 o->connector_name, o->width, o->height, o->x_offset, o->y_offset);
        pthread_rwlock_wrlock(&state->output_list_lock);
        struct output_state **pp = &state->outputs;
        while (*pp && *pp != o) pp = &(*pp)->next;
        bool unlinked = false;
        if (*pp == o) {
            *pp = o->next;
            if (state->output_count > 0) state->output_count--;
            unlinked = true;
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        if (unlinked) output_unref(o);  /* drop the list's reference */
        output_unref(o);                /* drop our transient ref */
    }

    /* Pass 2: add outputs for monitors that have no matching output yet. */
    for (int i = 0; i < n; i++) {
        bool present = false;
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            if (x11_mon_matches(o, &mons[i])) { present = true; break; }
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        if (present) continue;

        log_info("Hotplug: adding monitor %s: %dx%d at +%d+%d",
                 mons[i].name, mons[i].width, mons[i].height, mons[i].x, mons[i].y);
        struct output_state *out = x11_make_output(state, &mons[i]);
        if (!out) continue;

        pthread_rwlock_wrlock(&state->output_list_lock);
        out->next = state->outputs;
        state->outputs = out;
        state->output_count++;
        pthread_rwlock_unlock(&state->output_list_lock);

        if (!x11_bring_output_online(state, out)) {
            pthread_rwlock_wrlock(&state->output_list_lock);
            struct output_state **pp = &state->outputs;
            while (*pp && *pp != out) pp = &(*pp)->next;
            if (*pp == out) {
                *pp = out->next;
                if (state->output_count > 0) state->output_count--;
            }
            pthread_rwlock_unlock(&state->output_list_lock);
            output_unref(out);
        }
    }
}


/* ============================================================================
 * WM APPEARED LATE (heal override-redirect -> managed)
 * ============================================================================ */

/* Destroy and rebuild every wallpaper window with the current
 * backend->override_redirect.
 *
 * The window has to be *recreated*, not re-propertied: override_redirect is a
 * window attribute fixed at XCreateWindow time. XChangeWindowAttributes can set
 * it on a mapped window, but the WM decided whether to manage the window when it
 * saw the MapRequest (or, for an override-redirect window, when it saw the
 * MapNotify it was told to ignore), so flipping the attribute afterwards does not
 * retroactively hand the window to the WM.
 *
 * The EGL *context* is a single shared state->egl_context, not one per surface;
 * only the EGLSurface is tied to the native window. So destroying the EGLSurface
 * and the X window and rebuilding both keeps every GL object — textures, shader
 * programs, VAOs — alive in the context. No re-upload, no re-compile, and hence
 * no call to output_init_render() here (unlike the hotplug path, which is
 * building a brand-new output_state that has no render state yet).
 *
 * Runs on the event-loop thread, which owns the EGL context, so the GL work is
 * safe inline. Same thread and same teardown order as the hotplug reconcile.
 * Caller must NOT hold output_list_lock. */
static void x11_recreate_surfaces(x11_backend_data_t *backend) {
    struct neowall_state *state = backend->state;
    if (!state) return;

    /* Snapshot the list under the read lock, with a ref on each output; the
     * rebuild below runs without the lock. */
    struct output_state *outs[MAX_OUTPUTS];
    int n = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n < MAX_OUTPUTS; o = o->next) {
        output_ref(o);
        outs[n++] = o;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    for (int i = 0; i < n; i++) {
        struct output_state *o = outs[i];

        /* Release the EGL surface from this thread before destroying it. */
        egl_core_make_current(state, NULL);

        if (o->compositor_surface) {
            if (o->compositor_surface->egl_surface != EGL_NO_SURFACE) {
                compositor_surface_destroy_egl(o->compositor_surface, state->egl_display);
            }
            compositor_surface_destroy(o->compositor_surface);
            o->compositor_surface = NULL;
        }

        if (!x11_attach_surface(state, o) ||
            !output_create_egl_surface(o) ||
            !egl_core_make_current(state, o)) {
            log_error("X11: failed to recreate wallpaper window for %s",
                      o->connector_name);
            output_unref(o);
            continue;
        }

        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        log_info("X11: wallpaper window for %s recreated as %s",
                 o->connector_name,
                 backend->override_redirect ? "override-redirect" : "managed");
        output_unref(o);
    }
}

/* Mark the output that owns an X window for redraw.
 *
 * An X11 window does not retain its contents: when it is obscured, restacked,
 * reparented into (or out of) a WM frame, or remapped, whatever was drawn is
 * discarded and the server sends Expose. A shader wallpaper repaints every frame
 * anyway, but a static image is drawn once and then needs_redraw is cleared
 * (eventloop.c), so without this the window stays black for the rest of the
 * session. Observed on Xvfb: a static wallpaper goes black the moment the WM
 * takes the window over, and again when the WM dies. */
static void x11_mark_window_dirty(x11_backend_data_t *backend, Window window) {
    struct neowall_state *state = backend->state;
    if (!state) return;

    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (!o->compositor_surface) continue;
        x11_surface_data_t *sd = o->compositor_surface->backend_data;
        if (sd && sd->x_window == window) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            break;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);
}

/* Act on the current WM state: rebuild the wallpaper windows if the world no
 * longer matches how they were created. Idempotent. */
static void x11_apply_wm_transition(x11_backend_data_t *backend) {
    x11_wm_action_t action = x11_wm_transition(backend->or_env_forced,
                                               backend->override_redirect,
                                               backend->wm_present);
    switch (action) {
        case X11_WM_ACTION_RECREATE_MANAGED:
            log_info("X11: EWMH window manager appeared - recreating wallpaper "
                     "windows as managed");
            backend->override_redirect = false;
            x11_recreate_surfaces(backend);
            break;

        case X11_WM_ACTION_RECREATE_OVERRIDE:
            /* Observed on Xvfb (metacity, and a reparenting test WM): when the WM
             * dies the wallpaper window's contents are lost, and with a static
             * image needs_redraw has already been cleared, so nothing ever
             * repaints it — the wallpaper stays black. Rebuilding on the
             * override-redirect path restores the no-WM setup (XLowerWindow plus
             * the raise-the-other-children loop) and repaints. */
            log_info("X11: EWMH window manager went away - recreating wallpaper "
                     "windows as override-redirect");
            backend->override_redirect = true;
            x11_recreate_surfaces(backend);
            break;

        case X11_WM_ACTION_NONE:
        default:
            break;
    }
}

/* Re-run the WM probe after _NET_SUPPORTING_WM_CHECK changed on the root.
 *
 * The WM-appeared edge is acted on at once. The WM-went-away edge is deferred by
 * X11_WM_GONE_DEBOUNCE_MS and re-checked in x11_dispatch_events, so a WM restart
 * (which flaps the property down and straight back up) does not destroy and
 * recreate every window twice. */
static void x11_recheck_wm(x11_backend_data_t *backend) {
    bool wm_now = x11_ewmh_wm_present(backend);

    if (wm_now == backend->wm_present) {
        return;  /* Property churn that does not change the answer. */
    }
    backend->wm_present = wm_now;

    if (wm_now) {
        /* A WM is up. Any pending "the WM is gone" action is stale — this is what
         * makes a restart a no-op rather than two rebuilds. */
        if (backend->wm_gone_deadline_ms) {
            log_debug("X11: window manager came back before the rebuild deadline "
                      "- cancelling");
            backend->wm_gone_deadline_ms = 0;
        }
        x11_apply_wm_transition(backend);
        return;
    }

    /* The WM is gone. Do not believe it yet. */
    if (x11_wm_transition(backend->or_env_forced, backend->override_redirect,
                          false) == X11_WM_ACTION_NONE) {
        return;  /* Nothing would change anyway (forced, or already override-redirect). */
    }
    log_info("X11: EWMH window manager went away - waiting %d ms for a replacement",
             X11_WM_GONE_DEBOUNCE_MS);
    backend->wm_gone_deadline_ms = get_time_ms() + X11_WM_GONE_DEBOUNCE_MS;
}

/* Called every event-loop iteration. Fires the deferred WM-went-away rebuild once
 * the debounce window has passed and no replacement WM has turned up. */
static void x11_check_wm_gone_deadline(x11_backend_data_t *backend) {
    if (!backend->wm_gone_deadline_ms ||
        get_time_ms() < backend->wm_gone_deadline_ms) {
        return;
    }
    backend->wm_gone_deadline_ms = 0;

    /* Re-probe rather than trusting the earlier answer: a replacement WM may have
     * come up without us having processed its PropertyNotify yet. */
    backend->wm_present = x11_ewmh_wm_present(backend);
    if (backend->wm_present) {
        log_info("X11: a replacement window manager is running - keeping the "
                 "managed wallpaper windows");
        return;
    }
    x11_apply_wm_transition(backend);
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

static bool x11_dispatch_events(void *backend_data) {
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


static bool x11_flush(void *backend_data) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !backend->x_display) {
        return false;
    }

    XFlush(backend->x_display);
    return true;
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

/* ============================================================================
 * PUBLIC API FUNCTIONS
 * ============================================================================ */

int x11_backend_get_fd(struct compositor_backend *backend) {
    if (!backend || !backend->data) {
        return -1;
    }

    x11_backend_data_t *x11_backend = (x11_backend_data_t *)backend->data;
    return x11_get_connection_fd(x11_backend);
}

bool x11_backend_handle_events(struct compositor_backend *backend) {
    if (!backend || !backend->data) {
        return false;
    }

    x11_backend_data_t *x11_backend = (x11_backend_data_t *)backend->data;
    return x11_handle_events(x11_backend);
}
