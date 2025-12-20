<p align="center">
  <img src="packaging/neowall.svg" alt="NeoWall" width="300"/>
</p>

<h1 align="center">NeoWall</h1>

<p align="center">
  <strong>Live GPU shaders as your wallpaper. Yes, really.</strong>
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

## What is this?

NeoWall renders **Shadertoy shaders** directly on your desktop. Wayland, X11, multi-monitor, 60fps, ~2% CPU.

```bash
neowall   # That's it. You now have an animated wallpaper.
```

## Install

**Arch (AUR):**
```bash
yay -S neowall-git
```

**Build it yourself:**
```bash
git clone https://github.com/1ay1/neowall && cd neowall
meson setup build && ninja -C build
sudo ninja -C build install
```

<details>
<summary>Dependencies</summary>

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
```
</details>

## Config

Lives at `~/.config/neowall/config.vibe`

**Shader wallpaper:**
```
default {
  shader retro_wave.glsl
  shader_speed 0.8
}
```

**Image slideshow:**
```
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition glitch
}
```

**Multi-monitor:**
```
output {
  DP-1 { shader matrix_rain.glsl }
  HDMI-A-1 { path ~/Pictures/ duration 600 }
}
```

## Commands

```bash
neowall          # start
neowall kill     # stop
neowall next     # next wallpaper
neowall pause    # pause
neowall resume   # resume
neowall list     # show cycle
neowall set 3    # jump to index 3
neowall current  # what's playing?
```

## Shaders

30+ included. Some highlights:

| Vibe | Shaders |
|------|---------|
| 🌆 Synthwave | `retro_wave` `synthwave` `neonwave_sunrise` |
| 🌊 Nature | `ocean_waves` `aurora` `sunrise` `moon_ocean` |
| 💻 Cyber | `matrix_rain` `matrix_real` `glowing_triangles` |
| 🔮 Abstract | `fractal_land` `plasma` `mandelbrot` |
| 🌌 Space | `star_next` `starship_reentry` `cross_galactic_ocean` |

**Use any Shadertoy shader:**
1. Copy code from shadertoy.com
2. Save to `~/.config/neowall/shaders/cool.glsl`
3. Config: `shader cool.glsl`
4. Done

## GLEditor

[**GLEditor**](https://github.com/1ay1/gleditor) — live shader editor that exports directly to NeoWall. Write, preview, one-click install.

```bash
yay -S gleditor-git
```

## How it works

```
┌────────────────────────────────────────┐
│            NeoWall Daemon              │
├────────────────────────────────────────┤
│  Config Parser → Event Loop → Shaders │
├────────────────────────────────────────┤
│  Wayland (layer-shell)  │  X11 (EWMH) │
├────────────────────────────────────────┤
│           EGL / OpenGL 3.3             │
└────────────────────────────────────────┘
```

- Pure C, single binary
- GPU does the work, CPU chills
- timerfd/signalfd — no busy loops
- Same code runs everywhere

## vs Others

| | NeoWall | swww | mpvpaper | hyprpaper |
|-|---------|------|----------|-----------|
| Live shaders | ✅ | ❌ | ❌ | ❌ |
| Shadertoy | ✅ | ❌ | ❌ | ❌ |
| Videos | ❌ | GIFs | ✅ | ❌ |
| Images | ✅ | ✅ | ❌ | ✅ |
| X11 | ✅ | ❌ | ❌ | ❌ |
| Wayland | ✅ | ✅ | ✅ | ✅ |
| Interactive | ✅ | ❌ | ❌ | ❌ |

**NeoWall = only Linux tool for live GPU shader wallpapers.**

## Caveats

- **KDE Plasma**: Desktop icons might hide. Use a dock.
- **No video wallpapers**: Use mpvpaper for that.

## Contributing

```bash
meson setup build --buildtype=debug
ninja -C build
./build/neowall -f -v
```

PRs welcome: shaders, bug fixes, docs, testing.

## License

MIT — do whatever you want.

---

<p align="center">
  <a href="https://github.com/1ay1/neowall/issues">Bugs</a> · <a href="https://github.com/1ay1/neowall/discussions">Chat</a> · <a href="https://github.com/1ay1/neowall">⭐ Star if cool</a>
</p>