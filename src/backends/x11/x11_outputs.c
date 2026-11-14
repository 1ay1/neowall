/* Monitors: enumerate them with RandR and keep the output list in step on
 * hotplug. */

#include "x11_internal.h"

/* Enumerate the active monitors of the X screen.
 *
 * Prefers XRandR 1.5 monitors (XRRGetMonitors) which already merges CRTCs into
 * logical monitors and carries real connector names. Falls back to walking
 * active CRTCs (RandR < 1.5), and finally to a single whole-screen monitor when
 * RandR is unavailable. Writes up to `max` entries and returns the count. */
static int x11_enumerate_monitors(x11_backend_data_t *backend, x11_monitor_t *mons, int max) {
    Display *dpy = backend->x_display;
    int count = 0;

    if (backend->has_xrandr) {
        int n = 0;
        XRRMonitorInfo *info = XRRGetMonitors(dpy, backend->root_window, True, &n);
        if (info && n > 0) {
            for (int i = 0; i < n && count < max; i++) {
                if (info[i].width <= 0 || info[i].height <= 0) continue;
                char *cn = info[i].name ? XGetAtomName(dpy, info[i].name) : NULL;
                snprintf(mons[count].name, sizeof(mons[count].name), "%.63s",
                         cn ? cn : "X11-0");
                if (cn) XFree(cn);
                mons[count].x = info[i].x;
                mons[count].y = info[i].y;
                mons[count].width = info[i].width;
                mons[count].height = info[i].height;
                count++;
            }
            XRRFreeMonitors(info);
            if (count > 0) return count;
        } else if (info) {
            XRRFreeMonitors(info);
        }

        /* Fallback: active CRTCs with their connector names (RandR < 1.5). */
        XRRScreenResources *res = XRRGetScreenResources(dpy, backend->root_window);
        if (res) {
            for (int i = 0; i < res->ncrtc && count < max; i++) {
                XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
                if (!ci) continue;
                if (ci->mode != None && ci->noutput > 0 && ci->width > 0 && ci->height > 0) {
                    const char *name = NULL;
                    XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, ci->outputs[0]);
                    if (oi && oi->name) name = oi->name;
                    snprintf(mons[count].name, sizeof(mons[count].name), "%.63s",
                             name ? name : "X11-0");
                    if (oi) XRRFreeOutputInfo(oi);
                    mons[count].x = ci->x;
                    mons[count].y = ci->y;
                    mons[count].width = ci->width;
                    mons[count].height = ci->height;
                    count++;
                }
                XRRFreeCrtcInfo(ci);
            }
            XRRFreeScreenResources(res);
            if (count > 0) return count;
        }
    }

    /* Last resort: one monitor covering the whole screen. */
    snprintf(mons[0].name, sizeof(mons[0].name), "X11-0");
    mons[0].x = 0;
    mons[0].y = 0;
    mons[0].width = DisplayWidth(dpy, backend->screen);
    mons[0].height = DisplayHeight(dpy, backend->screen);
    return 1;
}

/* Build one output_state for a monitor and link it into the global list. The
 * compositor surface is created lazily by the caller. Returns NULL on OOM. */
static struct output_state *x11_make_output(struct neowall_state *state,
                                            const x11_monitor_t *mon) {
    struct output_state *out = output_new(state, NULL, 0);
    if (!out) return NULL;
    /* model and connector_name both carry the real RandR name so per-output
     * config blocks (matched by either) work and `neowall current` is right. */
    snprintf(out->model, sizeof(out->model), "%.63s", mon->name);
    snprintf(out->connector_name, sizeof(out->connector_name), "%.63s", mon->name);
    out->x_offset = mon->x;
    out->y_offset = mon->y;
    out->pixel_width = out->width = out->logical_width = mon->width;
    out->pixel_height = out->height = out->logical_height = mon->height;
    out->configured = true;   /* X11 has no configure round trip */
    return out;
}

/* Create and attach the positioned wallpaper window for an output that has a
 * geometry but no compositor surface yet. Adopts the real surface geometry the
 * backend ends up with. Returns false (and leaves the output surface-less) on
 * failure. Caller must NOT hold output_list_lock. */
