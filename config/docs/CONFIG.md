# NeoWall Configuration Guide

Complete reference for configuring NeoWall.

## Quick Start

Edit `~/.config/neowall/config.vibe`:

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
}
```

Save and changes apply automatically.

## Config File Location

- User config: `~/.config/neowall/config.vibe`
- System config: `/etc/neowall/config.vibe` (fallback)

Config is auto-created on first run with Matrix rain as default.

## VIBE Syntax

Simple bracket-based format:

```vibe
# Comments start with #
section {
  key value
  another_key value
}
```

- No quotes needed for simple strings
- No colons or semicolons
- Whitespace flexible
- Braces show hierarchy

## Configuration Sections

### `default` - Global Settings

Applies to all monitors unless overridden by `output`.

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
  mode fill
}
```

### `output` - Per-Monitor Settings

Override settings for specific monitors:

```vibe
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/wallpaper.png
  }
}
```

Get monitor names:
- Hyprland: `hyprctl monitors`
- Sway: `swaymsg -t get_outputs`
- Generic: `wlr-randr`

## Options Reference

### Shader Options

#### `shader` - GLSL Shader File

Run GPU shader as wallpaper:

```vibe
shader matrix_real.glsl           # From ~/.config/neowall/shaders/
shader ~/custom/shader.glsl       # Absolute path
shader /usr/share/shaders/x.glsl  # System path
```

Included shaders:
- `matrix_real.glsl` - Matrix rain with detail
- `matrix_rain.glsl` - Classic Matrix effect
- `plasma.glsl` - Flowing plasma waves
- `aurora.glsl` - Northern lights
- `2d_clouds.glsl` - Procedural clouds
- `sunrise.glsl` - Dynamic sky
- `fractal_land.glsl` - Fractal landscapes
- `mandelbrot.glsl` - Mandelbrot zoom
- And more in `~/.config/neowall/shaders/`

#### `shader_speed` - Animation Speed

Control shader animation speed:

```vibe
shader_speed 1.0   # Normal (default)
shader_speed 2.0   # 2x faster
shader_speed 0.5   # Half speed
shader_speed 0.1   # Very slow
```

Only affects shaders, not images.

### Image Options

#### `path` - Image File or Directory

Static image wallpaper:

```vibe
path ~/Pictures/wallpaper.png     # Single file
path ~/Pictures/Wallpapers/       # Directory (cycles all images)
```

Supported formats: PNG, JPEG/JPG

Directory mode:
- Add trailing slash: `~/Pictures/Wallpapers/`
- Loads all PNG/JPEG files
- Cycles alphabetically
- Use with `duration` to auto-cycle

#### `mode` - Display Mode

How image fills screen:

```vibe
mode fill      # Scale to fill, crop if needed (default, recommended)
mode fit       # Scale to fit, may show borders
mode center    # No scaling, center image
mode stretch   # Stretch to fill (may distort)
mode tile      # Repeat image as tiles
```

#### `duration` - Cycle Interval

Seconds between wallpaper changes:

```vibe
duration 300    # 5 minutes
duration 900    # 15 minutes
duration 1800   # 30 minutes
duration 3600   # 1 hour
duration 0      # No cycling (default)
```

Works with:
- Image directories (`path ~/Pictures/Wallpapers/`)
- Shader directories (`shader ~/.config/neowall/shaders/`)

#### `transition` - Transition Effect

Effect when changing wallpapers:

```vibe
transition fade         # Smooth crossfade (default)
transition slide_left   # Slide left
transition slide_right  # Slide right
transition glitch       # Digital glitch
transition pixelate     # Mosaic blocks
transition none         # Instant switch
```

Only applies to image cycling, not shaders.

#### `transition_duration` - Transition Speed

Transition length in milliseconds:

```vibe
transition_duration 300   # Default
transition_duration 100   # Fast
transition_duration 500   # Smooth
transition_duration 1000  # Slow
```

## Example Configurations

### Matrix Rain (Default)

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
}
```

### Static Image

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

### Cycling Images

```vibe
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition fade
  mode fill
}
```

### Multi-Monitor

```vibe
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/monitor.png
    mode fill
  }
}
```

### Cycling Shaders

```vibe
default {
  shader ~/.config/neowall/shaders/
  duration 600
  shader_speed 1.0
}
```

### Mixed Setup

```vibe
default {
  shader plasma.glsl
  shader_speed 0.5
}

