# neowall architecture

This document is the map of the codebase: the module boundaries, the data flow,
the threading and lock model, and the conventions every file follows. Read it
before making structural changes.

---

## 1. One-paragraph mental model

neowall is a single-process daemon that renders a wallpaper (a static image, an
image slideshow, a live GLSL shader, or a full terminal running any TUI) onto
every monitor, on both Wayland and X11. It runs on a small **Elm-style
runtime**: one loop thread waits on the compositor socket, timers, signals
(via signalfd) and a mailbox, turns each into a message, folds it into the
model, and draws. All display-server specifics sit behind a **compositor
backend vtable**, so the core never sees a `wl_*` or `X*` type. Rendering goes
through **EGL → OpenGL 3.3 core**.

```
config.vibe ─► config_load ─► output_state(s)
                                   │
   signals / control socket ─┐     ▼
                             │  ┌─────────── rt loop (src/runtime/rt.c) ───────────┐
   compositor events ────────┼─►│ msg → update(model) → subscribe(timers) → present │
   timers / worker posts ────┘  └─────────────────────────┬───────────────────────┘
                                                    ▼
                              compositor backend vtable  ◄──►  EGL / OpenGL 3.3
                              (wlr-layer-shell │ kde │ x11)
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
  constants.h  child_signals.h  ipc.h  app.h
  runtime/rt.h               the Elm-style loop's API
  compositor/                compositor abstraction + backend interfaces
  config/                    vibe parser + config model
  output/                    per-monitor state + lifecycle, frame_clock.h
  render/  image/  egl/       rendering, image decode, EGL context
  occlusion/                 "pause when covered" dispatcher
  source/                    open data plane: live scalar/texture providers
                             (see architecture/SHADER_ENGINE_V2.md)
  gfx/  graph/               GL object layer, render graph (engine v2 slices)
  shader/                    shader system: source model, engine, per-output
                             wallshader, terminal textures, clock, docs
  terminal/                  in-tree VT/xterm emulator (terminal-as-wallpaper)
src/                       IMPLEMENTATION (.c) + truly-private impl headers
  main.c                     CLI, daemonize, startup/shutdown
  runtime/rt.c               the loop: wait, messages, timers, mailbox
  app.c  app_render.inc      the program: update / subscribe / present
  ipc.c                      control socket (CLI ↔ daemon)
  watch.c                    `neowall watch` file watcher
  clock.c                    virtual calendar clock (`--date`, `--timelapse`)
  utils.c
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
  probe) and picks the highest-priority backend that fits, or refuses with a
  clear error when nothing can show a wallpaper there.

Backends: `wlr-layer-shell` (Hyprland/Sway/River/wlroots), `kde-plasma`
and `x11` (EWMH + root window).
GNOME/Mutter on Wayland is refused with a clear error: it has no wallpaper
surface role for clients.

**Adding a backend:** implement `compositor_backend_ops`, call
`compositor_backend_register()` in your init, and the registry auto-selects it.
No core changes required.

---

## 5. The runtime (`src/runtime/`, `src/app.c`)

neowall runs on a small Elm-style loop in C. `rt` waits on fds, timers and
signals and turns each into a message; `app_update` folds it into the model
(the only place state changes); `app_subscribe` declares which timers should
exist; `app_present` draws (`render_outputs`, snapshot → render → present,
with a reference held on each output so hotplug can't free one mid-frame).
Other threads `app_post()` a message instead of setting a flag.

vsync outputs on Wayland are paced by their own frame callback and swap with
interval 0, so one slow monitor can't stall the others. vsync-off outputs use a
phase-locked pacer (`output_pace_advance`) whose next deadline is a timer
subscription.

The CLI talks to a running daemon over a unix socket (`src/ipc.c`,
`$XDG_RUNTIME_DIR/neowall.sock`); signals remain as a fallback when nothing
answers on the socket.

Full description: [architecture/RUNTIME.md](architecture/RUNTIME.md).

---

## 6. Threading & lock model

One thread owns state: the runtime loop does config, update, render and
present. Every other thread only produces input:

| Thread | Does | Talks to the loop by |
|--------|------|----------------------|
| slideshow preload (per output) | decodes the next image | mutex-guarded handoff + atomic flag; cooperatively cancelled, always joined in `output_destroy` (libpng/libjpeg aren't cancel-safe) |
| terminal reader (per terminal) | drains the PTY into the screen model | sets `needs_redraw`, posts a wake only if the loop armed one (`app_request_output_redraw`) |
| reactive sampler | reads /proc, /sys, hwmon at 4 Hz | publishes a snapshot under `g_lock` (`src/shader/reactive.c`) |
| audio capture | runs `parec` on `@DEFAULT_MONITOR@`, FFT | same snapshot; only started when a shader reads audio |
| nvidia-smi reader | parses `nvidia-smi -lms 500` | same snapshot |

Workers never touch the model. `app_post()` is the one way in: it counts
posts in flight, and `app_run` waits for them to drain before freeing the
loop, so a late post can't use a freed mailbox.

Spawned children (terminal shell, `parec`, `nvidia-smi`, `exec()` sources)
start with a clean signal mask and default SIGPIPE
(`include/neowall/child_signals.h`). The daemon blocks its control signals for
signalfd and ignores SIGPIPE; without the reset, children inherit both and
can't be stopped.

**Locks (acquire in this order — documented in `neowall.h`):**

1. `output_list_lock` (rwlock) — guards the output linked list structure.
2. `state_mutex` — guards individual fields.

Coarse-before-fine prevents deadlock. Never acquire them reversed, and never
take `output_list_lock` again while already holding it (a recursive rdlock can
deadlock behind a waiting writer).

**Atomics:** every flag shared across threads is `atomic_*` with explicit
ordering (`acquire`/`release` on flags, `acq_rel` on the refcount release).
Command state (pause, next, set-index) is not shared at all: it lives in the
loop's model and only changes through `update()`.

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
core`. Four layers, each usable on its own:

