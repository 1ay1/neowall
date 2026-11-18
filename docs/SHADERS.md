# Writing shaders

A neowall wallpaper is a GLSL fragment shader. Shaders written for
[Shadertoy](https://www.shadertoy.com/) run unmodified; on top of that
neowall gives every shader live inputs from your machine (audio, system load,
the time of day, your windows, your colour theme) and a small standard
library.

- [Your first shader](#your-first-shader)
- [The authoring loop](#the-authoring-loop)
- [Inputs](#inputs)
- [The standard library](#the-standard-library)
- [3D scenes without boilerplate](#3d-scenes-without-boilerplate)
- [Reacting to your windows](#reacting-to-your-windows)
- [Following your colour theme](#following-your-colour-theme)
- [Manifests and multiple passes](#manifests-and-multiple-passes)
- [Live values from anywhere](#live-values-from-anywhere)
- [More features](#more-features)
- [Troubleshooting](#troubleshooting)

## Your first shader

```bash
neowall new hello            # writes ~/.config/neowall/shaders/hello.glsl
neowall watch hello          # runs it and reloads on every save
```

Or write one by hand. This is a complete wallpaper that breathes with your
music and warms up when the CPU is busy:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);

    float ring = nwPulse(r - 0.3 - 0.15 * iAudioBass);       // bass moves the ring
    vec3 col = nwPalette(0.55 - 0.25 * iCpu) * ring;         // CPU load shifts the colour
    col += 0.15 * iAudioBeat;                                // flash on the beat

    fragColor = vec4(nwTonemap(col), 1.0);
}
```

Everything starting with `i` or `nw` is provided: no `uniform` declarations
and no includes. To make it your wallpaper, put `shader hello` in your
[config](CONFIGURATION.md).

`neowall new` takes `--template=scene|flat|reactive`; `neowall new --list`
describes them. The shaders in [`examples/shaders/`](../examples/shaders/) are
the best reference: `tokyo_night` (the default) shows multipass rendering,
bloom and window awareness; `scene_demo` and `neon_city` use the 3D scene kit;
`system_deck` and `audio_bars` are dashboards.

## The authoring loop

| Command | Use it to |
|---------|-----------|
| `neowall watch <shader>` | Run a shader in the foreground and recompile on every save. A broken save keeps the last good version on screen and prints the error with its real file and line. |
| `neowall preview <shader>` | Run a shader next to your wallpaper without touching the daemon or its config. |
| `neowall render <shader>` | Draw it offscreen to a PNG: no compositor needed, safe over SSH and in CI. |
| `neowall docs [word]` | List every uniform and helper, or search them. |

`render` is the fastest way to check how a shader looks and performs:

```bash
neowall render myshader                        # myshader.png, 1920x1080, t = 3 s
neowall render myshader --sheet 6 --time 30    # contact sheet of six moments
neowall render myshader --at 19:30             # how it looks at dusk
neowall render myshader --set iAudioBeat=1     # pin any input
neowall render myshader --mouse 0.2,0.5        # pointer on the left
neowall render myshader --video clip.mp4 --duration 10   # mp4, webp or gif
neowall render myshader --bench                # GPU milliseconds per pass
neowall render myshader --check                # find NaN/Inf pixels, per pass
```

Inputs are pinned to calm values (an idle machine, no audio) unless you pass
`--live`, and `--at` fixes the time of day, so the same command always gives
the same image. `--bench` measures real GPU time per pass with timer queries
and states what it costs at 30 and 60 fps; measure at your monitor's
resolution with `--size`. `--check` names the first pass that produces NaN
and paints those pixels magenta in the PNG (see
[Troubleshooting](#troubleshooting)).

To try a shader at another time or date: `neowall preview garden --date=+6d`
jumps six days ahead, and `--timelapse=30d/20s` plays 30 days in 20 seconds.
Only the shader's clock moves.

## Inputs

All Shadertoy uniforms work as usual: `iTime`, `iResolution`, `iMouse`,
`iFrame`, `iTimeDelta`, `iFrameRate`, `iDate`, `iChannel0..3`,
`iChannelResolution`. neowall adds these, all `float` in `0..1` unless noted:

| Group | Uniforms |
|-------|----------|
| Audio | `iAudioLevel`, `iAudioBass`, `iAudioMid`, `iAudioTreble`, `iAudioBeat` (decaying pulse), `iAudioActive` (1 while capturing) |
| CPU | `iCpu`, `iCpuMax` (hottest core), `iCpuSpread`, `iCpuCores[64]`, `iCpuCoreCount` (int), `iLoad`, `iLoadRaw` |
| Memory, disk, network | `iRam`, `iRamGB`, `iRamTotalGB`, `iSwap`, `iDiskRead`, `iDiskWrite`, `iNetDown`, `iNetUp`, `iNetDownRaw`, `iNetUpRaw` (MB/s) |
| Temperature, GPU | `iCpuTemp`, `iCpuTempC`, `iGpu`, `iGpuTemp`, `iGpuTempC`; NVIDIA: `iNvGpu`, `iNvVram`, `iNvGpuTempC`, `iNvPower`, `iNvActive` |
| Power | `iBattery`, `iCharging` |
| Time | `iTimeOfDay` (0 = midnight), `iSun` (0 night, 1 noon), `iDayFraction` (through the year), `iUptimeHours` |
| Activity | `iKeyEnergy`, `iMouseEnergy`, `iActivity`, `iThermal`, `iPulse`, `iProcs` |
| Desktop | `iWindows[16]`, `iWindowCount`, `iFocusedWindow` (see [below](#reacting-to-your-windows)) |
| Theme | `iPalette[16]`, `iPaletteSize` (see [below](#following-your-colour-theme)) |
| Memory between runs | `iState[4]`, `iStateAge` (see [More features](#more-features)) |

Audio is captured from your speakers' output (the default sink's monitor),
never the microphone, through `parec`. Capture only runs while a visible
shader reads an audio uniform. Signals that a machine can't provide read 0.

## The standard library

Helpers prefixed `nw` are always available; `neowall docs` lists every
signature. The most used:

| Area | Functions |
|------|-----------|
| Noise | `nwHash11/21/22/33`, `nwValueNoise`, `nwGradNoise`, `nwFbm(p)` / `nwFbm(p, octaves)`, `nwWorley`, `nwCurl` |
| Colour | `nwPalette(t)` and `nwPalette(t, a, b, c, d)` (cosine palettes), `nwHsv2rgb`, `nwSaturate`, `nwTonemap` (ACES), `nwGamma` |
| 2D shapes | `nwSdCircle`, `nwSdBox`, `nwSdSegment`, `nwSdHex`, `nwOpSmoothUnion`, `nwOpSmoothSub`, `nwRot` |
| Audio | `nwAudioBand(lo, hi)`, `nwSpectrum(x)`, `nwWaveform(x)`, `nwBeat()` |
| Time | `nwDayNight()`, `nwTimeOfDayTint()`, `nwPulse(x)` |
| Text | `nwChar`, `nwGlyph`, `nwDigit`, `nwHexDigit` (with the `font` channel) |

Short aliases without the prefix (`spectrum`, `waveform`, `beat`, `audioBand`,
`sdCircle`, `sdSegment`, `sdHex`, `opSmoothUnion`, `opSmoothSub`) are also
provided unless your shader defines a function of that name itself.

## 3D scenes without boilerplate

Define `nwMap` and neowall supplies the raymarcher: lighting, soft shadows,
ambient occlusion, reflections, fog, tonemapping, and a sky that follows the
real time of day.

```glsl
float nwMap(vec3 p) {
    return min(nwGround(p, -0.5), nwSdSphere(p - vec3(0.0, 0.5, 0.0), 1.0));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(nwRender(nwCameraOrbit(fragCoord, 5.0, iTime * 0.2, 0.3)), 1.0);
}
```

Optionally define `nwMaterial(p, n)` for base colour, `nwGloss(p, n)` for
roughness and metalness, and `nwEmissive(p, n)` for glowing surfaces. Cameras:
`nwCameraOrbit` and `nwCameraLookAt`. Shapes and space: `nwSdBox`,
`nwSdTorus`, `nwSdCylinder`, `nwSdCone`, `nwSdCapsule`, `nwSdOctahedron`,
`nwOpOnion`, `nwRepeat`, `nwPolarRepeat`, `nwTwist`, `nwBend`, and more in
`neowall docs`. The kit is only compiled in when a shader defines `nwMap`.

## Reacting to your windows

On Hyprland, neowall passes the geometry of the windows on each monitor:
`iWindows[i]` is `x, y, width, height` in pixels (y down), `iWindowCount` how
many are valid, and `iFocusedWindow` the active one. On other compositors
`iWindowCount` is 0, so a shader written for it still looks right everywhere.

| Helper | Returns |
|--------|---------|
| `nwWinGlow(fragCoord, radius)` | A soft glow leaking out from behind every window. |
| `nwWinNearest(fragCoord)` | Distance in pixels to the nearest window edge. |
| `nwWinCovered(fragCoord)` | 1 where a window covers the pixel. |
| `nwWinBusy()` | How much of the screen windows occupy, `0..1`. |

```glsl
col += vec3(0.5, 0.6, 1.0) * 0.3 * nwWinGlow(fragCoord, 70.0);   // light the gaps
col *= 1.0 - 0.2 * nwWinBusy();                                   // calmer when busy
```

## Following your colour theme

Write colours against the theme palette and the wallpaper recolours when the
desktop theme changes:

```glsl
vec3 sky  = mix(nwPalBg(), nwPalAccent(0), uv.y * 0.3);
vec3 neon = nwPalSmooth(id * 0.17);        // blends through the accents
vec3 text = nwPalFg();
```

`nwPal(i)` reads entry `i`; `nwPalBg()` and `nwPalFg()` are entries 0 and 1,
`nwPalAccent(k)` is accent `k`. The palette comes from the `palette` key in
the [config](CONFIGURATION.md#shaders) (any theme file with `#rrggbb` colours
works); without one, shaders see a built-in night palette. `neowall render
--palette` previews a shader in another theme.

## Manifests and multiple passes

A file with several `mainImage` functions is split into passes by the comment
above each one: `// Buffer A` to `// Buffer D`, any `// Pass: Name`, and
`// Image` for the pass that reaches the screen. Code outside every
`mainImage` is shared by all passes.

A `.neowall` manifest next to the shader, with the same name, wires the passes
together and configures them:

```
shader myshader.glsl

bufferA {
  ch0 self             # its own previous frame: feedback, trails
  ch1 audio            # the audio spectrum texture
}

bufferB {
  ch0 bufferA
  scale 0.5            # render at half resolution
}

image {
  ch0 bufferA
  ch1 bufferB
  ch2 texture.png      # an image next to the shader
}

uniforms {
  uGlow   0.7          # a constant
  uHeat   cpu          # a live signal
  uPulse  bass
}
```

Channel sources: another pass by name, `self`, `audio`, `noise`,
`blue_noise`, `keyboard`, `font`, `sky`, `none`, or an image path (relative to
the shader). Per-pass options:

| Key | Values |
|-----|--------|
| `scale` | Fraction of the output size, `(0, 4]`. Blur, bloom and glow passes look the same at `0.5` or `0.25` and cost a quarter or a sixteenth as much. The pass sees its own size in `iResolution`. |
| `size` | A fixed size such as `512x512` instead. |
| `format` | `rgba16f` (default), `rgba8`, `rgba32f`. |
| `filter` | `linear` (default), `nearest`, `mipmap` (enables `textureLod`). |
| `wrap` | `clamp` (default), `repeat`, `mirror`. |

Signals for `uniforms`: `cpu`, `cpu_max`, `cpu_spread`, `cpu_cores`, `ram`,
`ram_gb`, `ram_total_gb`, `swap`, `net_down`, `net_up`, `net_down_mbs`,
`net_up_mbs`, `disk_read`, `disk_write`, `load`, `load_raw`, `cpu_temp`,
`cpu_temp_c`, `gpu`, `gpu_temp`, `gpu_temp_c`, `nv_gpu`, `nv_vram`,
`nv_temp_c`, `nv_power`, `nv_active`, `thermal`, `activity`, `pulse`,
`uptime`, `procs`, `proc_count`, `battery`, `charging`, `time_of_day`, `sun`,
`day_fraction`, `key_energy`, `mouse_energy`, `audio_level`, `audio_bass`,
`audio_mid`, `audio_treble`, `audio_beat`, `audio_active` (short forms:
`bass`, `mid`, `treble`, `beat`, `audio`, `keys`, `mouse`), or a number for a
constant. You may declare a bound uniform yourself (`uniform float uHeat;`)
or leave the declaration out.

## Live values from anywhere

A manifest uniform can also read a file or the output of a command, polled in
the background at the interval you give:

```
uniforms {
  uFan     "file('/sys/class/hwmon/hwmon2/fan1_input', 2s)"
  uUnread  "exec('notmuch count tag:unread', 30s)"
}
```

`exec()` runs a command, so it is off until you enable it with `allow_exec
true` in your config or `neowall --allow-exec`. A shader can't turn it on by
itself.

## More features

**Memory between runs.** A feedback buffer forgets everything when neowall
restarts. For a wallpaper with continuity (a garden that keeps growing), name
a pass in the manifest with `state bufferA`: its first four pixels are saved
and come back as `iState[4]` on the next run, with `iStateAge` the real
seconds that passed meanwhile.

**Text.** Bind `font` to a channel and draw characters with `nwChar(iChannelN,
ascii, fragCoord, position, size)`; `nwDigit` and `nwHexDigit` help with
numbers. See `system_deck` and `time`.

**Shadertoy shaders.** `neowall import export.json [name]` converts a
Shadertoy JSON export (every pass and its channel bindings) into a shader and
manifest in `~/.config/neowall/shaders/`. Pasting a shader by hand works too.

**Includes and versions.** `#include "common.glsl"` includes a file relative
to the shader. `#pragma neowall requires 0.10.0` refuses to load on an older
neowall with a clear message instead of a compile error.

## Troubleshooting

**A compile error.** `neowall watch` and `neowall render` print it with the
real file and line, in every pass, and suggest the helper or uniform you
probably meant.

**Black areas or blocks.** Almost always NaN: `x / 0`, `sqrt`, `log` or `pow`
of a negative number, `normalize(vec3(0))`, or `0.0 * exp(large)`. A blur or
bloom pass then smears one bad pixel into a large black shape.
`neowall render myshader --check` names the pass where it starts and its
location.

**Too slow.** `neowall render myshader --size 3440x1440 --bench` shows which
pass costs what. The usual fixes: `scale 0.5` on blur and glow passes, fewer
`nwFbm` octaves, skipping work where a pixel can't be affected, and
`shader_fps 30` in the config. See [PERFORMANCE.md](PERFORMANCE.md).
