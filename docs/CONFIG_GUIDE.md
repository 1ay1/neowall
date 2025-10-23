# Staticwall Configuration Guide

**Staticwall** uses the **VIBE** (Values In Bracket Expression) configuration format - a clean, human-readable format that's easier to write and parse than TOML or YAML.

## Table of Contents

- [Quick Start](#quick-start)
- [VIBE Format Basics](#vibe-format-basics)
- [Configuration Structure](#configuration-structure)
- [Configuration Options](#configuration-options)
- [Display Modes](#display-modes)
- [Multi-Monitor Setup](#multi-monitor-setup)
- [Wallpaper Cycling](#wallpaper-cycling)
- [Transitions](#transitions)
- [Complete Examples](#complete-examples)
- [Troubleshooting](#troubleshooting)

## Quick Start

Create `~/.config/staticwall/config.vibe`:

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

That's it! This sets a single wallpaper for all monitors.

## VIBE Format Basics

VIBE is simpler than TOML - no complex quoting rules, no confusing syntax.

### Basic Syntax

```vibe
# Comments start with #
key value

# Nested objects use braces
section {
  key1 value1
  key2 value2
}

# Arrays use brackets
list [
  item1
  item2
  item3
]
```

### Key Rules

1. **No quotes needed** for simple strings (paths, modes, etc.)
2. **Whitespace flexible** - newlines separate key-value pairs
3. **Comments** start with `#`
4. **Nesting** with `{ }` for objects
5. **Lists** with `[ ]` for arrays

### Data Types

```vibe
# Strings (no quotes needed for simple values)
path /home/user/wallpaper.png
mode fill

# Numbers
duration 300
transition_duration 500

# Booleans
# (staticwall doesn't currently use booleans, but VIBE supports them)
enabled true
disabled false

# Arrays
cycle [
  /path/to/image1.png
  /path/to/image2.jpg
  /path/to/image3.png
]
```

## Configuration Structure

Staticwall configs have two main sections:

```vibe
# Default configuration (applied to all outputs)
default {
  # ... options ...
}

# Per-output configuration (overrides default)
output {
  monitor-name {
    # ... options ...
  }
  
  another-monitor {
    # ... options ...
  }
}
```

## Configuration Options

### Wallpaper Type

You must specify **either** `path` (for images) **or** `shader` (for live wallpapers):

- **path** - Path to wallpaper image or directory (for static/cycling wallpapers)
- **shader** - Path to GLSL fragment shader file (for live animated wallpapers)

**Note:** If both are specified, `shader` takes precedence.

### Optional Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `mode` | string | `fill` | Display mode (only applies to image wallpapers) |
| `duration` | integer | `0` | Seconds between wallpaper changes (0 = no cycling, only for images) |
| `transition` | string | `none` | Transition effect when changing wallpapers (only for images) |
| `transition_duration` | integer | `300` | Transition duration in milliseconds (only for images) |
| `cycle` | array | none | List of wallpapers to cycle through (only for images) |

### Path Format

Paths can be:
- **Absolute**: `/home/user/Pictures/wallpaper.png`
- **Home directory**: `~/Pictures/wallpaper.png` (recommended)
- **Relative**: Not recommended, behavior may vary

**Important**: Paths with spaces don't need quotes in VIBE!

```vibe
# This works fine:
path ~/Pictures/My Wallpapers/sunset.png
```

## Display Modes

Control how wallpapers are displayed on your screen.

### Available Modes

#### `fill` (default, recommended)
Scales the image to fill the entire screen while maintaining aspect ratio. Crops edges if needed.
```vibe
mode fill
```
**Best for**: Most use cases, modern wallpapers

#### `fit`
Scales the image to fit inside the screen while maintaining aspect ratio. May show letterboxing.
```vibe
mode fit
```
**Best for**: Preserving entire image, artistic wallpapers

#### `center`
Centers the image without scaling.
```vibe
mode center
```
**Best for**: Images already sized for your screen

#### `stretch`
Stretches the image to fill the screen (may distort).
```vibe
mode stretch
```
**Best for**: Rare cases, usually not recommended

#### `tile`
Repeats the image to fill the screen.
```vibe
mode tile
```
**Best for**: Patterns, textures, small images

### Mode Comparison

```
Original Image: 1920x1080
Screen: 2560x1440

fill    -> Image scaled to 2560x1440, sides cropped
fit     -> Image scaled to 1920x1080 in center, black bars
center  -> Image shown at 1920x1080 in center, black bars
stretch -> Image stretched to 2560x1440 (distorted)
tile    -> Image repeated to fill screen
```

## Multi-Monitor Setup

### Finding Your Monitor Names

**Sway:**
```bash
swaymsg -t get_outputs | grep "Output"
```

**Hyprland:**
```bash
hyprctl monitors | grep "Monitor"
```

**wlr-randr (generic):**
```bash
wlr-randr
```

Common monitor names:
- Laptop: `eDP-1`, `LVDS-1`
- HDMI: `HDMI-A-1`, `HDMI-A-2`
- DisplayPort: `DP-1`, `DP-2`, `DP-3`

### Single Monitor

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

### Different Wallpaper Per Monitor

```vibe
# Default for unknown monitors
default {
  path ~/Pictures/default.png
  mode fill
}

# Specific monitors
output {
  eDP-1 {
    path ~/Pictures/laptop-wallpaper.jpg
    mode fill
  }
  
  HDMI-A-1 {
    path ~/Pictures/external-monitor.png
    mode fit
  }
  
  DP-1 {
    path ~/Pictures/vertical-wallpaper.png
    mode fill
  }
}
```

### Same Wallpaper, Different Modes

```vibe
default {
  path ~/Pictures/landscape.png
  mode fill
}

output {
  # Vertical monitor - different mode
  DP-2 {
    path ~/Pictures/portrait.png
    mode fit
  }
}
```

## Wallpaper Cycling

Automatically change wallpapers at intervals.

### Basic Cycling

```vibe
default {
  path ~/Pictures/wallpaper1.png
  mode fill
  duration 300  # Change every 5 minutes
  cycle [
    ~/Pictures/wallpaper1.png
    ~/Pictures/wallpaper2.jpg
    ~/Pictures/wallpaper3.png
  ]
}
```

### Per-Monitor Cycling

```vibe
output {
  eDP-1 {
    path ~/Pictures/laptop1.png
    mode fill
    duration 600  # 10 minutes
    cycle [
      ~/Pictures/laptop1.png
      ~/Pictures/laptop2.png
      ~/Pictures/laptop3.png
    ]
  }
  
  HDMI-A-1 {
    path ~/Pictures/monitor1.jpg
    mode fill
    duration 300  # 5 minutes
    cycle [
      ~/Pictures/monitor1.jpg
      ~/Pictures/monitor2.jpg
    ]
  }
}
```

### Cycling with Transitions

```vibe
default {
  path ~/Pictures/wallpaper1.png
  mode fill
  duration 600
  transition fade
  transition_duration 1000  # 1 second fade
  cycle [
    ~/Pictures/wallpaper1.png
    ~/Pictures/wallpaper2.png
    ~/Pictures/wallpaper3.png
  ]
}
```

### Time-Based Durations

```vibe
# Quick changes
duration 60       # 1 minute

# Moderate
duration 300      # 5 minutes

# Slow
duration 1800     # 30 minutes

# Hourly
duration 3600     # 1 hour

# Once per day
duration 86400    # 24 hours
```

## Transitions

Smooth transitions when changing wallpapers.

### Available Transitions

- `none` - Instant change (default)
- `fade` - Smooth crossfade between wallpapers
- `slide_left` - Slide from right to left
- `slide_right` - Slide from left to right
- `glitch` - Digital glitch effect with RGB separation, scan lines, and corruption
- `pixelate` - Image breaks into pixels and dissolves block-by-block (retro aesthetic!)

### Transition Configuration

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
  transition fade
  transition_duration 500  # 0.5 seconds
}
```

### Transition Duration Guidelines

```vibe
transition_duration 100   # Very fast (barely noticeable)
transition_duration 300   # Fast (default)
transition_duration 500   # Moderate (smooth)
transition_duration 1000  # Slow (dramatic)
transition_duration 2000  # Very slow (artistic)
```

**Note:** The `glitch` and `pixelate` transitions look best with slightly longer durations (500-800ms) to let the full effect play out.

## Live Shader Wallpapers

Staticwall supports custom **GLSL fragment shaders** for procedurally animated wallpapers.

### Basic Usage

```vibe
default {
  shader ~/shaders/plasma.glsl
}
```

That's it! The shader will continuously render and animate.

### Writing Shaders

Shaders are GLSL ES 1.0 fragment shaders. Minimal example:

```glsl
#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    
    // Simple animated color
    vec3 color = vec3(
        sin(time + uv.x) * 0.5 + 0.5,
        cos(time + uv.y) * 0.5 + 0.5,
        sin(time) * 0.5 + 0.5
    );
    
    gl_FragColor = vec4(color, 1.0);
}
```

### Available Uniforms

Your shader automatically receives these uniforms:

- `uniform float time;` - Elapsed time in seconds (for animation)
- `uniform vec2 resolution;` - Screen resolution in pixels (width, height)

### Example Shaders

Staticwall includes example shaders in `examples/shaders/`:

- **plasma.glsl** - Colorful plasma effect with sine waves
- **wave.glsl** - Radial wave patterns
- **gradient.glsl** - Smooth animated gradients
- **matrix.glsl** - Matrix-style digital rain

Copy them to use:

```bash
mkdir -p ~/.local/share/staticwall/shaders
cp examples/shaders/* ~/.local/share/staticwall/shaders/
```

Then reference in config:

```vibe
default {
  shader ~/.local/share/staticwall/shaders/plasma.glsl
}
```

### Per-Monitor Shaders

Different shaders for each monitor:

```vibe
output {
  eDP-1 {
    shader ~/shaders/gradient.glsl  # Laptop: simple gradient
  }
  
  HDMI-A-1 {
    shader ~/shaders/matrix.glsl    # External: matrix effect
  }
}
```

### Shadertoy Conversion

Convert [Shadertoy](https://www.shadertoy.com/) shaders easily:

**Shadertoy format:**
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float t = iTime;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(t), 1.0);
}
```

**Staticwall format:**
```glsl
#version 100
precision mediump float;

uniform float time;       // Replaces iTime
uniform vec2 resolution;  // Replaces iResolution.xy

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    float t = time;
    gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(t), 1.0);
}
```

### Performance

- Shaders run entirely on GPU (zero CPU usage)
- Continuous rendering at display refresh rate
- Minimal power usage on modern GPUs
- No external dependencies (no video codecs)

### Tips

- Keep shaders simple for better performance
- Use `mediump` or `lowp` precision when possible
- Test on your hardware - complex shaders may impact laptop battery
- Hot-reload works: `staticwall reload` to test shader changes

## Complete Examples

### Example 1: Simple Single Monitor

```vibe
# Minimal configuration for one monitor
default {
  path ~/Pictures/nature.jpg
  mode fill
}
```

### Example 2: Laptop + External Monitor

```vibe
# Default for all monitors
default {
  path ~/Pictures/default-wallpaper.png
  mode fill
}

# Laptop screen
output {
  eDP-1 {
    path ~/Pictures/laptop-specific.jpg
    mode fill
  }
  
  # External monitor
  HDMI-A-1 {
    path ~/Pictures/ultrawide-wallpaper.png
    mode fill
  }
}
```

### Example 3: Cycling Wallpapers

```vibe
default {
  path ~/Pictures/Wallpapers/001.png
  mode fill
  duration 600  # 10 minutes
  transition fade
  transition_duration 500
  cycle [
    ~/Pictures/Wallpapers/001.png
    ~/Pictures/Wallpapers/002.png
    ~/Pictures/Wallpapers/003.png
    ~/Pictures/Wallpapers/004.png
    ~/Pictures/Wallpapers/005.png
  ]
}
```

### Example 4: Advanced Multi-Monitor

```vibe
# Fallback for any unexpected monitor
default {
  path ~/Pictures/default.png
  mode fill
}

output {
  # Main laptop display - static wallpaper
  eDP-1 {
    path ~/Pictures/laptop-main.jpg
    mode fill
  }
  
  # Left monitor - cycling nature photos
  DP-1 {
    path ~/Pictures/Nature/forest.jpg
    mode fill
    duration 900  # 15 minutes
    transition fade
    transition_duration 800
    cycle [
      ~/Pictures/Nature/forest.jpg
      ~/Pictures/Nature/mountains.jpg
      ~/Pictures/Nature/ocean.jpg
      ~/Pictures/Nature/desert.jpg
    ]
  }
  
  # Right monitor - cycling abstract art
  DP-2 {
    path ~/Pictures/Abstract/001.png
    mode fill
    duration 600  # 10 minutes
    transition fade
    transition_duration 1000
    cycle [
      ~/Pictures/Abstract/001.png
      ~/Pictures/Abstract/002.png
      ~/Pictures/Abstract/003.png
    ]
  }
  
  # Top monitor (vertical) - different aspect ratio
  HDMI-A-1 {
    path ~/Pictures/vertical-wallpaper.png
    mode fit  # Use fit for unusual aspect ratios
  }
}
```

### Example 5: Work/Home Profiles

You can maintain multiple config files and switch between them:

**~/.config/staticwall/work.vibe:**
```vibe
default {
  path ~/Pictures/Work/professional-background.jpg
  mode fill
}

output {
  eDP-1 {
    path ~/Pictures/Work/minimalist-dark.png
    mode fill
  }
  
  HDMI-A-1 {
    path ~/Pictures/Work/company-logo.png
    mode center
  }
}
```

**~/.config/staticwall/home.vibe:**
```vibe
default {
  path ~/Pictures/Personal/favorite.jpg
  mode fill
  duration 1800
  transition fade
  cycle [
    ~/Pictures/Personal/favorite.jpg
    ~/Pictures/Personal/vacation.jpg
    ~/Pictures/Personal/family.jpg
  ]
}
```

Switch with:
```bash
staticwall -c ~/.config/staticwall/work.vibe
staticwall -c ~/.config/staticwall/home.vibe
```

### Example 6: Minimal Resource Usage

```vibe
# No cycling, no transitions minimal CPU usage
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

## Troubleshooting

### Config Not Loading

**Problem:** "Configuration file not found"

**Solution:**
```bash
# Check if file exists
ls -la ~/.config/staticwall/config.vibe

# Create directory if needed
mkdir -p ~/.config/staticwall

# Create basic config
cat > ~/.config/staticwall/config.vibe << 'EOF'
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
EOF
```

### Wallpaper Not Showing

**Problem:** Wallpaper not displaying

**Check these:**

1. **File exists:**
   ```bash
   ls -la ~/Pictures/wallpaper.png
   ```

2. **Path is correct:**
   ```vibe
   # Use absolute path to test
   path /home/username/Pictures/wallpaper.png
   ```

3. **File format supported:**
   - ✅ PNG (.png)
   - ✅ JPEG (.jpg, .jpeg)
   - ❌ Other formats not yet supported

4. **Permissions:**
   ```bash
   chmod 644 ~/Pictures/wallpaper.png
   ```

### Monitor Not Recognized

**Problem:** Output name doesn't match

**Solution:**
```bash
# Find your actual monitor names
swaymsg -t get_outputs

# Or
hyprctl monitors

# Use the exact name in config:
output {
  HDMI-A-0 {  # Use exact name from above
    path ~/Pictures/wallpaper.png
  }
}
```

### Cycling Not Working

**Problem:** Wallpapers not changing

**Check:**

1. **Duration is set:**
   ```vibe
   duration 300  # Must be > 0
   ```

2. **Cycle array has items:**
   ```vibe
   cycle [
     ~/Pictures/wall1.png
     ~/Pictures/wall2.png
   ]
   ```

3. **All files exist:**
   ```bash
   ls -la ~/Pictures/wall1.png ~/Pictures/wall2.png
   ```

### Syntax Errors

**Problem:** "Failed to parse VIBE config"

**Common mistakes:**

❌ **Wrong:**
```vibe
default {
  path ~/Pictures/wallpaper.png  # No sign needed
  mode fill                     # No quotes needed
}
```

✅ **Correct:**
```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

❌ **Wrong:**
```vibe
output {
  eDP-1 {
    path ~/Pictures/wallpaper.png
    mode fill
  # Missing closing brace
}
```

✅ **Correct:**
```vibe
output {
  eDP-1 {
    path ~/Pictures/wallpaper.png
    mode fill
  }  # All braces closed
}
```

### Hot-Reload Issues

**Problem:** Config changes not applying

**Solution:**
```bash
# Send reload signal
killall -HUP staticwall

# Or restart
staticwall kill && staticwall

# Or use watch mode (automatic reload)
staticwall -w
```

## Best Practices

### 1. Use Home Directory Paths
```vibe
# Good
path ~/Pictures/wallpaper.png

# Avoid
path /home/username/Pictures/wallpaper.png
```

### 2. Organize Wallpapers
```bash
mkdir -p ~/Pictures/Wallpapers/{Nature,Abstract,Space}
```

### 3. Start Simple
```vibe
# Begin with minimal config
default {
  path ~/Pictures/wallpaper.png
  mode fill
}

# Add features as needed
```

### 4. Test Before Daemonizing
```bash
# Test in foreground first
staticwall -f -v

# Then run as daemon
staticwall
```

### 5. Use Reasonable Durations
```vibe
# Too fast: annoying, high CPU
duration 10

# Too slow: pointless
duration 86400

# Sweet spot: 5-30 minutes
duration 300
```

### 6. Comment Your Config
```vibe
# Work monitor - professional wallpapers
output {
  HDMI-A-1 {
    path ~/Pictures/Work/background.jpg
    mode fill
    # Change every hour during work
    duration 3600
  }
}
```

## Advanced Tips

### Dynamic Monitor Detection

If monitors change (laptop docking/undocking), create a robust config:

```vibe
# Always have a default
default {
  path ~/Pictures/universal-wallpaper.png
  mode fill
}

# Configure all possible monitors
output {
  eDP-1 { path ~/Pictures/laptop.jpg }
  HDMI-A-1 { path ~/Pictures/home-monitor.jpg }
  DP-1 { path ~/Pictures/work-monitor.jpg }
}
```

### Seasonal Wallpapers

Maintain seasonal configs:

```bash
# Switch configs seasonally
cp ~/.config/staticwall/winter.vibe ~/.config/staticwall/config.vibe
killall -HUP staticwall
```

### Scripted Wallpaper Management

```bash
#!/bin/bash
# wallpaper-switcher.sh

CONFIG=~/.config/staticwall/config.vibe

cat > "$CONFIG" << EOF
default {
  path ~/Pictures/current-wallpaper.png
  mode fill
}
EOF

killall -HUP staticwall
```

## Resources

- **VIBE Format**: See `include/vibe.h` for full specification
- **Example Configs**: Check `config/staticwall.vibe`
- **Wallpaper Sources**:
  - [Unsplash](https://unsplash.com/)
  - [Wallhaven](https://wallhaven.cc/)
  - [Reddit /r/wallpapers](https://reddit.com/r/wallpapers)

## Summary

Staticwall's VIBE configuration is designed to be:
- **Simple**: No complex syntax rules
- **Readable**: Clear structure
- **Flexible**: Support for simple and complex setups
- **Forgiving**: Whitespace and formatting don't matter

Start with a basic config and build up as needed. The format is intuitive enough that you'll rarely need to reference this guide after your first setup.

**Remember:** Staticwall sets wallpapers until it... doesn't. But with VIBE configs, it usually does. 🌊