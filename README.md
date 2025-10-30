# NeoWall

> "The Matrix, but make it wallpaper."

**NeoWall** brings GPU-powered animated wallpapers to Wayland. Run Shadertoy shaders at 60 FPS with only 2% CPU usage. 10,000+ shaders work out of the box.

**Take the red pill.** 🔴💊

https://github.com/user-attachments/assets/386243d0-53ca-4287-9ab6-873265d3d53a

## Why NeoWall?

Named after Neo from The Matrix - because your wallpaper can now run The Matrix. Also fits the "neo" family of modern Linux tools (neovim, neofetch). Plus, GPU shaders are the next evolution of wallpapers. 🎬

## Features

- 🚀 **Insanely Efficient** - 2% CPU at 60 FPS (lighter than video wallpapers)
- ✨ **10,000+ Shaders Ready** - Full Shadertoy compatibility out of the box
- 🎬 **Live GPU Animation** - Matrix rain, plasma, fractals, and more
- 🖼️ **Smooth Transitions** - Fade, slide, glitch, pixelate between wallpapers
- 🔄 **Auto Cycling** - Point at a folder, automatically switch on interval
- 🖥️ **Multi-Monitor** - Different wallpaper per screen
- 🔥 **Hot-Reload** - Edit config, see changes instantly (no restart)
- ⚡ **Just Works** - Hyprland, Sway, River, and other Wayland compositors

## Why NeoWall?

| Feature | NeoWall | swaybg | hyprpaper | wpaperd |
|---------|------------|--------|-----------|---------|
| Live GPU Shaders | ✅ | ❌ | ❌ | ❌ |
| Shadertoy Compatible | ✅ | ❌ | ❌ | ❌ |
| Smooth Transitions | ✅ | ❌ | ❌ | ✅ |
| Hot Config Reload | ✅ | ❌ | ❌ | ✅ |
| Auto Directory Cycling | ✅ | ❌ | ❌ | ✅ |
| Multi-Monitor | ✅ | ✅ | ✅ | ✅ |
| Performance | 2% CPU @ 60fps | N/A | N/A | Low |

## Quick Start (30 seconds)

```bash
# Arch Linux
yay -S neowall-git
neowall  # Matrix rain starts automatically 🟢

# Build from source (other distros)
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc)
sudo make install
neowall

# That's it. Your wallpaper is now alive. 🎬
```

**Pro tip:** Edit `~/.config/neowall/config.vibe` and see changes instantly. No restart needed.

## Configuration

Edit `~/.config/neowall/config.vibe` - clean syntax, no quotes needed:

```vibe
# Run Matrix rain (the featured shader!)
default {
  shader matrix_real.glsl
  shader_speed 1.0
}
```

That's it. Matrix rain on your desktop. 🟢

**Want more?**

```vibe
# Static image
default {
  path ~/Pictures/wallpaper.png
}

# Cycle through images every 5 minutes
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition fade
}

# Different wallpaper per monitor
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/Nature/
  }
}
```

**13+ included shaders:** matrix_real.glsl, matrix_rain.glsl, plasma.glsl, aurora.glsl, 2d_clouds.glsl, fractal_land.glsl, and more in `~/.config/neowall/shaders/`

**Find monitor names:** `hyprctl monitors` (Hyprland) • `swaymsg -t get_outputs` (Sway) • `wlr-randr` (generic)

**Cycle through shaders:** Point at a directory with trailing slash
```vibe
default {
  shader ~/.config/neowall/shaders/
  duration 300
}
```

## Display Modes

- `fill` - Scale to fill screen (crop if needed) **← recommended**
- `fit` - Scale to fit inside screen (may show borders)
- `center` - No scaling, just center it
- `stretch` - Fill screen (may distort aspect ratio)
- `tile` - Repeat image to fill screen

## 🎨 Included Shaders

NeoWall comes with 13+ high-quality shaders ready to use:

