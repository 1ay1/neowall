/* Unit tests for the X11 window-manager detection decisions (x11_wm_detect.c).
 *
 * Pure decision logic, no Xlib / display server — runs headless in CI. Covers
 * the two-step _NET_SUPPORTING_WM_CHECK handshake (including the stale-property
 * case that raises BadWindow), the NEOWALL_X11_OVERRIDE_REDIRECT contract, and
 * the "a window manager started after we did" heal edge.
 */
#include <stdio.h>
#include <string.h>

#include "x11_wm_detect.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

/* An arbitrary window id standing in for the WM's check window. */
#define WM_WIN 0x400001UL

/* ---------------------------------------------------------------------------
 * The two-step _NET_SUPPORTING_WM_CHECK handshake
 * ------------------------------------------------------------------------- */

static void test_wm_check_decide(void) {
    /* A live EWMH WM: root names a window, that window names itself. */
    x11_wm_check_obs_t live = {
        .root_prop_valid = true,
        .root_names = WM_WIN,
        .child_prop_valid = true,
        .child_names = WM_WIN,
        .x_error = false,
    };
    CHECK(x11_ewmh_wm_check_decide(&live));

    /* Bare X server: no property on the root at all. */
    x11_wm_check_obs_t bare;
    memset(&bare, 0, sizeof(bare));
    CHECK(!x11_ewmh_wm_check_decide(&bare));

    /* Property present but names None. */
    x11_wm_check_obs_t names_none = live;
    names_none.root_names = 0;
    CHECK(!x11_ewmh_wm_check_decide(&names_none));

    /* Root property present but the wrong type/format/count — treated as absent. */
    x11_wm_check_obs_t bad_type = live;
    bad_type.root_prop_valid = false;
    CHECK(!x11_ewmh_wm_check_decide(&bad_type));

    /* STALE: a WM exited without cleaning up. The root still names a window, but
     * that window is gone, so reading its property raised BadWindow. This is the
     * case the temporary error handler in x11_core.c exists for; the answer must
     * be "no live WM", not a crash and not a false positive. */
    x11_wm_check_obs_t stale = {
        .root_prop_valid = true,
        .root_names = WM_WIN,
        .child_prop_valid = false,
        .child_names = 0,
        .x_error = true,
    };
    CHECK(!x11_ewmh_wm_check_decide(&stale));

    /* An x_error must lose even if a child property was somehow also read. */
    x11_wm_check_obs_t stale_but_read = live;
    stale_but_read.x_error = true;
    CHECK(!x11_ewmh_wm_check_decide(&stale_but_read));

    /* The window exists and carries the property, but it names someone else —
     * not a self-reference, so it does not prove a live WM. */
    x11_wm_check_obs_t not_self = live;
    not_self.child_names = WM_WIN + 1;
    CHECK(!x11_ewmh_wm_check_decide(&not_self));

    /* The window exists but has no such property. */
    x11_wm_check_obs_t no_child_prop = live;
    no_child_prop.child_prop_valid = false;
    CHECK(!x11_ewmh_wm_check_decide(&no_child_prop));

    CHECK(!x11_ewmh_wm_check_decide(NULL));
}

/* ---------------------------------------------------------------------------
 * Desktop-type support: liveness is not enough for a managed window
 * ------------------------------------------------------------------------- */

static void test_wm_manages_desktop(void) {
    /* metacity / bspwm: live, and _NET_SUPPORTED advertises
     * _NET_WM_WINDOW_TYPE_DESKTOP. Managed is safe. */
    x11_wm_check_obs_t desktop_wm = {
        .root_prop_valid = true,
        .root_names = WM_WIN,
        .child_prop_valid = true,
        .child_names = WM_WIN,
        .x_error = false,
        .supported_prop_valid = true,
        .desktop_in_supported = true,
    };
    CHECK(x11_ewmh_wm_manages_desktop(&desktop_wm));

    /* dwm / i3: pass the liveness handshake, but _NET_SUPPORTED does not list
     * _NET_WM_WINDOW_TYPE_DESKTOP. dwm tiles every managed window; i3 floats a
     * window with _NET_WM_DESKTOP=0xFFFFFFFF above the tiling layer. Handing
     * either a managed wallpaper is worse than override-redirect, so the answer
     * must be no even though the WM is alive. */
    x11_wm_check_obs_t tiling_wm = desktop_wm;
    tiling_wm.desktop_in_supported = false;
    CHECK(x11_ewmh_wm_check_decide(&tiling_wm));       /* alive... */
    CHECK(!x11_ewmh_wm_manages_desktop(&tiling_wm));   /* ...but not desktop-capable */

    /* Live WM with no _NET_SUPPORTED at all (an EWMH violation): conservative
     * answer, keep the override-redirect path. */
    x11_wm_check_obs_t no_supported = desktop_wm;
    no_supported.supported_prop_valid = false;
    no_supported.desktop_in_supported = false;
    CHECK(!x11_ewmh_wm_manages_desktop(&no_supported));

    /* A stale _NET_SUPPORTED left by a dead WM must not outvote liveness. */
    x11_wm_check_obs_t dead_wm = desktop_wm;
    dead_wm.child_prop_valid = false;
    dead_wm.x_error = true;
    CHECK(!x11_ewmh_wm_manages_desktop(&dead_wm));

    CHECK(!x11_ewmh_wm_manages_desktop(NULL));
}

