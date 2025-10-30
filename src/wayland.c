#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "neowall.h"
#include "../protocols/wlr-layer-shell-unstable-v1-client-protocol.h"
#include "../protocols/xdg-shell-client-protocol.h"

/* Output listener callbacks */
static void output_handle_geometry(void *data, struct wl_output *wl_output,
                                   int32_t x, int32_t y,
                                   int32_t physical_width, int32_t physical_height,
                                   int32_t subpixel,
                                   const char *make, const char *model,
                                   int32_t transform) {
    struct output_state *output = data;
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;

    if (make) {
        strncpy(output->make, make, sizeof(output->make) - 1);
    }
    if (model) {
        strncpy(output->model, model, sizeof(output->model) - 1);
    }
    output->transform = transform;

    log_debug("Output %s: geometry - make=%s, model=%s, transform=%d",
              output->model, output->make, output->model, transform);
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh) {
    struct output_state *output = data;
    (void)wl_output;
    (void)refresh;

    if (flags & WL_OUTPUT_MODE_CURRENT) {
        output->width = width;
        output->height = height;
        output->needs_redraw = true;

        log_info("Output %s: mode %dx%d @ %d mHz",
                 output->model[0] ? output->model : "unknown",
                 width, height, refresh);
    }
}

static void output_handle_done(void *data, struct wl_output *wl_output) {
    struct output_state *output = data;
    (void)wl_output;

    output->configured = true;
    log_info("Output %s: configuration done (reconnect recovery enabled)",
              output->model[0] ? output->model : "unknown");
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t factor) {
    struct output_state *output = data;
    (void)wl_output;

    output->scale = factor;
    output->needs_redraw = true;

    log_debug("Output %s: scale factor %d",
              output->model[0] ? output->model : "unknown", factor);
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
};

