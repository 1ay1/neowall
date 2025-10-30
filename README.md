# NeoWall

> GPU shaders as wallpapers. Because why not.

Run Shadertoy shaders on your desktop at 60 FPS. Works with most of the 10,000+ shaders on shadertoy.com.

Wayland only. Tested on Hyprland, Sway, and River.

https://github.com/user-attachments/assets/386243d0-53ca-4287-9ab6-873265d3d53a

## What

A Wayland wallpaper daemon that renders GLSL shaders on your desktop. Also handles static images with transitions.

## Features

- Shadertoy-compatible fragment shaders
- ~2% CPU at 60 FPS (GPU does the work)
- Static images with transitions (fade, slide, glitch, pixelate)
- Directory cycling for images and shaders
- Multi-monitor with per-output configs
- Hot-reload config on save
- wlr-layer-shell protocol



## Install

```bash
# Arch
yay -S neowall-git

# From source
git clone https://github.com/1ay1/neowall
cd neowall && make -j$(nproc)
sudo make install
```

Run: `neowall`

Config: `~/.config/neowall/config.vibe` (auto-reloads on save)

For detailed configuration guide, see [docs/CONFIG.md](docs/CONFIG.md)

## Configuration

`~/.config/neowall/config.vibe`:

```vibe
# Shader
default {
  shader matrix_real.glsl
  shader_speed 1.0
}

# Static image
default {
  path ~/Pictures/wallpaper.png
}

# Cycle images
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition fade
}

# Per-monitor
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/Nature/
  }
}

# Cycle shaders (directory with trailing slash)
default {
  shader ~/.config/neowall/shaders/
  duration 300
}
```

Get monitor names: `hyprctl monitors` or `swaymsg -t get_outputs` or `wlr-randr`

Included shaders: matrix_real.glsl, matrix_rain.glsl, plasma.glsl, aurora.glsl, 2d_clouds.glsl, fractal_land.glsl, and more

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

Most Shadertoy shaders work with minimal changes. We provide `iTime`, `iResolution`, `iChannel0-4` uniforms. Browse [shadertoy.com](https://www.shadertoy.com/) for more.

## Technical

- Single binary, statically linked (except glibc, wayland, EGL)
- Multi-version EGL/GLES support (1.0 through 3.2)
- wlr-layer-shell protocol for Wayland
- Shader compilation at runtime
- ~2% CPU utilization at 60 FPS (GPU-accelerated)

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

PRs welcome. Test on at least one compositor before submitting.

## License

MIT

## Credits

- Shader examples adapted from Shadertoy (various authors)
- Built for the Wayland and r/unixporn communities

---

**NeoWall** - Shaders as wallpapers.
