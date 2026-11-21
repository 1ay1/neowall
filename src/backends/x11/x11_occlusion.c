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
#include "neowall/state/state.h"
#include "x11_occlusion.h"
#include "x11_geometry.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/output/output.h"

/* Polled, so throttled by time: each check is a few round trips per client
 * window. Counting loop steps made it 6-24 checks a second while animating,
 * depending on the refresh rate. */
#define CHECK_INTERVAL_MS 250

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
/* Xlib error handlers get no context pointer, so this one flag is global.
 * Loop thread only, set and read within one check. */
static unsigned char occ_x_error; /* Last X error code seen, 0 = none. */

static int occ_error_handler(Display *dpy, XErrorEvent *err) {
    occ_x_error = err->error_code;

    if (err->error_code != BadWindow && err->error_code != BadDrawable) {
        char text[128] = "";
        XGetErrorText(dpy, err->error_code, text, sizeof(text));
        log_error("x11_occlusion: unexpected X error while walking "
                  "_NET_CLIENT_LIST_STACKING: %s (code %u, request %u.%u, "
                  "resource 0x%lx) - skipping that window",
                  text, err->error_code, err->request_code, err->minor_code, err->resourceid);
    }
    return 0;
}

/* Whether a window is fullscreen AND actually showing. _NET_WM_STATE keeps
 * FULLSCREEN on a window that is minimised (HIDDEN) or on another desktop
 * (unmapped by the WM), and counting those kept the wallpaper paused after
 * the user had switched away from the game. */
static bool window_is_visible_fullscreen(const x11_occlusion *occ, Window win) {
    Display *dpy = occ->dpy;
    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, win, occ->wm_state, 0, 64, False, XA_ATOM, &type, &format, &n, &rem,
                           &data) != Success ||
        !data) {
        return false;
    }
    /* format 32 items are longs in Xlib's buffer; anything else isn't Atoms */
    if (type != XA_ATOM || format != 32) n = 0;

    bool fullscreen = false, hidden = false;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < n; i++) {
        if (atoms[i] == occ->wm_state_fullscreen) fullscreen = true;
        if (atoms[i] == occ->wm_state_hidden) hidden = true;
    }
    XFree(data);
    if (!fullscreen || hidden) return false;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, win, &attr)) return false;
    return attr.map_state == IsViewable;
}

static bool get_window_geometry(Display *dpy, Window win, int *x, int *y, int *w, int *h) {
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

static void check_fullscreen_state(x11_occlusion *occ) {
    Display *x_display            = occ->dpy;
    struct neowall_state *g_state = occ->state;
    if (!x_display || !g_state) {
        return;
    }

    Atom type;
    int format;
    unsigned long n, rem;
    unsigned char *data = NULL;

    /* No list (no WM, or it died and took the property with it) means no
     * fullscreen windows. Returning early instead would leave every output
     * paused on the last answer, with nothing left to ever change it. */
    if (XGetWindowProperty(x_display, occ->root, occ->client_list_stacking, 0, 1024, False,
                           XA_WINDOW, &type, &format, &n, &rem, &data) != Success ||
        !data) {
        data = NULL;
        n    = 0;
    } else if (type != XA_WINDOW || format != 32) {
        n = 0;
    }
    Window *windows = (Window *)data;

    /* Collect fullscreen window rects. Grow geometrically (doubling) instead
     * of one-at-a-time — amortized O(n) instead of O(n^2) reallocations, and
     * a failed grow now logs and bails for the rest of the list rather than
     * silently dropping a single fullscreen window from consideration. */
    rect_t *fs   = NULL;
    int fs_count = 0;
    int fs_cap   = 0;

    /* Scoped to the per-window queries below, and only to those: the root property
     * fetch above ran under the default handler, so a failure there is still fatal
     * and visible rather than being read as "a window died". Every query below
     * touches a window we do not own and cannot keep alive. */
    XErrorHandler prev_handler = XSetErrorHandler(occ_error_handler);

    for (unsigned long i = 0; i < n; i++) {
        occ_x_error = 0;

        bool is_fullscreen = window_is_visible_fullscreen(occ, windows[i]);
        if (occ_x_error) {
            continue; /* Window died (silent), or something else went wrong and the
                       * handler logged it. Either way there is no answer to use. */
        }
        if (!is_fullscreen) {
            continue;
        }

        int wx, wy, ww, wh;
        if (!get_window_geometry(x_display, windows[i], &wx, &wy, &ww, &wh) || occ_x_error) {
            continue; /* Window died under us — it cannot be occluding anything. */
        }
        if (fs_count == fs_cap) {
            int new_cap = fs_cap ? fs_cap * 2 : 8;
            rect_t *tmp = realloc(fs, (size_t)new_cap * sizeof(*fs));
            if (!tmp) {
                log_error("x11_occlusion: realloc failed for %d fullscreen rects, "
                          "truncating list",
                          new_cap);
                break;
            }
            fs     = tmp;
            fs_cap = new_cap;
        }
        fs[fs_count++] = (rect_t){wx, wy, ww, wh};
    }

    XSync(x_display, False); /* Deliver any straggling error before we un-install. */
    XSetErrorHandler(prev_handler);

    if (data) XFree(data);

    /* Decide occlusion per output: each output is occluded when a fullscreen
     * window covers its own monitor rect. Every output is decided, so one
     * whose option was turned off comes back too (output_set_occluded). */
    pthread_rwlock_rdlock(&g_state->output_list_lock);

    for (struct output_state *o = g_state->outputs; o; o = o->next) {
        x11_rect_t orect = {o->x_offset, o->y_offset, o->width, o->height};
        output_set_occluded(o, x11_rect_is_covered(orect, fs, fs_count), "fullscreen window");
    }

    pthread_rwlock_unlock(&g_state->output_list_lock);

    free(fs);
}

/* ----- public ops (called by x11_core via the backend ops table) ----- */

bool x11_occlusion_init(x11_occlusion *occ, Display *display, struct neowall_state *state) {
    if (!occ || !display || !state) return false;
    *occ = (x11_occlusion){
        .dpy                  = display,
        .root                 = DefaultRootWindow(display),
        .state                = state,
        .client_list_stacking = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False),
        .wm_state             = XInternAtom(display, "_NET_WM_STATE", False),
        .wm_state_fullscreen  = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False),
        .wm_state_hidden      = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False),
        .active               = true,
    };
    check_fullscreen_state(occ);
    return true;
}

void x11_occlusion_update(x11_occlusion *occ) {
    if (!occ || !occ->active) return;
    /* Throttled, so a burst of events costs one check. The loop also runs
     * this on a steady tick while anything is covered, so a check skipped
     * here is made up within a tick rather than waiting for an event that
     * may never come (a window leaving fullscreen changes only its own
     * property, which the root doesn't hear about). */
    uint64_t now = get_time_ms();
    if (occ->last_check_ms && now - occ->last_check_ms < CHECK_INTERVAL_MS) return;
    occ->last_check_ms = now;
    check_fullscreen_state(occ);
}

void x11_occlusion_cleanup(x11_occlusion *occ) {
    if (occ) *occ = (x11_occlusion){0};
}

#endif /* HAVE_X11_BACKEND */