- `source.c` (`shader_source.h`) — GL-free. A `.glsl` (plus its `.neowall`
  sidecar) becomes passes, channel bindings, `// @param`s and manifest
  uniform bindings, with every `#include` expanded and a map from each
  generated line back to `file:line`. Passes are ordered so each runs after
  the ones it reads; there's no fixed Buffer A–D list. Tested headless in
  `test_shader_source`, fuzzed by `fuzz_shader`.
- `assemble.c` + `compat.c` — the full fragment text for a pass: uniforms,
  stdlib, `#line`-tagged user code, `main()`. `compat.c` fixes what real
  exported shaders do that desktop drivers reject (redeclared `iTime`,
  `precision`, non-constant `const` globals).
- `engine.c` (`engine.h`) — the GL half. Compiles every pass at once (in
  parallel where the driver has `KHR_parallel_shader_compile`), owns the
  buffer targets, draws a frame from a plain `nw_frame_in`. Driver errors
  come back as `file:line` with the source line and a caret, including inside
  `#include`d files. Every pass renders every frame at full resolution.
- `wallshader.c` — one output's running shader: source + engine + manifest
  bindings (opened as `nw_source` providers) + reactive uniforms + iState
  persistence + an optional terminal (`term_gpu.c`, which owns the PTY and
  the `iTerm*` textures). This is the only part `output.c` and `render.c`
  use. A shader that fails to load leaves the current one on screen.

**Shader time.** `iTime` is a float32, so two things keep it exact:

- *Frame clock* (`src/output/frame_clock.c`). On Wayland each frame is stamped
  with the vblank it will be shown at, from presentation-time feedback, so
  `iTime` advances by exactly one refresh per refresh whatever the draw-start
  jitter. `iTimeDelta` uses the same stamps. X11, or before the first present,
  falls back to the wall clock.
- *Rebase* (`src/shader/shader_clock.c`). Past an hour, the clock resets for
  free while the output is hidden. A wallpaper that's never hidden resets
  after 2 hours behind a 1.2 s cross-fade from its last frame, before float32
  steps get coarse (7.8 ms after a day, 62.5 ms after ten).

Reactive load and input uniforms (`iCpu`, `iRam`, key/mouse energy, ...) are
sampled at 4 Hz and eased per frame with a ~0.25 s time constant, so nothing
they drive steps.

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

`meson test -C build` runs the whole suite headless (no display, no GPU). The
ones that matter most when changing the runtime:

| Test | Covers |
|------|--------|
| `runtime` | rt loop: timers keep phase and never fire early, posts arrive in order, signals become messages, an idle loop sleeps |
| `ipc` | control socket end to end, stuck/oversize/malformed clients |
| `terminal_wake` | a real child on a real PTY wakes an idle-gated renderer |
| `child_signals` | spawned children get an empty signal mask and default SIGPIPE |
| `frame_clock` | vblank stamping under jitter, timestamp noise, missed vblanks, latency changes |
| `shader_clock` | the rebase policy, exhaustively |
| `output_refcount` | refcount protocol under concurrency (8 threads × 20 000 iterations) |
| `shader_source` | passes, includes, params, manifests, default bindings, pass order, line maps |
| `shader_gl` | the engine on llvmpipe: pixels, feedback, spanning, errors as file:line, every shipped shader |
| `foundation`, `vibe`, `utils`, `shadertoy_compat`, ... | pure units |

CI (`.github/workflows/`):

- `build.yml` — multi-distro build matrix (Arch / Debian / Ubuntu, Wayland-only,
  X11-only, both) plus an Xvfb RandR smoke test.
- `quality.yml` — the full suite under ASan+UBSan, the concurrency tests under
  **ThreadSanitizer**, clang static analyzer + `clang-tidy`, `gcc -fanalyzer`,
  `clang-format`, and a check that every version string agrees.
- `release.yml` — on a `v*` tag: Arch, Fedora, openSUSE, Debian, Ubuntu
  packages, an AppImage, the AUR push and the flake version bump.

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
- **Signals:** blocked in every thread at startup and read from a signalfd by
  the runtime, never real work in a handler.
- **Child processes:** reset the signal state first
  (`nw_child_reset_signals()` after fork, `nw_spawnattr_reset_signals()` for
  posix_spawn).
- **Cross-thread input:** `app_post()` a message; never write the model from
  another thread.