output {
  eDP-1 {
    shader matrix_real.glsl
    shader_speed 2.0
  }
  HDMI-A-1 {
    path ~/Pictures/Wallpapers/
    duration 300
    transition fade
  }
  DP-1 {
    path ~/Pictures/static.png
    mode fit
  }
}
```

## Mutually Exclusive Options

Don't mix these in the same section:

**Images vs Shaders:**
- Use `path` OR `shader`, not both
- Use `mode` with images, not shaders
- Use `transition` with images, not shaders
- Use `shader_speed` with shaders, not images

**Valid:**
```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
  duration 0
}
```

**Invalid:**
```vibe
default {
  path ~/Pictures/wallpaper.png
  shader matrix_real.glsl  # ERROR: Can't use both
}
```

## Daemon Commands

Control running daemon:

```bash
neowall              # Start daemon
neowall kill         # Stop daemon
neowall reload       # Reload config
neowall next         # Skip to next wallpaper/shader
neowall pause        # Pause cycling
neowall resume       # Resume cycling
neowall current      # Show current wallpaper
```

## Hot-Reload

Config auto-reloads on save. No restart needed.

To enable:
```bash
neowall --watch      # Built-in file watcher
```

Or just edit `~/.config/neowall/config.vibe` - daemon checks for changes automatically.

## Custom Shaders

### Writing Shaders

Create `~/.config/neowall/shaders/myshader.glsl`:

```glsl
#version 100
precision highp float;

uniform float time;        // Seconds since start
uniform vec2 resolution;   // Screen dimensions

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    vec3 color = vec3(uv, sin(time));
    gl_FragColor = vec4(color, 1.0);
}
```

Use in config:
```vibe
default {
  shader myshader.glsl
}
```

### Shadertoy Compatibility

Most Shadertoy shaders work with minimal changes. NeoWall provides:

- `iTime` - Time in seconds
- `iResolution` - Screen resolution
- `iChannel0` through `iChannel4` - Texture samplers
- `iChannelTime[4]` - Per-channel time
- `iChannelResolution[4]` - Per-channel resolution
- `iMouse` - Mouse position (always vec4(0))
- `iDate` - Date vector
- `iFrame` - Frame counter (always 0)

To convert Shadertoy shader:
1. Copy shader code
2. Save to `~/.config/neowall/shaders/name.glsl`
3. Use in config: `shader name.glsl`

Most shaders work as-is. Some may need minor adjustments.

Browse shaders: [shadertoy.com](https://www.shadertoy.com/)

## Troubleshooting

### Config not reloading
- Run: `neowall reload`
- Check: `~/.config/neowall/config.vibe` exists
- Check logs: `neowall -fv` (foreground, verbose)

### Shader not found
- Shaders in: `~/.config/neowall/shaders/`
- Use filename only: `shader matrix_real.glsl`
- Or full path: `shader ~/.config/neowall/shaders/matrix_real.glsl`

### Black screen with shader
- Check logs: `neowall -fv`
- Verify GPU supports OpenGL ES 2.0+
- Try different shader: `shader plasma.glsl`
- Check shader syntax errors in logs

### Image not showing
- Verify file exists: `ls -la ~/Pictures/wallpaper.png`
- Check format: PNG or JPEG only
- Try absolute path: `/home/user/Pictures/wallpaper.png`

### Monitor not recognized
- Get exact name: `hyprctl monitors` or `swaymsg -t get_outputs`
- Check spelling in config
- Monitor must be active

### Cycling not working
- Set `duration > 0`
- For directories, add trailing slash: `path ~/Pictures/Wallpapers/`
- Verify directory contains images: `ls ~/Pictures/Wallpapers/`

## Performance

### CPU Usage
- Shaders: ~2% CPU at 60 FPS (GPU accelerated)
- Static images: ~0% CPU (after load)
- Image cycling: Brief spike during transition

### Memory Usage
- Base: ~10-20 MB
- Per shader: +5-10 MB
- Per image: +image file size (uncompressed)

### GPU Usage
- Shaders render at 60 FPS
- Uses OpenGL ES 2.0+ for compatibility
- Automatic fallback for older GPUs

## Advanced Topics

### Environment Variables

Override config location:
```bash
NEOWALL_CONFIG=~/custom/config.vibe neowall
```

### Multiple Configs

Switch between configs:
```bash
neowall -c ~/.config/neowall/work.vibe
neowall -c ~/.config/neowall/gaming.vibe
```

### Debug Mode

Run in foreground with verbose logging:
```bash
neowall -fv
```

Output shows:
- Config parsing
- Shader compilation
- Image loading
- Monitor detection
- Frame timing

## See Also

- Main README: `../README.md`
- Example config: `../config/neowall.vibe`
- Shader directory: `~/.config/neowall/shaders/`
- GitHub: [github.com/1ay1/neowall](https://github.com/1ay1/neowall)