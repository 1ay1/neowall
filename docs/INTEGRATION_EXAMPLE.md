# Compositor Abstraction Integration Example

This document shows how to integrate the compositor abstraction layer into NeoWall's existing codebase.

## Overview

The compositor abstraction replaces direct wlr-layer-shell protocol calls with a backend-agnostic API. This allows NeoWall to work on any Wayland compositor.

## Quick Integration Steps

### 1. Include New Header

**File:** `src/wayland.c`, `src/output.c`, `src/main.c`

```c
// Old includes
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// New includes
#include "compositor/compositor.h"
```

### 2. Update State Structure

**File:** `include/neowall.h`

```c
struct neowall_state {
    /* Wayland globals */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    
    // OLD: Direct layer shell reference
    // struct zwlr_layer_shell_v1 *layer_shell;
    
    // NEW: Compositor backend abstraction
    struct compositor_backend *compositor_backend;
    
    /* Rest of state... */
};

struct output_state {
    struct wl_output *output;
    
    // OLD: Direct layer surface
    // struct zwlr_layer_surface_v1 *layer_surface;
    // struct wl_surface *surface;
    
    // NEW: Compositor surface abstraction
    struct compositor_surface *compositor_surface;
    
    /* Rest of output state... */
};
```

### 3. Initialize Backend

**File:** `src/wayland.c`

```c
bool wayland_init(struct neowall_state *state) {
    /* Initialize Wayland display connection */
    state->display = wl_display_connect(NULL);
    if (!state->display) {
        log_error("Failed to connect to Wayland display");
        return false;
    }
    
    /* Get registry and bind to globals */
    state->registry = wl_display_get_registry(state->display);
    wl_registry_add_listener(state->registry, &registry_listener, state);
    wl_display_roundtrip(state->display);
    
    /* OLD: Check for layer shell
    if (!state->layer_shell) {
        log_error("Compositor does not support wlr-layer-shell");
        return false;
    }
    */
    
    /* NEW: Initialize compositor backend (auto-detects) */
    state->compositor_backend = compositor_backend_init(state);
    if (!state->compositor_backend) {
        log_error("Failed to initialize compositor backend");
        log_error("Your compositor may not be supported");
        return false;
    }
    
    /* Log compositor info */
    compositor_info_t info = compositor_detect(state->display);
    log_info("Compositor: %s", info.name);
    log_info("Backend: %s", state->compositor_backend->name);
    
    return true;
}

void wayland_cleanup(struct neowall_state *state) {
    /* NEW: Cleanup compositor backend */
    if (state->compositor_backend) {
        compositor_backend_cleanup(state->compositor_backend);
        state->compositor_backend = NULL;
    }
    
    /* OLD: Cleanup layer shell
    if (state->layer_shell) {
        zwlr_layer_shell_v1_destroy(state->layer_shell);
    }
    */
    
    /* Cleanup other Wayland globals */
    if (state->compositor) {
        wl_compositor_destroy(state->compositor);
    }
    if (state->registry) {
        wl_registry_destroy(state->registry);
    }
    if (state->display) {
        wl_display_disconnect(state->display);
    }
}
```

### 4. Create Output Surfaces

**File:** `src/output.c`

```c
bool output_configure_layer_surface(struct output_state *output) {
    /* OLD: Direct layer surface creation
    output->surface = wl_compositor_create_surface(output->state->compositor);
    if (!output->surface) {
        log_error("Failed to create Wayland surface");
        return false;
    }
    
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        output->state->layer_shell,
        output->surface,
        output->output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        "neowall"
    );
    
    if (!output->layer_surface) {
        log_error("Failed to create layer surface");
        return false;
    }
    
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                       &layer_surface_listener,
                                       output);
    
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface,
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    
    wl_surface_commit(output->surface);
    */
    
    /* NEW: Compositor-agnostic surface creation */
    compositor_surface_config_t config = {
        .layer = COMPOSITOR_LAYER_BACKGROUND,
        .anchor = COMPOSITOR_ANCHOR_FILL,
        .exclusive_zone = -1,
        .keyboard_interactivity = false,
        .width = 0,   /* Auto from configure event */
        .height = 0,  /* Auto from configure event */
        .output = output->output,
    };
    
    output->compositor_surface = compositor_surface_create(
        output->state->compositor_backend,
        &config
    );
    
    if (!output->compositor_surface) {
        log_error("Failed to create compositor surface for output");
        return false;
    }
    
    /* Set callbacks for configure events */
    compositor_surface_set_callbacks(
        output->compositor_surface,
        output_on_configure,
        output_on_closed,
        output
    );
    
    /* Commit initial configuration */
    compositor_surface_commit(output->compositor_surface);
    
    return true;
}

/* Callback when compositor configures our surface */
static void output_on_configure(struct compositor_surface *surface,
                               int32_t width, int32_t height) {
    struct output_state *output = surface->user_data;
    
    log_debug("Output %s configured: %dx%d", output->name, width, height);
    
    output->width = width;
    output->height = height;
    output->configured = true;
    
    /* Create EGL surface if not already created */
    if (!output->egl_surface) {
        output_create_egl_surface(output);
    }
}

/* Callback when compositor closes our surface */
static void output_on_closed(struct compositor_surface *surface) {
    struct output_state *output = surface->user_data;
    
    log_info("Output %s surface closed by compositor", output->name);
    
    /* Cleanup and recreate if necessary */
    output_destroy(output);
}
```

