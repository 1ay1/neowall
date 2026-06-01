/* Occlusion detection — thin wrapper over the compositor abstraction.
 *
 * The actual implementation lives next to each backend
 * (src/compositor/backends/wayland/occlusion.c, .../x11/x11_occlusion.c).
 * This file just dispatches via the backend ops table, so core code never
 * needs to know which display server is in use. */

#include "neowall/occlusion/occlusion.h"
#include "neowall/neowall.h"
#include "neowall/compositor/compositor.h"

bool occlusion_init(struct neowall_state *state) {
    if (!state || !state->compositor_backend) {
        return false;
    }
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    if (!ops || !ops->occlusion_init) {
        log_info("Occlusion detection not supported by backend '%s'",
                 state->compositor_backend->name);
        return false;
    }
    if (!ops->occlusion_init(state->compositor_backend->data, state)) {
        log_info("Occlusion detection unavailable on this compositor");
        return false;
    }
    log_info("Occlusion detection enabled (%s)", state->compositor_backend->name);
    return true;
}

void occlusion_update(struct neowall_state *state) {
    if (!state || !state->compositor_backend) {
        return;
    }
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    if (ops && ops->occlusion_update) {
        ops->occlusion_update(state->compositor_backend->data, state);
    }
}

void occlusion_cleanup(struct neowall_state *state) {
    if (!state || !state->compositor_backend) {
        return;
    }
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    if (ops && ops->occlusion_cleanup) {
        ops->occlusion_cleanup(state->compositor_backend->data);
    }
}
