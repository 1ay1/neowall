# NeoWall

> Transform your desktop into a living canvas with GPU-accelerated shader wallpapers

Watch fractals breathe, waves ripple, and neon cities pulse—all at silky 60 FPS while sipping just 2% CPU.

**For Wayland desktops that deserve better than static wallpapers.**

**Multi-compositor support:** Native backends for KDE Plasma, Hyprland, Sway, River, and universal fallback for any Wayland compositor.

[https://github.com/user-attachments/assets/386243d0-53ca-4287-9ab6-873265d3d53a](https://github.com/user-attachments/assets/3a55d4e2-7257-4884-8aa2-9024ec86a560)

## ✨ Why NeoWall?

**Static wallpapers are so 2010.** Your desktop should be as dynamic as your workflow.

- 🎮 **Browse Shadertoy** for inspiration and compatible shaders
- 🔥 **60 FPS animations** that make your desktop feel alive  
- ⚡ **2% CPU usage** - your GPU does the heavy lifting
- 🎨 **30+ included shaders** - retro synthwave, plasma storms, matrix rain
- 🔄 **Hot-reload configs** - changes apply instantly
- 🖥️ **Multi-monitor magic** - different shaders per display
- 🌊 **Smooth transitions** - fade, glitch, pixelate between wallpapers

**Perfect for:** r/unixporn enthusiasts, shader artists, anyone tired of boring desktops

## 🚀 Quick Start

```bash
# Arch Linux
yay -S neowall-git

# From source (2 minutes)
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc) && sudo make install

# Launch and enjoy
neowall
```

Your first run creates `~/.config/neowall/config.vibe` with a gorgeous retro synthwave shader.

**Just works** on KDE Plasma, Hyprland, Sway, River, and any Wayland compositor.

## 🎨 Included Visual Experiences

**Grab-and-go shaders** that'll make your friends ask "how did you do that?"

| Shader | Vibe | Perfect For |
|--------|------|-------------|
| `retro_wave.glsl` | 🌆 Synthwave nostalgia | Coding sessions |
| `matrix_rain.glsl` | 🟢 Digital rainfall | Terminal work |
| `plasma.glsl` | 🌈 Flowing energy | Creative work |
| `aurora.glsl` | 🌌 Northern lights | Late-night browsing |
| `ocean_waves.glsl` | 🌊 Endless ocean | Focus time |
| `fractal_land.glsl` | 🔮 Infinite geometry | Mind expansion |

**30+ more** waiting in `~/.config/neowall/shaders/` after first run.

Want something specific? Browse [Shadertoy.com](https://shadertoy.com) for inspiration - many shaders can be adapted.

## ⚙️ Dead Simple Config

`~/.config/neowall/config.vibe`:

```vibe
# Live shader wallpaper
default {
  shader retro_wave.glsl
  shader_speed 1.2
}
```

```vibe
# Cycling photo slideshow  
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition glitch
}
```

```vibe
# Multi-monitor setup
output {
  eDP-1 {
    shader plasma.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/nature.jpg
  }
}
```

Config auto-reloads on save. No daemon restarts needed.

## 🎮 Real-Time Control

```bash
neowall next         # Switch to next wallpaper
neowall pause        # Freeze current animation  
neowall reload       # Apply config changes
neowall current      # What's running now?
```

Perfect for switching vibes mid-session.

## 🛠️ Create Your Own Magic

Drop any shader into `~/.config/neowall/shaders/`:

```glsl
#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    vec3 color = 0.5 + 0.5 * cos(time + uv.xyx + vec3(0,2,4));
    gl_FragColor = vec4(color, 1.0);
}
```

**Shadertoy-inspired** - provides `iTime`, `iResolution`, `iChannel0-4` uniforms for easier porting.

## 🔧 Installation Details

**System Requirements:**
- Wayland compositor (sorry X11, it's 2024)
- OpenGL ES 2.0+ GPU
- Basic build tools

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols
```

**Arch Linux:**
```bash
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo wayland-protocols
```

**Build:**
```bash
git clone https://github.com/1ay1/neowall
cd neowall
make -j$(nproc)
sudo make install
```

## 💡 Pro Tips

- **Performance**: Shaders run on GPU, images cycle with smart caching
- **Battery life**: Animations pause when screen locks automatically  
- **Multi-monitor**: Each display can run different content independently
- **Transitions**: `glitch` and `pixelate` effects add serious style points
- **Hot-reload**: Edit configs with live preview - no restarts

## 🤝 Contributing

Found a bug? Have an idea? PRs and issues welcome!

**Shader artists**: Submit your creations to grow the included collection.

**Developers**: Test on your compositor and report compatibility.

## 🏆 Credits

Built with love for the Wayland and r/unixporn communities.

Shader examples adapted from the incredible Shadertoy community.

## 📜 License

MIT License - Use it, modify it, share it.

---

**Make your desktop legendary.** ⭐ Star if NeoWall transformed your setup!