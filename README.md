<img src="packaging/neowall.svg" alt="NeoWall Logo" width="150" align="left"/>

# NeoWall

**GPU-Accelerated Live Wallpapers for Wayland & X11**
<br/>

<p align="left">
  <a href="https://github.com/1ay1/neowall/actions/workflows/build.yml">
    <img src="https://github.com/1ay1/neowall/actions/workflows/build.yml/badge.svg" alt="Build Status"/>
  </a>
  <a href="https://github.com/1ay1/neowall/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/1ay1/neowall" alt="License"/>
  </a>
  <a href="https://github.com/1ay1/neowall/stargazers">
    <img src="https://img.shields.io/github/stars/1ay1/neowall?style=social" alt="GitHub Stars"/>
  </a>
</p>

<p align="left">
  <a href="#-why-neowall">Why NeoWall?</a> •
  <a href="#-features">Features</a> •
  <a href="#-quick-start">Quick Start</a> •
  <a href="#-configuration">Configuration</a> •
  <a href="#-installation">Installation</a>
</p>

<br/>

---

<table>
  <tr>
    <td align="center">
      https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560
      <br/>
      <em>Interactive mouse tracking.</em>
    </td>
    <td align="center">
      https://github.com/user-attachments/assets/c1e38d88-5c1e-4db4-9948-da2ad86c6a69
      <br/>
      <em>Smooth transitions & live wallpapers.</em>
    </td>
  </tr>
</table>

---

## 🤔 Why NeoWall?

NeoWall is a high-performance wallpaper daemon for Linux that brings your desktop to life with animated shaders and seamless image slideshows. It is designed from the ground up for efficiency, compatibility, and extensive customization.

- **High Performance:** Renders animations at a smooth 60 FPS with minimal CPU usage by leveraging GPU acceleration.
- **Broad Compatibility:** Works flawlessly across a wide range of Wayland compositors (Hyprland, Sway, KDE, GNOME) and X11 window managers (i3, bspwm, dwm).
- **Highly Customizable:** Supports thousands of Shadertoy shaders out-of-the-box, offers powerful per-monitor configuration, and provides a rich set of transition effects.
- **Lightweight & Efficient:** Written in C with an event-driven architecture, ensuring it remains light on system resources.

## ✨ Features

<table>
<tr>
<td width="50%">

**Performance**
- Smooth 60 FPS shader animations
- GPU-accelerated rendering via EGL/OpenGL ES
- Precise FPS control (1-240 FPS)
- Optional VSync for tear-free rendering

</td>
<td width="50%">

**Compatibility**
- Multi-monitor support with independent configurations
- Wayland: Hyprland, Sway, River, KDE Plasma, GNOME
- X11: i3, bspwm, dwm, awesome, xmonad, qtile
- Excellent Shadertoy compatibility
- Mouse event support for interactive shaders

</td>
</tr>
</table>

- **Wallpaper Slideshows:** Automatically cycle through images in a directory.
- **Smooth Transitions:** A selection of eye-catching transitions like `fade`, `slide`, `glitch`, and `pixelate`.
- **Simple Configuration:** A clean, human-readable config format (`.vibe`).
- **Runtime Control:** Switch wallpapers, pause/resume, and check status on the fly.
- **Robust Architecture:** Event-driven design using `timerfd` and `signalfd` for reliability.

---

## 🚀 Quick Start

```bash
# Arch Linux
yay -S neowall-git

# From source (2 minutes)
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc) && sudo make install

# Launch the daemon
neowall
```

Your first run creates a default configuration at `~/.config/neowall/config.vibe`.

---

## 🎨 Included Shaders

NeoWall comes with over 30 pre-packaged shaders. Here is a small sample:

| Preview | Shader | Style |
|---------|--------|-------|
| _(preview)_ | `retro_wave.glsl` | Synthwave |
| _(preview)_ | `matrix_rain.glsl` | Digital rain |
| _(preview)_ | `plasma.glsl` | Flowing energy |
| _(preview)_ | `aurora.glsl` | Northern lights |
| _(preview)_ | `ocean_waves.glsl` | Ocean waves |
| _(preview)_ | `fractal_land.glsl` | Fractal geometry |

Browse [Shadertoy.com](https://shadertoy.com) for thousands more — most work with minimal changes.

---

## ⚙️ Configuration

Configuration is located at `~/.config/neowall/config.vibe`.

### Live Shader Wallpaper
```vibe
default {
  shader retro_wave.glsl
  shader_speed 1.2      # Animation speed multiplier (default: 1.0)
  shader_fps 60         # Target FPS (1-240, default: 60)
}
```

### Image Slideshow with Transitions
```vibe
default {
  path ~/Pictures/Wallpapers/ # Must end with a slash!
  duration 300                # Seconds between wallpapers
  transition glitch           # fade | slide-left | slide-right | glitch | pixelate
}
```

### Multi-Monitor Setup
```vibe
output {
  eDP-1 {
    shader plasma.glsl
    shader_fps 120      # Smooth 120 FPS on main display
  }
  HDMI-A-1 {
    path ~/Pictures/Wallpapers/
    duration 1800       # Cycle every 30 minutes
    shader_fps 30       # Power-saving 30 FPS on secondary
  }
}
```

**Apply changes by restarting the daemon:** `neowall kill && neowall`

---

## 🛠️ Installation

### Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols libx11-dev libxrandr-dev
```

**Arch Linux:**
```bash
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo \
    wayland-protocols libx11 libxrandr
```

**Fedora:**
```bash
sudo dnf install gcc make wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel \
    libX11-devel libXrandr-devel
```

### Build from Source
```bash
git clone https://github.com/1ay1/neowall
cd neowall
make -j$(nproc)
sudo make install
```

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a pull request, open an issue, or suggest features.

- **Shaders:** Submit new shaders via PR.
- **Testing:** Report compatibility issues on your compositor.
- **Bugs:** [Open an issue](https://github.com/1ay1/neowall/issues).

### Development
```bash
# Clone the repository
git clone https://github.com/1ay1/neowall
cd neowall

# Build in debug mode
make debug

# Run in foreground with verbose logging
./build/bin/neowall -f -v
```

---

## 📜 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.