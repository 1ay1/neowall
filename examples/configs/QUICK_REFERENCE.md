# Staticwall Quick Reference Card

## Minimal Config
```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

## Basic Options

### Image Mode
```vibe
path ~/Pictures/wallpaper.png     # Single image
path ~/Pictures/Wallpapers/       # Directory (trailing /)
```

### Shader Mode
```vibe
shader aurora.glsl                # Single shader
shader ~/.config/staticwall/shaders/  # Directory
shader_speed 1.0                  # Speed: 0.01-100.0
```

### Display Modes
```
fill      # Scale to fill (crop if needed) - RECOMMENDED
fit       # Scale to fit (may show borders)
center    # No scaling
stretch   # Fill screen (may distort)
tile      # Repeat pattern
```

### Cycling
```vibe
duration 300                      # Seconds: 0.0-86400.0 (supports decimals)
transition fade                   # fade, slide_left, slide_right, glitch, pixelate, none
transition_duration 0.3           # Seconds: 0.0-10.0 (supports decimals, e.g., 0.3 = 300ms)
```

## Multi-Monitor Setup
```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}

outputs {
  eDP-1 {
    shader plasma.glsl
  }
  
  HDMI-A-1 {
    path ~/Pictures/Nature/
    duration 600
  }
}
```

## Shader with Textures
```vibe
default {
  shader complex.glsl
  channels [
    rgba_noise              # Built-in: rgba_noise, gray_noise, blue_noise, wood, abstract
    ~/Pictures/texture.png  # Custom image
  ]
}
```

## Commands
```bash
staticwall              # Start daemon
staticwall -f           # Foreground mode
staticwall -fv          # Foreground + verbose

staticwall next         # Skip to next
staticwall pause        # Pause cycling
staticwall resume       # Resume cycling
staticwall current      # Show status
staticwall reload       # Reload config
staticwall kill         # Stop daemon
```

## Find Monitor Names
```bash
hyprctl monitors              # Hyprland
swaymsg -t get_outputs       # Sway
wlr-randr                    # Generic Wayland
```

## Rules

### ⚠️ Critical
- **NEVER use both `path` AND `shader` in same block** → ERROR!
- **Trailing `/` means directory** → loads all files alphabetically
- **All values validated** → invalid = safe defaults

### ✅ Valid Examples
```vibe
# Image only
default {
  path ~/Pictures/wallpaper.png
}

# Shader only
default {
  shader plasma.glsl
}

# Directory cycling
default {
  path ~/Pictures/Wallpapers/
  duration 300
}
```

### ❌ Invalid Examples
```vibe
# WRONG: Both path and shader
default {
  path ~/Pictures/wallpaper.png
  shader plasma.glsl            # ERROR!
}

# WRONG: Negative duration
default {
  path ~/Pictures/Wallpapers/
  duration -100                 # ERROR!
}
```

## File Locations
```
~/.config/staticwall/config.vibe           # Your config
~/.config/staticwall/shaders/*.glsl        # Shader files
$XDG_RUNTIME_DIR/staticwall.state          # Current state
$XDG_RUNTIME_DIR/staticwall.pid            # Daemon PID
```

## Common Setups

### Single wallpaper
```vibe
default { path ~/Pictures/wp.png mode fill }
```

### Slideshow (5 min)
```vibe
default { path ~/Pictures/Wallpapers/ duration 300 transition fade transition_duration 0.3 mode fill }
```

### Live shader
```vibe
default { shader plasma.glsl shader_speed 1.0 }
```

### Laptop + External
```vibe
default { path ~/Pictures/wp.png mode fill }
outputs {
  HDMI-A-1 { path ~/Pictures/Nature/ duration 600 transition fade transition_duration 0.5 }
}
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Cannot cycle" message | Use directory path with `/` or multiple files |
| Config not applying | Wait 2s or `staticwall reload` |
| Black shader screen | Check logs: `staticwall -fv` |
| "Both path and shader" | Remove one, use EITHER/OR |
| Changes not visible | Check `staticwall current` |

## Quick Tips
1. Start with `minimal.vibe`
2. Test with `staticwall -fv`
3. Name files `01-name.jpg` for order
4. Hot-reload: edit config, auto-applies
5. Check status: `staticwall current`
6. Invalid config → safe defaults (no crash)

## Validation Ranges
```
duration:              0.0 - 86400.0 seconds (24 hours, supports decimals)
transition_duration:   0.0 - 10.0 seconds (supports decimals, e.g., 0.3 = 300ms)
shader_speed:          0.01 - 100.0 (multiplier)
path:                  max 4096 characters
```

## Built-in Shaders
Check `~/.config/staticwall/shaders/` after first run:
- aurora.glsl, plasma.glsl, matrix_rain.glsl, waves.glsl, etc.

## Built-in Textures (for iChannel)
- rgba_noise, gray_noise, blue_noise, wood, abstract

---
**Full docs:** Check `complete_example.vibe` and `common_setups.vibe`
