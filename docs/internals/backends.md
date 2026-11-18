# Compositor Backend ABI

This page is the contract you implement when porting neowall to a new
compositor or display system. The structs and headers live in
[`include/neowall/surface/surface.h`](../../include/neowall/surface/surface.h);
existing backends are under
[`src/backends/`](../../src/backends/).

If you just want to add support for a wlroots-based compositor that ships
`wlr-layer-shell-v1`, you almost certainly don't need a new backend — the
existing `wlr_layer_shell` backend works. Read the [Layering](#layering)
section first.

---

## Layering

On Wayland there are two tiers: `backends/wayland/wayland_core.c` owns the
connection, outputs, frame callbacks and present feedback, and a
compositor-specific backend under `backends/wayland/compositors/` decides how
to host the wallpaper surface. X11 is a single backend.

```
runtime loop (src/runtime/rt.c, src/app/)
    │
    ▼
compositor_backend  ◀── picked from $WAYLAND_DISPLAY / $DISPLAY, the
    │                   desktop env vars and protocol probe (registry.c)
    │
    ├─ wlr_layer_shell    (wlroots: Sway, Hyprland, river, …)
    ├─ kde_plasma         (layer shell with click pass-through, for KWin)
    └─ x11                (EWMH root-window + per-output viewports)
```

A backend is picked at startup by `compositor_backend_init()`. On Wayland it
chooses one backend by compositor type and calls its `init()`; if that returns
NULL, startup fails with an error rather than trying a surface the compositor
can't map. If the Wayland connection itself fails and `$DISPLAY` is set, it
falls back to X11.

---

## What you implement

A backend exposes a `const compositor_backend_ops_t *ops` table. Every
function pointer either has a real implementation or is `NULL` (meaning
"not supported" — the runtime gracefully skips it where allowed).

### Required ops

These must not be NULL. Nothing checks this at startup; the core just calls
them.

| Op | What it does |
|---|---|
| `init` | Allocate backend-specific state, bind protocols. Returns the opaque `backend_data` pointer passed back to every other op. NULL = backend unavailable on this system. |
| `cleanup` | Tear down state from `init`. |
| `create_surface` / `destroy_surface` / `configure_surface` / `commit_surface` | The surface lifecycle. One surface per wallpaper output. |
| `create_egl_window` / `resize_egl_window` / `destroy_egl_window` / `get_native_window` | Plumbing for EGL. neowall renders via OpenGL; the backend tells EGL what to wrap. |
| `get_capabilities` | Bitmask of `compositor_capabilities_t` flags so the runtime knows what to ask for. |
| `damage_surface` | Mark a region as needing repaint. |
| `get_fd` / `dispatch_events` / `flush` | Event-loop integration. `get_fd` returns a pollable fd; `dispatch_events` drains it. |
| `get_native_display` / `get_egl_platform` | EGL needs the native display handle (`wl_display*` or `Display*`) and the platform enum. |

### Optional ops (NULL = not supported, runtime falls back)

| Op | Purpose |
|---|---|
| `prepare_events` / `read_events` / `cancel_read` | Wayland's `wl_display_prepare_read` dance. X11 leaves these NULL. |
| `has_pending_events` | True when events are already queued where poll() can't see them (Xlib's queue), so the loop dispatches instead of sleeping. |
| `frame_request` / `frame_ready` / `frame_cancel` | Per-surface frame pacing: ask for a frame callback on the next commit, report when it arrived (or a timeout passed). Lets each output pace itself instead of one slow monitor blocking the swap. |
| `present_feedback` | Ask for the real flip timestamp of the next commit, which the frame pacer phase-locks to. NULL = the pacer anchors on swap time. |
| `set_vsync` | Change the tearing/vsync hint on a live surface. |
| `set_scale` | HiDPI buffer scale. NULL means scale 1 forever. |
| `on_output_added` / `on_output_removed` | Hotplug. NULL = static output list. |
| `init_outputs` | For backends that synthesise outputs (X11 walks XRandR). |
| `sync` | Roundtrip (Wayland) or `XSync` (X11). |
| `get_error` | Surface connection errors back to the loop. |
| `occlusion_init` / `occlusion_update` / `occlusion_cleanup` | Fullscreen-window detection so the renderer can pause. Big win on wlr backends. |
| `apply_input_config` | Called after `config_load`; lets a backend release/rebind input devices when `mouse_interaction` changes. |

