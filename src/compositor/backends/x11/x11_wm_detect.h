#ifndef NEOWALL_X11_WM_DETECT_H
#define NEOWALL_X11_WM_DETECT_H

/* Pure, Xlib-free decision logic for the X11 backend's window-manager probe.
 *
 * The Xlib calls that gather the facts live in x11_core.c; the decisions made
 * from those facts live here, so they can be unit-tested headlessly with no X
 * server. Same split, and for the same reason, as x11_geometry.c.
 *
 * Nothing in this header names an Xlib type. An X11 Window is an XID, which is
 * an unsigned long, so the handshake observations below carry it as one. */

#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * NEOWALL_X11_OVERRIDE_REDIRECT
 * ------------------------------------------------------------------------- */

/* How the NEOWALL_X11_OVERRIDE_REDIRECT escape hatch was set.
 *
 * Only "0" and "1" are accepted. Anything else present in the environment is
 * X11_OR_ENV_INVALID: the caller warns and falls back to auto-detect, rather
 * than silently treating a typo ("true", "2", "") as "unset". */
typedef enum {
    X11_OR_ENV_UNSET = 0,   /* not in the environment: auto-detect */
    X11_OR_ENV_FORCE_ON,    /* "1": force override-redirect */
    X11_OR_ENV_FORCE_OFF,   /* "0": force a managed window */
    X11_OR_ENV_INVALID,     /* present but unrecognised: warn, then auto-detect */
} x11_or_env_t;

/* Classify a NEOWALL_X11_OVERRIDE_REDIRECT value. NULL (absent from the
 * environment) is X11_OR_ENV_UNSET; "" is X11_OR_ENV_INVALID, because the user
 * clearly meant to set something. */
x11_or_env_t x11_or_env_parse(const char *value);

/* True if the env var pinned the choice, so auto-detection (and the later
 * heal-on-WM-appearance) must not override it. */
bool x11_or_env_is_forced(x11_or_env_t env);

/* ---------------------------------------------------------------------------
 * _NET_SUPPORTING_WM_CHECK handshake
 * ------------------------------------------------------------------------- */

/* What the two-step _NET_SUPPORTING_WM_CHECK read actually observed.
 *
 * EWMH 1.5: the root property names a window, and that window must carry the
 * same property naming itself. A window manager that exited without cleaning up
 * leaves the root property behind, so the root alone proves nothing — only the
 * self-reference proves a WM is still alive. Reading the property off a window
 * that no longer exists raises BadWindow, which the caller catches and reports
 * here as x_error. */
typedef struct {
    bool root_prop_valid;      /* root carries the property, type XA_WINDOW, format 32, 1 item */
    unsigned long root_names;  /* the window the root property names; 0 == None */
    bool child_prop_valid;     /* that window carries the property, right type/format/count */
    unsigned long child_names; /* the window the child property names */
    bool x_error;              /* an X error was raised while reading the child */

    /* _NET_SUPPORTED, read off the root alongside the handshake. */
    bool supported_prop_valid;   /* root carries _NET_SUPPORTED, type XA_ATOM, format 32 */
    bool desktop_in_supported;   /* _NET_WM_WINDOW_TYPE_DESKTOP is in that list */
} x11_wm_check_obs_t;

/* True if the observations prove a live EWMH window manager owns the screen. */
bool x11_ewmh_wm_check_decide(const x11_wm_check_obs_t *obs);

/* True if a live EWMH window manager owns the screen AND advertises
 * _NET_WM_WINDOW_TYPE_DESKTOP in _NET_SUPPORTED.
 *
 * Liveness alone is the wrong question for choosing a managed window. The
 * handshake proves an EWMH WM is running; it says nothing about whether that WM
 * will stack a DESKTOP-typed window below everything. dwm and i3 both pass the
 * handshake, and neither handles _NET_WM_WINDOW_TYPE_DESKTOP: dwm tiles every
 * managed window (its only window-type rule is DIALOG -> floating, dwm.c
 * updatewindowtype), and i3 has no DESKTOP handling in manage.c — worse, our
 * _NET_WM_DESKTOP = 0xFFFFFFFF makes i3 float the window above the tiling
 * layer. A managed wallpaper under either is strictly worse than the
 * override-redirect path this backend has always used for them.
 *
 * EWMH 1.5 ("_NET_SUPPORTED... MUST be set by the Window Manager to indicate
 * which hints it supports") makes the advertisement authoritative, and dwm and
 * i3 both omit _NET_WM_WINDOW_TYPE_DESKTOP from it while metacity (observed on
 * Xvfb) and bspwm (bspwm.c net_atoms) include it. A WM that honours DESKTOP
 * without advertising it stays on the override-redirect path — the
 * pre-detection behaviour — and NEOWALL_X11_OVERRIDE_REDIRECT=0 forces managed
 * for exactly that case. */
