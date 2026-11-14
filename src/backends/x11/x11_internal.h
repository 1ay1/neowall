/* Shared by the X11 backend files (src/backends/x11/). Not public.
 *
 *   x11_core.c     display, ops table, events
 *   x11_wm.c       window manager detection and the window shape it implies
 *   x11_surface.c  wallpaper windows, commit, root pixmap
 *   x11_outputs.c  RandR monitors and hotplug */
#ifndef NEOWALL_X11_INTERNAL_H
#define NEOWALL_X11_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
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
#include "neowall/surface/surface.h"
#include "neowall/state/state.h"
#include "neowall/egl/egl_core.h"
#include "neowall/config/config.h"
#include "x11_occlusion.h"
#include "x11_geometry.h"
#include "x11_wm_detect.h"
#include "neowall/output/output.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/state_file.h"

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

    x11_occlusion occ;

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

/* Geometry of one physical monitor within the X screen. */
typedef struct {
    char name[64];   /* RandR connector name, e.g. "DVI-D-0" */
    int x, y;        /* Top-left within the screen */
    int width, height;
} x11_monitor_t;

bool x11_use_override_redirect(x11_backend_data_t *backend);
void x11_set_wallpaper_properties(x11_backend_data_t *backend, Window window,
                                         int pos_x, int pos_y, int width, int height);
void x11_get_screen_dimensions(x11_backend_data_t *backend, int *width, int *height);
struct compositor_surface *x11_create_surface(void *backend_data,
                                                     const compositor_surface_config_t *config);
void x11_destroy_surface(struct compositor_surface *surface);
bool x11_configure_surface(struct compositor_surface *surface,
                                  const compositor_surface_config_t *config);
void x11_distribute_mouse(x11_backend_data_t *backend, int root_x, int root_y);
void x11_unpublish_root_pixmap(x11_backend_data_t *backend);
void x11_commit_surface(struct compositor_surface *surface);
bool x11_create_egl_window(struct compositor_surface *surface,
                                  int32_t width, int32_t height);
void x11_destroy_egl_window(struct compositor_surface *surface);
bool x11_resize_egl_window(struct compositor_surface *surface,
                                 int32_t width, int32_t height);
EGLNativeWindowType x11_get_native_window(struct compositor_surface *surface);
bool x11_attach_surface(struct neowall_state *state, struct output_state *out);
bool x11_init_outputs(void *backend_data, struct neowall_state *state);
void x11_reconcile_outputs(x11_backend_data_t *backend);
void x11_mark_window_dirty(x11_backend_data_t *backend, Window window);
void x11_recheck_wm(x11_backend_data_t *backend);
void x11_check_wm_gone_deadline(x11_backend_data_t *backend);
bool x11_dispatch_events(void *backend_data);

#endif /* NEOWALL_X11_INTERNAL_H */
