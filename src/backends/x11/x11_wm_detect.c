/* Pure decision logic for the X11 backend's window-manager probe.
 *
 * No Xlib, no X server, no globals — see x11_wm_detect.h for why this is split
 * out of x11_core.c. Unit-tested by tests/test_x11_wm_detect.c. */

#include "x11_wm_detect.h"

x11_or_env_t x11_or_env_parse(const char *value) {
    if (!value) {
        return X11_OR_ENV_UNSET;
    }
    if (value[0] == '1' && value[1] == '\0') {
        return X11_OR_ENV_FORCE_ON;
    }
    if (value[0] == '0' && value[1] == '\0') {
        return X11_OR_ENV_FORCE_OFF;
    }
    /* Present but not "0"/"1" — including "" — is a mistake, not a default. */
    return X11_OR_ENV_INVALID;
}

bool x11_or_env_is_forced(x11_or_env_t env) {
    return env == X11_OR_ENV_FORCE_ON || env == X11_OR_ENV_FORCE_OFF;
}

bool x11_ewmh_wm_check_decide(const x11_wm_check_obs_t *obs) {
    if (!obs) {
        return false;
    }
    /* No usable property on the root: no EWMH WM ever announced itself. */
    if (!obs->root_prop_valid || obs->root_names == 0) {
        return false;
    }
    /* The window the root names is gone (BadWindow) — a WM that exited without
     * cleaning up. The root property is stale; there is no live WM. */
    if (obs->x_error || !obs->child_prop_valid) {
        return false;
    }
    /* The self-reference is the whole point of the two-step handshake. */
    return obs->child_names == obs->root_names;
}

bool x11_ewmh_wm_manages_desktop(const x11_wm_check_obs_t *obs) {
    if (!x11_ewmh_wm_check_decide(obs)) {
        return false;
    }
    /* Only the WM's own advertisement proves it will stack a DESKTOP window
     * (see x11_wm_detect.h); a live WM without it gets the override-redirect
     * path, same as no WM at all. */
    return obs->supported_prop_valid && obs->desktop_in_supported;
}

bool x11_or_decide(x11_or_env_t env, bool wm_present) {
    switch (env) {
        case X11_OR_ENV_FORCE_ON:
            return true;
        case X11_OR_ENV_FORCE_OFF:
            return false;
        case X11_OR_ENV_UNSET:
        case X11_OR_ENV_INVALID:
        default:
            return !wm_present;
    }
}

x11_wm_action_t x11_wm_transition(bool env_forced, bool current_override_redirect,
                                  bool wm_present_now) {
    /* The user pinned the choice; neither edge may override it. */
    if (env_forced) {
        return X11_WM_ACTION_NONE;
    }
    /* We are override-redirect and a WM is now running: it can stack the desktop
     * for us, but only if the window is managed. */
    if (current_override_redirect && wm_present_now) {
        return X11_WM_ACTION_RECREATE_MANAGED;
    }
    /* We are managed and the WM is gone: nothing will stack or repaint the window
     * any more, so go back to running it ourselves. */
    if (!current_override_redirect && !wm_present_now) {
        return X11_WM_ACTION_RECREATE_OVERRIDE;
    }
    /* Already in the right shape for the world as it is. */
    return X11_WM_ACTION_NONE;
}