bool x11_ewmh_wm_manages_desktop(const x11_wm_check_obs_t *obs);

/* ---------------------------------------------------------------------------
 * Decisions
 * ------------------------------------------------------------------------- */

/* Should the wallpaper windows be created override-redirect?
 *
 * An override-redirect window is by definition not managed by the window
 * manager, so no WM ever reads its _NET_WM_WINDOW_TYPE and the DESKTOP hint has
 * no effect. With an EWMH WM running we want a *managed* window so the WM sees
 * the hint and stacks it as the desktop. With no WM nothing would stack it for
 * us, so override-redirect plus the XLowerWindow path is kept. */
bool x11_or_decide(x11_or_env_t env, bool wm_present);

/* What to do about the wallpaper windows when the window manager comes or goes.
 *
 * The wallpaper window is created override-redirect when no WM is running (so
 * XLowerWindow + the raise-the-other-children loop can stack it) and managed
 * when one is (so the WM stacks it as the desktop). Both edges have to be acted
 * on, and in both directions the window must be *recreated*: override_redirect
 * is fixed at XCreateWindow time.
 *
 *  - WM appears (we are override-redirect): recreate managed. A wallpaper daemon
 *    autostarted at login routinely wins the race against the WM; without this it
 *    stays override-redirect for the whole session and paints over every window
 *    the WM later maps.
 *
 *  - WM disappears (we are managed): recreate override-redirect. This restores
 *    exactly the state the daemon would have been in had it started with no WM
 *    running, which re-arms the XLowerWindow path. It is not optional: observed
 *    on Xvfb under metacity, and under a deliberately reparenting test WM, the
 *    wallpaper window's contents are lost when the WM dies and — for a static
 *    image, where needs_redraw is cleared after the first frame (eventloop.c) —
 *    are never repainted, leaving the wallpaper permanently black.
 *
 *  - env_forced: the user pinned the choice with NEOWALL_X11_OVERRIDE_REDIRECT;
 *    never override it in either direction.
 *
 * Both edges are idempotent: the action depends on the window state we actually
 * have, so a repeated PropertyNotify for an unchanged situation does nothing.
 * The caller debounces the WM-disappeared edge (see X11_WM_GONE_DEBOUNCE_MS),
 * because a WM restart flaps the property down and straight back up. */
typedef enum {
    X11_WM_ACTION_NONE = 0,
    X11_WM_ACTION_RECREATE_MANAGED,   /* WM appeared: override-redirect -> managed */
    X11_WM_ACTION_RECREATE_OVERRIDE,  /* WM went away: managed -> override-redirect */
} x11_wm_action_t;

x11_wm_action_t x11_wm_transition(bool env_forced, bool current_override_redirect,
                                  bool wm_present_now);

/* How long to wait, after _NET_SUPPORTING_WM_CHECK says the WM is gone, before
 * believing it and recreating on the override-redirect path.
 *
 * A WM restart (`metacity --replace` and friends) tears the old WM down and
 * brings a new one up; if the property is briefly absent or stale in between, an
 * undebounced down-edge would destroy and recreate every wallpaper window, only
 * to do it again when the new WM announces itself. Waiting and re-probing turns
 * that into a no-op, at the cost of the wallpaper being stale for up to this long
 * after a genuine WM crash. */
#define X11_WM_GONE_DEBOUNCE_MS 1500

#endif /* NEOWALL_X11_WM_DETECT_H */
