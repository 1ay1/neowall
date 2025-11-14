<div align="center">
  <img src="packaging/neowall.svg" alt="NeoWall Logo" width="560"/>
  
  <h3>GPU-Accelerated Live Wallpapers for Wayland</h3>
  
  <p>
    <strong>Transform your desktop into a living canvas.</strong><br/>
    Watch fractals breathe, waves ripple, and neon cities pulse at silky 60 FPS.
  </p>

  <p>
    <a href="#-quick-start"><strong>Quick Start</strong></a> •
    <a href="#-included-shaders"><strong>Gallery</strong></a> •
    <a href="#%EF%B8%8F-configuration"><strong>Config</strong></a> •
    <a href="#-create-your-own"><strong>Create Shaders</strong></a>
  </p>

  <img src="https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560" alt="NeoWall Demo" width="100%"/>
</div>

---

## ✨ Features

<table>
<tr>
<td width="50%">

**Performance**
- 🔥 **60 FPS** shader animations
- ⚡ **GPU-accelerated** rendering
- 🎯 **Precise FPS control** (1-240 FPS)
- 🔄 **VSync support** for tear-free display

</td>
<td width="50%">

**Compatibility**
- 🖥️ **Multi-monitor** with independent configs
- 🌊 **Wayland-native** (Hyprland, Sway, River, KDE)
- 🎨 **Shadertoy-compatible** shaders
- 📦 **30+ included shaders** ready to use

</td>
</tr>
</table>

**Perfect for:** r/unixporn enthusiasts, shader artists, developers who want their desktop to match their vibe.

---

## 🚀 Quick Start

```bash
# Arch Linux
yay -S neowall-git

# From source (2 minutes)
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc) && sudo make install

# Launch
neowall
```

Your first run creates `~/.config/neowall/config.vibe` with a gorgeous retro synthwave shader.

> **Note:** Requires a Wayland compositor. X11 is not supported.

---

## 🎨 Included Shaders

| Preview | Shader | Vibe | Best For |
|---------|--------|------|----------|
| 🌆 | `retro_wave.glsl` | Synthwave nostalgia | Late-night coding |
| 🟢 | `matrix_rain.glsl` | Digital rainfall | Terminal hackers |
| 🌈 | `plasma.glsl` | Flowing energy | Creative work |
| 🌌 | `aurora.glsl` | Northern lights | Focus sessions |
| 🌊 | `ocean_waves.glsl` | Endless ocean | Meditation |
| 🔮 | `fractal_land.glsl` | Infinite geometry | Mind expansion |
| ⚡ | `electric_storm.glsl` | Lightning chaos | High-energy work |
| 🌀 | `vortex.glsl` | Hypnotic spiral | Deep focus |

**30+ more** waiting in `~/.config/neowall/shaders/` after first run.

Browse [Shadertoy.com](https://shadertoy.com) for thousands more - many can be adapted with minimal changes.

---

## ⚙️ Configuration

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

## 🎮 Runtime Controls

Control your wallpapers without editing config:

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

## 🛠️ Create Your Own

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

**Shadertoy Uniforms Supported:**
- `iTime` - Shader playback time
- `iResolution` - Viewport resolution
- `iChannel0-4` - Input textures
- `iMouse` - Mouse position
- `iDate` - Current date/time

Most Shadertoy shaders work with minimal adaptation!

---

## 📦 Installation

### Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols
```

**Arch Linux:**
```bash
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo wayland-protocols
```

**Fedora:**
```bash
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
- Wayland compositor (Hyprland, Sway, River, KDE Plasma, etc.)
- OpenGL ES 2.0+ GPU
- Linux kernel 3.17+ (for `timerfd` support)

---

## 💡 Pro Tips

- **Battery Life:** Lower `shader_fps` on battery power
- **Multi-Monitor:** Each display can have completely different content
- **Transitions:** `glitch` and `pixelate` add serious style points
- **VSync:** Enable for tear-free display, disable for precise FPS control
- **FPS Monitoring:** Use `show_fps true` to verify performance
- **State Persistence:** Wallpaper cycle position is remembered across restarts
- **Config Philosophy:** Treat config as immutable - restart to apply changes

### Recommended Settings

**For Gaming PC:**
```vibe
shader_fps 144
vsync false
```

**For Laptop:**
```vibe
shader_fps 30
vsync true
```

**For Multi-Monitor:**
```vibe
output {
  main { shader_fps 60 }
  secondary { shader_fps 30 }
}
```

---

## 🏗️ Architecture

NeoWall is built with performance and reliability in mind:

- **Event-driven architecture** - Uses `timerfd`/`signalfd` for minimal overhead
- **Compositor abstraction layer** - Native backends for different Wayland compositors
- **GPU-accelerated rendering** - All shader computation happens on GPU
- **Runtime capability detection** - Adapts between OpenGL ES 2.0/3.0/3.1/3.2
- **State persistence** - Remembers your place in wallpaper cycles across restarts
- **Zero-copy rendering** - Direct GPU upload, minimal memory copies

---

## 🤝 Contributing

Contributions welcome! Here's how you can help:

- **🎨 Shader Artists:** Submit your creations via PR to grow the included collection
- **🔧 Developers:** Test on your compositor and report compatibility issues
- **📝 Documentation:** Improve guides, add examples, fix typos
- **🐛 Bug Reports:** [Open an issue](https://github.com/1ay1/neowall/issues) with details

### Development

```bash
git clone https://github.com/1ay1/neowall
cd neowall
make debug        # Debug build with symbols
./build/bin/neowall -f -v  # Run in foreground with verbose logging
```

---

## 🏆 Credits

Built with ❤️ for the Wayland and r/unixporn communities.

**Shader examples** adapted from the incredible [Shadertoy](https://shadertoy.com) community.

**Inspiration:** The beautiful chaos of `mpvpaper`, `swww`, and `hyprpaper` - but faster.

---

## 📜 License

**MIT License** - Use it, modify it, share it.

See [LICENSE](LICENSE) for full text.

---

<div align="center">
  <h3>Make your desktop legendary.</h3>
  <p>⭐ Star if NeoWall transformed your setup!</p>
  
  <p>
    <a href="https://github.com/1ay1/neowall/issues">Report Bug</a> •
    <a href="https://github.com/1ay1/neowall/issues">Request Feature</a> •
    <a href="https://github.com/1ay1/neowall/discussions">Discussions</a>
  </p>
</div>
