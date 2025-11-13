# NeoWall Configuration Key to Command Mapping

**Complete reference for setting config values via commands**

---

## 📋 Overview

NeoWall provides two ways to modify configuration at runtime:

1. **Generic command**: `set-config` with any config key
2. **Specific commands**: Convenience commands that match config key names exactly

This document maps configuration keys to their corresponding commands for easy reference.

---

## 🎯 Command Naming Convention

Commands are named to **exactly match** their corresponding config keys:

- Config key: `default.path` → Command: `set-default-path`
- Config key: `output.mode` → Command: `set-output-mode`
- Config key: `default.shader_speed` → Command: `set-default-shader-speed`

**Pattern**: Replace dots (`.`) with hyphens (`-`) and underscores (`_`) with hyphens (`-`)

---

## 🌐 Global/Default Configuration

These commands set default values that apply to all outputs unless overridden.

### Static Wallpaper Settings (default.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `default.path` | `set-default-path` | Set default image wallpaper path | `neowall set-default-path ~/Pictures/wallpaper.png` |
| `default.mode` | `set-default-mode` | Set default display mode | `neowall set-default-mode fill` |
| `default.duration` | `set-default-duration` | Set default cycle duration (seconds) | `neowall set-default-duration 600` |
| `default.transition` | `set-default-transition` | Set default transition effect | `neowall set-default-transition fade` |
| `default.transition_duration` | `set-default-transition-duration` | Set default transition duration (ms) | `neowall set-default-transition-duration 500` |

**Valid values for `mode`**: `fill`, `fit`, `center`, `stretch`, `tile`

**Valid values for `transition`**: `none`, `fade`, `slide-left`, `slide-right`, `glitch`, `pixelate`

### Shader Wallpaper Settings (default.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `default.shader` | `set-default-shader` | Set default shader wallpaper | `neowall set-default-shader matrix_rain.glsl` |
| `default.shader_speed` | `set-default-shader-speed` | Set default shader animation speed | `neowall set-default-shader-speed 2.0` |
| `default.shader_fps` | *(via set-config)* | Set default shader FPS limit | `neowall set-config default.shader_fps 60` |
| `default.vsync` | *(via set-config)* | Enable/disable VSync | `neowall set-config default.vsync true` |
| `default.channels` | *(via set-config)* | Set shader texture channels | `neowall set-config default.channels "channel0.png"` |

### Universal Settings (default.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `default.show_fps` | *(via set-config)* | Show FPS counter | `neowall set-config default.show_fps true` |

---

## 🖥️ Per-Output Configuration

These commands override default settings for specific monitors.

**Pattern**: All output commands require an `--output` or `-o` parameter specifying the monitor name.

**Find your monitor name**:
```bash
neowall list-outputs
# Example output: DP-1, HDMI-A-1, eDP-1
```

### Static Wallpaper Settings (output.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `output.path` | `set-output-path` | Set image wallpaper for specific output | `neowall set-output-path -o DP-1 ~/Pictures/wallpaper.png` |
| `output.mode` | `set-output-mode` | Set display mode for specific output | `neowall set-output-mode -o DP-1 fill` |
| `output.duration` | `set-output-duration` | Set cycle duration for specific output | `neowall set-output-duration -o DP-1 600` |
| `output.transition` | `set-output-transition` | Set transition effect for specific output | `neowall set-output-transition -o DP-1 fade` |
| `output.transition_duration` | `set-output-transition-duration` | Set transition duration for specific output | `neowall set-output-transition-duration -o DP-1 500` |

### Shader Wallpaper Settings (output.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `output.shader` | `set-output-shader` | Set shader wallpaper for specific output | `neowall set-output-shader -o DP-1 matrix_rain.glsl` |
| `output.shader_speed` | `set-output-shader-speed` | Set shader speed for specific output | `neowall set-output-shader-speed -o DP-1 2.0` |
| `output.shader_fps` | *(via set-config)* | Set shader FPS for specific output | `neowall set-config output.shader_fps 60 -o DP-1` |
| `output.vsync` | *(via set-config)* | Enable/disable VSync for specific output | `neowall set-config output.vsync true -o DP-1` |
| `output.channels` | *(via set-config)* | Set shader channels for specific output | `neowall set-config output.channels "tex.png" -o DP-1` |

### Universal Settings (output.*)

| Config Key | Command | Description | Example |
|------------|---------|-------------|---------|
| `output.show_fps` | *(via set-config)* | Show FPS counter for specific output | `neowall set-config output.show_fps true -o DP-1` |

---

## 🔧 Generic set-config Command

For config keys without dedicated commands, use the generic `set-config`:

### Global/Default

```bash
neowall set-config <key> <value>
```

**Examples**:
```bash
neowall set-config default.shader_fps 60
neowall set-config default.vsync true
neowall set-config default.show_fps false
neowall set-config default.channels "~/textures/channel0.png"
```

### Per-Output

```bash
neowall set-config <key> <value> --output <monitor>
# or
neowall set-config <key> <value> -o <monitor>
```

**Examples**:
```bash
neowall set-config output.shader_fps 120 -o DP-1
neowall set-config output.vsync false -o HDMI-A-1
neowall set-config output.show_fps true -o eDP-1
```

---

## 📖 Usage Examples

### Example 1: Set up a static wallpaper with cycling

