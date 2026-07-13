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
#include "x11_geometry.h"

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

/* Coverage threshold lives in x11_geometry.h (X11_COVERAGE_NUM/DEN). */

/* Geometry of a fullscreen-marked window. */
typedef x11_rect_t rect_t;

/* A window named by _NET_CLIENT_LIST_STACKING can already be gone by the time we
 * query it: the list is a snapshot, and windows are destroyed asynchronously.
 * XGetWindowProperty/XGetGeometry on a window that no longer exists raises
 * BadWindow, and Xlib's default error handler terminates the process.
 *
 * This is not hypothetical, and the window is not always someone else's. A window
 * manager killed outright leaves _NET_CLIENT_LIST_STACKING behind on the root,
 * still naming the wallpaper window it was managing; x11_core then rebuilds that
 * window on the override-redirect path, destroying the old one. The next poll
 * reads the dead WM's stale list and queries a window that is gone. Observed on
 * Xvfb with metacity: kill -9 the WM and neowall exits with "BadWindow (invalid
 * Window parameter)" about a second later, naming the destroyed wallpaper window.
 *
 * Only the "that window is gone" errors are tolerated silently: BadWindow from
 * the property/translate requests, BadDrawable from XGetGeometry (its argument is
 * a DRAWABLE, so a dead XID comes back as BadDrawable, not BadWindow). Any other
 * error means something we did not predict — a wrong property type, a bad atom, a
 * protocol bug — and reading it as "that window died, skip it" would hide it
 * forever. Those are logged. The window is still skipped either way: whatever the
 * error was, the reply we needed did not arrive, so the window cannot be counted
 * as occluding anything.
 *
 * THREADING CONSTRAINT: XSetErrorHandler is process-global, not per-Display. The
 * save/install/restore below is safe only because the occlusion poll runs on the
 * event-loop thread, which is the only thread making X requests on this Display.
 * This is the same invariant the WM probe in x11_core.c relies on; if either ever
 * stops holding, both need their own Display connection instead. */
static unsigned char occ_x_error;  /* Last X error code seen, 0 = none. */

static int occ_error_handler(Display *dpy, XErrorEvent *err) {
    occ_x_error = err->error_code;

    if (err->error_code != BadWindow && err->error_code != BadDrawable) {
        char text[128] = "";
        XGetErrorText(dpy, err->error_code, text, sizeof(text));
        log_error("x11_occlusion: unexpected X error while walking "
                  "_NET_CLIENT_LIST_STACKING: %s (code %u, request %u.%u, "
                  "resource 0x%lx) - skipping that window",
                  text, err->error_code, err->request_code, err->minor_code,
                  err->resourceid);
    }
    return 0;
}

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

    /* Collect fullscreen window rects. Grow geometrically (doubling) instead
     * of one-at-a-time — amortized O(n) instead of O(n^2) reallocations, and
     * a failed grow now logs and bails for the rest of the list rather than
     * silently dropping a single fullscreen window from consideration. */
    rect_t *fs = NULL;
    int fs_count = 0;
    int fs_cap = 0;

    /* Scoped to the per-window queries below, and only to those: the root property
     * fetch above ran under the default handler, so a failure there is still fatal
     * and visible rather than being read as "a window died". Every query below
     * touches a window we do not own and cannot keep alive. */
    XErrorHandler prev_handler = XSetErrorHandler(occ_error_handler);

    for (unsigned long i = 0; i < n; i++) {
        occ_x_error = 0;

        bool is_fullscreen =
            window_has_state(x_display, windows[i], atom_net_wm_state_fullscreen);
        if (occ_x_error) {
            continue;  /* Window died (silent), or something else went wrong and the
                        * handler logged it. Either way there is no answer to use. */
        }
        if (!is_fullscreen) {
            continue;
        }

        int wx, wy, ww, wh;
        if (!get_window_geometry(x_display, windows[i], &wx, &wy, &ww, &wh) ||
            occ_x_error) {
            continue;  /* Window died under us — it cannot be occluding anything. */
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

    XSync(x_display, False);  /* Deliver any straggling error before we un-install. */
    XSetErrorHandler(prev_handler);

    XFree(data);

    /* Decide occlusion per output: each output is occluded when a fullscreen
     * window covers its own monitor rect. */
    pthread_rwlock_rdlock(&g_state->output_list_lock);

    for (struct output_state *o = g_state->outputs; o; o = o->next) {
        if (!o->config->pause_on_fullscreen) {
            continue;
        }

        bool was = atomic_load_explicit(&o->occluded, memory_order_acquire);
        x11_rect_t orect = { o->x_offset, o->y_offset, o->width, o->height };
        bool now = x11_rect_is_covered(orect, fs, fs_count);

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
