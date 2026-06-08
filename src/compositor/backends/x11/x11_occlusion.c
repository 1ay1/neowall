#ifdef HAVE_X11_BACKEND

/* Occlusion detection on X11. EWMH _NET_CLIENT_LIST_STACKING +
 * _NET_WM_STATE_FULLSCREEN, evaluated per output.
 * Owned by the X11 backend.
 *
 * Multi-monitor semantics: the X11 backend creates ONE output per active
 * monitor, each carrying its own geometry (x_offset/y_offset/width/height).
 * An output is occluded when a fullscreen window covers its own monitor rect,
 * independent of what the other monitors are doing — so a fullscreen game on
 * one screen pauses only that screen's wallpaper, while the others keep
 * animating. We match against the output's stored geometry directly rather
 * than indexing RandR monitors, so list/RandR ordering can't desync. */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include "neowall/neowall.h"
#include "x11_occlusion.h"

static Display *x_display = NULL;
static Window root_window;
static struct neowall_state *g_state = NULL;

static Atom atom_net_client_list_stacking;
static Atom atom_net_wm_state;
static Atom atom_net_wm_state_fullscreen;

/* Throttle: x11 occlusion is polled, no need to query every loop tick.
 * Per-instance state lives here — reset in init/cleanup so re-init after a
 * display server restart starts fresh, not mid-cycle. */
static int check_counter = 0;
#define CHECK_INTERVAL 10

/* Coverage threshold: a fullscreen window must cover at least this fraction
 * of an output's rect for that output to count as "occluded". 9/10 = 90%. */
#define MONITOR_COVERAGE_NUM 9
#define MONITOR_COVERAGE_DEN 10

/* Geometry of a fullscreen-marked window. */
typedef struct { int x, y, w, h; } rect_t;

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

/* Is the rect (mx,my,mw,mh) covered by any of the fullscreen rects? */
static bool rect_is_covered(int mx, int my, int mw, int mh,
                            const rect_t *fs, int fs_count) {
    if (mw <= 0 || mh <= 0) {
        return false;
    }
    for (int i = 0; i < fs_count; i++) {
        int ox = fs[i].x > mx ? fs[i].x : mx;
        int oy = fs[i].y > my ? fs[i].y : my;
        int or_ = (fs[i].x + fs[i].w) < (mx + mw)
                 ? (fs[i].x + fs[i].w) : (mx + mw);
        int ob = (fs[i].y + fs[i].h) < (my + mh)
                 ? (fs[i].y + fs[i].h) : (my + mh);
        int ow = or_ - ox, oh = ob - oy;
        if (ow > 0 && oh > 0 &&
            (long)ow * oh * MONITOR_COVERAGE_DEN >=
            (long)mw * mh * MONITOR_COVERAGE_NUM) {
            return true;
        }
    }
    return false;
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

    /* Collect fullscreen window rects. Grow geometrically (doubling) instead
     * of one-at-a-time — amortized O(n) instead of O(n^2) reallocations, and
     * a failed grow now logs and bails for the rest of the list rather than
     * silently dropping a single fullscreen window from consideration. */
    rect_t *fs = NULL;
    int fs_count = 0;
    int fs_cap = 0;

    for (unsigned long i = 0; i < n; i++) {
        if (!window_has_state(x_display, windows[i], atom_net_wm_state_fullscreen)) {
            continue;
        }
        int wx, wy, ww, wh;
        if (!get_window_geometry(x_display, windows[i], &wx, &wy, &ww, &wh)) {
            continue;
        }
        if (fs_count == fs_cap) {
            int new_cap = fs_cap ? fs_cap * 2 : 8;
            rect_t *tmp = realloc(fs, (size_t)new_cap * sizeof(*fs));
            if (!tmp) {
                log_error("x11_occlusion: realloc failed for %d fullscreen rects, "
                          "truncating list", new_cap);
                break;
            }
            fs = tmp;
            fs_cap = new_cap;
        }
        fs[fs_count++] = (rect_t){ wx, wy, ww, wh };
    }
    XFree(data);

    /* Decide occlusion per output: each output is occluded when a fullscreen
     * window covers its own monitor rect. */
    pthread_rwlock_rdlock(&g_state->output_list_lock);

    for (struct output_state *o = g_state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen) {
            continue;
        }

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        bool now = rect_is_covered(o->x_offset, o->y_offset,
                                   o->width, o->height, fs, fs_count);

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
    check_counter = 0;  /* Fresh start — don't inherit count from previous init. */

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
