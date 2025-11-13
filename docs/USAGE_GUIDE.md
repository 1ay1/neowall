# NeoWall IPC Commands - Complete Usage Guide

## 📖 Table of Contents

1. [Quick Start](#quick-start)
2. [Starting the Daemon](#starting-the-daemon)
3. [Basic Commands](#basic-commands)
4. [Wallpaper Control](#wallpaper-control)
5. [Shader Control](#shader-control)
6. [System Commands](#system-commands)
7. [Command Reference](#command-reference)
8. [JSON Response Format](#json-response-format)
9. [Troubleshooting](#troubleshooting)
10. [Examples](#examples)

---

## Quick Start

```bash
# Build the project
meson setup builddir
ninja -C builddir

# Terminal 1: Start the daemon
./builddir/bin/neowalld

# Terminal 2: Control the wallpaper
./builddir/bin/neowall status
./builddir/bin/neowall next
./builddir/bin/neowall shader-pause
```

---

## Starting the Daemon

### Manual Start (Development)
```bash
# Start in foreground (see logs)
./builddir/bin/neowalld

# Start in background
./builddir/bin/neowalld &

# Or use nohup to keep running after logout
nohup ./builddir/bin/neowalld > /tmp/neowalld.log 2>&1 &
```

### Using the Client (Future)
```bash
# Once implemented:
./builddir/bin/neowall start    # Start daemon
./builddir/bin/neowall stop     # Stop daemon
./builddir/bin/neowall restart  # Restart daemon
```

### Check if Running
```bash
# Try to ping the daemon
./builddir/bin/neowall ping

# Check daemon status
./builddir/bin/neowall status

# Or use ps
ps aux | grep neowalld
```

---

## Basic Commands

### ping - Health Check
Test if the daemon is responding.

```bash
$ ./builddir/bin/neowall ping
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "pong"
  }
}
```

**Use case:** Check if daemon is running before sending other commands.

---

### version - Version Information
Get daemon version and protocol version.

```bash
$ ./builddir/bin/neowall version
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "version": "0.3.0",
    "protocol": "1.0"
  }
}
```

**Use case:** Verify daemon version, check compatibility.

---

### list - Available Commands
List all available commands with metadata.

```bash
$ ./builddir/bin/neowall list
```

**Response:**
```json
{
  "status": "ok",
  "commands": [
    {
      "name": "next",
      "category": "wallpaper",
      "description": "Switch to next wallpaper in cycle"
    },
    {
      "name": "status",
      "category": "system",
      "description": "Get current daemon status"
    }
    // ... more commands
  ]
}
```

**Use case:** Discover available commands, build UI/scripts.

---

### status - Daemon Status
Get comprehensive daemon state information.

```bash
$ ./builddir/bin/neowall status
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "daemon": "running",
    "pid": 12345,
    "outputs": 2,
    "paused": false,
    "shader_paused": false,
    "shader_speed": 1.00
  }
}
```

**Fields:**
- `daemon` - Daemon state ("running")
- `pid` - Process ID
- `outputs` - Number of connected displays
- `paused` - Is wallpaper cycling paused?
- `shader_paused` - Are shader animations paused?
- `shader_speed` - Current shader speed multiplier

**Use case:** Check daemon state, display in status bar, debugging.

---

### current - Current Wallpaper Info
Get detailed information about current wallpapers on all outputs.

```bash
$ ./builddir/bin/neowall current
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "outputs": [
      {
        "name": "HDMI-A-1",
        "type": "image",
        "path": "/home/user/wallpapers/mountain.jpg",
        "mode": 3,
        "cycle_index": 2,
        "cycle_total": 10
      },
      {
        "name": "DP-1",
        "type": "shader",
        "path": "/home/user/shaders/plasma.glsl",
        "mode": 0,
        "cycle_index": 0,
        "cycle_total": 1
      }
    ]
  }
}
```

**Fields:**
- `name` - Output/display name (e.g., HDMI-A-1)
- `type` - Wallpaper type ("image" or "shader")
- `path` - Full path to current wallpaper/shader
- `mode` - Display mode (0=center, 1=stretch, 2=fit, 3=fill, 4=tile)
- `cycle_index` - Current position in cycle (0-based)
- `cycle_total` - Total wallpapers in cycle

**Use case:** Show current wallpaper in UI, check what's displayed.

---

## Wallpaper Control

### next - Next Wallpaper
Advance to the next wallpaper in the cycle.

```bash
$ ./builddir/bin/neowall next
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Switched to next wallpaper"
  }
}
```

**Behavior:**
- Increments cycle index for all outputs with cycling enabled
- Wraps around to first wallpaper when reaching the end
- Respects per-output configuration
- Synchronized across outputs with same config

**Use case:** Skip wallpaper you don't like, test cycling.

---

### prev - Previous Wallpaper
Go back to the previous wallpaper in the cycle.

```bash
$ ./builddir/bin/neowall prev
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Switched to previous wallpaper"
  }
}
```

**Behavior:**
- Decrements cycle index
- Wraps around to last wallpaper when at the beginning
- Useful for going back to a wallpaper you liked

**Use case:** Go back to previous wallpaper.

---

### pause - Pause Wallpaper Cycling
Stop automatic wallpaper changes.

```bash
$ ./builddir/bin/neowall pause
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Paused wallpaper cycling"
  }
}
```

**Behavior:**
- Stops automatic timer-based cycling
- Current wallpaper remains displayed
- Manual `next`/`prev` commands still work
- Does NOT pause shader animations (use `shader-pause` for that)

**Use case:** Keep current wallpaper, temporarily disable cycling.

---

### resume - Resume Wallpaper Cycling
Resume automatic wallpaper changes.

```bash
$ ./builddir/bin/neowall resume
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Resumed wallpaper cycling"
  }
}
```

**Behavior:**
- Restarts automatic timer-based cycling
- Uses duration from config file
- Wallpaper will change after configured interval

**Use case:** Re-enable automatic cycling after pausing.

---

## Shader Control

### speed-up - Increase Shader Speed
Increase shader animation speed by 0.25x.

```bash
$ ./builddir/bin/neowall speed-up
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "shader_speed": 1.25
  }
}
```

**Behavior:**
- Increases global shader speed multiplier by 0.25x per call
- Maximum speed: 10.0x
- Affects all shader wallpapers on all outputs
- Multiplies with per-output shader_speed from config
- Effective speed = config.shader_speed × global shader_speed

**Use case:** Make shader animations faster, test at different speeds.

**Examples:**
```bash
# Make shader 2x faster
./builddir/bin/neowall speed-up  # 1.25x
./builddir/bin/neowall speed-up  # 1.50x
./builddir/bin/neowall speed-up  # 1.75x
./builddir/bin/neowall speed-up  # 2.00x
```

---

### speed-down - Decrease Shader Speed
Decrease shader animation speed by 0.25x.

```bash
$ ./builddir/bin/neowall speed-down
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "shader_speed": 0.75
  }
}
```

**Behavior:**
- Decreases global shader speed multiplier by 0.25x per call
- Minimum speed: 0.1x
- Slows down all shader animations

**Use case:** Slow down fast shaders, create slow-motion effect.

**Examples:**
```bash
# Make shader run at half speed
./builddir/bin/neowall speed-down  # 0.75x
./builddir/bin/neowall speed-down  # 0.50x
```

---

### shader-pause - Pause Shader Animation
Freeze shader animations (freeze time).

```bash
$ ./builddir/bin/neowall shader-pause
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Paused shader animation"
  }
}
```

**Behavior:**
- Freezes shader time uniform (iTime)
- Shader continues to render but doesn't animate
- Creates a "freeze frame" effect
- Saves GPU power if shader is compute-intensive

**Use case:** 
- Pause distracting animations
- Reduce CPU/GPU usage
- Create screenshot-friendly static frame

**Note:** Does NOT pause wallpaper cycling. Use `pause` for that.

---

### shader-resume - Resume Shader Animation
Unfreeze shader animations.

```bash
$ ./builddir/bin/neowall shader-resume
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "message": "Resumed shader animation"
  }
}
```

**Behavior:**
- Unfreezes shader time
- Animation continues from where it was paused
- Time continues incrementing normally

**Use case:** Resume animations after pausing.

---

## System Commands

### reload - Reload Configuration
**DEPRECATED:** This command has been removed.

```bash
$ ./builddir/bin/neowall reload
```

**Response:**
```json
{
  "status": "error",
  "message": "Reload not supported. Please restart the daemon: neowall restart"
}
```

**Why removed?**
- Hot-reload adds complexity with minimal benefit
- Config changes are rare for wallpaper daemons
- Restart is simple and reliable
- Brief interruption (1-2 seconds) is acceptable

**How to reload config:**
```bash
# Stop daemon
pkill neowalld
# Or: ./builddir/bin/neowall stop (when implemented)

# Edit config
vim ~/.config/neowall/config.toml

# Start daemon
./builddir/bin/neowalld &
# Or: ./builddir/bin/neowall start (when implemented)
```

**Future:** `neowall restart` command will automate this.

---

## Command Reference

### Quick Reference Table

| Command | Category | Description | State Modified |
|---------|----------|-------------|----------------|
| `ping` | System | Health check | None |
| `version` | System | Version info | None |
| `list` | System | List commands | None |
| `status` | System | Daemon status | None |
| `current` | Wallpaper | Current wallpaper info | None |
| `next` | Wallpaper | Next wallpaper | `next_requested` |
| `prev` | Wallpaper | Previous wallpaper | `prev_requested` |
| `pause` | Wallpaper | Pause cycling | `paused` |
| `resume` | Wallpaper | Resume cycling | `paused` |
| `speed-up` | Shader | Increase shader speed | `shader_speed` |
| `speed-down` | Shader | Decrease shader speed | `shader_speed` |
| `shader-pause` | Shader | Pause animations | `shader_paused` |
| `shader-resume` | Shader | Resume animations | `shader_paused` |

### Command Categories

**System (4):** ping, version, list, status  
**Wallpaper Control (5):** next, prev, pause, resume, current  
**Shader Control (4):** speed-up, speed-down, shader-pause, shader-resume  

---

## JSON Response Format

All commands return JSON with consistent structure.

### Success Response
```json
{
  "status": "ok",
  "data": {
    // Command-specific data or message
  }
}
```

### Error Response
```json
{
  "status": "error",
  "message": "Human-readable error message"
}
```

### Response Fields
- `status` - Always present: "ok" or "error"
- `data` - Present on success: command-specific data
- `message` - Present on error: error description

---

## Troubleshooting

### Daemon Not Responding
```bash
$ ./builddir/bin/neowall ping
# Error or no response
```

**Solutions:**
1. Check if daemon is running: `ps aux | grep neowalld`
2. Check socket exists: `ls -la $XDG_RUNTIME_DIR/neowalld.sock`
3. Check daemon logs
4. Restart daemon: `pkill neowalld && ./builddir/bin/neowalld &`

---

### Commands Have No Effect
```bash
$ ./builddir/bin/neowall next
# Returns OK but wallpaper doesn't change
```

**Possible causes:**
1. No cycling configured in config file
2. Only one wallpaper in cycle
3. Event loop not wired to handle command (implementation incomplete)

**Debug:**
```bash
# Check current config
./builddir/bin/neowall current

# Check status
./builddir/bin/neowall status

# Check daemon logs for errors
```

---

### Socket Permission Denied
```bash
$ ./builddir/bin/neowall ping
# Permission denied
```

**Solution:**
- Socket created with restrictive permissions
- Only user who started daemon can connect
- This is intentional for security

---

### Command Not Found
```bash
$ ./builddir/bin/neowall foobar
# Unknown command
```

**Solution:**
- Use `./builddir/bin/neowall list` to see available commands
- Check spelling

---

## Examples

### Basic Workflow
```bash
# Start daemon
./builddir/bin/neowalld &

# Check it's running
./builddir/bin/neowall ping

# See current wallpaper
./builddir/bin/neowall current

# Try different wallpapers
./builddir/bin/neowall next
sleep 2
./builddir/bin/neowall next
sleep 2
./builddir/bin/neowall prev

# Keep this wallpaper
./builddir/bin/neowall pause
```

---

### Shader Speed Control
```bash
# Check current speed
./builddir/bin/neowall status | grep shader_speed

# Make shader 2x faster
./builddir/bin/neowall speed-up
./builddir/bin/neowall speed-up
./builddir/bin/neowall speed-up
./builddir/bin/neowall speed-up

# Slow it down
./builddir/bin/neowall speed-down
./builddir/bin/neowall speed-down
```

---

### Pause Everything
```bash
# Pause wallpaper cycling
./builddir/bin/neowall pause

# Pause shader animation
./builddir/bin/neowall shader-pause

# Check status
./builddir/bin/neowall status
# Should show: "paused": true, "shader_paused": true
```

---

### Shell Script Integration
```bash
#!/bin/bash
# neowall-control.sh - Control wallpaper from script

case "$1" in
    status)
        ./builddir/bin/neowall status | jq '.data'
        ;;
    next)
        ./builddir/bin/neowall next
        echo "Switched to next wallpaper"
        ;;
    shuffle)
        # Shuffle to random wallpaper
        for i in {1..10}; do
            ./builddir/bin/neowall next
            sleep 0.1
        done
        ;;
    freeze)
        ./builddir/bin/neowall pause
        ./builddir/bin/neowall shader-pause
        echo "Everything paused"
        ;;
    *)
        echo "Usage: $0 {status|next|shuffle|freeze}"
        exit 1
        ;;
esac
```

---

### Waybar Integration
```json
// ~/.config/waybar/config
{
  "custom/neowall": {
    "exec": "~/scripts/waybar-neowall.sh",
    "interval": 5,
    "on-click": "neowall next",
    "on-click-right": "neowall pause"
  }
}
```

```bash
#!/bin/bash
# ~/scripts/waybar-neowall.sh

STATUS=$(./builddir/bin/neowall status 2>/dev/null)
if [ $? -ne 0 ]; then
    echo '{"text": "❌", "tooltip": "Daemon not running"}'
    exit 0
fi

PAUSED=$(echo "$STATUS" | jq -r '.data.paused')
SHADER_PAUSED=$(echo "$STATUS" | jq -r '.data.shader_paused')

if [ "$PAUSED" = "true" ]; then
    ICON="⏸️"
else
    ICON="▶️"
fi

echo "{\"text\": \"$ICON\", \"tooltip\": \"Paused: $PAUSED, Shader: $SHADER_PAUSED\"}"
```

---

### i3/Sway Keybindings
```
# ~/.config/sway/config

# Wallpaper control
bindsym $mod+n exec ~/builddir/bin/neowall next
bindsym $mod+Shift+n exec ~/builddir/bin/neowall prev
bindsym $mod+p exec ~/builddir/bin/neowall pause
bindsym $mod+Shift+p exec ~/builddir/bin/neowall resume

# Shader control
bindsym $mod+bracketright exec ~/builddir/bin/neowall speed-up
bindsym $mod+bracketleft exec ~/builddir/bin/neowall speed-down
bindsym $mod+s exec ~/builddir/bin/neowall shader-pause
bindsym $mod+Shift+s exec ~/builddir/bin/neowall shader-resume
```

---

## Advanced Usage

### Query Status in Scripts
```bash
#!/bin/bash

# Get current wallpaper path
WALLPAPER=$(./builddir/bin/neowall current | jq -r '.data.outputs[0].path')
echo "Current wallpaper: $WALLPAPER"

# Check if paused
IS_PAUSED=$(./builddir/bin/neowall status | jq -r '.data.paused')
if [ "$IS_PAUSED" = "true" ]; then
    echo "Cycling is paused"
else
    echo "Cycling is active"
fi
```

---

### Conditional Commands
```bash
#!/bin/bash
# Only change wallpaper if not paused

STATUS=$(./builddir/bin/neowall status)
PAUSED=$(echo "$STATUS" | jq -r '.data.paused')

if [ "$PAUSED" = "false" ]; then
    ./builddir/bin/neowall next
    echo "Changed wallpaper"
else
    echo "Paused - not changing wallpaper"
fi
```

---

### Monitor Multiple Outputs
```bash
#!/bin/bash
# Show current wallpaper on each output

./builddir/bin/neowall current | jq -r '.data.outputs[] | "\(.name): \(.path)"'

# Output:
# HDMI-A-1: /home/user/wallpapers/mountain.jpg
# DP-1: /home/user/shaders/plasma.glsl
```

---

## Tips & Tricks

### Rapid Cycling Through Wallpapers
```bash
# Quickly browse wallpapers
while true; do
    ./builddir/bin/neowall next
    sleep 2
done
```

### Reset Shader Speed
```bash
# Get current speed
CURRENT=$(./builddir/bin/neowall status | jq -r '.data.shader_speed')

# Calculate steps to get back to 1.0
# (manual calculation needed)

# Or just restart daemon for default 1.0
```

### Test All Commands
```bash
#!/bin/bash
# Test suite for all commands

echo "Testing neowall commands..."

./builddir/bin/neowall ping && echo "✓ ping"
./builddir/bin/neowall version && echo "✓ version"
./builddir/bin/neowall status && echo "✓ status"
./builddir/bin/neowall current && echo "✓ current"
./builddir/bin/neowall next && echo "✓ next"
./builddir/bin/neowall prev && echo "✓ prev"
./builddir/bin/neowall pause && echo "✓ pause"
./builddir/bin/neowall resume && echo "✓ resume"
./builddir/bin/neowall speed-up && echo "✓ speed-up"
./builddir/bin/neowall speed-down && echo "✓ speed-down"
./builddir/bin/neowall shader-pause && echo "✓ shader-pause"
./builddir/bin/neowall shader-resume && echo "✓ shader-resume"

echo "All commands tested!"
```

---

## Future Commands (Not Yet Implemented)

These commands are planned but not yet available:

- `start` - Start daemon (currently broken)
- `stop` - Stop daemon gracefully
- `restart` - Restart daemon
- `set-wallpaper <path>` - Set specific wallpaper
- `add-wallpaper <path>` - Add wallpaper to cycle
- `remove-wallpaper <path>` - Remove from cycle
- `shuffle` - Randomize wallpaper order
- `info` - Detailed system info
- `outputs` - List connected outputs

---

## See Also

- `COMMANDS_IMPLEMENTED.md` - Implementation status
- `IMPLEMENTATION_TODO.md` - Remaining work
- `IMPLEMENTATION_SUMMARY.md` - Technical details
- `README.md` - Project overview
- Config file documentation

---

## Support

If commands don't work as expected:

1. Check daemon is running: `./builddir/bin/neowall ping`
2. Check daemon logs for errors
3. Verify config file is valid
4. Check implementation status in `COMMANDS_IMPLEMENTED.md`
5. Some features may not be fully wired yet (see TODO files)

---

**Version:** 0.3.0  
**Protocol:** 1.0  
**Last Updated:** 2024