- **matrix_rain.glsl** - Classic Matrix falling code effect 🟢
- **matrix_real.glsl** - Enhanced Matrix with more detail 🟢
- **2d_clouds.glsl** - Procedural cloud formations
- **plasma.glsl** - Colorful flowing plasma waves
- **aurora.glsl** - Northern lights simulation
- **sunrise.glsl** - Dynamic sunrise/sunset sky
- **ocean_waves.glsl** - Realistic water simulation
- **fractal_land.glsl** - Infinite fractal landscapes
- **mandelbrot.glsl** - Classic Mandelbrot set zoom
- **star_field.glsl** - Animated starfield
- And more!

All shaders are installed to `~/.config/neowall/shaders/` on first run. Just reference them by filename in your config.

Want more? Browse [Shadertoy.com](https://www.shadertoy.com/) - most shaders work with minimal tweaks!

## Transitions

Available effects:
- `fade` - Smooth crossfade (default)
- `slide_left` - Slide old image left
- `slide_right` - Slide old image right  
- `glitch` - Digital corruption effect
- `pixelate` - Mosaic block transition
- `none` - Instant switch (boring!)

## Commands

```bash
neowall              # Start daemon
neowall kill         # Stop daemon
neowall reload       # Reload config
neowall next         # Skip to next wallpaper/shader
neowall pause        # Pause cycling
neowall resume       # Resume cycling
neowall current      # Show current wallpaper
```

## Writing Custom Shaders

Shaders use GLSL and get these uniforms automatically:

```glsl
#version 100
precision highp float;

uniform float time;        // Elapsed seconds
uniform vec2 resolution;   // Screen size

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    vec3 color = vec3(uv, sin(time));
    gl_FragColor = vec4(color, 1.0);
}
```

Save as `~/.config/neowall/shaders/myshader.glsl` and use:

```vibe
default {
  shader myshader.glsl
}
```

**Shadertoy Compatible!** Browse [Shadertoy.com](https://www.shadertoy.com/) - 10,000+ shaders work with minimal tweaks. We provide `iTime`, `iResolution`, `iChannel0-4` uniforms automatically.

## Why NeoWall is Better

**GPU shaders vs everything else:**
- **vs Static Images:** Boring. It's 2025, not 1995.
- **vs Video Wallpapers:** Battery killer. 10-20% CPU for a looping MP4.
- **vs NeoWall:** 2% CPU, infinite variety, 60 FPS. The future.

**What you get:**
- Single binary, no dependency hell
- Tested on Hyprland, Sway, River
- One config file, sensible defaults
- 13+ included shaders, 10,000+ more available

## Troubleshooting

**Shader shows black screen?**
- Check logs: `neowall -fv` (foreground + verbose)
- Shaders compile on load, may take a second
- Complex shaders may need optimization

**Config changes don't apply?**
- Hot-reload is enabled by default, just wait 2 seconds
- Force reload: `neowall reload`

**Wallpaper not visible?**
- Make sure no other wallpaper daemon is running (swaybg, hyprpaper, etc.)
- Check if it's in Hyprland layers: `hyprctl layers`

## Building from Source

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install build-essential libwayland-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols

# Dependencies (Arch)
sudo pacman -S base-devel wayland mesa libpng libjpeg-turbo wayland-protocols

# Build
make -j$(nproc)

# Install
sudo make install

# Run
neowall
```

## Contributing

Found a bug? Made a cool shader? Want to add a feature? **PRs welcome!**

Requirements:
- Write clean, maintainable code
- Test on at least one compositor (Hyprland, Sway, or River)
- Share your cool setups with the community

**Made something cool?** Post it on r/unixporn and tag NeoWall!

## License

MIT - Do whatever you want, just don't sue us if your wallpaper becomes sentient. (Looking at you, Matrix rain.) 🟢

## Credits

- VIBE parser by [1ay1/vibe](https://github.com/1ay1/vibe)
- Inspired by wpaperd, swaybg, and every wallpaper daemon that came before
- Shader examples adapted from Shadertoy (various authors)

---

**NeoWall** - Your desktop's red pill. 💊✨

*"I know kung fu... and GLSL shaders."* - Neo (probably)
