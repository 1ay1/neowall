<p align="center">
  <img src="packaging/neowall.svg" alt="NeoWall" width="280"/>
</p>

<p align="center">
  <strong>Run GLSL shaders as your Linux wallpaper.</strong>
</p>

<p align="center">
  <a href="https://github.com/1ay1/neowall/actions/workflows/build.yml"><img src="https://github.com/1ay1/neowall/actions/workflows/build.yml/badge.svg" alt="Build"/></a>
  <a href="https://github.com/1ay1/neowall/blob/main/LICENSE"><img src="https://img.shields.io/github/license/1ay1/neowall?color=blue" alt="License"/></a>
  <a href="https://github.com/1ay1/neowall/releases"><img src="https://img.shields.io/github/v/release/1ay1/neowall?include_prereleases&color=purple" alt="Release"/></a>
</p>

<p align="center">
  <video src="https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560" width="49%" controls autoplay loop muted></video>
  <video src="https://github.com/user-attachments/assets/c1e38d88-5c1e-4db4-9948-da2ad86c6a69" width="49%" controls autoplay loop muted></video>
</p>

---

Paste a [Shadertoy](https://www.shadertoy.com/) shader into a file, point neowall at it, get an animated desktop. Wayland + X11 + macOS, multi-monitor, single static binary, no daemon-in-Python anywhere.

```bash
neowall
```

It pauses itself the moment a window covers the wallpaper, so it isn't burning your battery while you actually use the computer.

## Install

```bash
# Arch (AUR)
yay -S neowall-git

# Prebuilt binary
# https://github.com/1ay1/neowall/releases/latest
tar -xzf neowall-linux-x86_64-*.tar.gz
sudo install -m755 neowall /usr/local/bin/

# From source
git clone https://github.com/1ay1/neowall && cd neowall
meson setup build && ninja -C build
sudo ninja -C build install
```

<details>
<summary>Build dependencies</summary>

```bash
# Debian/Ubuntu
sudo apt install build-essential meson ninja-build libwayland-dev \
    libgles2-mesa-dev libpng-dev libjpeg-dev wayland-protocols \
    libx11-dev libxrandr-dev

# Arch
sudo pacman -S base-devel meson ninja wayland mesa libpng libjpeg-turbo \
    wayland-protocols libx11 libxrandr

# Fedora
sudo dnf install gcc meson ninja-build wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel \
    libX11-devel libXrandr-devel

# macOS
brew install meson ninja libpng jpeg-turbo
```
</details>

## Config

`~/.config/neowall/config.vibe`

```
default {
  shader retro_wave.glsl
  shader_speed 0.8
}
```

Image slideshow:

```
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition glitch
}
```

Per-output:

```
output {
  DP-1     { shader matrix_rain.glsl }
  HDMI-A-1 { path ~/Pictures/ duration 600 }
}
```

Full reference: [`config/docs/CONFIG.md`](config/docs/CONFIG.md).

## Commands

```bash
neowall          # start
neowall kill     # stop
neowall reload   # reload config
neowall next     # next wallpaper / shader
neowall pause    # pause cycling
neowall resume
neowall pause-shader   # freeze the shader animation in place
neowall resume-shader  # resume a frozen shader animation
neowall list     # show queue
neowall set 3    # jump to index
neowall current  # print what's playing
```

## Shaders

30+ in the box. Bring your own — neowall accepts unmodified [Shadertoy](https://www.shadertoy.com/) GLSL: drop a `.glsl` file into `~/.config/neowall/shaders/` and reference it from your config.

| Vibe | A few |
|------|-------|
| Synthwave | `retro_wave` · `synthwave` · `neonwave_sunrise` |
| Nature | `ocean_waves` · `aurora` · `sunrise` |
| Cyber | `matrix_rain` · `matrix_real` · `glowing_triangles` |
| Abstract | `fractal_land` · `plasma` · `mandelbrot` |
| Space | `star_next` · `starship_reentry` |

Pair it with [**GLEditor**](https://github.com/1ay1/gleditor) for a live-preview shader workflow that one-clicks into neowall.

### Reactive shaders — a living desktop

neowall is a **superset of Shadertoy**: on top of full compatibility it feeds
shaders *live data from your machine* and ships a GLSL std-lib (noise, SDFs,
palettes, tonemap) injected automatically — no `#include`. Shaders can react to
system audio (FFT), CPU/RAM/network load, battery, the real time of day, and
your mouse/keyboard. See [`docs/REACTIVE_SHADERS.md`](docs/REACTIVE_SHADERS.md).

| Shader | Reacts to |
|--------|-----------|
| `audio_pulse` · `audio_bars` | system audio — spectrum, bass, beats |
| `mouse_ripples` | water ripples from your cursor (click = splash) |
| `plasma_touch` | plasma you push around with the mouse |
| `fireflies` | a swarm that gathers at your pointer, flares on beat |
| `starfield_warp` | warp speed scales with mouse motion + audio |
| `system_monitor` | CPU heat, RAM tide, network pulses, battery |
| `system_deck` | a command-deck HUD: CPU/RAM gauges, scrolling history graph, per-core meters, net traces, battery, clock |
| `rain_window` | rain on glass — heavier with network, lightning on beat |
| `circadian_sky` | a sky synced to your real clock: dawn → noon → dusk → stars |

Drop a `.neowall` sidecar next to any shader for explicit channel bindings and
custom reactive uniforms (e.g. `uniform uGlow audio_bass`). Audio capture uses
`parec` (PipeWire/PulseAudio) and degrades to silence if it's not installed.

**Changing what feeds `iChannel0..3`** (audio, noise, self-feedback, another
buffer, a texture): a bare `.glsl` guesses via a heuristic; a `.neowall` sidecar
lets you bind each channel explicitly — `channel0 audio`, `channel1 self`, etc.
See [`docs/REACTIVE_SHADERS.md`](docs/REACTIVE_SHADERS.md#3-neowall-manifests).

## How it works

Single C binary. The event loop is built on `timerfd` + `signalfd` — there is no polling thread anywhere. Rendering goes through EGL → OpenGL 3.3 (desktop) with a GLES 2.0 fallback for older GPUs.

```
config.vibe → event loop ─┬─→ EGL / OpenGL 3.3
                          ├─→ Wayland (layer-shell)
                          ├─→ X11 (root window + EWMH)
                          └─→ macOS (AppKit desktop-level windows + CGL)
```

Smart pause is the main thing that separates neowall from the obvious one-evening hack:

- Wayland: `wl_surface.frame` watchdog + `wlr-foreign-toplevel` state bits, plus a Hyprland-specific IPC path that handles the awkward "tiled mosaic of small windows covers the wallpaper" case
- X11: EWMH `_NET_WM_STATE_FULLSCREEN` on every output
- macOS: `NSWindowOcclusionState` — the OS tells us directly when the wallpaper window is covered

Result: while a fullscreen game, a maximized browser, or a wall of tiled terminals is covering the screen, neowall draws zero frames. The moment you switch workspaces, it picks back up.

## Compared to

| | neowall | swww | mpvpaper | hyprpaper |
|-|---------|------|----------|-----------|
| Animated GLSL shaders | ✓ | – | – | – |
| Drop-in Shadertoy support | ✓ | – | – | – |
| Image slideshow | ✓ | ✓ | – | ✓ |
| Video | – | gifs only | ✓ | – |
| Wayland | ✓ | ✓ | ✓ | ✓ |
| X11 | ✓ | – | – | – |
| macOS | ✓ | – | – | – |
| Pauses when occluded | ✓ | – | – | – |

If you want video wallpapers, use mpvpaper — neowall is deliberately not that.

## Caveats, honestly

- **KDE Plasma**: native desktop icons may hide while neowall owns the background layer. Use a dock or `plasma-desktop`'s widgets.
- **GNOME**: works through the fallback path; Mutter doesn't expose layer-shell, so z-order behavior is less guaranteed than on wlroots compositors.
- **macOS**: new — builds and runs natively (AppKit windows at desktop level, per-screen, HiDPI, occlusion pause), but audio-reactive uniforms and CPU/RAM stats aren't wired up yet on this platform. Run with `-f` (foreground).
- **No video**: by design.

## Hacking

```bash
meson setup build --buildtype=debug
ninja -C build
./build/neowall -f -v
```

Architecture, threading model, and contribution conventions are documented in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). Run the test suite (headless,
no display server needed) with `meson test -C build`; for the memory-safety and
concurrency gates use a sanitized build:

```bash
meson setup build-asan -Db_sanitize=address,undefined && meson test -C build-asan
meson setup build-tsan -Db_sanitize=thread && meson test -C build-tsan output_refcount
```

Shaders, bug reports, and compositor coverage PRs are all welcome. Issue tracker is the place.

## License

MIT.

---

<p align="center">
  <a href="https://github.com/1ay1/neowall/issues">Issues</a> · <a href="https://github.com/1ay1/neowall/discussions">Discussions</a> · <a href="https://github.com/1ay1/neowall">★ if it's your thing</a>
</p>
