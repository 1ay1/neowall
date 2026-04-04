#include <string.h>
#include "occlusion.h"
#include "neowall.h"
#include "compositor.h"

/* Backend-specific function declarations */
#ifdef HAVE_WAYLAND_BACKEND
extern bool occlusion_wayland_init(struct neowall_state *state);
extern void occlusion_wayland_update(struct neowall_state *state);
extern void occlusion_wayland_cleanup(void);
#endif

#ifdef HAVE_X11_BACKEND
extern bool occlusion_x11_init(struct neowall_state *state);
extern void occlusion_x11_update(struct neowall_state *state);
extern void occlusion_x11_cleanup(void);
#endif

static enum {
    OCCLUSION_NONE,
    OCCLUSION_WAYLAND,
    OCCLUSION_X11,
} active_backend = OCCLUSION_NONE;

bool occlusion_init(struct neowall_state *state) {
    if (!state || !state->compositor_backend) {
        return false;
    }

    const char *backend_name = state->compositor_backend->name;

#ifdef HAVE_WAYLAND_BACKEND
    /* If not using X11 backend, try Wayland occlusion */
    if (strcmp(backend_name, "x11-tiling-wm") != 0) {
        if (occlusion_wayland_init(state)) {
            active_backend = OCCLUSION_WAYLAND;
            log_info("Occlusion detection enabled (Wayland/wlr-foreign-toplevel)");
            return true;
        }
        log_info("Wayland occlusion detection not available "
                 "(compositor may not support wlr-foreign-toplevel-management)");
    }
#endif

#ifdef HAVE_X11_BACKEND
    if (strcmp(backend_name, "x11-tiling-wm") == 0) {
        if (occlusion_x11_init(state)) {
            active_backend = OCCLUSION_X11;
            log_info("Occlusion detection enabled (X11/EWMH)");
            return true;
        }
        log_info("X11 occlusion detection not available");
    }
#endif

    (void)backend_name;
    log_info("Occlusion detection not available for backend '%s'", backend_name);
    return false;
}

void occlusion_update(struct neowall_state *state) {
    switch (active_backend) {
#ifdef HAVE_WAYLAND_BACKEND
    case OCCLUSION_WAYLAND:
        occlusion_wayland_update(state);
        break;
#endif
#ifdef HAVE_X11_BACKEND
    case OCCLUSION_X11:
        occlusion_x11_update(state);
        break;
#endif
    case OCCLUSION_NONE:
    default:
        break;
    }
}

void occlusion_cleanup(struct neowall_state *state) {
    (void)state;

    switch (active_backend) {
#ifdef HAVE_WAYLAND_BACKEND
    case OCCLUSION_WAYLAND:
        occlusion_wayland_cleanup();
        break;
#endif
#ifdef HAVE_X11_BACKEND
    case OCCLUSION_X11:
        occlusion_x11_cleanup();
        break;
#endif
    case OCCLUSION_NONE:
    default:
        break;
    }

    active_backend = OCCLUSION_NONE;
}
