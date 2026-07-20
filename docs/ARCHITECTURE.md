# neowall architecture

This document is the map of the codebase: the module boundaries, the data flow,
the threading and lock model, and the conventions every file follows. Read it
before making structural changes.

---

## 1. One-paragraph mental model

neowall is a single-process daemon that renders a wallpaper (a static image, an
image slideshow, a live GLSL shader, or a full terminal running any TUI) onto
every monitor, on both Wayland and X11. It is built around a **`poll(2)` event
loop** over a small set of file
descriptors (compositor socket, cycle timer, wakeup eventfd, signalfd, and
per-output frame timers). All display-server specifics sit behind a **compositor
backend vtable**, so the core never sees a `wl_*` or `X*` type. Rendering goes
through **EGL → OpenGL 3.3** (desktop) with a GLES 2.0 fallback.

```
config.vibe ─► config_load ─► output_state(s)
                                   │
   signals (next/set/pause) ─┐     ▼
                             │  ┌─────────────── event loop (poll) ───────────────┐
   compositor events ────────┼─►│ dispatch → occlusion → render_outputs → present │
   timers (cycle / frame) ───┘  └──────────────────┬──────────────────────────────┘
                                                    ▼
                              compositor backend vtable  ◄──►  EGL / OpenGL 3.3
                              (wlr-layer-shell │ kde │ gnome │ fallback │ x11)
```

---

## 2. Directory layout

The layout follows the convention used by libuv / wlroots / systemd: **all
public headers live under `include/neowall/`, mirroring the `src/` module tree,
and every cross-module include is fully qualified** (`#include
"neowall/<module>/<file>.h"`). A single `-Iinclude` resolves the whole public
surface; there are **zero relative-path includes** (`../`) anywhere in the tree.

```
include/neowall/          PUBLIC headers (the project's API surface)
  result.h  vec.h  defer.h   foundation primitives (header-only, dep-free)
  neowall.h                  app state + umbrella declarations
  constants.h
  compositor/                compositor abstraction + backend interfaces
  config/                    vibe parser + config model
  output/                    per-monitor state + lifecycle
  render/  image/  egl/       rendering, image decode, EGL context
  occlusion/                 "pause when covered" dispatcher
  shader/                    GLSL engine: multipass, shadertoy-compat, optimizers
  terminal/                  in-tree VT/xterm emulator (terminal-as-wallpaper)
src/                       IMPLEMENTATION (.c) + truly-private impl headers
  main.c eventloop.c utils.c
  compositor/backends/{wayland,x11}/   backend implementations; private headers
                                       (frame_watchdog.h, *_occlusion.h) live here
  terminal/                  utf8 · vtparse (ANSI/DEC state machine) · screen
                             (cell grid + control functions) · pty (forkpty +
                             reader thread) · glyph_atlas · cbdt (colour emoji)
                             · term_render (grid → GPU cell texture)
protocols/                 generated Wayland *-client-protocol.{c,h}
tests/                     headless unit/concurrency tests (run under sanitizers)
```

**Rule of thumb for a new header:** if more than one module includes it, it is
public → `include/neowall/<module>/`. If exactly one `.c` (or one backend) uses
it, it is private → keep it next to that `.c`.

---

## 3. Foundation primitives (`include/neowall/`)

These are small, header-only, dependency-free building blocks used throughout.
They exist so the rest of the code can be written in a modern, composable style.

| Header | Purpose |
|--------|---------|
| `result.h` | `nw_result` / `nw_status` — value-typed error handling that carries *why* a call failed, plus `NW_TRY()` for propagation. Replaces "return false and log somewhere". |
| `vec.h` | `nw_vec` + `NW_VEC_DEFINE_STATIC(Name, T)` — type-safe growable arrays. Replaces fixed-size `T arr[MAX_N]` caps that silently truncate. |
| `defer.h` | `NW_DEFER_FREE` / `NW_DEFER_FILE` / `NW_DEFER(fn)` — scope-bound cleanup via `__attribute__((cleanup))`, for leak-free early returns. |

All three are exercised by `tests/test_foundation.c` under ASan/UBSan/LSan.

---

## 4. The compositor abstraction (`include/neowall/compositor/compositor.h`)

This is the central architectural seam. A `compositor_backend_ops` struct is a
vtable of function pointers; each backend implements it and registers itself.