bool x11_attach_surface(struct neowall_state *state, struct output_state *out) {
    compositor_surface_config_t surface_config = {
        .output = NULL,
        .x = out->x_offset,
        .y = out->y_offset,
        .width = out->width,
        .height = out->height,
        .layer = COMPOSITOR_LAYER_BACKGROUND,
        .anchor = COMPOSITOR_ANCHOR_FILL,
        .exclusive_zone = 0,
        .keyboard_interactivity = false,
    };

    out->compositor_surface =
        compositor_surface_create(state->compositor_backend, &surface_config);
    if (!out->compositor_surface) {
        return false;
    }

    /* Adopt the real surface geometry (the backend may have clamped). */
    out->width = out->compositor_surface->width;
    out->height = out->compositor_surface->height;
    out->pixel_width = out->compositor_surface->width;
    out->pixel_height = out->compositor_surface->height;
    out->logical_width = out->compositor_surface->width;
    out->logical_height = out->compositor_surface->height;
    return true;
}

/* Initialize outputs for X11 backend: one output + one positioned wallpaper
 * window per active monitor. */
bool x11_init_outputs(void *backend_data, struct neowall_state *state) {
    x11_backend_data_t *backend = backend_data;
    if (!backend || !state) {
        return false;
    }

    x11_monitor_t mons[NW_MAX_OUTPUTS];
    int n = x11_enumerate_monitors(backend, mons, NW_MAX_OUTPUTS);
    log_info("X11: detected %d monitor(s)", n);

    int created = 0;
    for (int i = 0; i < n; i++) {
        log_info("  monitor %s: %dx%d at +%d+%d",
                 mons[i].name, mons[i].width, mons[i].height, mons[i].x, mons[i].y);

        struct output_state *out = x11_make_output(state, &mons[i]);
        if (!out) continue;

        output_list_add(state, out);
        if (!x11_attach_surface(state, out)) {
            log_error("Failed to create compositor surface for monitor %s", mons[i].name);
            if (output_list_take(state, out)) output_unref(out);
            continue;
        }

        log_info("X11 output ready: %s (%dx%d at +%d+%d)",
                 out->model, out->width, out->height, out->x_offset, out->y_offset);
        created++;
    }

    if (created == 0) {
        log_error("X11: failed to create any outputs");
        return false;
    }

    return true;
}

/* ============================================================================
 * HOTPLUG RECONCILE (RRScreenChangeNotify)
 * ============================================================================ */

/* True if an output represents this physical connector. */
static bool x11_mon_same_name(const struct output_state *o, const x11_monitor_t *m) {
    return strncmp(o->connector_name, m->name, sizeof(o->connector_name)) == 0;
}

/* True if an output's geometry matches a freshly-enumerated monitor. */
static bool x11_mon_matches(const struct output_state *o, const x11_monitor_t *m) {
    if (strncmp(o->connector_name, m->name, sizeof(o->connector_name)) != 0) {
        return false;
    }
    x11_rect_t a = { o->x_offset, o->y_offset, o->width, o->height };
    x11_rect_t b = { m->x, m->y, m->width, m->height };
    return x11_rect_equal(a, b);
}

/* Bring a freshly-added output fully online: surface, EGL surface, GL render
 * state, and its wallpaper config. Runs on the event-loop thread, which owns the
 * EGL context, so the GL bring-up is safe inline. Caller must NOT hold
 * output_list_lock. Returns true on success. */
static bool x11_bring_output_online(struct neowall_state *state, struct output_state *out) {
    if (!x11_attach_surface(state, out)) {
        log_error("Hotplug: failed to create surface for %s", out->connector_name);
        return false;
    }
    if (out->width <= 0 || out->height <= 0) {
        log_error("Hotplug: %s has no dimensions", out->connector_name);
        return false;
    }
    if (!output_create_egl_surface(out) || !output_make_current(out) ||
        !output_init_render(out)) {
        log_error("Hotplug: GL bring-up failed for %s", out->connector_name);
        return false;
    }
    if (state_output_ready(state, out)) {
        atomic_store_explicit(&out->needs_redraw, true, memory_order_release);
    }
    log_info("Hotplug: output %s online (%dx%d at +%d+%d)",
             out->connector_name, out->width, out->height, out->x_offset, out->y_offset);
    return true;
}

