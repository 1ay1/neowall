<p align="center">
  <img src="packaging/neowall.svg" alt="NeoWall" width="420"/>
</p>

<h1 align="center">NeoWall</h1>

<p align="center">
  <strong>🔴 Take the red pill. Your desktop will never be the same.</strong>
</p>

<p align="center">
  <em>GPU-accelerated live wallpapers for Linux • Shadertoy compatible • 2% CPU usage</em>
</p>

<p align="center">
  <a href="https://github.com/1ay1/neowall/actions/workflows/build.yml">
    <img src="https://github.com/1ay1/neowall/actions/workflows/build.yml/badge.svg" alt="Build Status"/>
  </a>
  <a href="https://github.com/1ay1/neowall/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/1ay1/neowall?color=blue" alt="License"/>
  </a>
  <a href="https://github.com/1ay1/neowall/stargazers">
    <img src="https://img.shields.io/github/stars/1ay1/neowall?style=social" alt="Stars"/>
  </a>
  <a href="https://github.com/1ay1/neowall/releases">
    <img src="https://img.shields.io/github/v/release/1ay1/neowall?include_prereleases&color=purple" alt="Release"/>
  </a>
</p>

<br/>

<p align="center">
  <video src="https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560" width="49%" controls autoplay loop muted></video>
  <video src="https://github.com/user-attachments/assets/c1e38d88-5c1e-4db4-9948-da2ad86c6a69" width="49%" controls autoplay loop muted></video>
</p>

<br/>

---

## ✨ Why NeoWall?

| Feature | NeoWall | swww | mpvpaper | hyprpaper |
|---------|---------|------|----------|-----------|
| **Live GLSL Shaders** | ✅ Yes | ❌ No | ❌ No | ❌ No |
| **Shadertoy Compatible** | ✅ Yes | ❌ No | ❌ No | ❌ No |
| **Video Wallpapers** | ❌ No | ✅ GIFs | ✅ Yes | ❌ No |
| **Image Slideshows** | ✅ Yes | ✅ Yes | ❌ No | ❌ No |
| **Interactive (iMouse)** | ✅ Yes | ❌ No | ❌ No | ❌ No |
| **X11 Support** | ✅ Yes | ❌ No | ❌ No | ❌ No |
| **Wayland Support** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| **Multi-Monitor** | ✅ Per-output config | ✅ Yes | ✅ Yes | ✅ Yes |
| **Transitions** | ✅ 5 effects | ✅ Yes | ❌ No | ❌ No |

**TL;DR**: NeoWall is the only Linux tool that renders **live GPU shaders** as wallpapers with Shadertoy compatibility.

---

## 🚀 Features

<table>
<tr>
<td width="50%">

### 🎨 Stunning Visuals
- **30+ built-in shaders** — synthwave, matrix rain, fractals, auroras
- **Shadertoy compatible** — thousands of community shaders work out of the box
- **Smooth transitions** — fade, slide, glitch, pixelate effects
- **1-240 FPS** — configurable frame rate per monitor

</td>
<td width="50%">

### ⚡ Lightweight
- **Pure C** — minimal dependencies
- **GPU-accelerated** — OpenGL ES 2.0/3.0
- **Event-driven** — uses timerfd/signalfd
- **Single binary** — no runtime overhead

</td>
</tr>
<tr>
<td width="50%">

### 🖥️ Universal Compatibility
- **Wayland** — Hyprland, Sway, River, KDE, GNOME
- **X11** — i3, bspwm, dwm, awesome, xmonad, qtile
- **Multi-monitor** — independent settings per display
- **HiDPI** — automatic scaling support

</td>
<td width="50%">

### 🎛️ Full Control
- **Runtime commands** — next, pause, resume, set index
- **Simple config** — human-readable `.vibe` format
- **Image slideshows** — with smooth transitions
- **VSync support** — tear-free rendering

</td>
</tr>
</table>

---

## 📦 Installation

### Arch Linux (AUR)

```bash
yay -S neowall-git
```

### Build from Source

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install build-essential meson ninja-build libwayland-dev \
    libgles2-mesa-dev libpng-dev libjpeg-dev wayland-protocols \
    libx11-dev libxrandr-dev

# Build
git clone https://github.com/1ay1/neowall
cd neowall
meson setup build
ninja -C build
sudo ninja -C build install
```

<details>
<summary>📋 Dependencies for other distros</summary>

**Arch Linux:**
```bash
sudo pacman -S base-devel meson ninja wayland mesa libpng libjpeg-turbo \
    wayland-protocols libx11 libxrandr
```

**Fedora:**
```bash
sudo dnf install gcc meson ninja-build wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel \
    libX11-devel libXrandr-devel
```

</details>

---

## 🎬 Quick Start

```bash
# Start the daemon
neowall

