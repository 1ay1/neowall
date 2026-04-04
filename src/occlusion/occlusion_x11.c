#ifdef HAVE_X11_BACKEND

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include "neowall.h"
#include "compositor.h"

/* ============================================================================
 * X11 OCCLUSION DETECTION
 * ============================================================================
 * Uses EWMH properties to detect fullscreen windows on X11.
 *
 * Strategy: Query _NET_CLIENT_LIST_STACKING and check each window's
 * _NET_WM_STATE for _NET_WM_STATE_FULLSCREEN. Map fullscreen windows
 * to outputs using XRandR geometry.
 *
 * This uses polling via XGetWindowProperty rather than event-based detection
 * because the current x11_dispatch_events() consumes all events. Querying
 * properties directly is cheap and avoids coupling with the X11 backend.
 */

static Display *x_display = NULL;
static Window root_window;
static struct neowall_state *g_state = NULL;

/* Atoms */
static Atom atom_net_client_list_stacking;
static Atom atom_net_wm_state;
static Atom atom_net_wm_state_fullscreen;

/* Monitor geometry from XRandR */
typedef struct {
    int x, y, width, height;
} monitor_geom_t;

/* Throttle: only check every N event loop iterations */
static int check_counter = 0;
#define CHECK_INTERVAL 10  /* Check every ~10 loop iterations */

/* ============================================================================
 * HELPER: Check if a window has a specific _NET_WM_STATE atom
 * ============================================================================ */

static bool window_has_state(Display *dpy, Window win, Atom target_state) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, win, atom_net_wm_state,
                           0, 64, False, XA_ATOM,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) != Success || !data) {
        return false;
    }

    bool found = false;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < nitems; i++) {
        if (atoms[i] == target_state) {
            found = true;
            break;
        }
    }

    XFree(data);
    return found;
}

/* ============================================================================
 * HELPER: Get window geometry
 * ============================================================================ */

static bool get_window_geometry(Display *dpy, Window win,
                                int *x, int *y, int *w, int *h) {
    Window child;
    int wx, wy;
    unsigned int ww, wh, border, depth;

    if (!XGetGeometry(dpy, win, &child, &wx, &wy, &ww, &wh, &border, &depth)) {
        return false;
    }

    /* Translate to root coordinates */
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &wx, &wy, &child);

    *x = wx;
    *y = wy;
    *w = (int)ww;
    *h = (int)wh;
    return true;
}

/* ============================================================================
 * CORE: Check for fullscreen windows and update occlusion state
 * ============================================================================ */

