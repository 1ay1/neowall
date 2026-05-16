#ifdef HAVE_X11_BACKEND

/* Occlusion detection on X11. EWMH _NET_CLIENT_LIST_STACKING +
 * _NET_WM_STATE_FULLSCREEN, mapped onto XRandR monitor geometries.
 * Owned by the X11 backend. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include "neowall.h"
#include "x11_occlusion.h"

static Display *x_display = NULL;
static Window root_window;
static struct neowall_state *g_state = NULL;

static Atom atom_net_client_list_stacking;
static Atom atom_net_wm_state;
static Atom atom_net_wm_state_fullscreen;

/* Throttle: x11 occlusion is polled, no need to query every loop tick. */
static int check_counter = 0;
#define CHECK_INTERVAL 10

static bool window_has_state(Display *dpy, Window win, Atom target) {
    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, win, atom_net_wm_state,
                           0, 64, False, XA_ATOM,
                           &type, &format, &n, &rem, &data) != Success || !data) {
        return false;
    }

    bool found = false;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < n; i++) {
        if (atoms[i] == target) {
            found = true;
            break;
        }
    }
    XFree(data);
    return found;
}

static bool get_window_geometry(Display *dpy, Window win,
                                int *x, int *y, int *w, int *h) {
    Window child;
    int wx, wy;
    unsigned int ww, wh, border, depth;

    if (!XGetGeometry(dpy, win, &child, &wx, &wy, &ww, &wh, &border, &depth)) {
        return false;
    }
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &wx, &wy, &child);
    *x = wx;
    *y = wy;
    *w = (int)ww;
    *h = (int)wh;
    return true;
}

static void check_fullscreen_state(void) {
    if (!x_display || !g_state) {
        return;
    }

    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    if (XGetWindowProperty(x_display, root_window, atom_net_client_list_stacking,
                           0, 1024, False, XA_WINDOW,
                           &type, &format, &n, &rem, &data) != Success || !data) {
        return;
    }
    Window *windows = (Window *)data;

    int num_monitors = 0;
    XRRMonitorInfo *monitors = XRRGetMonitors(x_display, root_window, True, &num_monitors);

    /* Collect fullscreen window rects. */
    typedef struct { int x, y, w, h; } rect_t;
    rect_t *fs = NULL;
    int fs_count = 0;

    for (unsigned long i = 0; i < n; i++) {
        if (!window_has_state(x_display, windows[i], atom_net_wm_state_fullscreen)) {
            continue;
        }
        int wx, wy, ww, wh;
        if (!get_window_geometry(x_display, windows[i], &wx, &wy, &ww, &wh)) {
            continue;
        }
        rect_t *tmp = realloc(fs, (fs_count + 1) * sizeof(*fs));
        if (!tmp) {
            continue;
        }
        fs = tmp;
        fs[fs_count++] = (rect_t){ wx, wy, ww, wh };
    }
    XFree(data);

    pthread_rwlock_rdlock(&g_state->output_list_lock);

    int idx = 0;
    for (struct output_state *o = g_state->outputs; o; o = o->next, idx++) {
        if (!o->config->pause_on_fullscreen) {
            continue;
        }

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool now = false;

        if (monitors && idx < num_monitors) {
            int mx = monitors[idx].x, my = monitors[idx].y;
            int mw = monitors[idx].width, mh = monitors[idx].height;

            for (int i = 0; i < fs_count; i++) {
                int ox = fs[i].x > mx ? fs[i].x : mx;
                int oy = fs[i].y > my ? fs[i].y : my;
                int or_ = (fs[i].x + fs[i].w) < (mx + mw)
                         ? (fs[i].x + fs[i].w) : (mx + mw);
                int ob = (fs[i].y + fs[i].h) < (my + mh)
                         ? (fs[i].y + fs[i].h) : (my + mh);
                int ow = or_ - ox, oh = ob - oy;
                if (ow > 0 && oh > 0 && mw > 0 && mh > 0 &&
                    ow * oh >= (mw * mh * 9 / 10)) {
                    now = true;
                    break;
                }
            }
        }

        atomic_store_explicit(&o->occluded, now, memory_order_release);

        const char *name = o->connector_name[0] ? o->connector_name : o->model;
        if (was && !now) {
            atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
            log_info("Output %s un-occluded, resuming rendering", name);
        } else if (!was && now) {
            log_info("Output %s occluded by fullscreen window, pausing rendering", name);
        }
    }

    pthread_rwlock_unlock(&g_state->output_list_lock);

    if (monitors) {
        XRRFreeMonitors(monitors);
    }
    free(fs);
}

/* ----- public ops (called by x11_core via the backend ops table) ----- */

bool x11_occlusion_init(Display *display, struct neowall_state *state) {
    if (!display || !state) {
        return false;
    }
    x_display = display;
    root_window = DefaultRootWindow(x_display);
    g_state = state;

    atom_net_client_list_stacking =
        XInternAtom(x_display, "_NET_CLIENT_LIST_STACKING", False);
    atom_net_wm_state = XInternAtom(x_display, "_NET_WM_STATE", False);
    atom_net_wm_state_fullscreen =
        XInternAtom(x_display, "_NET_WM_STATE_FULLSCREEN", False);

    check_fullscreen_state();
    return true;
}

void x11_occlusion_update(struct neowall_state *state) {
    (void)state;
    if (++check_counter < CHECK_INTERVAL) {
        return;
    }
    check_counter = 0;
    check_fullscreen_state();
}

void x11_occlusion_cleanup(void) {
    x_display = NULL;
    g_state = NULL;
    check_counter = 0;
}

#endif /* HAVE_X11_BACKEND */
