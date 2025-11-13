# NeoWall User Manual

**Version 0.4.0**

Complete guide to using NeoWall - GPU-accelerated animated wallpapers for Wayland.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Understanding Wallpaper Types](#understanding-wallpaper-types)
4. [Command Reference](#command-reference)
5. [Configuration Guide](#configuration-guide)
6. [Advanced Usage](#advanced-usage)
7. [Troubleshooting](#troubleshooting)

---

## Introduction

NeoWall is a Wayland-native wallpaper manager that supports both **static** and **live** (animated) wallpapers. It uses GPU acceleration for smooth animations with minimal CPU usage.

### Key Features

- ✨ **Static Wallpapers** - JPG, PNG images with multiple display modes
- 🎬 **Live Wallpapers** - GLSL shaders (GIF, video, SVG coming soon)
- 🖥️ **Multi-Monitor** - Independent control for each display
- ⚡ **GPU Accelerated** - Smooth 60 FPS animations
- 🎮 **Per-Output Control** - Commands work globally or per-monitor
- 💾 **Persistent Config** - Changes survive restarts
- 🔄 **Auto-Cycling** - Rotate through wallpapers automatically

---

## Getting Started

### Starting the Daemon

```bash
# Start NeoWall daemon
neowall start

# Check if running
neowall status

# Stop daemon
neowall stop

# Restart daemon
neowall restart
```

### Quick Test

```bash
# Set a static wallpaper
neowall set-config default.path ~/Pictures/wallpaper.jpg
neowall set-config default.type image

# Or set a live shader wallpaper
neowall set-config default.path /usr/share/neowall/shaders/plasma.glsl
neowall set-config default.type shader
```

### Health Check

```bash
# Ping daemon
neowall ping          # Returns: pong

# Get version
neowall version

# List outputs
neowall list-outputs
```

---

## Understanding Wallpaper Types

NeoWall supports two broad categories of wallpapers:

### Static Wallpapers

**Type:** `image`  
**Formats:** JPG, PNG, BMP, etc.  
**Display Modes:** center, stretch, fit, fill, tile

**Config Example:**
```vibe
default {
    type image
    path ~/Pictures/mountain.jpg
    mode fill
}
```

### Live Wallpapers

**Type:** `shader` (GIF, video, SVG coming soon)  
**Formats:** GLSL shader files (.glsl)  
**Features:** Animated, GPU-rendered, customizable speed

**Config Example:**
```vibe
default {
    type shader
    path ~/.config/neowall/shaders/plasma.glsl
    shader_speed 1.5
    shader_fps 60
}
```

---

## Command Reference

NeoWall has **26 commands** organized into 6 categories:

### 1. Daemon Control (4 commands)

| Command | Description |
|---------|-------------|
| `neowall start` | Start the daemon |
| `neowall stop` | Stop the daemon |
| `neowall restart` | Restart the daemon |
| `neowall ping` | Health check (returns "pong") |

### 2. Info Commands (5 commands)

| Command | Description |
|---------|-------------|
| `neowall status` | Show daemon status and wallpaper info |
| `neowall version` | Show version information |
| `neowall current [output]` | Show current wallpaper (optional: for specific output) |
| `neowall list-commands` | List all available commands |
| `neowall command-stats [cmd]` | Show execution statistics |

**Examples:**
```bash
neowall status
neowall current
neowall current DP-1       # Current wallpaper on DP-1
neowall command-stats next # Stats for 'next' command
```

### 3. Wallpaper Control (2 commands)

| Command | Description |
|---------|-------------|
| `neowall next [output]` | Switch to next wallpaper |
| `neowall prev [output]` | Switch to previous wallpaper |

**Examples:**
```bash
# Global control (all outputs)
neowall next
neowall prev

# Per-output control
neowall next DP-1
neowall prev HDMI-1
```

### 4. Cycling Control (2 commands)

| Command | Description |
|---------|-------------|
| `neowall pause [output]` | Pause automatic wallpaper cycling |
| `neowall resume [output]` | Resume automatic wallpaper cycling |

**Examples:**
```bash
# Global pause/resume
neowall pause
neowall resume

# Per-output pause/resume
neowall pause DP-1
neowall resume DP-1
```

### 5. Live Wallpaper Control (4 commands)

| Command | Description |
|---------|-------------|
| `neowall shader-pause [output]` | Pause shader animation |
| `neowall shader-resume [output]` | Resume shader animation |
| `neowall speed-up [output]` | Increase animation speed by 0.25x |
| `neowall speed-down [output]` | Decrease animation speed by 0.25x |

**Examples:**
```bash
# Global shader control
neowall shader-pause
neowall shader-resume
neowall speed-up        # Speed increases to 1.25x, 1.5x, etc.
neowall speed-down      # Speed decreases to 0.75x, 0.5x, etc.

# Per-output shader control
neowall shader-pause DP-1
neowall speed-up HDMI-1
```

### 6. Output Commands (8 commands)

**Global vs Per-Output Behavior:**  
Most output commands accept an optional `[output]` argument:
- **With output specified**: Command applies to that specific display only
- **Without output**: Command applies to **ALL connected displays**

This makes it easy to control all your monitors at once or target specific ones.

| Command | Description |
|---------|-------------|
| `neowall list-outputs` | List all connected displays |
| `neowall output-info <output>` | Detailed info about specific output (required) |
| `neowall next-output [output]` | Next wallpaper (all outputs if omitted) |
| `neowall prev-output [output]` | Previous wallpaper (all outputs if omitted) |
| `neowall reload-output [output]` | Reload current wallpaper (all outputs if omitted) |
| `neowall pause-output [output]` | Pause cycling (all outputs if omitted) |
| `neowall resume-output [output]` | Resume cycling (all outputs if omitted) |
| `neowall jump-to-output [output] <index>` | Jump to cycle index (all outputs if omitted) |

**Examples:**
```bash
neowall list-outputs
neowall output-info DP-1

# Apply to specific output
neowall next-output DP-1
neowall reload-output DP-1
neowall jump-to-output DP-1 5  # Jump to 6th wallpaper (0-indexed)

# Apply to ALL outputs (omit output name)
neowall next-output              # Next wallpaper on all displays
neowall pause-output             # Pause all displays
neowall resume-output            # Resume all displays
neowall jump-to-output 3         # Jump all displays to index 3
```

### 7. Config Commands (4 commands) - **PERSISTENT**

| Command | Description |
|---------|-------------|
| `neowall get-config <key>` | Get config value |
| `neowall set-config <key> <value>` | Set config value (persists!) |
| `neowall reset-config <key\|--all>` | Reset config to defaults |
| `neowall list-config-keys` | List all config keys |

**Examples:**
```bash
# Get values
neowall get-config default.type
neowall get-config output.DP-1.mode

# Set values (these persist across restarts!)
neowall set-config default.type shader
neowall set-config default.path ~/shaders/plasma.glsl
neowall set-config default.shader_speed 1.5
neowall set-config output.DP-1.mode fill

# Reset values
neowall reset-config default.shader_speed
neowall reset-config --all  # Resets everything!

# List available keys
neowall list-config-keys
```

---

## Configuration Guide

### Config File Location

`~/.config/neowall/config.vibe`

### VIBE Format

NeoWall uses the **VIBE** (Values In Bracket Expression) format:

```vibe
# Comments start with #

default {
    type shader
    path ~/.config/neowall/shaders/matrix.glsl
    shader_speed 1.0
    shader_fps 60
    mode fill
}

output {
    DP-1 {
        type image
        path ~/Pictures/wallpapers/mountain.jpg
        mode fill
    }
    
    HDMI-1 {
        type shader
        path ~/.config/neowall/shaders/plasma.glsl
        shader_speed 2.0
    }
}
```

### Configuration Keys

#### Top-Level Sections

- `default { }` - Default settings for all outputs
- `output { }` - Per-output specific settings

#### Wallpaper Settings

| Key | Type | Description | Valid For |
|-----|------|-------------|-----------|
| `type` | string | Wallpaper type: `image` or `shader` | All |
| `path` | string | Path to wallpaper file | All |
| `mode` | string | Display mode (see below) | Images only |

#### Display Modes (for static images)

| Mode | Description |
|------|-------------|
| `center` | Center image without scaling |
| `stretch` | Stretch to fill screen (may distort) |
| `fit` | Scale to fit inside screen (maintain aspect) |
| `fill` | Scale to fill screen (maintain aspect, may crop) |
| `tile` | Tile image across screen |

#### Cycling Settings

| Key | Type | Description |
|-----|------|-------------|
| `cycle` | boolean | Enable wallpaper cycling |
| `duration` | float | Time in seconds between cycles |

#### Live Wallpaper Settings

| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `shader_speed` | float | Animation speed multiplier | 1.0 |
| `shader_fps` | integer | Target FPS | 60 |
| `vsync` | boolean | Enable vsync | false |
| `show_fps` | boolean | Show FPS counter | false |

#### Transition Settings

| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `transition` | string | Transition effect | fade |
| `transition_duration` | float | Transition time (seconds) | 0.3 |

**Available Transitions:**
- `none` - Instant switch
- `fade` - Fade in/out
- `slide_left`, `slide_right`, `slide_up`, `slide_down`
- `glitch` - Glitch effect
- `pixelate` - Pixelation effect

### Configuration Examples

#### Example 1: Simple Static Wallpaper

```vibe
default {
    type image
    path ~/Pictures/wallpaper.jpg
    mode fill
}
```

#### Example 2: Live Shader Wallpaper

```vibe
default {
    type shader
    path ~/.config/neowall/shaders/plasma.glsl
    shader_speed 1.5
    shader_fps 60
    vsync false
}
```

#### Example 3: Multi-Monitor Setup

```vibe
# Default for most monitors
default {
    type image
    path ~/Pictures/default-wallpaper.jpg
    mode fill
}

# Specific settings per monitor
output {
    DP-1 {
        type shader
        path ~/.config/neowall/shaders/matrix.glsl
        shader_speed 1.0
    }
    
    HDMI-1 {
        type image
        path ~/Pictures/ultrawide-wallpaper.jpg
        mode fill
    }
}
```

#### Example 4: Auto-Cycling Wallpapers

```vibe
default {
    type image
    path ~/Pictures/wallpapers/   # Directory path (ends with /)
    mode fill
    cycle true
    duration 300                   # 5 minutes
    transition fade
    transition_duration 0.5        # 0.5 second fade
}
```

#### Example 5: Shader with Custom Speed

```vibe
output {
    DP-1 {
        type shader
        path ~/.config/neowall/shaders/fast_plasma.glsl
        shader_speed 2.5              # 2.5x speed
        shader_fps 120                # 120 FPS (if monitor supports)
        vsync true                    # Sync to monitor refresh
        show_fps true                 # Show FPS counter
    }
}
```

---

## Advanced Usage

### VIBE Path Notation

Config commands use **dot notation** to access nested values:

```bash
# Access default section
neowall get-config default.type
neowall get-config default.path

# Access output-specific settings
neowall get-config output.DP-1.mode
neowall get-config output.HDMI-1.shader_speed

# Set nested values
neowall set-config output.DP-1.type shader
neowall set-config output.DP-1.path ~/shaders/cool.glsl
```

### Runtime vs Persistent Commands

**Important:** There are two types of commands:

#### Runtime Commands (Temporary)

These affect the **current session only** and are lost on daemon restart:

```bash
neowall next [output]
neowall prev [output]
neowall pause [output]
neowall resume [output]
neowall shader-pause [output]
neowall shader-resume [output]
neowall speed-up [output]
neowall speed-down [output]
```

**Use case:** Quick experimentation, temporary adjustments

#### Persistent Commands (Permanent)

These **write to config.vibe** and survive restarts:

```bash
neowall set-config <key> <value>
neowall reset-config <key>
```

**Use case:** Permanent settings you want to keep

### Workflow Example

```bash
# 1. Experiment with runtime commands
neowall next                    # Try next wallpaper
neowall speed-up               # Speed up shader
neowall shader-pause           # Pause animation

# 2. When you find settings you like, persist them
neowall set-config default.shader_speed 1.75
neowall set-config default.path ~/.config/neowall/shaders/favorite.glsl

# 3. On restart, your saved settings are restored
neowall restart
```

### Multi-Monitor Control Patterns

#### Pattern 1: Same Action on All Monitors

```bash
# Global command (no output specified)
neowall next          # Next on all outputs
neowall pause         # Pause all outputs
```

#### Pattern 2: Independent Control

```bash
# Different actions per monitor
neowall next DP-1           # Next on DP-1
neowall pause HDMI-1        # Pause HDMI-1
neowall shader-pause DP-2   # Pause shader on DP-2
```

#### Pattern 3: Mixed Static and Live

```bash
# DP-1: Static images
neowall set-config output.DP-1.type image
neowall set-config output.DP-1.path ~/Pictures/

# HDMI-1: Live shader
neowall set-config output.HDMI-1.type shader
neowall set-config output.HDMI-1.path ~/.config/neowall/shaders/plasma.glsl

# Control them independently
neowall next DP-1              # Next image on DP-1
neowall speed-up HDMI-1        # Speed up shader on HDMI-1
```

### Finding Your Output Names

```bash
# List all connected outputs with details
neowall list-outputs

# Example output:
# Connected Outputs:
# ─────────────────────────────────────────────────────────
#   • DP-1 (LG 34WK95U)
#     Resolution: 3440x1440
#     Wallpaper:  shader
#   
#   • HDMI-1 (Dell P2419H)
#     Resolution: 1920x1080
#     Wallpaper:  image
```

Use the name in parentheses (e.g., `DP-1`, `HDMI-1`) for commands.

### Performance Tuning

#### For Shaders

```bash
# Lower FPS for better battery life
neowall set-config default.shader_fps 30

# Higher FPS for smoother animation (if monitor supports it)
neowall set-config default.shader_fps 120

# Enable vsync to match monitor refresh rate
neowall set-config default.vsync true

# Show FPS counter to monitor performance
neowall set-config default.show_fps true
```

#### For Static Images

```bash
# Faster cycling
neowall set-config default.duration 60     # 1 minute

# Slower cycling
neowall set-config default.duration 600    # 10 minutes

# Faster transitions
neowall set-config default.transition_duration 0.2

# Instant switching (no transition)
neowall set-config default.transition none
```

### Command Statistics

Monitor which commands you use most:

```bash
# View all command stats
neowall command-stats

# View stats for specific command
neowall command-stats next

# Example output:
# Command: next
# Total calls: 42
# Success: 41
# Failed: 1
# Avg time: 15ms
# Min time: 10ms
# Max time: 25ms
```

---

## Troubleshooting

### Daemon Won't Start

```bash
# Check if already running
neowall status

# Kill stale processes
pkill -9 neowalld

# Start with verbose output (if available)
neowalld -v

# Check logs
journalctl -u neowalld -f
```

### Wallpaper Not Changing

```bash
# Check daemon status
neowall status

# Verify output name
neowall list-outputs

# Check current config
neowall get-config default.path
neowall get-config default.type

# Try manual reload
neowall reload-output DP-1
```

### Shader Not Loading

```bash
# Verify shader file exists
ls -la ~/.config/neowall/shaders/

# Check shader type is set
neowall get-config default.type

# Should return: shader

# Try setting explicitly
neowall set-config default.type shader
neowall set-config default.path ~/.config/neowall/shaders/plasma.glsl
```

### Performance Issues

```bash
# Lower FPS
neowall set-config default.shader_fps 30

# Slow down animation
neowall speed-down
neowall speed-down  # Call multiple times

# Enable vsync
neowall set-config default.vsync true

# Check current speed
neowall status  # Look for "shader_speed" value
```

### Config Not Persisting

**Remember:** Only `set-config` and `reset-config` persist changes!

```bash
# ❌ WRONG (temporary, lost on restart)
neowall next
neowall speed-up

# ✅ CORRECT (persistent)
neowall set-config default.path ~/new-wallpaper.jpg
neowall set-config default.shader_speed 1.5
```

### Reset to Default Config

```bash
# Reset specific keys
neowall reset-config default.shader_speed
neowall reset-config output.DP-1.mode

# Nuclear option: reset everything
neowall reset-config --all

# Or manually delete config file
rm ~/.config/neowall/config.vibe
neowall restart
```

### Multi-Monitor Issues

```bash
# List outputs to verify names
neowall list-outputs

# Get info about specific output
neowall output-info DP-1

# Reload specific output
neowall reload-output DP-1

# Reload ALL outputs
neowall reload-output

# Set output-specific config
neowall set-config output.DP-1.path ~/wallpaper.jpg
```

---

## Quick Reference Card

### Essential Commands

```bash
# Daemon
neowall start / stop / restart / status / ping

# Wallpaper Control
neowall next [output]
neowall prev [output]

# Cycling
neowall pause [output]
neowall resume [output]

# Shaders
neowall shader-pause [output]
neowall shader-resume [output]
neowall speed-up [output]
neowall speed-down [output]

# Config (persistent!)
neowall set-config <key> <value>
neowall get-config <key>
neowall reset-config <key|--all>

# Outputs
neowall list-outputs
neowall output-info <output>
```

### Common Config Keys

```bash
# Type and path
default.type              # image or shader
default.path              # /path/to/file

# Display
default.mode              # center/stretch/fit/fill/tile

# Cycling
default.cycle             # true/false
default.duration          # seconds

# Transitions
default.transition        # fade/slide_*/glitch/pixelate/none
default.transition_duration  # seconds

# Live wallpapers
default.shader_speed      # float multiplier
default.shader_fps        # integer FPS
default.vsync             # true/false
default.show_fps          # true/false

# Per-output (replace DP-1 with your output name)
output.DP-1.type
output.DP-1.path
output.DP-1.mode
output.DP-1.shader_speed
```

---

## Tips & Best Practices

### 1. Test Before Persisting

```bash
# Try runtime commands first
neowall next
neowall speed-up

# When you find what you like, persist it
neowall set-config default.shader_speed 1.25
```

### 2. Use Directory Paths for Cycling

```bash
# Automatically cycle through all images in directory
neowall set-config default.path ~/Pictures/wallpapers/
neowall set-config default.cycle true
neowall set-config default.duration 300
```

### 3. Create Output-Specific Configs

```bash
# Main monitor: Live shader
neowall set-config output.DP-1.type shader
neowall set-config output.DP-1.path ~/.config/neowall/shaders/matrix.glsl

# Secondary monitor: Static image
neowall set-config output.HDMI-1.type image
neowall set-config output.HDMI-1.path ~/Pictures/secondary.jpg
```

### 4. Check Status Regularly

```bash
# See what's currently active
neowall status

# Check specific output
neowall output-info DP-1
```

### 5. Use Transitions for Smooth Changes

```bash
# Nice fade between wallpapers
neowall set-config default.transition fade
neowall set-config default.transition_duration 0.5
```

---

## Getting Help

- **List all commands:** `neowall list-commands`
- **Command help:** `neowall help`
- **Version info:** `neowall version`
- **GitHub:** https://github.com/1ay1/neowall
- **Issues:** https://github.com/1ay1/neowall/issues

---

**NeoWall User Manual v0.4.0**  
Last updated: 2024
