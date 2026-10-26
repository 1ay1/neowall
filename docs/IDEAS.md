# NeoWall — Ideas Backlog

> For how the existing system works (uniforms, std-lib, manifests, build), see
> [`SHADER_NOTES.md`](./SHADER_NOTES.md). This file is the forward-looking backlog.

Cool, distinctive features for NeoWall. Each is grounded in plumbing the daemon
already has: foreign-toplevel window data, the audio FFT, `glReadPixels`, and the
reactive uniform pipeline. Ordered loosely by "wow per unit of effort."

The throughline: most wallpaper tools are output-only — they paint pixels and ignore
everything else. NeoWall already receives data about your windows, your audio, and
the screen itself. The interesting features close feedback loops between the wallpaper
and the live machine.

---

## At a glance

| Idea | Effort | New deps | Already have |
|------|--------|----------|--------------|
| ~~Window-aware / light leaks~~ | — | — | **shipped** (`iWindows`, Hyprland) |
| Shadertoy import | S | HTTPS fetch (on demand) | multipass + manifest format |
| Record a clip | S | ffmpeg (pipe) | offscreen render path |
| Album-art theming | M | MPRIS / playerctl | `parec` spawn, palette helpers |
| ~~Garden grows over days~~ | — | — | **shipped** (`garden.glsl`) |
| System stress as weather | S | none | `iCpu`/`iGpu`/`iLoad`/temps |
| Battery as light | S | none | `iBattery`/`iCharging` |
| Circadian palette | S | none | `iTimeOfDay`/`iSun` |
| Weather mirror | M | weather fetch | rain shader exists |
| Ambilight | L | `wlr-screencopy` | X11 `glReadPixels` |
| Frosted glass | S | none | multipass buffers |
| App-id mood | S | none | foreign-toplevel app-id |
| Workspace as world | M | compositor IPC | per-output rendering |
| Notification bloom | M | D-Bus listen | ripple shader exists |
| Power modes | S | ext-idle-notify | `pause-shader`, `shader_fps` |
| ~~Live hot-swap~~ | — | — | **shipped** (`neowall watch`) |
| ~~Save-to-reload~~ | — | — | **shipped** (`neowall watch`) |
| Click interaction | S | none | `iMouse` plumbing |
| Scene packs | M | packaging | manifest format |

Effort: S = a sitting, M = a day or two, L = a real project.

---

## Tier 1 — Showpieces (unique to NeoWall's existing capabilities)

### Window-aware rendering ("light leaks") — shipped on Hyprland
Shaders get `iWindows[]` (pixel rects, y down), `iWindowCount` and
`iFocusedWindow`, from Hyprland's IPC; `window_forge.glsl` is the demo.
`wlr-foreign-toplevel-management` is bound too, but it carries window state
(fullscreen, maximised, activated), not geometry, so other compositors report
`iWindowCount = 0` until they expose rects some other way.

Still open: an `iWindowActivity` pulse on focus/move, and skipping
fully-covered regions as a perf win.

### Album-art theming
`reactive.c` already spawns `parec`. Add an MPRIS (D-Bus) or `playerctl` poll to read
the current track. Extract the cover art's dominant palette and expose:

- `iAccentColor` — dominant album-art color
- `iTrackProgress` — 0..1 through the current song
- `iTrackChanged` — pulses on song change

The whole shader recolors to match the music's cover art.

### A garden that grows over days
A reaction-diffusion or L-system shader seeded from the date, with state persisted to
disk between sessions. It evolves continuously — come back next week and it has spread
into new territory. Wallpaper with continuity instead of a 10-second loop.

---

## Tier 2 — Atmosphere

### System stress as weather
Calm clear sky when idle; clouds gather and lightning strikes as CPU/GPU load climbs.
The machine's effort becomes the weather. Uses `iCpu`, `iGpu`, `iLoad`, temps.

### Battery as light
Warmth and brightness fade as the battery drains; plugging in makes the scene glow
back to life. Uses `iBattery`, `iCharging`.

### Mood drift / circadian palette
Palette migrates across the day: cool dawn, bright noon, amber dusk, deep night —
tied to the real clock and sun elevation. Uniforms (`iTimeOfDay`, `iSun`) already
exist; this is mostly a polished shader.

### Weather mirror
Pull local weather; real rain on the glass when it rains outside, snow when it snows,
fog at dawn. Needs a small weather fetch added to the sampler.

---

## Tier 3 — Screen feedback loops

### Ambilight
Sample actual screen content (X11 `glReadPixels` is already throttled to 1 Hz; Wayland
needs `wlr-screencopy`, a sibling of protocols already bound). Extract edge/dominant
colors, expose as `iAmbient`. The wallpaper harmonizes with whatever is on screen.

### Frosted glass
The wallpaper renders a live blur of itself into a feedback buffer, so semi-transparent
terminals get a genuine frosted-glass backdrop. Uses the existing multipass buffers.

---

## Tier 4 — Desktop awareness

### App-id → mood
Map the focused app to a vibe: editor/terminal → calm, desaturated, low motion;
fullscreen video/game → dim to near-black or pause entirely (saves GPU + battery).
The desktop's behavior drives the art.

### Workspace as a world
Each workspace is a different time of day or biome; switching slides the sun across the
sky or crossfades biomes. Needs compositor IPC (Hyprland/sway) for the active workspace.

### Notification bloom
Hook D-Bus notifications — a ripple or bloom crosses the wallpaper when something
arrives. Peripheral awareness baked into the background.

---

