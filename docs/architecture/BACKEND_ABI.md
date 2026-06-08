# Compositor Backend ABI

This page is the contract you implement when porting neowall to a new
compositor or display system. The structs and headers live in
[`include/neowall/compositor/compositor.h`](../../include/neowall/compositor/compositor.h);
existing backends are under
[`src/compositor/backends/`](../../src/compositor/backends/).

If you just want to add support for a wlroots-based compositor that ships
`wlr-layer-shell-v1`, you almost certainly don't need a new backend — the
existing `wlr_layer_shell` backend works. Read the [Layering](#layering)
section first.

---

## Layering

neowall has two backend tiers:

1. **Transport backend** — wires up the connection to the display server.
   One per display protocol: `wayland`, `x11`.
2. **Compositor-specific backend** — chooses a strategy for hosting a
   wallpaper surface within that transport.

The compositor-specific tier is what changes between e.g. wlroots, KDE Plasma,
GNOME Shell — they all speak Wayland but expose different shell protocols.

```
event loop
    │
    ▼
compositor_backend  ◀── auto-detected from $XDG_SESSION_TYPE +
    │                   protocol availability (compositor_registry.c)
    │
    ├─ wlr_layer_shell    (wlroots: Sway, Hyprland, river, …)
    ├─ kde_plasma         (Plasma's org_kde_plasma_shell)
    ├─ gnome_shell        (stub — Mutter doesn't expose layer-shell)
    ├─ fallback           (any Wayland: borderless xdg_toplevel)
    └─ x11                (EWMH root-window + per-output viewports)
```

A backend is picked at startup by `compositor_backend_init()`. The highest-
`priority` backend whose `init()` returns non-NULL wins.

---

## What you implement

A backend exposes a `const compositor_backend_ops_t *ops` table. Every
function pointer either has a real implementation or is `NULL` (meaning
"not supported" — the runtime gracefully skips it where allowed).

### Required ops

These must not be NULL — `compositor_backend_init()` rejects the backend if
they are.

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
| `set_scale` | HiDPI buffer scale. NULL means scale 1 forever. |
| `on_output_added` / `on_output_removed` | Hotplug. NULL = static output list. |
| `init_outputs` | For backends that synthesise outputs (X11 walks XRandR). |
| `sync` | Roundtrip (Wayland) or `XSync` (X11). |
| `get_error` | Surface connection errors back to the loop. |
| `occlusion_init` / `occlusion_update` / `occlusion_cleanup` | Fullscreen-window detection so the renderer can pause. Big win on wlr backends. |
| `apply_input_config` | Called after `config_load`; lets a backend release/rebind input devices when `mouse_interaction` changes. |

### Capability bits

`compositor_capabilities_t` is the cheap, cap-bit version of the vtable.
Currently includes `LAYER_SHELL`, `KDE_SHELL`, `XDG_SHELL`, `MULTI_OUTPUT`,
`OCCLUSION`, `DAMAGE_TRACKING`, etc. — see compositor.h for the live list.
The runtime checks bits before asking for behaviour that's behind an
optional op, so you don't have to handle "asked for layer-shell on a
backend that doesn't have it".

---

## Backend lifetime

```
compositor_backend_init(state)
    └── tries each backend by priority order:
            init() → if NULL, try next
                   → if non-NULL, this is your backend

event loop runs:
    poll(get_fd())
    prepare_events() ; read_events() ; dispatch_events()
    occlusion_update()  ← if op present
    [render frame(s) for any non-occluded outputs]
    flush()

config_load() runs:
    apply_input_config()  ← if op present

shutdown:
    cleanup()
```

`backend_data` is yours to allocate however you want; the runtime treats it
as opaque. Most backends use a `static` struct or a `malloc`'d one — both
are fine.

---

## Threading rules

- All ops are called from the event-loop thread (main thread). No locking
  needed for backend-internal state.
- EXCEPTION: render-thread paths (`get_native_window`, EGL ops) are called
  from the GL context's thread, which may be the same thread or a dedicated
  one depending on configuration. Touching `wl_display` from there is
  unsafe — return immutable pointers only.
- Audio capture in `reactive.c` runs on its own thread but doesn't touch the
  backend; not relevant here.

---

## Adding a new backend: minimum viable patch

1. Create `src/compositor/backends/<name>/<name>_core.{c,h}`.
2. Implement `compositor_backend_<name>_init(struct neowall_state *state)`
   returning a `compositor_backend*` whose `ops` points at your vtable.
3. Add it to `src/compositor/compositor_registry.c`'s priority table.
4. Add a `meson.build` entry (look at how the existing backends are
   conditionally compiled with `has_wayland` / `has_x11`).
5. Update [`compositor/README.md`](../../src/compositor/README.md) with a
   one-line description.

That's the whole contract. The "compositor-specific" parts you'll spend
real time on are surface creation, layering / z-order, and occlusion
detection — those are the rabbit holes worth budgeting time for.

---

## Conventions

- All ops document their preconditions in the header comment in
  `compositor.h`. If you can't satisfy a precondition, leave the op NULL.
- Log via `log_info` / `log_error` from `neowall/log.h` — same prefix scheme
  as the rest of the daemon.
- No `printf` or `fprintf` from a backend. The daemon may be detached from
  stdio; the log system routes correctly.

---

## See also

- [`src/compositor/README.md`](../../src/compositor/README.md) — high-level
  overview of the layering, includes the per-compositor quirks.
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — where backends fit in the
  event loop and rendering pipeline.