/* Layer surface listener callbacks */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
    struct output_state *output = data;

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    bool dimensions_changed = false;
    if (output->width != (int32_t)width || output->height != (int32_t)height) {
        output->width = width;
        output->height = height;
        output->needs_redraw = true;
        dimensions_changed = true;

        log_info("Layer surface configured for output %s: %dx%d (reconnection detected)",
                 output->model[0] ? output->model : "unknown", width, height);

        /* Recreate EGL surface with new dimensions */
        if (output->egl_window) {
            wl_egl_window_resize(output->egl_window, width, height, 0, 0);
            log_debug("Resized EGL window for output %s", output->model);
        } else {
            log_debug("No EGL window to resize for output %s, will be created later", output->model);
        }
    }

    /* Apply deferred configuration if surface just became ready */
    if (dimensions_changed && output->egl_surface != EGL_NO_SURFACE && output->egl_window) {
        log_debug("Surface ready after configuration, applying deferred config for output %s",
                  output->model[0] ? output->model : "unknown");
        output_apply_deferred_config(output);
    }
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *layer_surface) {
    struct output_state *output = data;
    (void)layer_surface;

    log_info("Layer surface closed for output %s", output->model);
    output_destroy(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* Registry listener callbacks */
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    struct neowall_state *state = data;
    (void)version;

    log_debug("Registry: interface=%s, name=%u, version=%u", interface, name, version);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name,
                                            &wl_compositor_interface, 4);
        log_info("Bound to compositor");
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        log_info("Bound to shared memory");
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output_obj = wl_registry_bind(registry, name,
                                                         &wl_output_interface, 3);
        struct output_state *output = output_create(state, output_obj, name);

        if (output) {
            wl_output_add_listener(output_obj, &output_listener, output);
            log_info("New output detected (name=%u, model=%s) - will initialize on configuration", 
                     name, output->model[0] ? output->model : "pending");
            /* Set flag to trigger initialization in event loop */
            state->outputs_need_init = true;
            log_debug("Set outputs_need_init flag, will initialize after Wayland events are processed");
        } else {
            log_error("Failed to create output state");
            wl_output_destroy(output_obj);
        }
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell = wl_registry_bind(registry, name,
                                             &zwlr_layer_shell_v1_interface, 1);
        log_info("Bound to wlr-layer-shell");
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name) {
    struct neowall_state *state = data;
    (void)registry;

    log_info("Registry: global removed (name=%u)", name);

    /* Find and remove the output with this name */
    struct output_state **output_ptr = &state->outputs;
    while (*output_ptr) {
        struct output_state *output = *output_ptr;
        if (output->name == name) {
            log_info("Removing output %s (name=%u)", output->model, name);
            *output_ptr = output->next;
            output_destroy(output);
            state->output_count--;
            return;
        }
        output_ptr = &output->next;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* Public Wayland functions */
bool wayland_init(struct neowall_state *state) {
    if (!state) {
        log_error("Invalid state pointer");
        return false;
    }

    /* Connect to Wayland display */
    state->display = wl_display_connect(NULL);
    if (!state->display) {
        log_error("Failed to connect to Wayland display. Is WAYLAND_DISPLAY set?");
        log_error("Make sure you're running under a Wayland compositor.");
        return false;
    }

    log_info("Connected to Wayland display");

    /* Get registry */
    state->registry = wl_display_get_registry(state->display);
    if (!state->registry) {
        log_error("Failed to get Wayland registry");
        wl_display_disconnect(state->display);
        state->display = NULL;
        return false;
    }

    /* Add registry listener */
    wl_registry_add_listener(state->registry, &registry_listener, state);

    /* Roundtrip to get all globals */
    wl_display_roundtrip(state->display);

    /* Verify we have required interfaces */
    if (!state->compositor) {
        log_error("Compositor not available");
        wayland_cleanup(state);
        return false;
    }

    if (!state->layer_shell) {
        log_error("wlr-layer-shell protocol not available");
        log_error("Your compositor must support wlr-layer-shell-unstable-v1");
        log_error("Supported compositors: Sway, Hyprland, river, etc.");
        wayland_cleanup(state);
        return false;
    }

    if (state->output_count == 0) {
        log_error("No outputs detected");
        wayland_cleanup(state);
        return false;
    }

    log_info("Found %u output(s)", state->output_count);

    /* Do another roundtrip to ensure outputs are configured */
    wl_display_roundtrip(state->display);

    /* Configure layer surfaces for all outputs */
    struct output_state *output = state->outputs;
    while (output) {
        if (!output_configure_layer_surface(output)) {
            log_error("Failed to configure layer surface for output %s", output->model);
        }
        output = output->next;
    }

    /* Final roundtrip */
    wl_display_roundtrip(state->display);

    return true;
}

void wayland_cleanup(struct neowall_state *state) {
    if (!state) {
        return;
    }

    log_debug("Cleaning up Wayland resources");

    /* Destroy all outputs */
    while (state->outputs) {
        struct output_state *next = state->outputs->next;
        output_destroy(state->outputs);
        state->outputs = next;
    }
    state->output_count = 0;

    /* Destroy Wayland objects */
    if (state->layer_shell) {
        zwlr_layer_shell_v1_destroy(state->layer_shell);
        state->layer_shell = NULL;
    }

    if (state->shm) {
        wl_shm_destroy(state->shm);
        state->shm = NULL;
    }

    if (state->compositor) {
        wl_compositor_destroy(state->compositor);
        state->compositor = NULL;
    }

    if (state->registry) {
        wl_registry_destroy(state->registry);
        state->registry = NULL;
    }

    if (state->display) {
        wl_display_disconnect(state->display);
        state->display = NULL;
    }

    log_debug("Wayland cleanup complete");
}

/* Configure layer surface for an output */
bool output_configure_layer_surface(struct output_state *output) {
    if (!output || !output->surface) {
        log_error("Invalid output or surface");
        return false;
    }

    struct neowall_state *state = output->state;

    /* Create layer surface */
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state->layer_shell,
        output->surface,
        output->output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        "neowall"
    );

    if (!output->layer_surface) {
        log_error("Failed to create layer surface");
        return false;
    }

    /* Configure layer surface */
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);

    /* Add listener */
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                       &layer_surface_listener, output);

    /* Commit surface to trigger configure event */
    wl_surface_commit(output->surface);

    log_debug("Layer surface configured for output %s", output->model);

    return true;
}