```bash
# Set default image path to a directory (cycles through all images)
neowall set-default-path ~/Pictures/Wallpapers/

# Set cycle duration to 5 minutes (300 seconds)
neowall set-default-duration 300

# Set display mode to fill screen
neowall set-default-mode fill

# Set smooth fade transition
neowall set-default-transition fade
neowall set-default-transition-duration 500
```

### Example 2: Set up an animated shader wallpaper

```bash
# Set default shader
neowall set-default-shader matrix_rain.glsl

# Increase animation speed to 2x
neowall set-default-shader-speed 2.0

# Set FPS limit (via set-config)
neowall set-config default.shader_fps 60

# Enable VSync for smooth rendering
neowall set-config default.vsync true
```

### Example 3: Multi-monitor setup

```bash
# Laptop screen: Static image
neowall set-output-path -o eDP-1 ~/Pictures/laptop-wallpaper.png
neowall set-output-mode -o eDP-1 fit

# External monitor 1: Animated shader
neowall set-output-shader -o DP-1 plasma.glsl
neowall set-output-shader-speed -o DP-1 1.5

# External monitor 2: Cycling wallpapers
neowall set-output-path -o HDMI-A-1 ~/Pictures/Nature/
neowall set-output-duration -o HDMI-A-1 900  # 15 minutes
neowall set-output-transition -o HDMI-A-1 slide-left
```

### Example 4: Using generic set-config for advanced settings

```bash
# Set shader FPS limit globally
neowall set-config default.shader_fps 120

# Enable VSync on specific output
neowall set-config output.vsync true -o DP-1

# Set shader texture channels
neowall set-config default.channels "~/textures/noise.png"

# Show FPS counter for debugging
neowall set-config default.show_fps true
```

---

## 🔍 Command Discovery

### List all available commands

```bash
neowall list-commands
```

### List commands by category

```bash
neowall list-commands --category config
neowall list-commands --category output
```

### Get help for a specific command

```bash
neowall help set-default-path
neowall help set-output-shader
```

### View current configuration

```bash
# Get all config
neowall get-config

# Get specific key
neowall get-config default.path
neowall get-config output.shader -o DP-1
```

---

## ⚠️ Important Notes

### 1. Mutual Exclusivity: Static vs Shader

You **cannot** use both `path` and `shader` for the same output:

```bash
# ❌ INVALID - Can't mix static and shader
neowall set-default-path ~/Pictures/wallpaper.png
neowall set-default-shader matrix_rain.glsl  # ERROR!

# ✅ VALID - Choose one
neowall set-default-path ~/Pictures/wallpaper.png
# OR
neowall set-default-shader matrix_rain.glsl
```

### 2. Type-Specific Settings

Certain settings only work with their respective wallpaper type:

**Static-only** (only with `path`):
- `mode`, `transition`, `transition_duration`

**Shader-only** (only with `shader`):
- `shader_speed`, `shader_fps`, `vsync`, `channels`

**Universal** (works with both):
- `duration`, `show_fps`

### 3. Validation

All commands validate their inputs using the config rules system:

```bash
# ❌ Invalid: Can't set mode on shader
neowall set-default-shader plasma.glsl
neowall set-default-mode fill  # ERROR: mode only works with images

# ❌ Invalid: Can't set shader_speed on static image
neowall set-default-path ~/wallpaper.png
neowall set-default-shader-speed 2.0  # ERROR: shader_speed only works with shaders
```

### 4. Persistence

All runtime configuration changes are:
1. **Applied immediately** - No restart needed
2. **Validated** - Invalid settings are rejected with clear errors
3. **Persistent** - Changes are written to `~/.config/neowall/config.vibe`

---

## 🆚 Deprecated Commands

The following commands have been **renamed** to match config keys:

| Old Command | New Command | Status |
|-------------|-------------|--------|
| `set-output-interval` | `set-output-duration` | ✅ Renamed |
| `set-output-wallpaper` | `set-output-path` (images) or `set-output-shader` (shaders) | ✅ Split into two |

**Migration guide**:
```bash
# Old way
neowall set-output-interval -o DP-1 600
neowall set-output-wallpaper -o DP-1 ~/wallpaper.png

# New way (matches config keys)
neowall set-output-duration -o DP-1 600
neowall set-output-path -o DP-1 ~/wallpaper.png
```

---

## 📚 Related Documentation

- [Configuration Guide](CONFIG_RULES.md) - Complete config file reference
- [Command Reference](commands/COMMANDS.md) - All available commands
- [Config Rules System](CONFIG_RULES.md) - Validation rules explained
- [Usage Guide](USAGE_GUIDE.md) - User manual

---

## 🐛 Troubleshooting

### Command not found

```bash
neowall set-output-interval  # ERROR: Unknown command
```

**Solution**: Use the new name:
```bash
neowall set-output-duration
```

### Invalid config key error

```bash
neowall set-config output.invalid_key value
# ERROR: Unknown config key
```

**Solution**: List all valid keys:
```bash
neowall list-config-keys
```

### Validation error

```bash
neowall set-default-mode fill
# ERROR: 'mode' only applies to static wallpapers
```

**Solution**: Check wallpaper type and use appropriate settings:
```bash
# If using shader, remove it first or use set-default-path instead
neowall set-default-path ~/Pictures/wallpaper.png
neowall set-default-mode fill  # Now it works
```

---

**Questions?** Open an issue on [GitHub](https://github.com/1ay1/neowall/issues) or check the main [README](../README.md).