static void check_fullscreen_state(void) {
    if (!x_display || !g_state) {
        return;
    }

    /* Get the stacking order of all client windows */
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(x_display, root_window, atom_net_client_list_stacking,
                           0, 1024, False, XA_WINDOW,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) != Success || !data) {
        return;
    }

    Window *windows = (Window *)data;

    /* Get monitor geometries from XRandR */
    int num_monitors = 0;
    XRRMonitorInfo *monitors = XRRGetMonitors(x_display, root_window, True, &num_monitors);

    /* Build a list of fullscreen window geometries */
    typedef struct {
        int x, y, w, h;
    } fs_window_t;

    fs_window_t *fs_windows = NULL;
    int fs_count = 0;

    for (unsigned long i = 0; i < nitems; i++) {
        if (window_has_state(x_display, windows[i], atom_net_wm_state_fullscreen)) {
            int wx, wy, ww, wh;
            if (get_window_geometry(x_display, windows[i], &wx, &wy, &ww, &wh)) {
                fs_window_t *tmp = realloc(fs_windows, (fs_count + 1) * sizeof(fs_window_t));
                if (tmp) {
                    fs_windows = tmp;
                    fs_windows[fs_count].x = wx;
                    fs_windows[fs_count].y = wy;
                    fs_windows[fs_count].w = ww;
                    fs_windows[fs_count].h = wh;
                    fs_count++;
                }
            }
        }
    }

    XFree(data);

    /* For each output, check if any fullscreen window covers it */
    pthread_rwlock_rdlock(&g_state->output_list_lock);

    struct output_state *output = g_state->outputs;
    int output_idx = 0;
    while (output) {
        if (!output->config->pause_on_fullscreen) {
            output = output->next;
            output_idx++;
            continue;
        }

        bool was_occluded = atomic_load_explicit(&output->occluded, memory_order_acquire);
        bool is_occluded = false;

        /* Find the XRandR monitor matching this output index */
        if (output_idx < num_monitors && monitors) {
            int mx = monitors[output_idx].x;
            int my = monitors[output_idx].y;
            int mw = monitors[output_idx].width;
            int mh = monitors[output_idx].height;

            /* Check if any fullscreen window covers this monitor */
            for (int i = 0; i < fs_count; i++) {
                /* A fullscreen window covers a monitor if it overlaps significantly */
                int overlap_x = (fs_windows[i].x > mx) ? fs_windows[i].x : mx;
                int overlap_y = (fs_windows[i].y > my) ? fs_windows[i].y : my;
                int overlap_r = ((fs_windows[i].x + fs_windows[i].w) < (mx + mw))
                    ? (fs_windows[i].x + fs_windows[i].w) : (mx + mw);
                int overlap_b = ((fs_windows[i].y + fs_windows[i].h) < (my + mh))
                    ? (fs_windows[i].y + fs_windows[i].h) : (my + mh);

                int overlap_w = overlap_r - overlap_x;
                int overlap_h = overlap_b - overlap_y;

                /* Consider occluded if fullscreen window covers >= 90% of monitor */
                if (overlap_w > 0 && overlap_h > 0) {
                    int overlap_area = overlap_w * overlap_h;
                    int monitor_area = mw * mh;
                    if (monitor_area > 0 && overlap_area >= (monitor_area * 9 / 10)) {
                        is_occluded = true;
                        break;
                    }
                }
            }
        }

        atomic_store_explicit(&output->occluded, is_occluded, memory_order_release);

        if (was_occluded && !is_occluded) {
            output->needs_redraw = true;
            const char *name = output->connector_name[0] ? output->connector_name : output->model;
            log_info("Output %s un-occluded, resuming rendering", name);
        } else if (!was_occluded && is_occluded) {
            const char *name = output->connector_name[0] ? output->connector_name : output->model;
            log_info("Output %s occluded by fullscreen window, pausing rendering", name);
        }

        output = output->next;
        output_idx++;
    }

    pthread_rwlock_unlock(&g_state->output_list_lock);

    if (monitors) {
        XRRFreeMonitors(monitors);
    }
    free(fs_windows);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

bool occlusion_x11_init(struct neowall_state *state) {
    if (!state || !state->compositor_backend || !state->compositor_backend->ops) {
        return false;
    }

    /* Get the X11 display from the backend */
    if (!state->compositor_backend->ops->get_native_display) {
        return false;
    }

    x_display = state->compositor_backend->ops->get_native_display(
        state->compositor_backend->data);
    if (!x_display) {
        return false;
    }

    root_window = DefaultRootWindow(x_display);
    g_state = state;

    /* Intern atoms */
    atom_net_client_list_stacking = XInternAtom(x_display, "_NET_CLIENT_LIST_STACKING", False);
    atom_net_wm_state = XInternAtom(x_display, "_NET_WM_STATE", False);
    atom_net_wm_state_fullscreen = XInternAtom(x_display, "_NET_WM_STATE_FULLSCREEN", False);

    /* Do an initial check */
    check_fullscreen_state();

    return true;
}

void occlusion_x11_update(struct neowall_state *state) {
    (void)state;

    /* Throttle checks - no need to query X properties every single loop iteration */
    if (++check_counter < CHECK_INTERVAL) {
        return;
    }
    check_counter = 0;

    check_fullscreen_state();
}

void occlusion_x11_cleanup(void) {
    x_display = NULL;
    g_state = NULL;
    check_counter = 0;
}

#endif /* HAVE_X11_BACKEND */