## Tier 5 — Interaction & authoring (turns it into a platform)

### Live hot-swap
`neowall shader <name>` swaps the active shader instantly — no restart, no flash. The
IPC table (`next`/`set`/`pause`) and the command-file mechanism already exist; add one
command that writes a path and signals an in-place recompile.

### Save-to-reload dev mode
`neowall watch foo.glsl` — inotify recompile on every save, errors printed inline. This
is what makes people actually *write* shaders for NeoWall; today the test loop is manual.

### Click-through interaction
Click the desktop and the wallpaper responds at the click point — ripples, spawns,
paint. Pointer plumbing is already wired for `iMouse`.

### Shareable scene packs
A `.neowallpkg` bundling shader + manifest + textures + default reactive bindings, with
a one-line install and a small browsable gallery. Ecosystem over features.

---

## Reactive-data extensions (unlock richer dashboards)

- Network throughput in real MB/s (expose raw byte-rate, not just normalized)
- GPU VRAM gauge via `mem_info_vram_used`
- CPU frequency via `scaling_cur_freq`
- Fan RPM via `hwmon fan*_input`
- Per-disk I/O traces
- Microphone input as a separate reactive channel (voice ripples)

---

## Top three next

1. **`neowall import <shadertoy-id>`** — fetch a Shadertoy shader with its
   buffers and channel bindings, write the `.glsl` + `.neowall`, preview it.
   Makes the whole Shadertoy catalogue a wallpaper in one command. Network only
   when the user runs it.
2. **`neowall record 10s out.mp4`** — render the live wallpaper offscreen at
   full rate and pipe frames to ffmpeg, for sharing.
3. **Album-art theming** — MPRIS accent colour, track progress and a
   track-change pulse, so the wallpaper follows what's playing.

---

## Light leaks: what shipped

The window data comes from Hyprland's IPC (a throttled snapshot shared with
the occlusion path), published per frame as `iWindows[]` (pixel rects, y
down), `iWindowCount` and `iFocusedWindow`; `window_forge.glsl` is the
demo. Still open from the original sketch: the same data on other
compositors (wlr-foreign-toplevel carries state but not geometry), a decaying
`iWindowActivity` pulse on focus/move, and early-out for fully covered tiles.

## Anti-goals

Things to deliberately NOT build, so the project keeps its shape:

- **A config GUI.** NeoWall is a daemon driven by a text config and a few IPC
  commands. A settings app is a different project. Keep the surface a file + CLI.
- **A shader editor / IDE.** Hot-reload (`neowall watch`) is the right scope; an
  in-app editor is not. People have editors.
- **Bundling a compositor or window manager.** NeoWall is a client. It must keep
  working as a guest on whatever the user already runs.
- **Telemetry or any network call the user did not ask for.** Weather/album-art
  fetches must be opt-in and clearly local-only otherwise.
- **Per-frame heap allocation on the render path.** Every reactive/window snapshot
  is copied by value into a frame-coherent struct; keep it that way.
- **Breaking Shadertoy compatibility.** Every addition is additive. A vanilla
  Shadertoy shader must always run unmodified.

---

## Shipped

Features from this backlog that have landed. Move items here as they ship.

- Reactive uniform pipeline (CPU/GPU/temps/RAM/net/disk/load/battery/time/input).
- Audio FFT channel (`iAudio`, bass/mid/treble/beat) via spawned `parec`.
- GLSL standard library injected into every shader (noise, palettes, SDFs, audio).
- `.neowall` manifest format (channels, multipass buffers, named uniform binds).
- Ten bundled reactive example shaders, including the `system_deck` dashboard.
- **Live hot-swap / save-to-reload** — `neowall watch <shader>` recompiles on
  every save, prints GLSL errors inline, and validates on a throwaway copy so a
  broken save keeps the last good shader on screen.
- **`neowall preview <shader>`** — run a shader in the foreground beside your
  real wallpaper without touching the config, saved state, or the daemon's pid
  file.
- **Shader time travel** — `--date=+6d` and `--timelapse=30d/20s` move the
  shader-visible clock (`iDate`/`iTimeOfDay`/`iSun`/`iDayFraction`) without
  touching the system clock, making day-scale shaders testable at all.
- **Persistent shader state** — `iState[4]`/`iStateAge` plus a manifest
  `state <bufferX>` binding, saved under `$XDG_STATE_HOME` and keyed by shader
  path, so a wallpaper can accumulate across restarts rather than only across
  frames. This is the plumbing the "garden that grows over days" needs.
- **Version floor** — `#pragma neowall requires X.Y` turns "silently receives
  nothing on an old daemon" into a clear refusal; `neowall current` reports the
  running daemon's binary and warns on drift from the CLI.
- `garden.glsl` — a calendar-driven garden whose plants run multi-day life
  cycles, reacting to CPU (wind), temperature (drought), RAM (foliage) and
  battery (light).
- **Window awareness** — `iWindows[]` / `iWindowCount` / `iFocusedWindow` on
  Hyprland, with `window_forge.glsl` as the showpiece.
- **Readable shader errors** — `neowall watch` reports `file:line` with the
  source line, a caret and "did you mean"; `neowall docs` looks up helpers.
- **Exact frame timing** (0.8) — `iTime` stamped with the presented vblank on
  Wayland, eased reactive uniforms, and a cross-faded clock reset so a
  wallpaper stays smooth for weeks.

_The authoring platform (Tier 5) and the first Tier 1 showpieces have
landed, which makes the rest substantially cheaper to build._
