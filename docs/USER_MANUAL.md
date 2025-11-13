# NeoWall User Manual

**Complete Guide to NeoWall - Wayland Animated Wallpaper Manager**

Version 1.0 | January 2025

---

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Installation](#installation)
4. [Configuration](#configuration)
5. [Static Wallpapers](#static-wallpapers)
6. [Shader Wallpapers](#shader-wallpapers)
7. [Command Line Interface](#command-line-interface)
8. [System Tray](#system-tray)
9. [IPC Protocol](#ipc-protocol)
10. [Advanced Features](#advanced-features)
11. [Troubleshooting](#troubleshooting)
12. [FAQ](#faq)
13. [Appendix](#appendix)

---

## Introduction

### What is NeoWall?

NeoWall is a modern, high-performance wallpaper manager for Wayland compositors. It supports both static images and animated GLSL shaders, providing a beautiful and dynamic desktop experience.

### Key Features

✨ **Static Wallpapers**
- Multiple image formats (PNG, JPEG, WebP)
- Flexible display modes (fill, fit, center, tile, stretch)
- Smooth transitions between wallpapers
- Directory-based cycling with configurable intervals

🎨 **Shader Wallpapers**
- Full GLSL shader support
- Real-time animated backgrounds
- Configurable speed and framerate
- V-sync support for smooth animations
- Multiple texture channels (iChannel0-3)
- FPS counter overlay

🖥️ **Multi-Monitor Support**
- Per-output configuration
- Independent wallpapers for each monitor
- Mix static and shader wallpapers

⚡ **Performance**
- GPU-accelerated rendering
- Minimal CPU usage
- Efficient memory management
- Adaptive framerate control

🎯 **Flexible Configuration**
- VIBE configuration format (human-readable)
- Runtime configuration via IPC
- Command-line interface
- System tray integration

### Supported Compositors

NeoWall works with Wayland compositors that support the `wlr-layer-shell` protocol:

- ✅ Hyprland
- ✅ Sway
- ✅ River
- ✅ Wayfire
- ✅ Labwc
- ✅ Other wlroots-based compositors

---

## Getting Started

### Quick Start

1. **Install NeoWall:**
   ```bash
   # See Installation section for details
   sudo make install
   ```

2. **Create a basic configuration:**
   ```bash
   mkdir -p ~/.config/neowall
   neowall --create-default-config
   ```

3. **Start the daemon:**
   ```bash
   neowall daemon
   ```

4. **Set a wallpaper:**
   ```bash
   neowall set-wallpaper ~/Pictures/wallpaper.png
   ```

### First Configuration

Create `~/.config/neowall/config.vibe`:

```vibe
# NeoWall Configuration

default {
    # Set your wallpaper
    path = "~/Pictures/wallpaper.png"
    
    # Display mode
    mode = "fill"
    
    # Optional: transition effect
    transition = "fade"
    transition_duration = 500
}
```

Start NeoWall:
```bash
neowall daemon
```

---

## Installation

### Prerequisites

**Build Dependencies:**
- `meson` >= 0.60.0
- `ninja`
- `gcc` or `clang`
- `pkg-config`

**Runtime Dependencies:**
- Wayland compositor with wlr-layer-shell
- `libwayland-client`
- `libpng`
- `libjpeg` (optional, for JPEG support)
- `libwebp` (optional, for WebP support)
- OpenGL ES 3.0 or higher
- `libEGL`

### Building from Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/1ay1/neowall.git
   cd neowall
   ```

2. **Configure the build:**
   ```bash
   meson setup build
   ```

   Optional features:
   ```bash
   meson setup build -Djpeg=enabled -Dwebp=enabled
   ```

3. **Compile:**
   ```bash
   meson compile -C build
   ```

4. **Install:**
   ```bash
   sudo meson install -C build
   ```

### Package Managers

**Arch Linux (AUR):**
```bash
yay -S neowall-git
```

**Homebrew (macOS/Linux):**
```bash
brew install neowall
```

**From Release:**
```bash
# Download latest release
wget https://github.com/1ay1/neowall/releases/latest/download/neowall-linux.tar.gz
tar xzf neowall-linux.tar.gz
cd neowall
sudo make install
```

### Verification

Check installation:
```bash
neowall --version
neowall --help
```

---

## Configuration

### Configuration File Location

NeoWall looks for configuration in this order:

1. `$XDG_CONFIG_HOME/neowall/config.vibe`
2. `~/.config/neowall/config.vibe`
3. Built-in defaults

### VIBE Configuration Format

NeoWall uses VIBE (Visual Indented Block Expressions) for configuration:

```vibe
# Comments start with #

section {
    key = "value"
    number = 42
    boolean = true
    
    nested {
        key = "value"
    }
}
```

### Basic Structure

```vibe
# Global default configuration
default {
    path = "~/Pictures/wallpaper.png"
    mode = "fill"
}

# Per-output configuration
output {
    DP-1 {
        path = "~/Pictures/monitor1.png"
    }
    
    HDMI-A-1 {
        shader = "~/shaders/plasma.glsl"
    }
}
```

### Configuration Rules

**IMPORTANT:** Static and shader settings are mutually exclusive!

#### Static Wallpaper Settings (with `path`)

```vibe
default {
    path = "~/wallpaper.png"         # Image file or directory
    mode = "fill"                     # Display mode
    transition = "fade"               # Transition effect
    transition_duration = 500         # Milliseconds
    duration = 300                    # Cycling interval (seconds)
}
```

#### Shader Wallpaper Settings (with `shader`)

```vibe
default {
    shader = "~/shader.glsl"          # Shader file or directory
    shader_speed = 1.5                # Animation speed multiplier
    shader_fps = 60                   # Target framerate
    vsync = true                      # Enable V-sync
    show_fps = true                   # Show FPS counter
    duration = 60                     # Cycling interval (seconds)
    
    # Optional: texture inputs
    channels = [
        "~/texture1.png",
        "~/texture2.png"
    ]
}
```

#### Universal Settings

```vibe
duration = 300    # Works with both static and shader
                  # For directories: cycling interval
```

### Validation Rules

NeoWall enforces strict validation:

❌ **INVALID - Cannot use both:**
```vibe
default {
    path = "~/wallpaper.png"
    shader = "~/shader.glsl"    # ERROR: Choose one!
}
```

❌ **INVALID - mode doesn't work with shaders:**
```vibe
default {
    shader = "~/shader.glsl"
    mode = "fill"               # ERROR: mode is static-only
}
```

❌ **INVALID - shader_speed doesn't work with images:**
```vibe
default {
    path = "~/wallpaper.png"
    shader_speed = 2.0          # ERROR: shader_speed is shader-only
}
```

✅ **VALID - Correct usage:**
```vibe
# Static wallpaper
output {
    DP-1 {
        path = "~/wallpaper.png"
        mode = "fill"
        transition = "fade"
    }
}

# Shader wallpaper
output {
    HDMI-A-1 {
        shader = "~/shader.glsl"
        shader_speed = 1.5
        vsync = true
    }
}
```

---

## Static Wallpapers

### Single Image

Set a single wallpaper:

```vibe
default {
    path = "~/Pictures/nature.jpg"
    mode = "fill"
}
```

### Display Modes

Control how images are displayed:

| Mode | Description |
|------|-------------|
| `fill` | Fill screen, crop to fit (default) |
| `fit` | Fit entire image, may have borders |
| `center` | Center image at original size |
| `tile` | Repeat image as tiles |
| `stretch` | Stretch to fill screen (may distort) |

Example:
```vibe
default {
    path = "~/Pictures/wallpaper.png"
    mode = "fit"    # Show entire image
}
```

### Transitions

Smooth transitions when changing wallpapers:

| Transition | Description |
|------------|-------------|
| `none` | Instant change |
| `fade` | Cross-fade between images |
| `slide` | Slide in new image |
| `wipe` | Wipe across screen |

Configuration:
```vibe
default {
    path = "~/Pictures/wallpaper.png"
    transition = "fade"
    transition_duration = 500    # Milliseconds (default: 300)
}
```

### Directory Cycling

Automatically cycle through images in a directory:

```vibe
default {
    # Path ending with / indicates directory
    path = "~/Pictures/wallpapers/"
    
    # Cycle every 5 minutes (300 seconds)
    duration = 300
    
    # Smooth transitions
    transition = "fade"
    transition_duration = 1000
    
    mode = "fill"
}
```

**Supported formats in directories:**
- `.png`
- `.jpg`, `.jpeg`
- `.webp` (if built with WebP support)

**Directory structure:**
```
~/Pictures/wallpapers/
├── nature1.png
├── nature2.jpg
├── abstract.png
└── sunset.jpg
```

NeoWall will automatically:
- Scan the directory for supported images
- Sort alphabetically
- Cycle through them at the specified interval

### Manual Control

Change wallpaper via command line:

```bash
# Set specific wallpaper
neowall set-wallpaper ~/Pictures/new-wallpaper.png

# Next wallpaper (when cycling)
neowall next-wallpaper

# Previous wallpaper (when cycling)
neowall prev-wallpaper
```

### Per-Monitor Configuration

Different wallpapers for different monitors:

```vibe
# Find output names with: neowall list-outputs

output {
    # Primary monitor
    DP-1 {
        path = "~/Pictures/primary.png"
        mode = "fill"
    }
    
    # Secondary monitor
    HDMI-A-1 {
        path = "~/Pictures/secondary.png"
        mode = "fit"
    }
    
    # Third monitor with cycling
    DP-2 {
        path = "~/Pictures/collection/"
        duration = 600
        transition = "fade"
    }
}
```

---

## Shader Wallpapers

### What are Shader Wallpapers?

Shader wallpapers are animated backgrounds created using GLSL (OpenGL Shading Language). They run directly on your GPU for smooth, efficient animations.

### Basic Shader Setup

```vibe
default {
    shader = "~/.config/neowall/shaders/plasma.glsl"
    shader_speed = 1.0
    shader_fps = 60
    vsync = true
}
```

### Shader Settings

#### Speed Control

```vibe
default {
    shader = "~/shader.glsl"
    shader_speed = 1.5    # 1.5x normal speed
                          # Range: 0.1 to 10.0
                          # Default: 1.0
}
```

#### Framerate

```vibe
default {
    shader = "~/shader.glsl"
    
    # Option 1: Fixed framerate
    shader_fps = 60
    vsync = false
    
    # Option 2: V-sync (matches monitor refresh)
    vsync = true           # shader_fps ignored when true
}
```

**Recommended settings:**

| Use Case | Settings |
|----------|----------|
| Smooth animation | `vsync = true` |
| Lower power usage | `shader_fps = 30, vsync = false` |
| Maximum smoothness | `shader_fps = 144, vsync = false` (high-refresh monitors) |

#### FPS Counter

Display current framerate:

```vibe
default {
    shader = "~/shader.glsl"
    show_fps = true        # Show FPS in corner
}
```

The FPS counter appears in the top-left corner showing:
- Current FPS
- Frame time (ms)
- Target FPS (if applicable)

### Shader Directory Cycling

Cycle through multiple shaders:

```vibe
default {
    # Directory of shaders
    shader = "~/.config/neowall/shaders/"
    
    # Change shader every minute
    duration = 60
    
    # Shader settings apply to all
    shader_speed = 1.0
    vsync = true
}
```

Directory structure:
```
~/.config/neowall/shaders/
├── plasma.glsl
├── waves.glsl
├── matrix.glsl
└── fractal.glsl
```

### Texture Channels (iChannel)

Pass textures to shaders (like Shadertoy):

```vibe
default {
    shader = "~/shader.glsl"
    
    channels = [
        "~/textures/noise.png",      # iChannel0
        "~/textures/pattern.png",    # iChannel1
        "~/textures/gradient.png",   # iChannel2
        "~/textures/detail.png"      # iChannel3
    ]
}
```

In your shader:
```glsl
uniform sampler2D iChannel0;  // First texture
uniform sampler2D iChannel1;  // Second texture
uniform sampler2D iChannel2;  // Third texture
uniform sampler2D iChannel3;  // Fourth texture

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    vec4 noise = texture(iChannel0, uv);
    // ... your shader code
}
```

### Writing Custom Shaders

NeoWall provides Shadertoy-compatible uniforms:

```glsl
#version 300 es
precision highp float;

// Provided by NeoWall
uniform vec3 iResolution;      // Screen resolution (width, height, 1.0)
uniform float iTime;           // Time in seconds
uniform float iTimeDelta;      // Time since last frame
uniform int iFrame;            // Frame number
uniform vec4 iMouse;           // Mouse coordinates (future)
uniform sampler2D iChannel0;   // Texture channel 0
uniform sampler2D iChannel1;   // Texture channel 1
uniform sampler2D iChannel2;   // Texture channel 2
uniform sampler2D iChannel3;   // Texture channel 3

out vec4 fragColor;

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Your shader code here
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}

void main() {
    mainImage(fragColor, gl_FragCoord.xy);
}
```

### Example Shaders

#### Simple Gradient
```glsl
#version 300 es
precision highp float;
uniform vec3 iResolution;
uniform float iTime;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    vec3 color = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(iTime));
    fragColor = vec4(color, 1.0);
}
```

#### Plasma Effect
```glsl
#version 300 es
precision highp float;
uniform vec3 iResolution;
uniform float iTime;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    vec2 pos = uv * 10.0;
    
    float v = 0.0;
    v += sin(pos.x + iTime);
    v += sin(pos.y + iTime);
    v += sin(pos.x + pos.y + iTime);
    v += sin(length(pos) + iTime * 1.5);
    
    vec3 color = 0.5 + 0.5 * cos(v + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(color, 1.0);
}
```

### Shader Resources

**Where to find shaders:**
- [Shadertoy](https://www.shadertoy.com/) - Convert to NeoWall format
- [GLSL Sandbox](https://glslsandbox.com/)
- [ShaderToy Gallery](https://www.shadertoy.com/browse)

**Converting Shadertoy shaders:**

1. Copy the shader code
2. Add NeoWall header:
   ```glsl
   #version 300 es
   precision highp float;
   out vec4 fragColor;
   ```
3. Replace `void mainImage(...)` content
4. Add `void main() { mainImage(fragColor, gl_FragCoord.xy); }`

### Performance Tips

**For better performance:**

1. **Lower resolution:** Shaders render at display resolution
2. **Reduce FPS:** `shader_fps = 30` instead of 60
3. **Simplify shader:** Complex shaders use more GPU
4. **Use V-sync:** `vsync = true` prevents unnecessary rendering
5. **Monitor GPU usage:** `show_fps = true` to check performance

**Power saving:**
```vibe
default {
    shader = "~/shader.glsl"
    shader_fps = 20        # Lower FPS
    vsync = false
    shader_speed = 0.5     # Slower animation
}
```

---

## Command Line Interface

### Basic Commands

#### Start Daemon

```bash
# Start NeoWall daemon
neowall daemon

# Start with specific config
neowall daemon --config ~/my-config.vibe

# Verbose logging
neowall daemon --verbose

# Debug mode
neowall daemon --debug
```

#### Wallpaper Management

```bash
# Set wallpaper (static)
neowall set-wallpaper ~/Pictures/wallpaper.png

# Set wallpaper for specific output
neowall set-wallpaper --output DP-1 ~/Pictures/wallpaper.png

# Set shader wallpaper
neowall set-shader ~/shaders/plasma.glsl

# Set shader for specific output
neowall set-shader --output HDMI-A-1 ~/shaders/waves.glsl
```

#### Cycling Control

```bash
# Next wallpaper/shader
neowall next

# Previous wallpaper/shader
neowall prev

# Next on specific output
neowall next --output DP-1

# Pause cycling
neowall pause

# Resume cycling
neowall resume

# Reset to first wallpaper
neowall reset
```

#### Information

```bash
# List connected outputs
neowall list-outputs

# Show current status
neowall status

# Show detailed info
neowall info

# Show version
neowall --version

# Show help
neowall --help
```

#### Configuration

```bash
# Get config value
neowall get-config wallpaper.mode

# Set config value
neowall set-config wallpaper.mode fill

# Set config for specific output
neowall set-config --output DP-1 wallpaper.mode fit

# Reset config to defaults
neowall reset-config

# Reload config file
neowall reload
```

### Advanced Commands

#### IPC Commands

```bash
# Send raw IPC command
neowall ipc '{"command":"status"}'

# Query specific information
neowall ipc '{"command":"get-config","args":"{\"key\":\"wallpaper.mode\"}"}'

# Set configuration
neowall ipc '{"command":"set-config","args":"{\"key\":\"wallpaper.mode\",\"value\":\"fill\"}"}'
```

#### Testing

```bash
# Test shader compilation
neowall test-shader ~/shader.glsl

# Validate config file
neowall validate-config ~/.config/neowall/config.vibe

# Check compositor compatibility
neowall check-compositor
```

#### Debugging

```bash
# Run with debug output
NEOWALL_DEBUG=1 neowall daemon

# Log to file
neowall daemon 2>&1 | tee neowall.log

# Trace protocol messages
WAYLAND_DEBUG=1 neowall daemon
```

### Command Line Options

```
neowall [OPTIONS] <COMMAND>

OPTIONS:
  -h, --help              Show help message
  -v, --version           Show version
  -c, --config PATH       Use specific config file
  -o, --output NAME       Target specific output
  --verbose               Enable verbose logging
  --debug                 Enable debug mode
  --no-fork               Don't daemonize (stay in foreground)

COMMANDS:
  daemon                  Start the wallpaper daemon
  set-wallpaper PATH      Set static wallpaper
  set-shader PATH         Set shader wallpaper
  next                    Next wallpaper/shader
  prev                    Previous wallpaper/shader
  pause                   Pause cycling
  resume                  Resume cycling
  reset                   Reset to first wallpaper
  list-outputs            List available outputs
  status                  Show current status
  info                    Show detailed information
  get-config KEY          Get configuration value
  set-config KEY VALUE    Set configuration value
  reset-config [KEY]      Reset configuration
  reload                  Reload configuration
  ipc JSON                Send raw IPC command
  test-shader PATH        Test shader compilation
  validate-config PATH    Validate config file
  check-compositor        Check compositor support
```

### Environment Variables

```bash
# Config file location
export NEOWALL_CONFIG=~/.config/neowall/config.vibe

# Socket location
export NEOWALL_SOCKET=/tmp/neowall-$UID.sock

# Debug level (0-3)
export NEOWALL_DEBUG=2

# Force software rendering
export NEOWALL_SOFTWARE_RENDER=1

# Wayland display
export WAYLAND_DISPLAY=wayland-1
```

---

## System Tray

### Overview

NeoWall provides a system tray icon for quick access to common functions.

### Features

- 🖼️ Quick wallpaper selection
- ⏯️ Pause/Resume cycling
- ⏭️ Next/Previous controls
- 🎨 Shader browser
- ⚙️ Settings access
- 📊 Status display

### Starting the Tray

```bash
# Start system tray
neowall-tray

# Start with daemon
neowall daemon &
neowall-tray &
```

**Autostart:**

Create `~/.config/autostart/neowall.desktop`:
```desktop
[Desktop Entry]
Type=Application
Name=NeoWall
Exec=neowall daemon
X-GNOME-Autostart-enabled=true
```

Create `~/.config/autostart/neowall-tray.desktop`:
```desktop
[Desktop Entry]
Type=Application
Name=NeoWall Tray
Exec=neowall-tray
X-GNOME-Autostart-enabled=true
```

### Tray Menu

**Right-click menu:**
```
NeoWall
├── Current: wallpaper.png
├── ─────────────────────
├── 📁 Browse Wallpapers...
├── 🎨 Browse Shaders...
├── ─────────────────────
├── ⏯️ Pause Cycling
├── ⏮️ Previous
├── ⏭️ Next
├── ─────────────────────
├── ⚙️ Settings
├── 📊 Status
├── 🔄 Reload Config
├── ─────────────────────
└── ❌ Quit
```

### Tray Configuration

Configure tray behavior in config file:

```vibe
tray {
    # Enable/disable tray icon
    enable = true
    
    # Icon theme
    theme = "default"
    
    # Show notifications
    notifications = true
    
    # Update interval (ms)
    update_interval = 1000
}
```

---

## IPC Protocol

### Overview

NeoWall uses a JSON-based IPC protocol over Unix sockets for communication between the daemon and clients.

### Socket Location

Default socket: `/tmp/neowall-$UID.sock`

Custom socket:
```bash
NEOWALL_SOCKET=/custom/path.sock neowall daemon
```

### Message Format

**Request:**
```json
{
    "command": "command-name",
    "args": "{\"key\":\"value\"}"
}
```

**Response:**
```json
{
    "success": true,
    "message": "Operation successful",
    "data": "{...}"
}
```

### Available Commands

#### Status Commands

**Get Status:**
```json
{
    "command": "status"
}
```

Response:
```json
{
    "success": true,
    "data": "{
        \"running\": true,
        \"outputs\": [
            {
                \"name\": \"DP-1\",
                \"type\": \"static\",
                \"current\": \"/home/user/wallpaper.png\"
            }
        ]
    }"
}
```

**Get Info:**
```json
{
    "command": "info"
}
```

**List Outputs:**
```json
{
    "command": "list-outputs"
}
```

#### Wallpaper Commands

**Set Wallpaper:**
```json
{
    "command": "set-wallpaper",
    "args": "{\"path\":\"/home/user/wallpaper.png\"}"
}
```

With output:
```json
{
    "command": "set-wallpaper",
    "args": "{\"output\":\"DP-1\",\"path\":\"/home/user/wallpaper.png\"}"
}
```

**Set Shader:**
```json
{
    "command": "set-shader",
    "args": "{\"path\":\"/home/user/shader.glsl\"}"
}
```

**Next/Previous:**
```json
{
    "command": "next"
}
```

```json
{
    "command": "prev"
}
```

**Pause/Resume:**
```json
{
    "command": "pause"
}
```

```json
{
    "command": "resume"
}
```

#### Configuration Commands

**Get Config:**
```json
{
    "command": "get-config",
    "args": "{\"key\":\"wallpaper.mode\"}"
}
```

**Set Config:**
```json
{
    "command": "set-config",
    "args": "{\"key\":\"wallpaper.mode\",\"value\":\"fill\"}"
}
```

**Reset Config:**
```json
{
    "command": "reset-config",
    "args": "{\"key\":\"wallpaper.mode\"}"
}
```

**Reload Config:**
```json
{
    "command": "reload"
}
```

### Example Client (Python)

```python
#!/usr/bin/env python3
import socket
import json
import os

def send_ipc_command(command, args=None):
    # Connect to socket
    socket_path = f"/tmp/neowall-{os.getuid()}.sock"
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(socket_path)
    
    # Build request
    request = {"command": command}
    if args:
        request["args"] = json.dumps(args)
    
    # Send request
    sock.sendall(json.dumps(request).encode() + b'\n')
    
    # Receive response
    response = sock.recv(4096)
    sock.close()
    
    return json.loads(response)

# Example usage
result = send_ipc_command("set-wallpaper", {"path": "/home/user/wallpaper.png"})
print(result)

result = send_ipc_command("next")
print(result)

result = send_ipc_command("status")
print(json.loads(result["data"]))
```

### Example Client (Bash)

```bash
#!/bin/bash

# Send IPC command
send_command() {
    local cmd=$1
    local args=$2
    
    local socket="/tmp/neowall-$(id -u).sock"
    
    if [ -n "$args" ]; then
        echo "{\"command\":\"$cmd\",\"args\":$args}" | nc -U "$socket"
    else
        echo "{\"command\":\"$cmd\"}" | nc -U "$socket"
    fi
}

# Set wallpaper
send_command "set-wallpaper" '"{\"path\":\"/home/user/wallpaper.png\"}"'

# Get status
send_command "status"

# Next wallpaper
send_command "next"
```

---

## Advanced Features

### Hot Reload

NeoWall supports hot-reloading configuration without restart:

```bash
# Edit config file
vim ~/.config/neowall/config.vibe

# Reload
neowall reload
```

Changes take effect immediately for:
- Display modes
- Transition settings
- Shader parameters
- Cycling intervals
- Per-output configurations

### Custom Transitions

Define custom transition effects (advanced):

```vibe
transitions {
    custom-fade {
        type = "shader"
        duration = 1000
        shader = "~/.config/neowall/transitions/custom.glsl"
    }
}

default {
    path = "~/wallpaper.png"
    transition = "custom-fade"
}
```

### Multi-Display Scenarios

#### Clone displays (same wallpaper):
```vibe
default {
    path = "~/wallpaper.png"
    mode = "fill"
}
# All outputs use default
```

#### Extend displays (different wallpapers):
```vibe
output {
    DP-1 {
        path = "~/Pictures/left.png"
    }
    DP-2 {
        path = "~/Pictures/right.png"
    }
}
```

#### Mixed mode (static + shader):
```vibe
output {
    DP-1 {
        path = "~/wallpaper.png"
        mode = "fill"
    }
    DP-2 {
        shader = "~/shader.glsl"
        vsync = true
    }
}
```

### Performance Tuning

#### Low Power Mode

```vibe
default {
    shader = "~/shader.glsl"
    shader_fps = 15          # Very low FPS
    vsync = false
    shader_speed = 0.5       # Slow animation
}
```

#### High Performance Mode

```vibe
default {
    shader = "~/shader.glsl"
    shader_fps = 144         # High FPS
    vsync = false
    shader_speed = 1.0
}
```

#### Balanced Mode

```vibe
default {
    shader = "~/shader.glsl"
    vsync = true             # Match monitor refresh
    shader_speed = 1.0
}
```

### Scripting Integration

#### Change wallpaper based on time

```bash
#!/bin/bash
# time-based-wallpaper.sh

hour=$(date +%H)

if [ $hour -ge 6 -a $hour -lt 12 ]; then
    # Morning
    neowall set-wallpaper ~/Pictures/morning.png
elif [ $hour -ge 12 -a $hour -lt 18 ]; then
    # Afternoon
    neowall set-wallpaper ~/Pictures/afternoon.png
else
    # Evening/Night
    neowall set-wallpaper ~/Pictures/night.png
fi
```

Add to crontab:
```cron
0 * * * * /path/to/time-based-wallpaper.sh
```

#### Change based on battery status

```bash
#!/bin/bash
# battery-aware-wallpaper.sh

battery=$(cat /sys/class/power_supply/BAT0/capacity)

if [ $battery -lt 20 ]; then
    # Low battery - use static wallpaper
    neowall set-wallpaper ~/Pictures/wallpaper.png
else
    # Good battery - use shader
    neowall set-shader ~/shaders/plasma.glsl
fi
```

#### Integration with pywal

```bash
#!/bin/bash
# Use pywal colors with NeoWall

# Generate color scheme
wal -i ~/Pictures/wallpaper.png

# Set wallpaper in NeoWall
neowall set-wallpaper ~/Pictures/wallpaper.png

# Optional: Generate shader with pywal colors
# (requires custom shader that reads pywal colors)
```

---

## Troubleshooting

### Common Issues

#### 1. NeoWall won't start

**Symptoms:**
- Daemon exits immediately
- "Failed to connect to Wayland" error

**Solutions:**

1. Check Wayland is running:
   ```bash
   echo $WAYLAND_DISPLAY
   # Should output: wayland-0 or similar
   ```

2. Check compositor supports wlr-layer-shell:
   ```bash
   neowall check-compositor
   ```

3. Try running in foreground:
   ```bash
   neowall daemon --no-fork --verbose
   ```

4. Check logs:
   ```bash
   journalctl --user -u neowall -f
   ```

#### 2. Wallpaper not displaying

**Symptoms:**
- NeoWall runs but no wallpaper visible
- Black screen

**Solutions:**

1. Check compositor layer configuration:
   ```bash
   # For Hyprland, add to hyprland.conf:
   layerrule = blur, neowall
   layerrule = ignorezero, neowall
   ```

2. Verify file exists and is readable:
   ```bash
   ls -l ~/Pictures/wallpaper.png
   file ~/Pictures/wallpaper.png
   ```

3. Try different display mode:
   ```bash
   neowall set-config wallpaper.mode center
   ```

4. Check OpenGL support:
   ```bash
   glxinfo | grep "OpenGL version"
   ```

#### 3. Shader won't load

**Symptoms:**
- "Shader compilation failed" error
- Shader wallpaper shows black screen

**Solutions:**

1. Test shader compilation:
   ```bash
   neowall test-shader ~/shader.glsl
   ```

2. Check shader syntax:
   - Must be GLSL ES 3.0 (`#version 300 es`)
   - Must have `out vec4 fragColor`
   - Must define `main()` function

3. Check shader uniforms match NeoWall's:
   ```glsl
   uniform vec3 iResolution;
   uniform float iTime;
   // etc.
   ```

4. Enable debug mode:
   ```bash
   NEOWALL_DEBUG=3 neowall daemon
   ```

#### 4. High CPU/GPU usage

**Symptoms:**
- System feels slow
- High CPU usage by NeoWall
- GPU temperature high

**Solutions:**

1. Check current FPS:
   ```vibe
   default {
       shader = "~/shader.glsl"
       show_fps = true
   }
   ```

2. Reduce framerate:
   ```vibe
   default {
       shader = "~/shader.glsl"
       shader_fps = 30
       vsync = false
   }
   ```

3. Enable V-sync:
   ```vibe
   default {
       shader = "~/shader.glsl"
       vsync = true
   }
   ```

4. Simplify shader or use static wallpaper

#### 5. Config changes not applying

**Symptoms:**
- Edited config but nothing changed
- "Invalid configuration" errors

**Solutions:**

1. Validate config syntax:
   ```bash
   neowall validate-config ~/.config/neowall/config.vibe
   ```

2. Reload config:
   ```bash
   neowall reload
   ```

3. Check for validation errors:
   ```bash
   journalctl --user -u neowall | grep "ERROR\|INVALID"
   ```

4. Verify file permissions:
   ```bash
   ls -l ~/.config/neowall/config.vibe
   ```

#### 6. Crashes or segfaults

**Symptoms:**
- NeoWall crashes unexpectedly
- Segmentation fault errors

**Solutions:**

1. Update to latest version:
   ```bash
   git pull
   meson compile -C build
   sudo meson install -C build
   ```

2. Check for driver issues:
   ```bash
   # Update graphics drivers
   # Check compositor is stable
   ```

3. Run with debugger:
   ```bash
   gdb neowall
   run daemon --no-fork
   # When it crashes: bt
   ```

4. Report bug with backtrace

### Debug Mode

Enable maximum verbosity:

```bash
# Method 1: Command line
neowall daemon --debug

# Method 2: Environment variable
NEOWALL_DEBUG=3 neowall daemon

# Method 3: Log to file
neowall daemon --debug 2>&1 | tee neowall-debug.log
```

Debug levels:
- `0` - Errors only
- `1` - Warnings
- `2` - Info (default with --verbose)
- `3` - Debug (all messages)

### Log Files

**Location:**
- systemd: `journalctl --user -u neowall`
- Manual: Check where you redirected stderr

**Useful log commands:**
```bash
# Follow logs in real-time
journalctl --user -u neowall -f

# Show last 100 lines
journalctl --user -u neowall -n 100

# Filter errors only
journalctl --user -u neowall | grep ERROR

# Show logs since last boot
journalctl --user -u neowall -b
```

### Getting Help

1. **Check documentation:**
   - This manual
   - `docs/CONFIG_RULES.md`
   - `README.md`

2. **Search issues:**
   - https://github.com/1ay1/neowall/issues

3. **Ask for help:**
   - GitHub Discussions
   - Discord server
   - Matrix room

4. **Report bugs:**
   - Include NeoWall version: `neowall --version`
   - Include config file
   - Include debug logs
   - Include compositor name and version
   - Steps to reproduce

---

## FAQ

### General Questions

**Q: What compositors does NeoWall support?**

A: NeoWall works with any Wayland compositor that supports the `wlr-layer-shell` protocol. This includes Hyprland, Sway, River, Wayfire, and other wlroots-based compositors.

**Q: Can I use NeoWall on X11?**

A: No, NeoWall is Wayland-only. For X11, consider alternatives like `feh`, `nitrogen`, or `xwinwrap`.

**Q: Does NeoWall work on GNOME/KDE?**

A: GNOME and KDE Plasma use different protocols. NeoWall currently doesn't support them. There are compositor-specific alternatives for these desktop environments.

**Q: How much RAM/CPU does NeoWall use?**

A: Typical usage:
- Static wallpaper: ~5-10 MB RAM, negligible CPU
- Shader wallpaper: ~20-50 MB RAM, depends on shader complexity
- GPU usage depends on shader complexity and FPS

### Configuration Questions

**Q: Can I use both path and shader?**

A: No, they are mutually exclusive. Choose one per output. However, you can use path on one monitor and shader on another.

**Q: Where do I put my config file?**

A: `~/.config/neowall/config.vibe` or use `--config` flag.

**Q: How do I find my output names?**

A: Run `neowall list-outputs`

**Q: Can I use environment variables in config?**

A: Yes, use `~` for home directory. Full `$VAR` expansion is not currently supported.

**Q: What if I make a mistake in config?**

A: NeoWall validates config and shows helpful error messages. Fix the error and reload.

### Wallpaper Questions

**Q: What image formats are supported?**

A: PNG (always), JPEG (if enabled), WebP (if enabled). Check with `neowall --version`.

**Q: Can I use GIF or videos?**

A: No, use static images or shaders. For video-like effects, use shader wallpapers.

**Q: How do I create a slideshow?**

A: Use directory cycling:
```vibe
default {
    path = "~/Pictures/slideshow/"
    duration = 300
    transition = "fade"
}
```

**Q: Can NeoWall change my desktop wallpaper?**

A: NeoWall manages its own background layer. Desktop environment wallpaper settings are separate.

### Shader Questions

**Q: Where can I find shaders?**

A: Shadertoy.com, GLSL Sandbox, or write your own.

**Q: How do I convert Shadertoy shaders?**

A: See the "Writing Custom Shaders" section. You need to add NeoWall headers and adjust the code structure.

**Q: My shader is too slow/laggy**

A: Reduce `shader_fps`, enable `vsync`, or simplify the shader code.

**Q: Can I use Shadertoy inputs?**

A: Partial support:
- ✅ iResolution, iTime, iTimeDelta, iFrame
- ✅ iChannel0-3 (texture inputs)
- ❌ iMouse (not yet supported)
- ❌ Audio inputs (not supported)

**Q: Do shaders drain battery?**

A: Shaders use GPU continuously. For battery saving:
- Lower `shader_fps`
- Use simpler shaders
- Switch to static wallpaper when on battery

### Performance Questions

**Q: NeoWall is using too much CPU**

A: For shaders: lower `shader_fps` or enable `vsync`. For static: check if transition is running continuously.

**Q: Should I use vsync?**

A: Generally yes. Vsync prevents unnecessary rendering and saves power.

**Q: What's the best FPS setting?**

A: Match your monitor refresh rate or use vsync. For most users: `vsync = true` is best.

**Q: Can I pause NeoWall?**

A: Yes: `neowall pause` to pause, `neowall resume` to resume.

### Troubleshooting Questions

**Q: NeoWall won't start**

A: Check compositor supports wlr-layer-shell: `neowall check-compositor`

**Q: Wallpaper is black**

A: Check file path is correct, file is readable, and OpenGL is working.

**Q: Config changes don't apply**

A: Run `neowall reload` or restart daemon.

**Q: How do I reset to defaults?**

A: `neowall reset-config` or delete config file.

---

## Appendix

### A. Configuration Reference

#### Complete Config Example

```vibe
# NeoWall Complete Configuration Example

# Global defaults
default {
    # Static wallpaper
    path = "~/Pictures/wallpaper.png"
    mode = "fill"
    transition = "fade"
    transition_duration = 500
    
    # OR shader wallpaper (mutually exclusive with path)
    # shader = "~/shaders/plasma.glsl"
    # shader_speed = 1.0
    # shader_fps = 60
    # vsync = true
    # show_fps = false
    
    # Universal (works with both)
    duration = 300
}

# Per-output configuration
output {
    # Primary monitor - static with cycling
    DP-1 {
        path = "~/Pictures/collection/"
        duration = 600
        mode = "fill"
        transition = "fade"
        transition_duration = 1000
    }
    
    # Secondary monitor - shader
    HDMI-A-1 {
        shader = "~/shaders/waves.glsl"
        shader_speed = 1.5
        vsync = true
        show_fps = false
        
        channels = [
            "~/textures/noise.png"
        ]
    }
    
    # Laptop screen - low power
    eDP-1 {
        path = "~/Pictures/minimal.png"
        mode = "center"
    }
}

# Tray configuration
tray {
    enable = true
    theme = "default"
    notifications = true
    update_interval = 1000
}
```

#### All Configuration Keys

**Static Wallpaper Keys** (with `path`):
- `path` - Image file or directory path
- `mode` - Display mode: fill, fit, center, tile, stretch
- `transition` - Transition effect: none, fade, slide, wipe
- `transition_duration` - Transition time in milliseconds
- `duration` - Cycling interval in seconds (for directories)

**Shader Wallpaper Keys** (with `shader`):
- `shader` - Shader file or directory path
- `shader_speed` - Animation speed multiplier (0.1-10.0)
- `shader_fps` - Target framerate (1-240)
- `vsync` - Enable V-sync (true/false)
- `show_fps` - Show FPS counter (true/false)
- `channels` - Array of texture paths for iChannel0-3
- `duration` - Cycling interval in seconds (for directories)

**Universal Keys**:
- `duration` - Directory cycling interval (works with both)

### B. Command Reference

#### Complete Command List

```
Daemon Management:
  neowall daemon                    Start daemon
  neowall daemon --no-fork          Run in foreground
  neowall daemon --verbose          Verbose logging
  neowall daemon --debug            Debug mode

Wallpaper Control:
  neowall set-wallpaper PATH        Set static wallpaper
  neowall set-shader PATH           Set shader wallpaper
  neowall next                      Next wallpaper
  neowall prev                      Previous wallpaper
  neowall pause                     Pause cycling
  neowall resume                    Resume cycling
  neowall reset                     Reset to first

Output Management:
  neowall list-outputs              List outputs
  neowall status                    Show status
  neowall info                      Detailed info

Configuration:
  neowall get-config KEY            Get config value
  neowall set-config KEY VALUE      Set config value
  neowall reset-config [KEY]        Reset config
  neowall reload                    Reload config file
  neowall validate-config PATH      Validate config

Testing:
  neowall test-shader PATH          Test shader
  neowall check-compositor          Check support

Utility:
  neowall --version                 Show version
  neowall --help                    Show help
```

### C. Keyboard Shortcuts

NeoWall doesn't have built-in keyboard shortcuts, but you can bind commands in your compositor:

**Hyprland example** (`~/.config/hypr/hyprland.conf`):
```
# NeoWall shortcuts
bind = SUPER, W, exec, neowall next
bind = SUPER SHIFT, W, exec, neowall prev
bind = SUPER, P, exec, neowall pause
bind = SUPER SHIFT, P, exec, neowall resume
```

**Sway example** (`~/.config/sway/config`):
```
# NeoWall shortcuts
bindsym $mod+w exec neowall next
bindsym $mod+Shift+w exec neowall prev
bindsym $mod+p exec neowall pause
bindsym $mod+Shift+p exec neowall resume
```

### D. Systemd Integration

#### User Service

Create `~/.config/systemd/user/neowall.service`:
```ini
[Unit]
Description=NeoWall Wallpaper Manager
Documentation=https://github.com/1ay1/neowall
After=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/neowall daemon --no-fork
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

Enable and start:
```bash
systemctl --user enable neowall
systemctl --user start neowall
systemctl --user status neowall
```

#### With Tray

Create `~/.config/systemd/user/neowall-tray.service`:
```ini
[Unit]
Description=NeoWall System Tray
After=neowall.service
Requires=neowall.service

[Service]
Type=simple
ExecStart=/usr/bin/neowall-tray
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

### E. Shader Template

Basic shader template for NeoWall:

```glsl
#version 300 es
precision highp float;

// NeoWall uniforms
uniform vec3 iResolution;      // viewport resolution (in pixels)
uniform float iTime;           // shader playback time (in seconds)
uniform float iTimeDelta;      // render time (in seconds)
uniform int iFrame;            // shader playback frame
uniform sampler2D iChannel0;   // input channel 0
uniform sampler2D iChannel1;   // input channel 1
uniform sampler2D iChannel2;   // input channel 2
uniform sampler2D iChannel3;   // input channel 3

// Output
out vec4 fragColor;

// Main image function (Shadertoy compatible)
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Normalized pixel coordinates (from 0 to 1)
    vec2 uv = fragCoord / iResolution.xy;
    
    // Your shader code here
    vec3 color = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(iTime));
    
    // Output to screen
    fragColor = vec4(color, 1.0);
}

// Entry point
void main() {
    mainImage(fragColor, gl_FragCoord.xy);
}
```

### F. Resources

#### Official Links
- **GitHub:** https://github.com/1ay1/neowall
- **Documentation:** https://github.com/1ay1/neowall/tree/main/docs
- **Issue Tracker:** https://github.com/1ay1/neowall/issues

#### Community
- **Discord:** [Join Server]
- **Matrix:** [Join Room]
- **Reddit:** r/neowall

#### Shader Resources
- **Shadertoy:** https://www.shadertoy.com/
- **GLSL Sandbox:** https://glslsandbox.com/
- **The Book of Shaders:** https://thebookofshaders.com/
- **ShaderToy Tutorial:** https://inspirnathan.com/posts/47-shadertoy-tutorial-part-1

#### Wallpaper Sources
- **Unsplash:** https://unsplash.com/
- **Pexels:** https://www.pexels.com/
- **Wallhaven:** https://wallhaven.cc/
- **r/wallpapers:** https://reddit.com/r/wallpapers

### G. License

NeoWall is released under the MIT License.

```
MIT License

Copyright (c) 2024 NeoWall Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### H. Contributing

We welcome contributions! See `CONTRIBUTING.md` for guidelines.

**Areas for contribution:**
- Bug fixes
- Feature implementations
- Documentation improvements
- Shader examples
- Translations
- Testing

### I. Credits

**Created by:** [1ay1](https://github.com/1ay1)

**Contributors:** See [CONTRIBUTORS.md](https://github.com/1ay1/neowall/blob/main/CONTRIBUTORS.md)

**Special Thanks:**
- wlroots developers
- Wayland community
- Shadertoy creators
- All users and testers

---

## Quick Reference Card

### Essential Commands
```bash
neowall daemon              # Start daemon
neowall set-wallpaper PATH  # Set wallpaper
neowall next                # Next wallpaper
neowall list-outputs        # List monitors
neowall status              # Show status
```

### Basic Config
```vibe
default {
    path = "~/wallpaper.png"
    mode = "fill"
}
```

### Static vs Shader Rules
- ✅ Static: `path`, `mode`, `transition`
- ✅ Shader: `shader`, `shader_speed`, `show_fps`
- ✅ Both: `duration`
- ❌ Never: `path` + `shader` together

### Getting Help
- Manual: Read this file
- Help: `neowall --help`
- Issues: GitHub Issues
- Debug: `neowall daemon --debug`

---

**End of Manual**

For the latest version of this manual, visit:
https://github.com/1ay1/neowall/blob/main/docs/USER_MANUAL.md

Last updated: January 2025