- **Opaque handles.** `struct compositor_surface` carries `void *native_surface`
  etc. — the core treats them as opaque. No `wl_*` / `X*` type appears in the
  public API.
- **Capability flags.** `compositor_capabilities_t` advertises what a backend
  supports (layer-shell, viewporter, occlusion, …) so the core can adapt instead
  of hard-coding per-compositor behaviour.
- **Registration + selection.** Backends call `compositor_backend_register()`;
  `compositor_registry.c` detects the running compositor (env vars + protocol
  probe) and picks the highest-priority backend that fits, with a `fallback`
  backend as the floor.

Backends: `wlr-layer-shell` (Hyprland/Sway/River/wlroots), `kde-plasma`,
`gnome-shell` (subsurface fallback), `fallback` (any compositor), and `x11`
(EWMH + root window).

**Adding a backend:** implement `compositor_backend_ops`, call
`compositor_backend_register()` in your init, and the registry auto-selects it.
No core changes required.

---

## 5. The event loop (`src/eventloop.c`)

A single `poll(2)` over:

| fd | source | meaning |
|----|--------|---------|
| `fds[0]` | compositor socket | Wayland/X11 events |
| `fds[1]` | `timerfd` | wallpaper cycle due |
| `fds[2]` | `eventfd` | internal wakeup (e.g. config events) |
| `fds[3]` | `signalfd` | `next`/`pause`/`set`/`kill` etc. — **race-free** signal handling, no work in a signal handler |
| `fds[4..]` | per-output `timerfd` | high-precision **phase-locked** frame pacing for vsync-off animated wallpapers |

There is **no polling thread** — the loop blocks in `poll` and wakes only on a
real event or a capped 1 s timeout (so signals stay responsive).

**Frame pacing.** vsync-off animated wallpapers (shaders + the live terminal)
are paced by a per-output `timerfd` armed as a **one-shot absolute deadline**
(`TFD_TIMER_ABSTIME`), re-armed after every present one period ahead. This is a
*phase-locked* schedule, not a free-running interval: it anchors to the real
present time so it can't drift against the display's vblank cadence (the source
of periodic judder a fixed-interval timer produces). Overruns snap the phase
forward instead of firing catch-up bursts. When the compositor supports
`wp_presentation`, the real flip timestamp + hardware refresh period feed the
pacer (`output_pace_note_present`), which then anchors to true present times and
quantises its period to a whole number of refreshes; without it the pacer falls
back to swap-completion time. Default presentation is **tearing-control async**
(immediate flips, bypasses compositor FPS caps); `vsync=true` uses EGL vsync.

`render_outputs()` runs in three phases, which is the key to its concurrency
safety:

1. **Snapshot** the output list under the read lock, taking a **reference** on
   each output, then **drop the lock**.
2. **Render** each output (cycling, transitions, `eglMakeCurrent`, draw) — all
   the *blocking* GL work happens here, with **no lock held**.
3. **Present**, still lockless:
   - compute the changed region — a crisp terminal that moved only a few rows
     reports a pixel band, everything else full-surface;
   - queue `wl_surface` damage + request `wp_presentation` feedback;
   - present via `eglSwapBuffersWithDamage` (only the changed rect is re-scanned;
     falls back to `eglSwapBuffers` when the extension is absent) → `commit`;
   - phase-lock the next frame to this present (`output_pace_advance`).

Finally it **unrefs** every snapshotted output. See §7 for why the ref matters.

---

## 6. Threading & lock model

neowall is mostly single-threaded (the event-loop thread does config, render,
and present). Two sources of concurrency exist:

- The **compositor dispatch** can add/remove outputs (hotplug) — on Wayland this
  happens during `dispatch_events`, on the same thread, but output *removal* can
  also be driven by `registry_handle_global_remove`.
- A **per-output background thread** decodes the next slideshow image off the GL
  thread (`preload_thread_func`), handing the decoded pixels back via a mutex +
  an atomic "upload pending" flag. It is **cooperatively cancelled**
  (`preload_should_stop`) and always **joined** in `output_destroy` — never
  `pthread_cancel`'d, because libpng/libjpeg are not async-cancel-safe.

**Locks (acquire in this order — documented in `neowall.h`):**

1. `output_list_lock` (rwlock) — guards the output linked list structure.
2. `state_mutex` — guards individual fields.

Coarse-before-fine prevents deadlock. Never acquire them reversed.

