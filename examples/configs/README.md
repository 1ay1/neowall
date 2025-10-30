# Staticwall Configuration Examples

This directory contains example configuration files to help you get started with Staticwall.

## Quick Start

1. **Choose a config file** from the examples below
2. **Copy to your config directory:**
   ```bash
   cp <example-file> ~/.config/staticwall/config.vibe
   ```
3. **Edit paths** to match your system
4. **Start Staticwall:**
   ```bash
   staticwall
   ```

## Available Examples

### 📄 `minimal.vibe`
The simplest possible configuration - single wallpaper, no frills.
```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```
**Use when:** You just want one wallpaper on all monitors.

---

### 📚 `complete_example.vibe`
Comprehensive documentation with ALL available options explained in detail.

**Features:**
- Every configuration option documented
- Inline comments explaining each setting
- Common mistakes and how to avoid them
- Validation rules explained
- Tips and tricks section

**Use when:** You want to understand all capabilities and customize everything.

---

### 🎨 `common_setups.vibe`
12 ready-to-use configurations for common scenarios:

1. **Single Static Wallpaper** - One image, all monitors
2. **Image Slideshow** - Cycling directory of photos
3. **Live Shader Wallpaper** - Animated GLSL background
4. **Cycling Shaders** - Rotate through shader effects
5. **Multi-Monitor Different** - Shader on laptop, photos on external
6. **Shader with Textures** - Advanced iChannel usage
7. **Fast Slideshow** - Quick cycling with effects
8. **Vertical Monitor** - Different images per orientation
9. **Minimal Power** - Static, no animations
10. **Maximum Impact** - Fast cycling, dramatic effects
11. **Work Setup** - Slow, subtle changes
12. **Gaming Setup** - Dark, non-distracting shader

**Use when:** You want a pre-made setup that matches your use case.

---

### 🔬 Testing Examples

#### `simple_ichannels.vibe`
Basic shader with texture inputs (iChannel0).

#### `ichannels_practical.vibe`
Real-world shader texture configuration.

#### `shadertoy_ichannels.vibe`
Shadertoy-compatible texture setup.

---

## Configuration Rules

### ⚠️ IMPORTANT: Mutual Exclusivity

**`path` and `shader` are MUTUALLY EXCLUSIVE!**

```vibe
# ✅ VALID - Image mode
default {
  path ~/Pictures/wallpaper.png
}

# ✅ VALID - Shader mode
default {
  shader plasma.glsl
}

# ❌ INVALID - Cannot use both!
default {
  path ~/Pictures/wallpaper.png
  shader plasma.glsl  # ERROR!
}
```

### 📂 Directory Loading

**Trailing slash** indicates directory mode:

```vibe
# Loads single file
path ~/Pictures/wallpaper.png

# Loads ALL images from directory (sorted alphabetically)
path ~/Pictures/Wallpapers/
```

### 🔄 Cycling Requirements

To enable wallpaper cycling, you need:
- **Multiple files** (directory with 2+ images/shaders)
- **Duration** setting (optional but recommended)

```vibe
default {
  path ~/Pictures/Wallpapers/    # Directory with multiple images
  duration 300                    # Change every 5 minutes (in seconds)
  transition fade                 # Smooth transition
  transition_duration 0.3         # Transition takes 0.3 seconds (300ms)
}
```

## Configuration Options Reference

### Image/Shader Selection
```vibe
path ~/Pictures/wallpaper.png       # Image file or directory
shader aurora.glsl                  # Shader file or directory
```

### Display Modes
```vibe
mode fill      # Scale to fill, crop if needed [RECOMMENDED]
mode fit       # Scale to fit inside, may show borders
mode center    # No scaling, just center
mode stretch   # Fill screen, may distort
mode tile      # Repeat to fill
```

### Cycling
```vibe
duration 300                    # Seconds between changes (0.0-86400.0, supports decimals)
transition fade                 # fade, slide_left, slide_right, glitch, pixelate, none
transition_duration 0.3         # Transition duration in seconds (0.0-10.0, supports decimals)
```

### Shader Options
```vibe
shader_speed 1.0               # Animation speed multiplier (0.01-100.0)
channels [                     # Texture inputs for shader
  rgba_noise                   # Built-in procedural texture
  ~/Pictures/texture.png       # Custom image path
  blue_noise
]
```

