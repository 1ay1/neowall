<p align="center">
  <img src="packaging/neowall.svg" alt="NeoWall" width="400"/>
</p>

<p align="center">
  <strong>GPU-Accelerated Live Wallpapers for Wayland & X11</strong>
</p>

<p align="center">
  <a href="https://github.com/1ay1/neowall/actions/workflows/build.yml">
    <img src="https://github.com/1ay1/neowall/actions/workflows/build.yml/badge.svg" alt="Build Status"/>
  </a>
  <a href="https://github.com/1ay1/neowall/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/1ay1/neowall" alt="License"/>
  </a>
  <a href="https://github.com/1ay1/neowall/stargazers">
    <img src="https://img.shields.io/github/stars/1ay1/neowall?style=social" alt="Stars"/>
  </a>
</p>

<table>
  <tr>
    <td width="50%">
      <video src="https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560" width="100%" controls></video>
    </td>
    <td width="50%">
      <video src="https://github.com/user-attachments/assets/c1e38d88-5c1e-4db4-9948-da2ad86c6a69" width="100%" controls></video>
    </td>
  </tr>
</table>

## Overview

NeoWall is a high-performance wallpaper daemon for Linux desktop environments, designed to render animated shaders and seamless image slideshows with minimal resource consumption. Built from the ground up in C with GPU acceleration, NeoWall delivers smooth 60 FPS animations while maintaining exceptional compatibility across Wayland compositors and X11 window managers.

### Key Capabilities

- **High Performance Rendering**: GPU-accelerated shader execution via EGL/OpenGL ES with configurable frame rates (1-240 FPS)
- **Broad Platform Support**: Native integration with Wayland (Hyprland, Sway, River, KDE Plasma, GNOME) and X11 (i3, bspwm, dwm, awesome, xmonad, qtile)
- **Multi-Monitor Configuration**: Independent wallpaper settings per display with granular control
- **Extensive Shader Library**: Out-of-the-box compatibility with thousands of Shadertoy shaders
- **Efficient Architecture**: Event-driven design using timerfd and signalfd for minimal CPU overhead

## Features

**Rendering Engine**
- GPU-accelerated shader execution
- Configurable frame rate control (1-240 FPS)
- Optional VSync synchronization
- Interactive mouse event support for shader inputs

**Display Management**
- Multi-monitor support with per-display configuration
- Automatic display detection and reconfiguration
- Independent shader/image settings per output

**Content Options**
- Animated GLSL shader wallpapers
- Image slideshow capabilities with directory monitoring
- Multiple transition effects (fade, slide, glitch, pixelate)
- Adjustable animation speed and timing

**System Integration**
- Runtime control via daemon commands
- Human-readable configuration format (`.vibe`)
- Minimal dependencies and lightweight footprint
- Systemd service support

## Installation

### Package Manager

**Arch Linux**
```bash
yay -S neowall-git
```

### Build from Source

**Dependencies**

Debian/Ubuntu:
```bash
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols libx11-dev libxrandr-dev
```

Arch Linux:
```bash
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo \
    wayland-protocols libx11 libxrandr
```

Fedora:
```bash
sudo dnf install gcc make wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel \
    libX11-devel libXrandr-devel
```

**Build Steps**
```bash
git clone https://github.com/1ay1/neowall
cd neowall
make -j$(nproc)
sudo make install
```

## Quick Start

Launch the daemon:
```bash
neowall
```

The initial run generates a default configuration file at `~/.config/neowall/config.vibe`.

## Configuration

Configuration files are located at `~/.config/neowall/config.vibe`. The `.vibe` format provides a structured, human-readable syntax for wallpaper settings.

### Shader Wallpaper

```vibe
default {
  shader retro_wave.glsl
  shader_speed 1.2      # Animation speed multiplier (default: 1.0)
  shader_fps 60         # Target FPS (1-240, default: 60)
}
```

### Image Slideshow

```vibe
default {
  path ~/Pictures/Wallpapers/ # Directory path (must end with /)
  duration 300                # Seconds between transitions
  transition glitch           # fade | slide-left | slide-right | glitch | pixelate
}
```

### Multi-Monitor Configuration

```vibe
output {
  eDP-1 {
    shader plasma.glsl
    shader_fps 120      # 120 FPS on primary display
  }
  HDMI-A-1 {
    path ~/Pictures/Wallpapers/
    duration 1800       # Cycle every 30 minutes
    shader_fps 30       # 30 FPS on secondary display
  }
}
```

**Applying Configuration Changes**: Restart the daemon with `neowall kill && neowall`

## Included Shaders

NeoWall ships with over 30 pre-configured shaders optimized for desktop backgrounds:

| Shader | Description |
|--------|-------------|
| `retro_wave.glsl` | Synthwave aesthetic with animated waves |
| `matrix_rain.glsl` | Digital rain effect inspired by The Matrix |
| `plasma.glsl` | Flowing plasma energy patterns |
| `aurora.glsl` | Aurora borealis simulation |
| `ocean_waves.glsl` | Realistic ocean wave dynamics |
| `fractal_land.glsl` | Animated fractal geometry |

Additional shaders are available at [Shadertoy.com](https://shadertoy.com), with most requiring minimal adaptation for NeoWall compatibility.

## Runtime Control

```bash
neowall                # Start daemon
neowall kill           # Stop daemon
neowall status         # Check daemon status
neowall reload         # Reload configuration
```

## Development

### Building in Debug Mode

```bash
git clone https://github.com/1ay1/neowall
cd neowall
make debug
```

### Running with Verbose Logging

```bash
./build/bin/neowall -f -v
```

## Contributing

Contributions are welcome. Please submit pull requests for:
- New shader implementations
- Platform compatibility improvements
- Bug fixes and performance optimizations
- Documentation enhancements

**Bug Reports**: Submit issues at [github.com/1ay1/neowall/issues](https://github.com/1ay1/neowall/issues)

**Testing**: Compatibility reports for various compositors and window managers are appreciated.

## License

Licensed under the MIT License. See [LICENSE](LICENSE) for full terms.