# That's it! Edit the config to customize:
# ~/.config/neowall/config.vibe
```

### First Run

NeoWall automatically creates a default config with the `retro_wave` shader. Your desktop instantly transforms into a synthwave dreamscape.

---

## ⚙️ Configuration

Config location: `~/.config/neowall/config.vibe`

### 🌊 Live Shader Wallpaper

```
default {
  shader retro_wave.glsl
  shader_speed 0.8        # Slow it down for chill vibes
  shader_fps 60           # Smooth 60 FPS
}
```

### 🖼️ Image Slideshow

```
default {
  path ~/Pictures/Wallpapers/    # Directory of images
  duration 300                   # Change every 5 minutes
  transition glitch              # Cyberpunk transition effect
}
```

### 🖥️ Multi-Monitor Setup

```
output {
  DP-1 {
    shader matrix_rain.glsl
    shader_fps 60
  }
  HDMI-A-1 {
    path ~/Pictures/Landscapes/
    duration 600
    transition fade
  }
}
```

---

## 🎮 Runtime Commands

Control NeoWall while it's running:

```bash
neowall              # Start daemon
neowall kill         # Stop daemon
neowall next         # Next wallpaper in cycle
neowall pause        # Pause cycling/animation
neowall resume       # Resume cycling/animation
neowall list         # Show all wallpapers with indices
neowall set 5        # Jump to wallpaper at index 5
neowall current      # Show current wallpaper info
```

### Example Workflow

```bash
# See what's available
$ neowall list
Wallpaper cycle list:

  [0] synthwave.glsl
  [1] matrix_rain.glsl
  [2] fractal_land.glsl  <-- current
  [3] aurora.glsl
  [4] ocean_waves.glsl

# Jump to aurora
$ neowall set 3
Setting wallpaper to index 3...
```

---

## 🎨 Included Shaders

NeoWall ships with **30+ hand-picked shaders** optimized for desktop use:

| Category | Shaders |
|----------|---------|
| 🌆 **Synthwave** | `retro_wave`, `synthwave`, `neonwave_sunrise` |
| 🌊 **Nature** | `ocean_waves`, `aurora`, `sunrise`, `moon_ocean` |
| 💻 **Cyber** | `matrix_rain`, `matrix_real`, `glowing_triangles` |
| 🔮 **Abstract** | `fractal_land`, `plasma`, `mandelbrot` |
| 🌌 **Space** | `star_next`, `starship_reentry`, `cross_galactic_ocean` |

### Using Shadertoy Shaders

Most Shadertoy shaders work with minimal or no modification:

1. Copy the shader code from Shadertoy
2. Save as `~/.config/neowall/my_shader.glsl`
3. Set in config: `shader my_shader.glsl`

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      NeoWall Daemon                      │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │   Config    │  │  Event Loop │  │  Shader Engine  │  │
│  │   Parser    │  │  (poll/fd)  │  │   (GLSL/ES)     │  │
│  └─────────────┘  └─────────────┘  └─────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                 Compositor Abstraction                   │
│  ┌──────────────────────┐  ┌──────────────────────────┐ │
│  │   Wayland Backend    │  │     X11 Backend          │ │
│  │  (wlr-layer-shell)   │  │    (EWMH/XRandR)         │ │
│  └──────────────────────┘  └──────────────────────────┘ │
├─────────────────────────────────────────────────────────┤
│                    EGL / OpenGL ES                       │
└─────────────────────────────────────────────────────────┘
```

**Key Design Decisions:**
- **Zero-copy rendering** — GPU does all the heavy lifting
- **signalfd** — race-free signal handling
- **timerfd** — precise frame timing without busy loops
- **Compositor abstraction** — same codebase for all platforms

---

## 🤝 Contributing

We welcome contributions! Here's how you can help:

- 🎨 **New shaders** — Add beautiful wallpaper shaders
- 🐛 **Bug fixes** — Found an issue? Fix it!
- 📖 **Documentation** — Improve guides and examples
- 🧪 **Testing** — Report compatibility with your compositor

```bash
# Development build
meson setup build --buildtype=debug
ninja -C build
./build/neowall -f -v  # Foreground + verbose
```

---

---

## 🙏 Credits

- Shader inspiration from [Shadertoy](https://shadertoy.com) community
- Built with love for the Linux desktop community

---

## 📄 License

MIT License — Use it, modify it, share it.

---

<p align="center">
  <strong>⭐ Star this repo if NeoWall made your desktop awesome!</strong>
</p>

<p align="center">
  <a href="https://github.com/1ay1/neowall/issues">Report Bug</a>
  •
  <a href="https://github.com/1ay1/neowall/issues">Request Feature</a>
  •
  <a href="https://github.com/1ay1/neowall/discussions">Discussions</a>
</p>