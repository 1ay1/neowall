<div align="center">
  <img src="packaging/neowall.svg" alt="NeoWall Logo" width="100%"/>
  
  <h3>GPU-Accelerated Live Wallpapers for Wayland & X11</h3>
  
  <p>
    Run shaders and animated wallpapers on your desktop.<br/>
    Smooth 60 FPS, multi-monitor support, works anywhere.
  </p>

  <p>
    <a href="#quick-start"><strong>Quick Start</strong></a> •
    <a href="#included-shaders"><strong>Gallery</strong></a> •
    <a href="#configuration"><strong>Config</strong></a> •
    <a href="#create-your-own"><strong>Create Shaders</strong></a>
  </p>

  https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560
  
  <p><em>Interactive mouse tracking - shaders respond to cursor movement</em></p>
  
  https://github.com/user-attachments/assets/c1e38d88-5c1e-4db4-9948-da2ad86c6a69
</div>

---

## Features

<table>
<tr>
<td width="50%">

**Performance**
- 60 FPS shader animations
- GPU-accelerated rendering
- Precise FPS control (1-240)
- Optional VSync

</td>
<td width="50%">

**Compatibility**
- Multi-monitor with independent configs
- Wayland: Hyprland, Sway, River, KDE, GNOME
- X11: i3, bspwm, dwm, awesome, xmonad, qtile
- Shadertoy-compatible shaders
- Mouse event support for interactive shaders
- 30+ included shaders

</td>
</tr>
</table>

---

## Quick Start

```bash
# Arch Linux
yay -S neowall-git

# From source (2 minutes)
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc) && sudo make install

# Launch (auto-detects Wayland or X11)
neowall
```

Your first run creates `~/.config/neowall/config.vibe` with a default shader.

> Works on Wayland compositors (Hyprland, Sway, River, KDE, GNOME) and X11 tiling window managers (i3, bspwm, dwm, awesome, xmonad, qtile). Auto-detects your environment.

---

## Included Shaders

| Preview | Shader | Style | 
|---------|--------|-------|
| | `retro_wave.glsl` | Synthwave |
| | `matrix_rain.glsl` | Digital rain |
| | `plasma.glsl` | Flowing energy |
| | `aurora.glsl` | Northern lights |
| | `ocean_waves.glsl` | Ocean waves |
| | `fractal_land.glsl` | Fractal geometry |
| | `electric_storm.glsl` | Lightning |
| | `vortex.glsl` | Hypnotic spiral |

