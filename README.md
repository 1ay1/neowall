# Staticwall

> GPU-accelerated Wayland wallpaper daemon with live shader support

**Staticwall** brings GPU-powered animated wallpapers to Wayland. Run Shadertoy shaders, cycle through images with smooth transitions, and configure everything with a single clean config file.

https://github.com/user-attachments/assets/386243d0-53ca-4287-9ab6-873265d3d53a

## Why "Staticwall" if wallpapers can be animated?

It's ironic branding - we're statically compiled but dynamically animated. Like calling a racecar "SlowPoke." Plus it's catchier than "DynamicLiveAnimatedShaderWallpaperDaemon." 🤷

## Features

- ✨ **Shadertoy Compatible** - Thousands of existing shaders work out of the box
- 🎬 **Live GPU Shaders** - 60 FPS animated wallpapers with minimal CPU usage
- 🖼️ **Image Support** - PNG, JPEG with smooth transitions (fade, slide, glitch, pixelate)
- 🔄 **Auto Cycling** - Point at a folder of images or shaders, auto-switch on interval
- 🖥️ **Multi-Monitor** - Different wallpaper per screen
- 🔥 **Hot-Reload** - Edit config, changes apply instantly
- ⚡ **Hyprland Ready** - Works on Hyprland, Sway, River, and other wlroots compositors

## Why Staticwall?

| Feature | Staticwall | swaybg | hyprpaper | wpaperd |
|---------|------------|--------|-----------|---------|
| Live GPU Shaders | ✅ | ❌ | ❌ | ❌ |
| Shadertoy Compatible | ✅ | ❌ | ❌ | ❌ |
| Smooth Transitions | ✅ | ❌ | ❌ | ✅ |
| Hot Config Reload | ✅ | ❌ | ❌ | ✅ |
| Auto Directory Cycling | ✅ | ❌ | ❌ | ✅ |
| Multi-Monitor | ✅ | ✅ | ✅ | ✅ |
| Performance | 2% CPU @ 60fps | N/A | N/A | Low |

## Quick Start

```bash
# Install (Arch)
yay -S staticwall-git

# Or build from source
make
sudo make install

# Run (creates default config on first run)
staticwall

# Edit your config
$EDITOR ~/.config/staticwall/config.vibe

# Changes apply automatically! No restart needed.
```

## Configuration

Staticwall uses **VIBE** config format - no quotes, no colons, just clean hierarchy. Visual structure at a glance.

```vibe
default {
  path ~/Pictures/cool-mountain.jpg
  mode fill
}

output {
  HDMI-A-1 {
    shader plasma.glsl
  }
}
```

Simple, readable, and perfect for wallpaper configs. YAML is for Kubernetes, this is for wallpapers. 🌊

### Static Image Wallpaper

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

### Cycle Through Images

```vibe
default {
  path ~/Pictures/Wallpapers/    # Trailing slash = directory mode
  duration 300                    # Change every 5 minutes
  mode fill
  transition fade                 # Smooth transitions
}
```

### Live Shader Wallpaper 🔥

```vibe
default {
  shader aurora.glsl              # Just the filename!
  shader_speed 1.0                # Animation speed (0.5 = slower, 2.0 = faster)
}
```

Shaders are installed to `~/.config/staticwall/shaders/` on first run. Includes:
- `aurora.glsl` - Northern lights effect
- `matrix_rain.glsl` - The Matrix green code
- `plasma.glsl` - Colorful plasma waves
- And many more!

### Multi-Monitor Setup

```vibe
# Laptop screen gets a shader
output {
  eDP-1 {
    shader plasma.glsl
  }
  
  # External monitor cycles through photos
  HDMI-A-1 {
    path ~/Pictures/Nature/
    duration 600
    transition fade
  }
}
```

Find your monitor names:
- **Sway**: `swaymsg -t get_outputs`
- **Hyprland**: `hyprctl monitors`
- **Generic**: `wlr-randr`

### Cycle Through Shaders

```vibe
default {
  shader ~/.config/staticwall/shaders/    # Point at directory
  duration 300                             # Change shader every 5 minutes
}
```

All shaders in the directory cycle alphabetically. Name them `01-aurora.glsl`, `02-plasma.glsl`, etc. for control.

## Display Modes

- `fill` - Scale to fill screen (crop if needed) **← recommended**
- `fit` - Scale to fit inside screen (may show borders)
- `center` - No scaling, just center it
- `stretch` - Fill screen (may distort aspect ratio)
- `tile` - Repeat image to fill screen

## 🎨 Included Shaders

Staticwall comes with 13+ high-quality shaders ready to use:

- **2d_clouds.glsl** - Procedural cloud formations
- **matrix_rain.glsl** - Classic Matrix falling code effect
- **matrix_real.glsl** - Enhanced Matrix with more detail
- **plasma.glsl** - Colorful flowing plasma waves
- **aurora.glsl** - Northern lights simulation
- **sunrise.glsl** - Dynamic sunrise/sunset sky
- **ocean_waves.glsl** - Realistic water simulation
- **fractal_land.glsl** - Infinite fractal landscapes
- **mandelbrot.glsl** - Classic Mandelbrot set zoom
- **star_field.glsl** - Animated starfield
- And more!

All shaders are installed to `~/.config/staticwall/shaders/` on first run. Just reference them by filename in your config.

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
staticwall              # Start daemon
staticwall kill         # Stop daemon
staticwall reload       # Reload config
staticwall next         # Skip to next wallpaper/shader
staticwall pause        # Pause cycling
staticwall resume       # Resume cycling
staticwall current      # Show current wallpaper
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

Save as `~/.config/staticwall/shaders/myshader.glsl` and use:

```vibe
default {
  shader myshader.glsl
}
```

**Shadertoy Compatible!** Most Shadertoy shaders work with minimal changes. We provide `iTime`, `iResolution`, `iChannel0-4` uniforms.

## Why Staticwall?

- **No dependencies** - Single binary, statically linked (except system libs)
- **Actually works** - Tested on Hyprland, Sway, River
- **Fast** - Shaders run at 60 FPS with minimal CPU usage
- **Simple** - One config file, sensible defaults
- **Funny name** - Worth it for the puns alone

## Troubleshooting

**Shader shows black screen?**
- Check logs: `staticwall -fv` (foreground + verbose)
- Shaders compile on load, may take a second
- Complex shaders may need optimization

**Config changes don't apply?**
- Hot-reload is enabled by default, just wait 2 seconds
- Force reload: `staticwall reload`

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
staticwall
```

## Contributing

Found a bug? Want to add a feature? PRs welcome! 

Please:
- Keep it simple (we're "static" for a reason)
- Test on at least one compositor
- Don't break the puns

## License

MIT - Do whatever you want, just don't sue us if your wallpaper becomes sentient.

## Credits

- VIBE parser by [1ay1/vibe](https://github.com/1ay1/vibe)
- Inspired by wpaperd, swaybg, and every wallpaper daemon that came before
- Shader examples adapted from Shadertoy (various authors)

---

**Staticwall** - Because your wallpaper deserves better than being... static. 🎨✨