### Capability bits

`compositor_capabilities_t` is the cheap, cap-bit version of the vtable.
Currently `LAYER_SHELL`, `SUBSURFACES`, `VIEWPORT`, `EXCLUSIVE_ZONE`,
`KEYBOARD_INTERACTIVITY`, `ANCHOR`, `MULTI_OUTPUT` and `OCCLUSION` (each
prefixed `COMPOSITOR_CAP_`); see `surface.h` for the live list.
Today they're only logged at startup; the core decides what to call by
whether an optional op is NULL, not by these bits.

---

## Backend lifetime

```
compositor_backend_init(state)
    └── picks one backend for the detected compositor:
            init() → if non-NULL, this is your backend
                   → if NULL, startup fails

every runtime step (see docs/internals/runtime.md):
    prepare:  prepare_events() ; flush() ; has_pending_events()
    wait:     ppoll(get_fd(), timers, mailbox)
    fd ready: read_events()       (or cancel_read() if it wasn't)
    after:    dispatch_events() ; get_error()
    present:  occlusion_update()  ← if op present
              frame_ready()?  render, present_feedback(), frame_request(), swap

config_load() runs:
    apply_input_config()  ← if op present

shutdown:
    cleanup()
```

`backend_data` is yours to allocate however you want; the runtime treats it
as opaque. Allocate it in `init()` and free it in `cleanup()`.

---

## Threading rules

- All ops are called from the runtime's loop thread, which is also the GL
  thread. No locking needed for backend-internal state.
- Never call `wl_display_roundtrip` / `dispatch` or `XSync` while holding
  `output_list_lock`: a listener that runs during the dispatch may take it
  for writing (hotplug removal), and that deadlocks.
- Add and remove monitors only with `output_list_add` / `output_list_take*` /
  `output_list_clear` (`output/output.h`); they take the lock for you.
- Tell the app about changes with `state_output_dirty()`,
  `state_outputs_changed()` and `state_output_ready()` (`state/state.h`).
- Worker threads (terminal PTY readers, the reactive sampler, audio, preload)
  never touch the backend; they post to the loop.

---

## Adding a new backend: minimum viable patch

1. For a Wayland compositor, add `src/backends/wayland/compositors/<name>.c`
   implementing `compositor_backend_ops_t` (copy `kde_plasma.c`).
2. Export a `const compositor_backend_desc_t <name>_backend` and declare it in
   `surface.h`.
3. Add it to the `wayland_backends` table in `src/backends/registry.c` and
   teach `select_wayland_backend()` when to pick it.
4. Add a `meson.build` entry (look at how the existing backends are
   conditionally compiled with `has_wayland` / `has_x11`).
5. Update [`backends/README.md`](../../src/backends/README.md).

That's the whole contract. The "compositor-specific" parts you'll spend
real time on are surface creation, layering / z-order, and occlusion
detection — those are the rabbit holes worth budgeting time for.

---

## Conventions

- All ops document their preconditions in the header comment in
  `surface.h`. If you can't satisfy a precondition, leave the op NULL.
- Log via `log_info` / `log_error` from `neowall/core/log.h` — same prefix
  scheme as the rest of the daemon.
- No `printf` or `fprintf` from a backend. The daemon may be detached from
  stdio; the log system routes correctly.

---

## See also

- [`src/backends/README.md`](../../src/backends/README.md) — file layout and
  how a backend is selected.
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — where backends fit in the
  runtime and rendering pipeline.