30+ more included. Browse [Shadertoy.com](https://shadertoy.com) for thousands more - most work with minimal changes.

---

## Configuration

Config lives at `~/.config/neowall/config.vibe` - a simple, human-readable format:

### Live Shader Wallpaper

```vibe
default {
  shader retro_wave.glsl
  shader_speed 1.2      # Animation speed multiplier (default: 1.0)
  shader_fps 60         # Target FPS (1-240, default: 60)
  vsync false           # Use custom FPS (false) or sync to monitor (true)
  show_fps true         # Display FPS counter (default: false)
}
```

### Image Slideshow with Transitions

```vibe
default {
  path ~/Pictures/Wallpapers/
  duration 300          # Seconds between wallpapers
  transition glitch     # fade | slide-left | slide-right | glitch | pixelate | none
  mode fill             # fill | fit | center | stretch | tile
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
    shader matrix_rain.glsl
    shader_fps 30       # Power-saving 30 FPS on secondary
  }
}
```

**To apply changes:** Restart the daemon
```bash
neowall kill && neowall
```

---

## Runtime Controls

Control wallpapers at runtime:

```bash
neowall next         # Switch to next wallpaper/shader
neowall pause        # Pause cycling
neowall resume       # Resume cycling
neowall current      # Show current wallpaper info
neowall kill         # Stop daemon
```

### Configuration vs Runtime

**Configuration** (requires restart):
- Which wallpapers/shaders to display
- Shader speed, FPS, transitions
- Monitor assignments
- Edit `~/.config/neowall/config.vibe` and restart

**Runtime Controls** (instant):
- Navigate through configured wallpapers
- Pause/resume cycling
- Query current state

---

## Create Your Own

Drop any GLSL shader into `~/.config/neowall/shaders/`:

```glsl
#version 100
precision highp float;

uniform float iTime;
uniform vec2 iResolution;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution;
    vec3 color = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0,2,4));
    gl_FragColor = vec4(color, 1.0);
}
```

**Supported Shadertoy Uniforms:**
- `iTime` - Shader playback time
- `iResolution` - Viewport resolution
- `iChannel0-4` - Input textures
- `iMouse` - Mouse position
- `iDate` - Current date/time

---

## Installation

### Dependencies

**Debian/Ubuntu:**
```bash
# Wayland + X11 (recommended)
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols libx11-dev libxrandr-dev

# Wayland only
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols
```

**Arch Linux:**
```bash
# Wayland + X11 (recommended)
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo \
    wayland-protocols libx11 libxrandr

# Wayland only
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo wayland-protocols
```

**Fedora:**
```bash
# Wayland + X11 (recommended)
sudo dnf install gcc make wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel \
    libX11-devel libXrandr-devel

# Wayland only
sudo dnf install gcc make wayland-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel wayland-protocols-devel
```

### Build from Source

```bash
git clone https://github.com/1ay1/neowall
cd neowall
make -j$(nproc)
sudo make install
```

**System Requirements:**
- **Wayland:** Hyprland, Sway, River, KDE Plasma, GNOME Shell, or any wlroots-based compositor
- **X11:** i3, bspwm, dwm, awesome, xmonad, qtile, herbstluftwm (tiling WMs)
- OpenGL ES 2.0+ GPU
- Linux kernel 3.17+ (for `timerfd` support)

**Backend Auto-Detection:**
NeoWall automatically detects your environment and uses the appropriate backend:
- Wayland compositors → wlr-layer-shell or compositor-specific backend
- X11 tiling WMs → X11 backend with root pixmap integration

See [X11 Backend Documentation](src/compositor/backends/x11/README.md) for X11-specific features and troubleshooting.

---

## Tips

- **Battery:** Lower `shader_fps` when on battery
- **Multi-Monitor:** Each display can have different content
- **VSync:** Enable for tear-free, disable for precise FPS control
- **FPS Monitor:** Use `show_fps true` to check performance

### X11 Notes

- Renders to root window pixmap (works with Conky pseudo-transparency)
- Designed for tiling WMs (i3, bspwm, dwm)
- XRandR auto-detection for multi-monitor setups
- See [X11 Backend Documentation](src/compositor/backends/x11/README.md) for details

### Example Configs

**High refresh rate:**
```vibe
shader_fps 144
vsync false
```

**Power saving:**
```vibe
shader_fps 30
vsync true
```

---

## Mouse Events

Real-time mouse tracking on both Wayland and X11. Mouse position is available via the `iMouse` uniform:

```glsl
uniform vec4 iMouse;  // .xy = current position, .zw = click position
```

### Example Interactive Shader

Create `~/.config/neowall/shaders/interactive.glsl`:

```glsl
#version 100
precision highp float;

uniform float iTime;
uniform vec2 iResolution;
uniform vec4 iMouse;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution;
    vec2 mouse = iMouse.xy / iResolution;
    
    // Distance from mouse cursor
    float dist = length(uv - mouse);
    
    // Ripple effect following mouse
    float ripple = sin(dist * 20.0 - iTime * 3.0) * 0.5 + 0.5;
    
    vec3 color = vec3(ripple);
    gl_FragColor = vec4(color, 1.0);
}
```

### Technical Details

Mouse events are captured when the cursor is over the wallpaper (empty desktop areas). This is standard for wallpaper applications since they sit below all windows.

---

## Architecture

NeoWall is built with performance in mind:

- Event-driven architecture using `timerfd`/`signalfd`
- Compositor abstraction layer with native backends
- GPU-accelerated rendering
- Runtime OpenGL ES capability detection (2.0/3.0/3.1/3.2)
- State persistence across restarts

---

## Contributing

Contributions welcome:

- **Shaders:** Submit via PR
- **Testing:** Report compatibility issues on your compositor
- **Bugs:** [Open an issue](https://github.com/1ay1/neowall/issues)

### Development

```bash
git clone https://github.com/1ay1/neowall
cd neowall
make debug
./build/bin/neowall -f -v
```

---

## License

MIT License. See [LICENSE](LICENSE).

---

<div align="center">
  <p>
    <a href="https://github.com/1ay1/neowall/issues">Report Bug</a> •
    <a href="https://github.com/1ay1/neowall/issues">Request Feature</a> •
    <a href="https://github.com/1ay1/neowall/discussions">Discussions</a>
  </p>
</div>