/* Reconcile the output list against the current monitor layout after an
 * RRScreenChangeNotify. Removes outputs whose monitor vanished or changed
 * geometry, and adds outputs for newly-appeared monitors. Idempotent: a
 * spurious notify with an unchanged layout is a no-op. Runs on the event-loop
 * thread. */
void x11_reconcile_outputs(x11_backend_data_t *backend) {
    struct neowall_state *state = backend->state;
    if (!state) return;

    x11_monitor_t mons[NW_MAX_OUTPUTS];
    int n = x11_enumerate_monitors(backend, mons, NW_MAX_OUTPUTS);

    /* Pass 1: collect outputs whose physical connector vanished. Geometry
     * changes are reconciled in place below so live GL/render state survives. */
    struct output_state *stale[NW_MAX_OUTPUTS];
    int n_stale = 0;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o && n_stale < NW_MAX_OUTPUTS; o = o->next) {
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (x11_mon_same_name(o, &mons[i])) { found = true; break; }
        }
        if (!found) {
            output_ref(o);
            stale[n_stale++] = o;
        }
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    /* Unlink + drop each stale output. output_unref frees it (surface + EGL +
     * render state) once the list's reference and our transient ref are gone. */
    for (int i = 0; i < n_stale; i++) {
        struct output_state *o = stale[i];
        log_info("Hotplug: removing output %s (%dx%d at +%d+%d)",
                 o->connector_name, o->width, o->height, o->x_offset, o->y_offset);
        if (output_list_take(state, o)) {
            if (!remove_wallpaper_state(o->connector_name)) {
                log_warn("Hotplug: failed to remove persisted state for %s",
                         o->connector_name);
            }
            output_unref(o);  /* drop the list's reference */
        }
        output_unref(o);                /* drop our transient ref */
    }

    /* Pass 2: resize/reposition surviving physical outputs in place. */
    for (int i = 0; i < n; i++) {
        struct output_state *out = NULL;
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            if (x11_mon_same_name(o, &mons[i])) {
                output_ref(o);
                out = o;
                break;
            }
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        if (!out) continue;

        if (!x11_mon_matches(out, &mons[i])) {
            compositor_surface_config_t config = out->compositor_surface->config;
            config.x = mons[i].x;
            config.y = mons[i].y;
            config.width = mons[i].width;
            config.height = mons[i].height;
            if (compositor_surface_configure(out->compositor_surface, &config)) {
                out->x_offset = mons[i].x;
                out->y_offset = mons[i].y;
                out->width = out->pixel_width = out->logical_width = mons[i].width;
                out->height = out->pixel_height = out->logical_height = mons[i].height;
                output_notify_geometry_change(out);
                log_info("Hotplug: resized output %s to %dx%d at +%d+%d",
                         out->connector_name, out->width, out->height,
                         out->x_offset, out->y_offset);
            } else {
                log_warn("Hotplug: failed to reconcile geometry for %s",
                         out->connector_name);
            }
        }
        output_unref(out);
    }

    /* Pass 3: add outputs for monitors that have no physical output yet. */
    for (int i = 0; i < n; i++) {
        bool present = false;
        pthread_rwlock_rdlock(&state->output_list_lock);
        for (struct output_state *o = state->outputs; o; o = o->next) {
            if (x11_mon_same_name(o, &mons[i])) { present = true; break; }
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        if (present) continue;

        log_info("Hotplug: adding monitor %s: %dx%d at +%d+%d",
                 mons[i].name, mons[i].width, mons[i].height, mons[i].x, mons[i].y);
        struct output_state *out = x11_make_output(state, &mons[i]);
        if (!out) continue;

        output_list_add(state, out);
        if (!x11_bring_output_online(state, out) && output_list_take(state, out)) {
            output_unref(out);
        }
    }
}