/* ---------------------------------------------------------------------------
 * NEOWALL_X11_OVERRIDE_REDIRECT contract
 * ------------------------------------------------------------------------- */

static void test_env_parse(void) {
    /* Absent from the environment. */
    CHECK(x11_or_env_parse(NULL) == X11_OR_ENV_UNSET);

    /* The two documented values. */
    CHECK(x11_or_env_parse("1") == X11_OR_ENV_FORCE_ON);
    CHECK(x11_or_env_parse("0") == X11_OR_ENV_FORCE_OFF);

    /* Everything else is a mistake, and must be distinguishable from "unset" so
     * the caller can warn instead of silently auto-detecting. */
    CHECK(x11_or_env_parse("") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("2") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("true") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("false") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("yes") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse(" ") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse(" 1") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("1 ") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("10") == X11_OR_ENV_INVALID);
    CHECK(x11_or_env_parse("01") == X11_OR_ENV_INVALID);

    /* Only an explicit 0/1 pins the choice. */
    CHECK(x11_or_env_is_forced(X11_OR_ENV_FORCE_ON));
    CHECK(x11_or_env_is_forced(X11_OR_ENV_FORCE_OFF));
    CHECK(!x11_or_env_is_forced(X11_OR_ENV_UNSET));
    CHECK(!x11_or_env_is_forced(X11_OR_ENV_INVALID));
}

static void test_or_decide(void) {
    /* Auto-detect: a WM means managed, no WM means override-redirect. */
    CHECK(x11_or_decide(X11_OR_ENV_UNSET, true) == false);
    CHECK(x11_or_decide(X11_OR_ENV_UNSET, false) == true);

    /* Forced, regardless of what is actually running. */
    CHECK(x11_or_decide(X11_OR_ENV_FORCE_ON, true) == true);
    CHECK(x11_or_decide(X11_OR_ENV_FORCE_ON, false) == true);
    CHECK(x11_or_decide(X11_OR_ENV_FORCE_OFF, true) == false);
    CHECK(x11_or_decide(X11_OR_ENV_FORCE_OFF, false) == false);

    /* An unrecognised value warns and then behaves exactly as if unset — it must
     * never be read as one of the two real values. */
    CHECK(x11_or_decide(X11_OR_ENV_INVALID, true) == x11_or_decide(X11_OR_ENV_UNSET, true));
    CHECK(x11_or_decide(X11_OR_ENV_INVALID, false) == x11_or_decide(X11_OR_ENV_UNSET, false));
}

/* ---------------------------------------------------------------------------
 * The heal edge: a window manager started after we did
 * ------------------------------------------------------------------------- */

static void test_transition(void) {
    const bool forced = true, autodetect = false;
    const bool is_or = true, is_managed = false;
    const bool wm = true, no_wm = false;

    /* Autostarted at login, raced the WM and lost, so we came up
     * override-redirect. The WM is up now: recreate as managed, or we paint over
     * every window for the rest of the session. */
    CHECK(x11_wm_transition(autodetect, is_or, wm) == X11_WM_ACTION_RECREATE_MANAGED);

    /* Managed, and the WM died. Recreate override-redirect: nothing is left to
     * stack the window, and (observed on Xvfb) nothing repaints it either — a
     * static-image wallpaper goes black and stays black, because needs_redraw was
     * cleared after its first frame. */
    CHECK(x11_wm_transition(autodetect, is_managed, no_wm) == X11_WM_ACTION_RECREATE_OVERRIDE);

    /* Steady states: the window already matches the world. Both must be no-ops, so
     * that a repeated PropertyNotify cannot churn windows. */
    CHECK(x11_wm_transition(autodetect, is_managed, wm) == X11_WM_ACTION_NONE);
    CHECK(x11_wm_transition(autodetect, is_or, no_wm) == X11_WM_ACTION_NONE);

    /* The user pinned the choice with NEOWALL_X11_OVERRIDE_REDIRECT. Never
     * second-guess it, in either direction, whatever the WM does. */
    CHECK(x11_wm_transition(forced, is_or, wm) == X11_WM_ACTION_NONE);
    CHECK(x11_wm_transition(forced, is_or, no_wm) == X11_WM_ACTION_NONE);
    CHECK(x11_wm_transition(forced, is_managed, wm) == X11_WM_ACTION_NONE);
    CHECK(x11_wm_transition(forced, is_managed, no_wm) == X11_WM_ACTION_NONE);
}

/* The whole late-WM sequence, as the daemon actually walks it: probe at init,
 * then re-probe on each root PropertyNotify. */