### 5. Create EGL Surfaces

**File:** `src/output.c`

```c
bool output_create_egl_surface(struct output_state *output) {
    if (!output || !output->compositor_surface) {
        log_error("Invalid output for EGL surface creation");
        return false;
    }
    
    /* OLD: Direct EGL window creation
    output->egl_window = wl_egl_window_create(output->surface,
                                              output->width,
                                              output->height);
    if (!output->egl_window) {
        log_error("Failed to create EGL window");
        return false;
    }
    
    output->egl_surface = eglCreateWindowSurface(
        output->state->egl_display,
        output->state->egl_config,
        (EGLNativeWindowType)output->egl_window,
        NULL
    );
    */
    
    /* NEW: Compositor-agnostic EGL surface creation */
    output->egl_surface = compositor_surface_create_egl(
        output->compositor_surface,
        output->state->egl_display,
        output->state->egl_config,
        output->width,
        output->height
    );
    
    if (output->egl_surface == EGL_NO_SURFACE) {
        log_error("Failed to create EGL surface for output %s", output->name);
        return false;
    }
    
    log_debug("EGL surface created for output %s: %dx%d",
              output->name, output->width, output->height);
    
    return true;
}
```

### 6. Cleanup Output

**File:** `src/output.c`

```c
void output_destroy(struct output_state *output) {
    if (!output) {
        return;
    }
    
    log_debug("Destroying output: %s", output->name);
    
    /* Cleanup rendering resources */
    render_cleanup_output(output);
    
    /* OLD: Cleanup EGL and layer surface
    if (output->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(output->state->egl_display, output->egl_surface);
    }
    if (output->egl_window) {
        wl_egl_window_destroy(output->egl_window);
    }
    if (output->layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
    }
    if (output->surface) {
        wl_surface_destroy(output->surface);
    }
    */
    
    /* NEW: Compositor-agnostic cleanup */
    if (output->compositor_surface) {
        /* Destroy EGL surface */
        if (output->egl_surface != EGL_NO_SURFACE) {
            compositor_surface_destroy_egl(
                output->compositor_surface,
                output->state->egl_display
            );
        }
        
        /* Destroy compositor surface */
        compositor_surface_destroy(output->compositor_surface);
        output->compositor_surface = NULL;
    }
    
    /* Cleanup output resources */
    if (output->output) {
        wl_output_destroy(output->output);
    }
    
    free(output);
}
```

### 7. Surface Commits

**File:** `src/render.c`

```c
bool render_frame(struct output_state *output) {
    if (!output || !output->compositor_surface) {
        return false;
    }
    
    /* Make EGL context current */
    eglMakeCurrent(output->state->egl_display,
                   output->egl_surface,
                   output->egl_surface,
                   output->state->egl_context);
    
    /* Render frame */
    glClear(GL_COLOR_BUFFER_BIT);
    /* ... render wallpaper ... */
    
    /* Swap buffers */
    eglSwapBuffers(output->state->egl_display, output->egl_surface);
    
    /* OLD: Commit surface
    wl_surface_commit(output->surface);
    */
    
    /* NEW: Compositor-agnostic commit */
    compositor_surface_commit(output->compositor_surface);
    
    return true;
}
```

### 8. Handle Output Resize

**File:** `src/output.c`

```c
void output_handle_geometry(struct output_state *output,
                           int32_t width, int32_t height) {
    if (output->width == width && output->height == height) {
        return;  /* No change */
    }
    
    log_debug("Output %s resized: %dx%d -> %dx%d",
              output->name, output->width, output->height, width, height);
    
    output->width = width;
    output->height = height;
    
    /* OLD: Resize EGL window
    if (output->egl_window) {
        wl_egl_window_resize(output->egl_window, width, height, 0, 0);
    }
    */
    
    /* NEW: Compositor-agnostic resize */
    if (output->compositor_surface) {
        compositor_surface_resize_egl(output->compositor_surface, width, height);
    }
}
```

