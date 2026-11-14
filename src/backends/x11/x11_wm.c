/* Window managers: is an EWMH WM running, and the window shape that follows
 * from it (managed vs override-redirect), re-checked as WMs come and go. */

#include "x11_internal.h"

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
static bool x11_read_window_prop(Display *dpy, Window target, Atom prop_atom, unsigned long *out) {
    Atom actual_type     = None;
    int actual_format    = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, target, prop_atom, 0, 1, False, XA_WINDOW, &actual_type,
                           &actual_format, &nitems, &bytes_after, &prop) != Success) {
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
    Atom actual_type     = None;
    int actual_format    = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(backend->x_display, backend->root_window, backend->atom_net_supported, 0,
                           4096, False, XA_ATOM, &actual_type, &actual_format, &nitems,
                           &bytes_after, &prop) != Success) {
        return;
    }
    if (actual_type != XA_ATOM || actual_format != 32 || nitems == 0 || !prop) {
        if (prop) XFree(prop);
        return;
    }

    obs->supported_prop_valid = true;
    Atom *atoms               = (Atom *)prop;
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
    Atom check   = backend->atom_net_supporting_wm_check;

    x11_wm_check_obs_t obs;
    memset(&obs, 0, sizeof(obs));

    obs.root_prop_valid = x11_read_window_prop(dpy, backend->root_window, check, &obs.root_names);

    /* Only the second read can touch a window that may not exist, so only it
     * needs the error handler. */
    bool present = false;
    if (obs.root_prop_valid && obs.root_names != 0) {
        x11_read_supported(backend, &obs);

        x11_probe_error    = false;
        XErrorHandler prev = XSetErrorHandler(x11_probe_error_handler);

        obs.child_prop_valid =
            x11_read_window_prop(dpy, (Window)obs.root_names, check, &obs.child_names);

        obs.x_error = x11_probe_error;
        present     = x11_ewmh_wm_manages_desktop(&obs);

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
            long mask                = StructureNotifyMask;
            if (backend->wm_check_window == backend->root_window) {
                mask |= PropertyChangeMask;
            }
            XSelectInput(dpy, backend->wm_check_window, mask);
        }

        XSync(dpy, False); /* Flush any BadWindow before restoring the handler. */
        XSetErrorHandler(prev);

        /* A BadWindow at any point in the probe — including XSelectInput racing
         * the check window's destruction — means the WM is already gone. Fold it
         * into the observations and re-decide, so present and wm_alive below are
         * derived from one consistent set of facts. */
        obs.x_error = obs.x_error || x11_probe_error;
        present     = x11_ewmh_wm_manages_desktop(&obs);
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
bool x11_use_override_redirect(x11_backend_data_t *backend) {
    const char *env   = getenv("NEOWALL_X11_OVERRIDE_REDIRECT");
    x11_or_env_t mode = x11_or_env_parse(env);

    if (mode == X11_OR_ENV_INVALID) {
        log_warn("X11: NEOWALL_X11_OVERRIDE_REDIRECT=\"%s\" is not recognised "
                 "(expected \"0\" for a managed window or \"1\" for "
                 "override-redirect) - ignoring it and auto-detecting",
                 env);
    }

    backend->or_env_forced = x11_or_env_is_forced(mode);
    backend->wm_present    = x11_ewmh_wm_present(backend);

    bool override_redirect = x11_or_decide(mode, backend->wm_present);

    if (backend->or_env_forced) {
        log_info("X11: NEOWALL_X11_OVERRIDE_REDIRECT=%s, forcing %s wallpaper window", env,
                 override_redirect ? "override-redirect" : "managed");
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
void x11_set_wallpaper_properties(x11_backend_data_t *backend, Window window, int pos_x, int pos_y,
                                  int width, int height) {
    Display *dpy = backend->x_display;

    XChangeProperty(dpy, window, backend->atom_net_wm_window_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&backend->atom_net_wm_window_type_desktop, 1);

    Atom states[] = {
        backend->atom_net_wm_state_below,
        backend->atom_net_wm_state_sticky,
        backend->atom_net_wm_state_skip_taskbar,
        backend->atom_net_wm_state_skip_pager,
    };
    XChangeProperty(dpy, window, backend->atom_net_wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, (int)(sizeof(states) / sizeof(states[0])));

    /* EWMH 5.5 / 9.2: a desktop window belongs on every desktop, which is what
     * 0xFFFFFFFF means. _NET_WM_STATE_STICKY above is not the same thing — it
     * means "fixed relative to the viewport", i.e. it does not scroll with a
     * large desktop, and says nothing about which desktops the window is on. A
     * format-32 property is passed to Xlib as an array of long. */
    unsigned long all_desktops = 0xFFFFFFFFUL;
    XChangeProperty(dpy, window, backend->atom_net_wm_desktop, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&all_desktops, 1);

    /* ICCCM 4.1.2.5: WM_CLASS "must be present when the window leaves the
     * Withdrawn state". An override-redirect window is never managed, so it was
     * exempt; a managed wallpaper window is not. This is also the handle a user
     * needs to write a WM rule against us if our stacking misbehaves on their
     * WM (i3 `for_window [class="NeoWall"]`, KWin window rules, openbox
     * `<application class="NeoWall">`). */
    XClassHint class_hint = {
        .res_name  = (char *)"neowall", /* instance name */
        .res_class = (char *)"NeoWall", /* class name */
    };
    XSetClassHint(dpy, window, &class_hint);

    /* WM_NAME (ICCCM, latin-1) and _NET_WM_NAME (EWMH, UTF-8). EWMH says a WM
     * must prefer _NET_WM_NAME where both are present; pagers and `xprop` read
     * either, so set both. */
    static const char wm_name[] = "NeoWall Wallpaper";
    XStoreName(dpy, window, wm_name);
    XChangeProperty(dpy, window, backend->atom_net_wm_name, backend->atom_utf8_string, 8,
                    PropModeReplace, (const unsigned char *)wm_name, (int)(sizeof(wm_name) - 1));

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
    size_hints.flags  = PPosition | PSize;
    size_hints.x      = pos_x;
    size_hints.y      = pos_y;
    size_hints.width  = width;
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
    struct output_state *outs[NW_MAX_OUTPUTS];
    int n = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n < NW_MAX_OUTPUTS; o = o->next) {
        output_ref(o);
        outs[n++] = o;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    for (int i = 0; i < n; i++) {
        struct output_state *o = outs[i];

        /* Release the EGL surface from this thread before destroying it. */
        egl_make_current_raw(state->egl_display, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (o->compositor_surface) {
            if (o->compositor_surface->egl_surface != EGL_NO_SURFACE) {
                compositor_surface_destroy_egl(o->compositor_surface, state->egl_display);
            }
            compositor_surface_destroy(o->compositor_surface);
            o->compositor_surface = NULL;
        }

        if (!x11_attach_surface(state, o) || !output_create_egl_surface(o) ||
            !output_make_current(o)) {
            log_error("X11: failed to recreate wallpaper window for %s", o->connector_name);
            output_unref(o);
            continue;
        }

        atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
        log_info("X11: wallpaper window for %s recreated as %s", o->connector_name,
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
 * (app.c), so without this the window stays black for the rest of the
 * session. Observed on Xvfb: a static wallpaper goes black the moment the WM
 * takes the window over, and again when the WM dies. */
void x11_mark_window_dirty(x11_backend_data_t *backend, Window window) {
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
    x11_wm_action_t action =
        x11_wm_transition(backend->or_env_forced, backend->override_redirect, backend->wm_present);
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
    default: break;
    }
}

/* Re-run the WM probe after _NET_SUPPORTING_WM_CHECK changed on the root.
 *
 * The WM-appeared edge is acted on at once. The WM-went-away edge is deferred by
 * X11_WM_GONE_DEBOUNCE_MS and re-checked in x11_dispatch_events, so a WM restart
 * (which flaps the property down and straight back up) does not destroy and
 * recreate every window twice. */
void x11_recheck_wm(x11_backend_data_t *backend) {
    bool wm_now = x11_ewmh_wm_present(backend);

    if (wm_now == backend->wm_present) {
        return; /* Property churn that does not change the answer. */
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
    if (x11_wm_transition(backend->or_env_forced, backend->override_redirect, false) ==
        X11_WM_ACTION_NONE) {
        return; /* Nothing would change anyway (forced, or already override-redirect). */
    }
    log_info("X11: EWMH window manager went away - waiting %d ms for a replacement",
             X11_WM_GONE_DEBOUNCE_MS);
    backend->wm_gone_deadline_ms = get_time_ms() + X11_WM_GONE_DEBOUNCE_MS;
}

/* Called every event-loop iteration. Fires the deferred WM-went-away rebuild once
 * the debounce window has passed and no replacement WM has turned up. */
void x11_check_wm_gone_deadline(x11_backend_data_t *backend) {
    if (!backend->wm_gone_deadline_ms || get_time_ms() < backend->wm_gone_deadline_ms) {
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
