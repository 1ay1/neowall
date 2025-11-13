# NeoWall Complete CLI Control

## Overview

NeoWall now provides **complete control** over the daemon through the CLI, including per-monitor management, configuration queries, and advanced wallpaper control. This document describes all available commands and their usage.

## Architecture

The CLI control system is organized into modular command categories:

- **Daemon Management** - Start, stop, restart the daemon
- **Global Wallpaper Control** - Control all outputs simultaneously
- **Output-Specific Control** - Per-monitor wallpaper management
- **Shader Control** - Animation speed and state management
- **Configuration** - Query and introspect configuration
- **System Information** - Status, version, and diagnostics

### Modular Command Implementation

The daemon command system is organized into separate modules:

```
src/neowalld/commands/
├── commands.h           # Core command API and types
├── registry.c           # Command registration and dispatch
├── output_commands.c    # Per-output control (NEW)
├── output_commands.h
├── config_commands.c    # Configuration queries (NEW)
└── config_commands.h
```

## Command Categories

### 1. Daemon Management

**Start the daemon:**
```bash
neowall start
```

**Stop the daemon:**
```bash
neowall stop
```

**Restart the daemon:**
```bash
neowall restart
```

**Get daemon status:**
```bash
neowall status
neowall --json status    # JSON output for scripting
```

---

### 2. Global Wallpaper Control

These commands affect **all outputs** simultaneously.

**Next wallpaper (all outputs):**
```bash
neowall next
```

**Previous wallpaper (all outputs):**
```bash
neowall prev
```

**Pause wallpaper cycling (all outputs):**
```bash
neowall pause
```

**Resume wallpaper cycling (all outputs):**
```bash
neowall resume
```

**Show current wallpaper information:**
```bash
neowall current
neowall --json current    # JSON output
```

---

### 3. Output-Specific Control

Control individual monitors/outputs independently.

#### List Outputs

**List all connected outputs:**
```bash
neowall list-outputs
neowall --json list-outputs
```

Example output:
```json
{
  "outputs": [
    {
      "name": "DP-1",
      "model": "Dell U2720Q",
      "width": 3840,
      "height": 2160,
      "scale": 2,
      "wallpaper_type": "image",
      "wallpaper_path": "/home/user/wallpapers/mountain.jpg",
      "mode": "fill",
      "cycle_index": 2,
      "cycle_total": 10
    }
  ]
}
```

#### Output Information

**Get detailed info about a specific output:**
```bash
neowall output-info <output-name>
neowall output-info DP-1
neowall output-info HDMI-A-1
```

#### Wallpaper Navigation

**Next wallpaper on specific output:**
```bash
neowall next-output <output-name>
neowall next-output DP-1
```

**Previous wallpaper on specific output:**
```bash
neowall prev-output <output-name>
neowall prev-output HDMI-A-1
```

**Reload current wallpaper:**
```bash
neowall reload-output <output-name>
neowall reload-output DP-1
```

#### Advanced Control

**Set display mode for output:**
```bash
neowall set-output-mode <output-name> <mode>
neowall set-output-mode DP-1 fill
neowall set-output-mode HDMI-A-1 fit
```

Available modes: `fill`, `fit`, `center`, `stretch`, `tile`

**Set wallpaper/shader for output:**
```bash
neowall set-output-wallpaper <output-name> <path>
neowall set-output-wallpaper DP-1 /path/to/wallpaper.jpg
neowall set-output-wallpaper HDMI-A-1 /path/to/shader.glsl
```

**Set cycle interval for output:**
```bash
neowall set-output-interval <output-name> <seconds>
neowall set-output-interval DP-1 300      # 5 minutes
neowall set-output-interval HDMI-A-1 60   # 1 minute
```

**Jump to specific cycle index:**
```bash
neowall jump-output <output-name> <index>
neowall jump-output DP-1 5      # Jump to 6th wallpaper (0-indexed)
```

---

### 4. Shader Control

Control shader animation globally across all outputs.

**Increase shader animation speed:**
```bash
neowall speed-up
```

**Decrease shader animation speed:**
```bash
neowall speed-down
```

**Pause shader animations:**
```bash
neowall shader-pause
```

**Resume shader animations:**
```bash
neowall shader-resume
```

---

### 5. Configuration Management

Query runtime configuration (read-only - edit config file to make changes).

**Get configuration value:**
```bash
neowall get-config [key]
neowall get-config                          # Get all config
neowall get-config general.cycle_interval   # Get specific key
```

**List all configuration keys:**
```bash
neowall list-config-keys
```

**Edit configuration file:**
```bash
neowall config
```

After editing, restart the daemon to apply changes:
```bash
neowall restart
```

---

### 6. System Information

**Show version:**
```bash
neowall version
```