## Complete Example: Output Lifecycle

```c
/* Create output */
struct output_state *output = output_create(state, wl_output, name);

/* Configure compositor surface */
output_configure_layer_surface(output);

/* Wait for configure event... */

/* Create EGL surface (after configured) */
output_create_egl_surface(output);

/* Initialize rendering */
render_init_output(output);

/* Render loop */
while (running) {
    render_frame(output);
    compositor_surface_commit(output->compositor_surface);
    wl_display_dispatch(state->display);
}

/* Cleanup */
output_destroy(output);
```

## Feature Detection

Check backend capabilities before using optional features:

```c
compositor_capabilities_t caps = 
    compositor_backend_get_capabilities(state->compositor_backend);

if (caps & COMPOSITOR_CAP_LAYER_SHELL) {
    log_info("Backend supports layer shell - full features available");
}

if (caps & COMPOSITOR_CAP_EXCLUSIVE_ZONE) {
    /* Can use exclusive zones */
    config.exclusive_zone = 32;
}

if (!(caps & COMPOSITOR_CAP_KEYBOARD_INTERACTIVITY)) {
    log_info("Backend cannot control keyboard interactivity");
}
```

## Error Handling

Always check return values:

```c
struct compositor_backend *backend = compositor_backend_init(state);
if (!backend) {
    log_error("No suitable compositor backend found");
    log_error("Supported backends:");
    log_error("  - wlr-layer-shell (Hyprland, Sway, River)");
    log_error("  - KDE Plasma Shell (not yet implemented)");
    log_error("  - Fallback (limited features)");
    return false;
}

struct compositor_surface *surface = 
    compositor_surface_create(backend, &config);
if (!surface) {
    log_error("Failed to create surface");
    log_error("Backend: %s", backend->name);
    log_error("Check compositor logs for more details");
    return false;
}
```

## Migration Checklist

- [ ] Update `neowall.h` with new state structures
- [ ] Replace layer shell init with backend init in `wayland.c`
- [ ] Update output creation in `output.c`
- [ ] Replace surface creation with abstraction API
- [ ] Update EGL surface creation
- [ ] Update surface commits in render loop
- [ ] Update cleanup code
- [ ] Remove direct protocol includes
- [ ] Test on multiple compositors
- [ ] Update documentation

## Benefits After Migration

✅ **Works on any Wayland compositor** (not just wlroots)
✅ **Automatic backend selection** (no user configuration)
✅ **Future-proof** (easy to add new backends)
✅ **Graceful degradation** (fallback for unsupported compositors)
✅ **Same performance** (zero overhead abstraction)
✅ **Better error messages** (backend-specific debugging)

## Testing After Migration

```bash
# Build with compositor abstraction
make clean && make -j$(nproc)

# Test on Hyprland (should use wlr-layer-shell backend)
Hyprland
neowall -fv
# Look for: [INFO] Using backend: wlr-layer-shell

# Test on Sway (should use wlr-layer-shell backend)
sway
neowall -fv
# Look for: [INFO] Using backend: wlr-layer-shell

# Test on unsupported compositor (should use fallback)
weston
neowall -fv
# Look for: [INFO] Using backend: fallback
# Note: May have limited features

# Verify wallpaper appears correctly on all outputs
# Verify hot-reload still works
# Verify multi-monitor support
# Verify shader rendering
```

## Troubleshooting

### Backend Selection Fails

```
[ERROR] No suitable compositor backend found
```

**Solution:** Check that compositor protocols are available. Run:
```bash
wayland-info | grep -E "zwlr_layer_shell|org_kde_plasma_shell"
```

### Surface Not Visible

```
[ERROR] Failed to create compositor surface
```

**Solution:** Check backend implementation logs. Enable verbose mode:
```bash
neowall -fv 2>&1 | grep -E "backend|surface"
```

### EGL Errors

```
[ERROR] Failed to create EGL surface
```

**Solution:** Verify compositor surface was created and configured:
```c
if (!compositor_surface_is_ready(output->compositor_surface)) {
    log_error("Compositor surface not ready for EGL");
}
```

## Next Steps

After integration:

1. **Test thoroughly** on multiple compositors
2. **Implement KDE backend** for KDE Plasma support
3. **Implement GNOME backend** for GNOME Shell support
4. **Add backend-specific optimizations** as needed
5. **Update user documentation** with supported compositors
6. **Add CI tests** for different compositor types

## Questions?

- Check the main README: `src/compositor/README.md`
- Review backend implementations: `src/compositor/backends/*.c`
- Open an issue on GitHub