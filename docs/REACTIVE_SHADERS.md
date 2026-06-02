# Reactive Shaders & the neowall Shader Library

neowall is a **superset of Shadertoy** for live wallpapers. On top of full
Shadertoy compatibility (multipass Buffer A–D, `iTime`, `iMouse`, `iChannel*`,
`iDate`, …) it adds three things Shadertoy fundamentally cannot have, because
your wallpaper runs *on your actual machine*:

1. **A GLSL standard library** auto-injected into every shader — noise, SDFs,
   color/tonemap helpers, audio + reactive helpers. No `#include`, no copy-paste.
2. **Reactive uniforms** — live system + audio signals (CPU, RAM, network,
   battery, time-of-day, sun angle, keyboard/mouse energy, audio FFT).
3. **`.neowall` manifests** — explicit channel bindings + custom reactive
   uniforms, so exotic shaders bind correctly and ordinary ones come alive.

Everything is **additive and zero-cost when unused**: a plain Shadertoy `.glsl`
renders exactly as before (unused uniforms/functions are stripped by the GLSL
compiler).

---

## 1. The std-lib (always available)

Injected into every shader's wrapper. Highlights (see `shader_stdlib.h`):

Noise / fields

- `nwHash11/21/22/33`, `nwValueNoise`, `nwGradNoise`, `nwWorley`
- `nwFbm(p)` / `nwFbm(p, octaves)`, `nwCurl(p)` (curl-noise flow)

Color

- `nwPalette(t)` and `nwPalette(t,a,b,c,d)` (IQ cosine palettes)
- `nwHsv2rgb`, `nwSaturate`, `nwTonemap` (ACES), `nwGamma` (sRGB)

SDF (2D + 3D)

- `sdCircle`, `sdBox`, `sdSegment`, `sdHex`, `opSmoothUnion/Sub`
- `sdSphere`, `sdBox(vec3)`, `sdTorus`, `nwRot(angle)`

Reactive shaping

- `pulse(x)` smoothstep emphasis, `dayNight()`, `timeOfDayTint()`

Convenience aliases (skip with `#define NW_NO_ALIASES` before your code):
`fbm`, `palette`, `hsv2rgb`, `rot2d`.

```glsl
void mainImage(out vec4 c, vec2 f){
    vec2 uv = f/iResolution.xy;
    float n = fbm(uv*4.0 + iTime*0.1);   // std-lib fbm
    c = vec4(nwGamma(nwTonemap(palette(n))), 1.0);
}
```

---

## 2. Reactive uniforms

Declared automatically; use any you want.

System

| Uniform | Meaning |
|---------|---------|
| `iCpu` | total CPU load 0..1 |
| `iCpuCores[8]`, `iCpuCoreCount` | per-core load |
| `iRam` | memory used 0..1 |
| `iNetDown`, `iNetUp` | network activity 0..1 (log-scaled) |
| `iBattery`, `iCharging` | charge 0..1, 1.0 if on AC |
| `iTimeOfDay` | 0..1 across the local day |
| `iSun` | sun-elevation proxy 0..1 (warm at noon) |
| `iDayFraction` | 0..1 across the year |
| `iKeyEnergy`, `iMouseEnergy` | recent input activity 0..1 |

Audio (live FFT of system output via `parec`)

| Uniform | Meaning |
|---------|---------|
| `iAudioLevel` | overall loudness 0..1 |
| `iAudioBass` / `iAudioMid` / `iAudioTreble` | band energies 0..1 |
| `iAudioBeat` | onset pulse 0..1 (spikes, decays) |
| `iAudioActive` | 1.0 if capture is live |
| `iAudio` | `sampler2D`: row 0 = spectrum, row 1 = waveform (512 wide) |

Audio helpers in the std-lib: `audioBand(lo,hi)`, `spectrum(x)`, `waveform(x)`,
`beat()`.

```glsl
float bass = iAudioBass;               // or audioBand(0.0, 0.12)
col *= 0.6 + bass + 0.4*beat();        // pump on the beat
```

**Audio capture** spawns `parec` (PulseAudio / PipeWire record) on the default
monitor source. If `parec` isn't installed the audio signals stay 0 and
everything else works — install `pulseaudio-utils` (Debian) /
`libpulse`+`pulseaudio` or PipeWire's `pipewire-pulse` to enable it.

---

## 3. `.neowall` manifests

A manifest is an optional **sidecar** next to your shader: `foo.glsl` →
`foo.neowall`. It is auto-loaded when you point your config at `foo.glsl`. You
can also point the config straight at `foo.neowall`.

It does two jobs:

**Explicit channel bindings** — kills the binding *heuristic* (which is a guess
for bare `.glsl`). Bind any `iChannelN` to: `audio`, `noise`, `self`,
`keyboard`, `bufferA`..`bufferD`, `texture`.

**Custom reactive uniforms** — declare `float` uniforms bound to a live signal
(or a constant). They're injected into the shader and updated every frame.

```
shader foo.glsl

channel0 audio
channel1 self

uniforms {
  uGlow   audio_bass     # bound to low-band energy
  uLoad   cpu            # bound to CPU load
  uWarm   sun            # bound to sun elevation
  uGain   0.85           # plain constant
}
```

Binding keywords for uniforms: `cpu`, `ram`, `net_down`, `net_up`, `battery`,
`time_of_day`, `sun`, `audio`/`audio_level`, `audio_bass`/`bass`,
`audio_mid`/`mid`, `audio_treble`/`treble`, `audio_beat`/`beat`,
`key_energy`/`keys`, `mouse_energy`/`mouse`. Anything else is treated as a
numeric constant.

Per-pass bindings (for multipass shaders) use blocks with `chN` keys:

```
bufferA { ch0 self  ch1 noise }
image   { ch0 bufferA  ch1 audio }
```

---

## Examples

- `examples/shaders/audio_pulse.glsl` + `.neowall` — audio-reactive neon tunnel.
- `examples/shaders/living_aurora.glsl` — aurora reacting to time-of-day,
  network, battery, and typing (no audio needed).

## Performance

Reactive sampling is throttled to ~4 Hz and runs on the main loop; audio FFT
runs on a dedicated thread with a 1024-point radix-2 FFT (cheap). Unused
uniforms cost nothing. The existing adaptive-resolution and multipass
optimizers apply unchanged.