**Atomics:** every flag shared with a signal handler or the background thread is
`atomic_*` with explicit memory ordering (`acquire`/`release` on the flag,
`acq_rel` on the refcount release). Signal-set flags (`running`, `paused`,
`shader_paused`, `next_requested`, …) live in `neowall_state` as atomics.

---

## 7. Output lifetime: reference counting

`struct output_state` is **reference counted** (`atomic_int refcount`). This
closes a real use-after-free: the render loop drops `output_list_lock` before
doing blocking GL work, so without a ref a concurrent hotplug-removal could
`free()` an output mid-frame.

- `output_create()` returns an output with **refcount 1** (the list's reference).
- The render snapshot calls `output_ref()` on each output and `output_unref()`
  after presenting. While it holds a ref, the object cannot be freed.
- **Removal** is always: unlink under the write lock, then `output_unref()` the
  list's reference. The object is destroyed (`output_destroy`) exactly when the
  last ref drops to 0 — possibly the render loop's, possibly the list's,
  whichever is last.

The ref/unref protocol is proven race-free and leak-free under **ThreadSanitizer
and AddressSanitizer** by `tests/test_output_refcount.c` (8 threads × 20 000
iterations × 20 rounds).

---

## 8. The shader engine (`src/shader/`)

Accepts unmodified Shadertoy GLSL and compiles it under desktop `#version 330
core`:

- `shadertoy_compat.c` — pure, GL-free source-text rewrites (`texture2D` →
  `texture`, etc.); unit-tested headless in `tests/test_shadertoy_compat.c`.
- `shader_multipass.c` — multi-pass (Buffer A–D + Image) rendering with
  ping-pong FBOs, channel-binding heuristics, cached uniform locations.
- `adaptive_scale.c` / `*_optimizer.c` — adaptive resolution + GL state caching
  to hit the frame budget.

Channel bindings (which texture feeds each `iChannelN`) are recovered by a
documented best-effort heuristic, since a bare `.glsl` file carries no binding
metadata.

---

## 9. Configuration (`src/config/`)

`config.vibe` is parsed by the **VIBE** parser (vendored single-header libvibe
from <https://github.com/1ay1/vibe>, compiled once via `vibe_impl.c`) into a
value tree, then
mapped into `wallpaper_config` by `config.c`. The parser is strict: malformed
input (value on the wrong line, integer overflow, dangling key) is **rejected
with a line/column error** rather than silently producing wrong config. Covered
by `tests/test_vibe.c`.

---

## 10. Testing & quality gates

| Test | Covers | Sanitizers |
|------|--------|-----------|
| `test_foundation` | result / vec / defer | ASan, UBSan, LSan |
| `test_vibe` | config parser (happy + malformed) | ASan, UBSan, LSan |
| `test_utils` | math/format/path helpers, secure-runtime-dir | ASan, UBSan, LSan |
| `test_shadertoy_compat` | GLSL source rewrites | ASan, UBSan, LSan |
| `test_output_refcount` | refcount protocol under concurrency | **TSan**, ASan |

CI (`.github/workflows/`):

- `build.yml` — multi-distro build matrix (Arch / Debian / Ubuntu, Wayland-only,
  X11-only, both).
- `quality.yml` — ASan+UBSan test run, **ThreadSanitizer** run of the
  concurrency tests, clang static analyzer (`scan-build`), `clang-tidy`, and a
  `clang-format` check.

Style is pinned by `.clang-format`, `.clang-tidy`, and `.editorconfig`:
4-space indent, K&R braces, 100-column soft limit, pointers bound to the type.

---

## 11. Conventions cheat-sheet

- **Includes:** always `#include "neowall/<module>/<file>.h"`. Never `../`.
- **Errors:** multi-reason failures return `nw_result`; trivial predicates may
  return `bool`.
- **Ownership:** constructors return ownership; `*_destroy` / `*_free` consume
  it. A pointer is either owned (you free it) or borrowed (you don't) — say which
  in a comment when it isn't obvious.
- **Dynamic arrays:** use `nw_vec` / `NW_VEC_DEFINE_STATIC`, not `T arr[MAX_N]`.
- **Cross-thread flags:** `atomic_*` with explicit ordering, never a plain `bool`.
- **Cleanup ladders:** prefer `NW_DEFER_*` for scope-bound resources over nested
  `if (!x) { free(...); return; }`.
- **Signals:** handled via `signalfd` in the event loop, never real work in a
  handler.
