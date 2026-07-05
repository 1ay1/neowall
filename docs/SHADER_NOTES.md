# NeoWall Shader Notes

> For features that don't exist yet (planned/proposed), see
> [`IDEAS.md`](./IDEAS.md). This file documents what's shipped and how to use it.

A working reference for authoring reactive shaders in NeoWall — the GPU live-wallpaper
daemon for Wayland and X11. This document collects the practical knowledge needed to
write shaders that feel *alive*: ones that breathe with your music, react to your
mouse, and reflect the live state of your machine.

---

## Contents

0. [Quick Start](#0-quick-start)
1. [What NeoWall Is](#1-what-neowall-is)
2. [The Reactive Uniform Catalog](#2-the-reactive-uniform-catalog)
3. [The Standard Library](#3-the-standard-library)
4. [Manifests](#4-manifests)
5. [Testing a Shader Live](#5-testing-a-shader-live)
6. [Build Notes](#6-build-notes)
7. [Performance & Multipass](#7-performance--multipass)
8. [Troubleshooting](#8-troubleshooting)
9. [Bundled Examples](#9-bundled-examples)
10. [Ideas Not Yet Built](#10-ideas-not-yet-built)
11. [Recipes](#11-recipes)
12. [Glossary](#12-glossary)
13. [FAQ](#13-faq)

---

## 0. Quick Start

Drop this in `~/.config/neowall/shaders/pulse.glsl` and point your config at it. It
breathes with your music and flushes warm when the CPU works hard — a complete
reactive shader in a dozen lines:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);

    // bass drives a breathing ring; treble adds shimmer
    float ring = pulse(r - 0.3 - 0.15 * iAudioBass);
    float shimmer = 0.5 + 0.5 * sin(40.0 * r - iTime * 4.0);
    ring *= mix(1.0, shimmer, iAudioTreble);

    // CPU load warms the palette from teal toward red
    vec3 col = nwPalette(0.55 - 0.25 * iCpu + 0.1 * iAudioLevel);
    col *= ring;
    col += iAudioBeat * 0.15;            // flash on the beat

    fragColor = vec4(nwTonemap(col), 1.0);
}
```

Everything in capitals or `i`-prefixed above is injected for you — no `uniform`
declarations, no includes. See sections 2 and 3 for the full catalog.

---

## 1. What NeoWall Is

NeoWall renders fragment shaders to your desktop background at native refresh rate.
It is a **Shadertoy superset** — every Shadertoy shader runs unmodified — but it adds
a layer of live system, audio, and input data on top, exposed as GLSL uniforms.

The daemon samples `/proc`, `/sys/class/hwmon`, `/sys/class/drm`, and an audio FFT
(via a spawned `parec`) once per frame, then feeds the results into your shader. The
result is a wallpaper that is not a loop but a continuously-updating instrument panel
for your computer.

---

## 2. The Reactive Uniform Catalog

Every shader is wrapped with a preamble that declares the following uniforms. You do
not need to declare them yourself — just use them.

All standard Shadertoy uniforms are also present and behave identically: `iResolution`,
`iTime`, `iTimeDelta`, `iFrame`, `iMouse`, `iDate`, `iSampleRate`, and `iChannel0`..
`iChannel3`. The reactive uniforms below are additive on top of those.

### CPU & Load

- `iCpu` — overall CPU utilization, 0..1
- `iCpuCores[8]` — per-core utilization, 0..1
- `iCpuCoreCount` — number of valid entries in `iCpuCores`
- `iCpuTemp` — CPU package temperature, normalized 0..1 (0 = 20 C, 1 = 100 C)
- `iCpuTempC` — CPU package temperature in raw degrees Celsius
- `iLoad` — 1-minute load average, normalized by core count
- `iLoadRaw` — 1-minute load average, raw
- `iProcs` — running process count, normalized
- `iProcCount` — running process count, raw integer

### GPU

- `iGpu` — GPU busy percentage, 0..1 (0 on hardware without `gpu_busy_percent`)
- `iGpuTemp` — GPU temperature, normalized 0..1
- `iGpuTempC` — GPU temperature in raw degrees Celsius

### Memory & Storage

- `iRam` — RAM usage, 0..1
- `iSwap` — swap usage, 0..1
- `iDiskRead` — disk read activity, normalized
- `iDiskWrite` — disk write activity, normalized

### Network

- `iNetDown` — download activity, normalized 0..1
- `iNetUp` — upload activity, normalized 0..1

### Power

- `iBattery` — battery charge, 0..1
- `iCharging` — 1.0 if charging, else 0.0

### Time & Sun

- `iTimeOfDay` — fraction of day elapsed, 0..1 (midnight = 0)
- `iSun` — sun elevation proxy, 0..1
- `iDayFraction` — alias for daylight intensity

### Input

- `iKeyEnergy` — decaying keypress energy, spikes on each key
- `iMouseEnergy` — decaying mouse-motion energy
- `iUptimeHours` — system uptime in hours

### Audio

- `iAudioLevel` — overall loudness, 0..1
- `iAudioBass` — low-band energy
- `iAudioMid` — mid-band energy
- `iAudioTreble` — high-band energy
- `iAudioBeat` — beat detector, pulses to 1 on transients
- `iAudioActive` — 1.0 if audio is playing, else 0.0
- `iAudio` — `sampler2D`; row 0 is the spectrum, row 1 is the waveform (512x2 R16F)

---

## 3. The Standard Library

The wrapper also injects a GLSL standard library. These functions are always
available.

### Audio helpers

- `audioBand(lo, hi)` — average energy between two normalized frequencies
- `spectrum(x)` — sample the spectrum at x in 0..1
- `waveform(x)` — sample the waveform at x in 0..1
- `beat()` — convenience wrapper around the beat detector

### Noise & hashing

- `nwHash11`, `nwHash21`, `nwHash22`, `nwHash33` — hash functions of varying arity
- `nwValueNoise` — value noise
- `nwGradNoise` — gradient (Perlin-style) noise
- `nwWorley` — cellular / Worley noise
- `nwFbm(p)` and `nwFbm(p, oct)` — fractional Brownian motion
- `nwCurl` — curl noise for fluid-like motion

### Color

- `nwPalette(t)` and `nwPalette(t, a, b, c, d)` — cosine palette (Inigo Quilez form)
- `nwHsv2rgb` — HSV to RGB
- `nwSaturate` — clamp to 0..1
- `nwTonemap` — ACES filmic tonemap
- `nwGamma` — gamma correction

### Signed distance fields

- `sdCircle`, `sdBox` (2D and 3D), `sdSegment`, `sdHex`, `sdSphere`, `sdTorus`
- `opSmoothUnion`, `opSmoothSub` — smooth boolean operators

### Misc

- `nwRot` — 2D rotation matrix
- `pulse(x)` — smooth unit pulse
- `dayNight()` — day/night blend factor from `iTimeOfDay`
- `timeOfDayTint()` — a color tint that warms at dawn and dusk

Aliases (disable with `#define NW_NO_ALIASES`): `fbm`, `palette`, `hsv2rgb`, `rot2d`.

---

## 4. Manifests

A shader `foo.glsl` may have a sidecar `foo.neowall` manifest that wires up channels,
multipass buffers, and named uniforms. The manifest is auto-loaded; a missing manifest
is the normal case and is not an error.

If the shader path contains a `/`, the manifest is read from right beside it.
Otherwise the daemon searches, in order:

1. `$XDG_CONFIG_HOME/neowall/shaders/foo.neowall`
2. `~/.config/neowall/shaders/foo.neowall`
3. `/usr/share/neowall/shaders/foo.neowall`
4. `/usr/local/share/neowall/shaders/foo.neowall`

The first hit wins. A relative `shader` path *inside* the manifest resolves relative
to the manifest's own directory, not the working directory.

### Syntax

```
shader foo.glsl

channel0 audio

bufferA {
  ch0 self
}

image {
  ch1 bufferA
}

uniforms {
  uIntensity cpu
  uThreshold 0.85
}
```

### A complete example

A two-pass audio visualizer with a feedback trail, a constant, and a live CPU bind:

```
shader visualizer.glsl

bufferA {
  ch0 audio      # spectrum/waveform texture
  ch1 self       # previous frame for trails
}

image {
  ch0 bufferA    # composite the buffer to screen
}

uniforms {
  uGlow     0.7  # constant
  uHeat     cpu  # live: warms with CPU load
  uPulse    bass # live: kicks on the low end
}
```

In the shader, `uHeat` and `uPulse` arrive as ordinary `uniform float`s you declare
yourself; the bind keyword tells the daemon what live value to feed them each frame.

### Rules and gotchas

- **Keys cannot be bare numbers.** The VIBE parser rejects `bufferA { 0 self }`.
  Use a non-numeric prefix: `bufferA { ch0 self }`. The parser strips the non-digit
  prefix and reads the trailing integer.
- **Channel sources:** `audio`, `noise`, `self`, `keyboard`, `texture`, `font`,
  and the buffer names `bufferA` through `bufferD`.
- **Uniform bind keywords:** `cpu`, `ram`, `swap`, `net_down`, `net_up`,
  `disk_read`, `disk_write`, `load`, `cpu_temp`, `gpu`, `gpu_temp`, `uptime`,
  `procs`, `battery`, `time_of_day`, `sun`, `audio`, `bass`, `mid`, `treble`,
  `beat`, `keys`, `mouse`. A bare float literal binds a constant.
- **Avoid `*/` inside block comments.** A path like `card*/device` written in a
  comment closes the comment early.
- **Channel numbering.** `iChannel0`..`iChannel3` are your manifest-bound channels.
  The reactive audio texture is bound separately on unit 4 and is always reachable as
  `iAudio` — you never spend a user channel on it.

### Rendering text: the bitmap font atlas

Bind a channel to the `font` source to get a crisp monospace ASCII atlas you can
sample in-shader, instead of hand-rolling a cramped 3x5 bit table.

Manifest:

```
image { ch1 bufferA  ch2 font }   # font atlas now on iChannel2
```

The atlas is a **128x72** texture: a **16-col x 6-row** grid of **8x12** px cells,
printable ASCII codes **32..127** laid out row-major (`cell = ascii - 32`), sampled
bilinearly (`GL_LINEAR`) so `nwGlyph` can anti-alias the ink edge with a fwidth
smoothstep. The std-lib provides the helpers:

| Helper | Purpose |
|--------|---------|
| `nwGlyph(fontTex, ascii, p)` | 1.0 on ink / 0.0 on paper; `p` in `[0,1)^2` across one cell (x left→right, y up; it flips v internally) |
| `nwChar(fontTex, ascii, fragPx, pos, s)` | draw one glyph at pixel `pos`, cell height `s` (advance ≈ `0.6*s`) — the everyday call |
| `nwHexDigit(v)` | `0..15` → ASCII of `0`-`9`,`A`-`F` |
| `nwDigit(v)` | `0..9` → ASCII of that decimal digit |

Draw a left-to-right string by stepping `pos.x` per glyph:

```glsl
// "CPU" at pixel (px,py), cell height 14, using iChannel2
float cw = 14.0 * 0.62;                                   // monospace advance
float ink = 0.0;
ink += nwChar(iChannel2, 67.0, fragCoord, vec2(px,        py), 14.0); // 'C'
ink += nwChar(iChannel2, 80.0, fragCoord, vec2(px+cw,     py), 14.0); // 'P'
ink += nwChar(iChannel2, 85.0, fragCoord, vec2(px+cw*2.0, py), 14.0); // 'U'
col += tint * ink;
```

`nwChar` returns 0 for pixels outside the glyph cell, so it is safe to sum many calls.
Because each call still executes for *every* pixel, wrap a block of text in a
screen-space bounding-box `if (fragCoord in rect)` so warps away from the text skip
the samples — text is a tiny fraction of the screen and this is a large win (see the
Performance section).

---

## 5. Testing a Shader Live

1. Stop any running daemon: `./build/neowall kill`
2. Copy your shader to the search path: `cp foo.glsl ~/.config/neowall/shaders/`
3. Write a throwaway config and run for a few seconds:

```
printf 'default {\n  shader foo.glsl\n}\n' > /tmp/t.vibe
timeout 4 ./build/neowall -f -v -c /tmp/t.vibe 2>&1 \
  | grep -iE "Successfully compiled|Failed to compile|Manifest:"
```

4. Restore your real config afterward.

A clean run prints `Successfully compiled pass Image`. Compile failures print the
GLSL error with line numbers relative to the wrapped source.

---

## 6. Build Notes

- Build: `ninja -C build` (incremental against the existing build dir).
- Tests: `meson test -C build` (5 suites: `shadertoy_compat`, `vibe`, `foundation`,
  `utils`, `output_refcount`).
- Reconfigure: `meson setup build --reconfigure`.
- The GLSL std-lib string exceeds the C99 4095-char literal limit, so it is split
  into two adjacent `static const char *` chunks and concatenated at wrap time.
- Backend-specific code (e.g. the Wayland reconnect handler) is gated behind
  `HAVE_WAYLAND_BACKEND` so X11-only builds link cleanly.
- Backend-restricted builds (for testing the gating):
  - X11-only: `meson setup build-x11 -Dwayland_backend=disabled`
  - Wayland-only: `meson setup build-wl -Dx11_backend=disabled`
  - On a Wayland session, `meson setup` aborts if Wayland deps are requested but
    missing; bypass with `env -u XDG_SESSION_TYPE WAYLAND_DISPLAY= meson setup ...`.

---

## 7. Performance & Multipass

A wallpaper runs forever, so its cost is paid continuously. Budget accordingly.

### Cost model

- The fragment shader runs once per pixel per frame. At 4K/60 that is roughly
  500 million invocations per second. A loop that is cheap on a 256x256 Shadertoy
  preview can melt a laptop at full resolution.
- Raymarchers dominate the budget. Cap step counts, use an early-exit distance
  threshold, and prefer analytic SDFs over iterating noise.
- `nwFbm` with many octaves is expensive. Three to four octaves usually suffice;
  reach for `nwValueNoise` directly when you do not need the fractal sum.

### Multipass pipeline

NeoWall supports ping-pong buffers `bufferA` through `bufferD` plus the final
`image` pass, wired through the manifest. Each buffer is a full-resolution render
target that the next pass can sample.

- A buffer that reads its own previous frame (`bufferA { ch0 self }`) is the
  foundation for feedback effects: trails, blur accumulation, reaction-diffusion.
- Order matters. Buffers render in `A, B, C, D` order, then `image`. A later buffer
  may sample an earlier one from the *current* frame; sampling a later buffer reads
  its *previous* frame.
- The audio texture lives on channel unit 4 and is bound automatically whenever a
  channel source is `audio` — you do not allocate it.

### Practical limits

- Keep the per-pixel branch count low; divergent branches stall warps.
- Texture taps are cheaper than recomputing noise; cache into a buffer when a value
  is reused across passes.
- **Gate text in a bounding box.** Every `nwChar`/`nwGlyph` call runs for all pixels
  even though a glyph covers a tiny rect. Wrap each block of text in an
  `if (fragCoord within its screen-space rect)` so warps outside skip the samples —
  a HUD with dozens of glyphs drops from "per-pixel everywhere" to nearly free.
- If the daemon stutters, drop resolution scale before optimizing math — it is the
  single biggest lever.

---

## 8. Troubleshooting

Things that bite, and how to tell them apart.

**Shader compiles but the screen is black.**
Usually `fragColor` alpha is 0, or the color was tonemapped/clamped to zero. Force a
sanity color first: `fragColor = vec4(1.0, 0.0, 1.0, 1.0);`. If magenta shows, your
math is the problem, not the pipeline.

**Audio uniforms are flat at zero.**
Check `iAudioActive` — if it reads 0, the daemon found no audio. Confirm `parec` is
installed and PulseAudio/PipeWire is publishing a monitor source. Silence (no playing
audio) also yields zeros, which is correct.

**`iGpu` is stuck at 0.**
The hardware exposes no `gpu_busy_percent` (common on Intel iGPUs). This is graceful
degradation, not a bug — the gauge simply reads empty. GPU *temperature* may still be
available via `iGpuTempC`.

**Compile error line numbers look wrong.**
They are relative to the *wrapped* source, which prepends the std-lib and uniform
declarations. Subtract the wrapper length, or run with `-v` and read the surrounding
context the daemon prints.

**Manifest seems ignored.**
Confirm the sidecar is named exactly `<shader>.neowall` and sits in one of the search
dirs listed in section 4. Run with `-v` and look for a `Manifest: applying` line — if
it is absent the file was never found. Note a bare-number key (`bufferA { 0 self }`) is
a hard parse error (the VIBE parser needs an identifier key) — use `ch0`, not `0`.

**Per-core meters show fewer cores than expected.**
`iCpuCores` is capped at 8 entries; `iCpuCoreCount` tells you how many are valid.
Iterate to `iCpuCoreCount`, never a hard-coded 8.

---

## 9. Bundled Examples

The reactive shaders in `examples/shaders/` double as a cookbook. Read the source of
the one closest to what you want.

| Shader | Drives off | Technique to crib |
|--------|-----------|-------------------|
| `audio_pulse.glsl` | bass, treble, beat | breathing ring + beat flash; minimal, start here |
| `audio_bars.glsl` | full spectrum | `spectrum(x)` sampled across the screen width |
| `mouse_ripples.glsl` | `iMouseEnergy`, mouse pos | expanding rings seeded by pointer motion |
| `system_monitor.glsl` | CPU, RAM, temp | compact text-free gauges, good first dashboard |
| `system_deck.glsl` | everything | full HUD: gauges, scrolling history, per-core meters, clock |
| `starfield_warp.glsl` | `iAudioLevel` | speed-modulated star streaks, cheap raymarch |
| `fireflies.glsl` | time, `iKeyEnergy` | particle field via hashed cells, soft glow |
| `rain_window.glsl` | time | refraction + droplet trails using feedback buffer |
| `circadian_sky.glsl` | `iTimeOfDay`, `iSun` | day/night gradient driven by real clock |
| `living_aurora.glsl` | `iAudioMid`, time | layered `nwFbm` curtains, `nwTonemap` finish |

The two with `.neowall` sidecars (`audio_pulse`, `mouse_ripples`, `system_deck`) are
the ones to study for manifest wiring — channels, buffers, and named uniform binds.

Beyond these reactive examples, `examples/shaders/` also ships dozens of classic
non-reactive Shadertoy ports (oceans, fractals, synthwave scenes, matrix rain). They
run unmodified and are a good baseline for confirming the pipeline works on your GPU.

---

## 10. Ideas Not Yet Built

The roadmap of planned metrics and shaders lives in [`IDEAS.md`](./IDEAS.md) — it is
the single source of truth for proposed work, with an effort/dependency matrix and
implementation sketches. Keeping it in one place avoids the two lists drifting apart.

---

## 11. Recipes

Drop-in snippets for the effects people reach for most. Each assumes the reactive
uniforms and std-lib are in scope (they always are).

**Beat-synced flash**
```glsl
col += vec3(1.0) * iAudioBeat * 0.2;     // brief white bloom on transients
```

**Bass-driven zoom / breathing**
```glsl
uv *= 1.0 - 0.1 * iAudioBass;            // scene pulses outward on the low end
```

**Spectrum bars across the screen**
```glsl
float h = spectrum(uv.x);                // 0..1 height for this column
float bar = step(uv.y, h);
```

**CPU-temperature heat tint**
```glsl
col = mix(col, vec3(1.0, 0.2, 0.1), smoothstep(0.6, 1.0, iCpuTemp));
```

**Mouse-following glow**
```glsl
vec2 m = iMouse.xy / iResolution.xy;
col += 0.05 / length(uv - (m - 0.5) * vec2(iResolution.x/iResolution.y, 1.0));
```

**Day/night color grade from the real clock**
```glsl
col *= timeOfDayTint();                   // warm at dawn/dusk, cool at night
```

**Trail / feedback (needs a `bufferA { ch0 self }` manifest)**

Manifest (`foo.neowall`):
```
shader foo.glsl
bufferA { ch0 self }
image   { ch0 bufferA }
```
Shader (in `bufferA`'s pass):
```glsl
vec3 prev = texture(iChannel0, uv).rgb;
col = max(col, prev * 0.96);              // fade the previous frame, keep brightest
```

**Keypress sparks**
```glsl
col += iKeyEnergy * nwHash21(floor(uv * 40.0)) * 0.3;
```

**Crisp text via the font atlas (needs `ch2 font` in the manifest)**
```glsl
// two-digit CPU % readout at pixel (px,py), cell height 16, on iChannel2
float v  = clamp(iCpu, 0.0, 0.99) * 100.0;
float cw = 16.0 * 0.62;
float ink = nwChar(iChannel2, nwDigit(floor(v/10.0)), fragCoord, vec2(px,    py), 16.0)
          + nwChar(iChannel2, nwDigit(mod(v,10.0)),   fragCoord, vec2(px+cw, py), 16.0);
col += vec3(0.35, 0.85, 0.48) * ink;     // gate in a bounding box if drawing lots
```

---

## 12. Glossary

- **FFT** — fast Fourier transform; turns the audio waveform into the spectrum row
  of `iAudio`.
- **SDF** — signed distance field; a function returning the distance to a surface,
  negative inside. The basis of the `sd*` helpers.
- **fbm** — fractional Brownian motion; summed octaves of noise at halving amplitude.
- **Ping-pong buffer** — a render target that reads its own previous frame, enabling
  feedback and accumulation.
- **Tonemap** — mapping unbounded HDR color into the 0..1 display range; `nwTonemap`
  uses the ACES filmic curve.
- **Manifest** — the `.neowall` sidecar that wires channels, buffers, and uniform
  binds for a shader.
- **Reactive uniform** — an `i`-prefixed uniform fed live system/audio/input data,
  injected automatically into every shader.

---

## 13. FAQ

**Will my existing Shadertoy shaders work?**
Yes — NeoWall is a Shadertoy superset. `mainImage`, `iTime`, `iResolution`,
`iMouse`, and `iChannel*` all behave as expected. The reactive uniforms are purely
additive.

**Do I have to use the `nw*` helpers?**
No. They are conveniences. Plain GLSL works fine; ignore the std-lib if you prefer.

**Does sampling system data cost much?**
The sampling happens once per frame on the CPU, reading a handful of `/proc` and
`/sys` files. It is negligible next to the shader's per-pixel work.

**Can a shader run without a manifest?**
Yes. Manifests are optional and only needed for multipass buffers, explicit channel
sources, or named uniform binds. A single-pass shader needs none.

**What happens on hardware that lacks a metric?**
The corresponding uniform reads 0 and the shader keeps running. Nothing errors out
over a missing sensor.