**Built-in textures:**
- `rgba_noise` - Colored noise
- `gray_noise` - Grayscale noise
- `blue_noise` - Blue noise pattern
- `wood` - Wood grain texture
- `abstract` - Abstract pattern

### Per-Output Configuration
```vibe
outputs {
  eDP-1 {
    # Laptop screen config
  }
  
  HDMI-A-1 {
    # External monitor config
  }
}
```

Find output names:
- **Hyprland:** `hyprctl monitors`
- **Sway:** `swaymsg -t get_outputs`
- **Generic:** `wlr-randr`

## Commands

```bash
staticwall              # Start daemon
staticwall -f           # Run in foreground (debugging)
staticwall -fv          # Foreground + verbose logs

staticwall next         # Skip to next wallpaper
staticwall pause        # Pause cycling
staticwall resume       # Resume cycling
staticwall current      # Show current status
staticwall reload       # Reload config
staticwall kill         # Stop daemon
```

## Troubleshooting

### ❓ "Cannot cycle wallpaper: Only one wallpaper/shader configured"

**Solution:** Use a directory path or configure multiple files:
```vibe
path ~/Pictures/Wallpapers/    # Note the trailing /
```

### ❓ Config changes don't apply

**Solution:** 
- Wait 2 seconds (auto-reload is enabled)
- Or force: `staticwall reload`

### ❓ Shader shows black screen

**Solution:**
- Check logs: `staticwall -fv`
- Verify shader file exists
- Check for GLSL syntax errors

### ❓ "INVALID CONFIG: Both 'path' and 'shader' specified"

**Solution:** Use ONLY one per block:
```vibe
# Choose one:
path ~/Pictures/wallpaper.png
# OR
shader aurora.glsl
```

### ❓ Want to see what's happening

**Solution:**
```bash
staticwall current          # Check current state
staticwall -fv             # Run with verbose logging
journalctl -u staticwall   # View systemd logs (if using service)
```

## File Locations

```
~/.config/staticwall/
├── config.vibe                    # Your configuration
└── shaders/                       # Shader files
    ├── aurora.glsl
    ├── plasma.glsl
    ├── matrix_rain.glsl
    └── ...

~/.local/share/staticwall/
└── default.png                    # Default wallpaper

$XDG_RUNTIME_DIR/
└── staticwall.state              # Current state (for 'staticwall current')
```

## Tips

1. **Start simple** - Use `minimal.vibe` and expand
2. **Test in foreground** - `staticwall -fv` to see what's happening
3. **Name files numerically** - `01-mountain.jpg`, `02-ocean.jpg` for order control
4. **Hot-reload works** - Edit config, changes apply in ~2 seconds
5. **Check status often** - `staticwall current` shows everything
6. **Validate first** - Invalid config falls back to safe defaults

## Getting Help

- **Check logs:** `staticwall -fv`
- **View state:** `staticwall current`
- **Test config:** `staticwall reload` after editing
- **Read errors:** Validation messages tell you exactly what's wrong

## Example Workflows

### Daily Driver Setup
```vibe
default {
  path ~/Pictures/Daily/
  duration 600                # 10 minutes
  transition fade
  transition_duration 0.5     # 0.5 second transition
  mode fill
}
```

### Development Setup
```vibe
outputs {
  eDP-1 {
    # Laptop: minimal, non-distracting
    path ~/Pictures/minimal-dark.png
    mode fill
  }
  
  HDMI-A-1 {
    # Main monitor: slow cycling photos
    path ~/Pictures/Nature/
    duration 3600               # 1 hour
    transition fade
    transition_duration 1.0     # 1 second transition
  }
  
  DP-1 {
    # Vertical monitor: coding wallpapers
    path ~/Pictures/Code-Themed/
    duration 1800               # 30 minutes
    transition_duration 0.5
    mode fit
  }
}
```

### Demo/Presentation Setup
```vibe
default {
  shader ~/.config/staticwall/shaders/
  duration 60                 # 1 minute
  shader_speed 2.0
}
```

---

**Need more help?** Run `staticwall --help` or check the main documentation.