**Show help:**
```bash
neowall help
```

---

## JSON Output

All commands support `--json` flag for machine-readable output:

```bash
neowall --json list-outputs
neowall --json output-info DP-1
neowall --json status
```

JSON response format:
```json
{
  "status": "ok",           // or "error"
  "message": "OK",          // Human-readable message
  "data": {                 // Command-specific data
    // ...
  }
}
```

---

## Usage Examples

### Multi-Monitor Workflow

```bash
# List all connected monitors
neowall list-outputs

# Set different wallpapers for each monitor
neowall set-output-wallpaper DP-1 ~/wallpapers/ultrawide.jpg
neowall set-output-wallpaper HDMI-A-1 ~/wallpapers/vertical.png

# Set different modes
neowall set-output-mode DP-1 fill
neowall set-output-mode HDMI-A-1 fit

# Control each monitor independently
neowall next-output DP-1        # Change only left monitor
neowall prev-output HDMI-A-1    # Change only right monitor
```

### Scripting Examples

**Monitor wallpaper slideshow script:**
```bash
#!/bin/bash
# Cycle through wallpapers on specific output every 10 seconds

OUTPUT="DP-1"
while true; do
    neowall next-output $OUTPUT
    sleep 10
done
```

**Multi-monitor synchronized cycling:**
```bash
#!/bin/bash
# Advance all monitors simultaneously

for output in $(neowall --json list-outputs | jq -r '.data.data.outputs[].name'); do
    neowall next-output $output
done
```

**Status monitoring:**
```bash
#!/bin/bash
# Display current wallpaper on all outputs

neowall --json list-outputs | jq -r '.data.data.outputs[] | "\(.name): \(.wallpaper_path)"'
```

---

## Implementation Details

### IPC Command Flow

1. **Client** (`neowall` CLI) sends JSON command via Unix socket
2. **Daemon** (`neowalld`) receives command and dispatches to handler
3. **Handler** executes command and returns response
4. **Client** displays result (text or JSON)

### Command Handler Modules

**Output Commands** (`output_commands.c`):
- Direct wallpaper loading via `output_set_wallpaper()` / `output_set_shader()`
- Updates `current_cycle_index` and `last_cycle_time`
- Handles both image and shader detection
- Thread-safe with `output_list_lock`

**Config Commands** (`config_commands.c`):
- Read-only configuration introspection
- Returns config metadata and current values
- Does not modify runtime config (restart required for changes)

### Key Functions

```c
// Output control
void output_set_wallpaper(struct output_state *output, const char *path);
void output_set_shader(struct output_state *output, const char *shader_path);
struct output_state *find_output_by_name(struct neowall_state *state, const char *name);

// Utilities
bool extract_output_name(const char *args_json, char *output_name, size_t size);
bool extract_json_int(const char *args_json, const char *key, int *value);
bool extract_json_string(const char *args_json, const char *key, char *value, size_t size);
```

---

## Command Schema

All commands are defined in `schema/commands.vibe` with metadata:

```vibe
output {
  next-output {
    description "Switch to next wallpaper on specific output"
    handler cmd_next_output
    requires_state true
    modifies_state true
    
    args {
      output {
        type string
        required true
      }
    }
  }
}
```

---

## Testing

**Verify output-specific commands work:**
```bash
# Get initial state
neowall output-info DP-1

# Change wallpaper
neowall next-output DP-1

# Verify it changed
neowall output-info DP-1
```

**Test multi-monitor control:**
```bash
# List all outputs
neowall --json list-outputs | jq -r '.data.data.outputs[].name'

# Control each independently
neowall next-output DP-1
neowall next-output HDMI-A-1
```

---

## Troubleshooting

**Command not recognized:**
```bash
neowall help                # List all commands
neowall list-config-keys    # Verify config command works
```

**Output name not found:**
```bash
neowall list-outputs        # Get correct output names
```

**Changes don't apply:**
- Ensure daemon is running: `neowall status`
- Check daemon logs for errors
- Verify wallpaper/shader path exists

---

## Future Enhancements

Planned features:
- **Runtime config modification** - Set config values without restart
- **Pause/resume per output** - Currently uses duration workaround
- **Batch commands** - Execute multiple commands atomically
- **Wallpaper preloading control** - Force preload next wallpaper
- **Output groups** - Control multiple outputs as a group

---

## Related Documentation

- [IPC Protocol](IPC_MIGRATION.md) - Low-level IPC details
- [System Tray](TRAY.md) - GUI control interface
- [Configuration](../config/docs/CONFIG.md) - Config file format
- [Multi-Monitor Sync](MULTI_MONITOR_SYNC.md) - Synchronized wallpaper behavior

---

**Version:** 0.4.0  
**Last Updated:** 2024-11-13  
**Author:** NeoWall Development Team