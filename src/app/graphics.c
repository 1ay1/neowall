/* graphics: the GL context and each output's surface, brought up after the
 * backend and torn down before it. */

#include "neowall/app/app.h"
#include "neowall/surface/surface.h"
#include "neowall/core/log.h"
#include "neowall/egl/egl_core.h"
#include "neowall/egl/gpu_health.h"
#include "neowall/output/output.h"
#include "neowall/state/state.h"

static const char *output_name(const struct output_state *o) {
    const char *n = o->connector_name[0] ? o->connector_name : o->model;
    return n[0] ? n : "unknown";
}

/* Graphics up: the shared context from the backend's display, then every
 * output that already has a size gets its surface and render state, then the
 * wallpapers that were waiting for a surface. */
bool app_graphics_up(struct neowall_state *state) {
    const struct compositor_backend *b = state->compositor_backend;
    if (!b || !b->ops) {
        log_error("No compositor backend available for EGL initialization");
        return false;
    }
    void *native = b->ops->get_native_display ? b->ops->get_native_display(b->data) : NULL;
    EGLenum platform = b->ops->get_egl_platform ? b->ops->get_egl_platform(b->data)
                                                : EGL_PLATFORM_WAYLAND_KHR;
    if (!egl_core_init(state, native, platform)) return false;

    pthread_rwlock_rdlock(&state->output_list_lock);
    bool first = true;
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->width <= 0 || o->height <= 0) continue;
        if (!output_create_egl_surface(o)) {
            log_error("Failed to create EGL surface for output %s", output_name(o));
            continue;
        }
        if (!output_make_current(o)) {
            log_error("Failed to make context current for output %s", output_name(o));
            continue;
        }
        if (first) {
            if (!eglSwapInterval(state->egl_display, 1)) log_debug("Could not set vsync");
            if (state->gpu_health_pending) {
                gpu_health_init(state->gl_robust, log_get_level() >= LOG_LEVEL_DEBUG);
                state->gpu_health_pending = false;
            }
            first = false;
        }
        if (!output_init_render(o)) log_error("Failed to initialize rendering for output %s", output_name(o));
    }
    for (struct output_state *o = state->outputs; o; o = o->next) output_apply_deferred_config(o);
    pthread_rwlock_unlock(&state->output_list_lock);
    return true;
}

/* Graphics down: shared GL objects and surfaces before the context. */
void app_graphics_down(struct neowall_state *state) {
    if (state->egl_display != EGL_NO_DISPLAY && state->egl_context != EGL_NO_CONTEXT) {
        bool current = false;
        for (struct output_state *o = state->outputs; o && !current; o = o->next)
            current = output_make_current(o);
        if (current) output_release_shared_gl();
    }
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->compositor_surface && o->compositor_surface->egl_surface != EGL_NO_SURFACE)
            compositor_surface_destroy_egl(o->compositor_surface, state->egl_display);
    }
    egl_core_cleanup(state);
}
