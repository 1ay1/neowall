# Backends

A backend puts a wallpaper surface on one kind of display server and hands
the core an EGL surface to draw into. The core never sees a `wl_*` or `X*`
type; it talks to backends only through `include/neowall/surface/surface.h`.

## Layout

```
src/backends/
├── registry.c                  detect the display server and compositor, pick a backend
├── wayland/
│   ├── wayland_core.c          display connection, outputs, frame callbacks, present feedback
│   ├── occlusion.c             fullscreen detection (foreign-toplevel)
│   ├── hyprland_coverage.c     window rects from Hyprland IPC (iWindows)
│   ├── frame_watchdog.c        flags outputs whose frame callbacks stop (an occlusion signal)
│   └── compositors/
│       ├── wlr_layer_shell.c   layer-shell surfaces (Hyprland, Sway, River, Wayfire)
│       ├── wlr_input.c         pointer, keyboard, seat for the wlr backend
│       ├── wlr_internal.h      shared by the two files above
│       └── kde_plasma.c        layer shell with an empty input region, for KWin
└── x11/                        see x11/README.md
```

## The interface

`compositor_backend_ops_t` (in `surface.h`) is a table of function pointers.
The required ones cover init/cleanup, creating, configuring, committing and
destroying a surface, its EGL window, and the event loop side (fd, prepare,
read, dispatch, flush). Optional ones may be NULL and the core copes:

- `frame_request` / `frame_ready` / `frame_cancel`: compositor-paced vsync.
- `present_feedback`: real flip timestamps for the frame pacer.
- `set_vsync`, `set_scale`, `set_keyboard_interactivity`.
- `occlusion_init` / `occlusion_update` / `occlusion_cleanup`.
- `on_output_added` / `on_output_removed`, `init_outputs`.

The core calls these through `compositor_surface_*()` wrappers in
`src/surface/surface.c`, never a backend's own functions.

Backends add and remove monitors only with `output_list_add`,
`output_list_take*` and `output_list_clear` from `output/output.h`, which own
the locking. They tell the app that something happened through the hooks on
`struct neowall_state` (`state_output_dirty()`, `state_outputs_changed()`,
`state_output_ready()`), since they can't include `app/`.

## Selection

`compositor_backend_init()` in `registry.c`:

1. `WAYLAND_DISPLAY` set: connect, probe the protocols, and work out the
   compositor from `XDG_CURRENT_DESKTOP` / `XDG_SESSION_DESKTOP` and friends.
   KWin gets `kde-plasma`; wlroots compositors, or anything with layer shell,
   get `wlr-layer-shell`. GNOME/Mutter is refused with an error unless it
   offers layer shell, because it has no other wallpaper role for clients. If
   connecting fails and `DISPLAY` is set, it falls back to X11.
2. Only `DISPLAY` set: X11.

Each Wayland backend is a `const compositor_backend_desc_t` (name, description,
priority, ops) listed in the `wayland_backends` table in `registry.c`. There is
no runtime registration. GNOME on X11 works through the X11 backend.

## Adding a Wayland backend

1. Add `src/backends/wayland/compositors/my_compositor.c` implementing
   `compositor_backend_ops_t`. `kde_plasma.c` is the smallest one to copy.
   Reuse `wayland_frame_request` / `wayland_frame_ready` /
   `wayland_frame_cancel` / `wayland_present_feedback` for the pacing ops.
2. Export a descriptor:

   ```c
   const compositor_backend_desc_t my_compositor_backend = {
       .name        = "my-compositor",
       .description = "what it's for",
       .priority    = 100,
       .ops         = &my_backend_ops,
   };
   ```

3. Declare it in `surface.h` next to `wlr_layer_shell_backend`, add it to
   `wayland_backends` in `registry.c`, and teach `select_wayland_backend()`
   when to pick it.
4. Add the file to `wayland_sources` in `meson.build`.
5. Run `scripts/check-layers.sh`: backends may include `core config egl output
   shader source state surface` and nothing above them.

## Testing

The e2e tests run the real daemon under a headless compositor, one per
backend: `meson test -C build --suite e2e` (`e2e_wayland`, `e2e_kde`,
`e2e_x11`, all driven by `tests/e2e_wayland.sh`). CI also builds with each
backend disabled (`-Dwayland_backend=disabled`, `-Dx11_backend=disabled`).

To see which backend was picked, run `neowall -f -v` and look for
`Detected compositor:` and `Selected backend:` (Wayland) or `X11 backend
initialized` in the log.
