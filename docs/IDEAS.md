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
| Window-aware / light leaks | M | none | foreign-toplevel data |
| Album-art theming | M | MPRIS / playerctl | `parec` spawn, palette helpers |
| Garden grows over days | M | state file | multipass buffers, noise lib |
| System stress as weather | S | none | `iCpu`/`iGpu`/`iLoad`/temps |
| Battery as light | S | none | `iBattery`/`iCharging` |
| Circadian palette | S | none | `iTimeOfDay`/`iSun` |
| Weather mirror | M | weather fetch | rain shader exists |
| Ambilight | L | `wlr-screencopy` | X11 `glReadPixels` |
| Frosted glass | S | none | multipass buffers |
| App-id mood | S | none | foreign-toplevel app-id |
| Workspace as world | M | compositor IPC | per-output rendering |
| Notification bloom | M | D-Bus listen | ripple shader exists |
| Live hot-swap | S | none | IPC table, command file |
| Save-to-reload | S | inotify | live-test workflow |
| Click interaction | S | none | `iMouse` plumbing |
| Scene packs | M | packaging | manifest format |

Effort: S = a sitting, M = a day or two, L = a real project.

---

## Tier 1 — Showpieces (unique to NeoWall's existing capabilities)

### Window-aware rendering ("light leaks")
The `wlr-foreign-toplevel-management` protocol is already bound, so the daemon knows
every window's rect, app-id, title, and focus state — currently unused by shaders.
Expose it:

- `iWindowCount` — number of open windows
- `iWindows[N]` — `vec4` rects (x, y, w, h) in screen space
- `iFocusedRect` — the focused window's rect
- `iWindowActivity` — pulses when focus changes or a window moves

Demo: godrays and glow seep from behind the focused window and follow it as it moves.
Bonus: skip animating fully-covered regions — a real perf win, not just an effect.

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

## Top three for sheer "whoa"

1. **Light leaks behind windows** — visually striking, doubles as a perf win, and uses
   window data no other wallpaper tool exposes.
2. **Album-art theming** — the wallpaper becomes part of your listening experience.
3. **The garden that grows over days** — gives the wallpaper continuity and a life of
   its own.

---

## If I build one next: light leaks (sketch)

The data already arrives over `wlr-foreign-toplevel-management` and is discarded. The
minimal path to first light:

1. **Capture** — in the toplevel event handlers, keep a small array of `{x, y, w, h,
   focused}` per window. Most of this is already parsed for window management; it just
   needs to be retained in a snapshot, same shape as the reactive snapshot.
2. **Publish** — add `iWindowCount`, `iWindows[16]` (`vec4` rects, normalized to
   `iResolution`), `iFocusedRect`, and `iWindowActivity` (a decaying pulse on focus or
   move) to the wrapper preamble and the per-frame uniform set, next to the reactive
   uniforms.
3. **Shade** — a demo that, for each pixel, finds the nearest window edge via the
   existing `sdBox` SDF, and emits a soft glow that is brightest along the focused
   window's border:

   ```glsl
   float d = 1e9;
   for (int i = 0; i < iWindowCount; i++) {
       vec4 w = iWindows[i];
       d = min(d, sdBox(uv - (w.xy + w.zw * 0.5), w.zw * 0.5));
   }
   float leak = exp(-40.0 * abs(d)) * (0.5 + 0.5 * iWindowActivity);
   col += leak * nwPalette(0.6 + 0.2 * iTimeOfDay);
   ```

4. **Optimize** — once rects are known, fully-covered tiles can early-out, recovering
   the cost of the feature and then some.

Risk: the array must be updated on the main loop thread and copied into the frame
snapshot the same way reactive data is, to stay lock-free on the render path. The
reactive subsystem is the template to copy.

---

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

_Nothing from Tiers 1–5 has shipped yet — that is the open work._