static void test_late_wm_sequence(void) {
    x11_or_env_t env = x11_or_env_parse(NULL);  /* nothing forced */
    bool forced = x11_or_env_is_forced(env);

    /* Init, no WM yet: the root property is absent. */
    x11_wm_check_obs_t obs;
    memset(&obs, 0, sizeof(obs));
    bool wm_present = x11_ewmh_wm_manages_desktop(&obs);
    bool override_redirect = x11_or_decide(env, wm_present);
    CHECK(!wm_present);
    CHECK(override_redirect);  /* came up override-redirect */

    /* metacity starts and publishes the handshake and its _NET_SUPPORTED.
     * PropertyNotify -> re-probe. */
    x11_wm_check_obs_t up = {
        .root_prop_valid = true,
        .root_names = WM_WIN,
        .child_prop_valid = true,
        .child_names = WM_WIN,
        .x_error = false,
        .supported_prop_valid = true,
        .desktop_in_supported = true,
    };
    wm_present = x11_ewmh_wm_manages_desktop(&up);
    CHECK(wm_present);

    /* A starting WM's write order between _NET_SUPPORTING_WM_CHECK and
     * _NET_SUPPORTED is not specified. If the handshake lands first, this probe
     * says "not yet" — and the _NET_SUPPORTED PropertyNotify that follows
     * triggers the re-probe that heals it (x11_dispatch_events watches both). */
    x11_wm_check_obs_t handshake_first = up;
    handshake_first.supported_prop_valid = false;
    handshake_first.desktop_in_supported = false;
    CHECK(!x11_ewmh_wm_manages_desktop(&handshake_first));
    CHECK(x11_wm_transition(forced, override_redirect, false) == X11_WM_ACTION_NONE);
    CHECK(x11_wm_transition(forced, override_redirect, wm_present) ==
          X11_WM_ACTION_RECREATE_MANAGED);
    override_redirect = false;  /* recreated managed */

    /* A second PropertyNotify for the same WM must not recreate anything again. */
    CHECK(x11_wm_transition(forced, override_redirect, wm_present) == X11_WM_ACTION_NONE);

    /* The WM is killed, leaving the property stale (BadWindow on the check
     * window). Nothing is left to stack or repaint the wallpaper, so go back to
     * the override-redirect setup we would have had if we had started with no WM. */
    x11_wm_check_obs_t gone = {
        .root_prop_valid = true,
        .root_names = WM_WIN,
        .child_prop_valid = false,
        .child_names = 0,
        .x_error = true,
        .supported_prop_valid = true,   /* stale, like the handshake property */
        .desktop_in_supported = true,
    };
    wm_present = x11_ewmh_wm_manages_desktop(&gone);
    CHECK(!wm_present);
    CHECK(x11_wm_transition(forced, override_redirect, wm_present) ==
          X11_WM_ACTION_RECREATE_OVERRIDE);
    override_redirect = true;  /* recreated override-redirect */

    /* And that is a steady state again. */
    CHECK(x11_wm_transition(forced, override_redirect, wm_present) == X11_WM_ACTION_NONE);
}

/* A WM restart (`--replace`). The property flaps down and straight back up. The
 * daemon debounces the down-edge (X11_WM_GONE_DEBOUNCE_MS) and re-probes when it
 * expires, so the whole restart must cost ZERO window rebuilds. */
static void test_wm_replace_is_a_noop(void) {
    bool forced = false;
    bool override_redirect = false;  /* running managed under the old WM */

    x11_wm_check_obs_t old_wm = {
        .root_prop_valid = true, .root_names = WM_WIN,
        .child_prop_valid = true, .child_names = WM_WIN,
        .supported_prop_valid = true, .desktop_in_supported = true,
    };
    x11_wm_check_obs_t none;
    memset(&none, 0, sizeof(none));
    x11_wm_check_obs_t new_wm = old_wm;
    new_wm.root_names = WM_WIN + 9;
    new_wm.child_names = WM_WIN + 9;

    CHECK(x11_ewmh_wm_manages_desktop(&old_wm));

    /* Down-edge: the old WM is gone. The action is real now, which is exactly why
     * it must be deferred rather than applied here. */
    CHECK(!x11_ewmh_wm_manages_desktop(&none));
    CHECK(x11_wm_transition(forced, override_redirect, false) ==
          X11_WM_ACTION_RECREATE_OVERRIDE);
    /* ...so the caller arms the deadline instead of rebuilding. */

    /* The replacement WM announces itself before the deadline expires. The caller
     * cancels the pending rebuild, and the transition from the state we are
     * ACTUALLY in (still managed) with a WM present is a no-op. */
    CHECK(x11_ewmh_wm_manages_desktop(&new_wm));
    CHECK(x11_wm_transition(forced, override_redirect, true) == X11_WM_ACTION_NONE);
    CHECK(!override_redirect);  /* never rebuilt, never flickered */
}

int main(void) {
    test_wm_check_decide();
    test_wm_manages_desktop();
    test_env_parse();
    test_or_decide();
    test_transition();
    test_late_wm_sequence();
    test_wm_replace_is_a_noop();

    printf("x11_wm_detect: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
