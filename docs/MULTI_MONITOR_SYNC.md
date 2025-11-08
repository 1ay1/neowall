# Multi-Monitor Synchronization

## Overview

This document describes the multi-monitor synchronization features in NeoWall, which ensure that outputs (monitors) with identical configurations stay perfectly synchronized during wallpaper cycling and transitions.

## Features

### 1. Synchronized Cycle Index at Startup

When NeoWall starts or reloads configuration, outputs with identical wallpaper lists automatically synchronize their starting positions.

**Priority Order:**
1. **Sync with other outputs**: Check if another output has the same configuration and use its current index
2. **Restore from state file**: Load the saved index from the persistent state file
3. **Default to index 0**: Start from the beginning if no sync or state is available

**Example:**
```
Monitor 1: [wall1.jpg, wall2.jpg, wall3.jpg] - already at index 2
Monitor 2: [wall1.jpg, wall2.jpg, wall3.jpg] - starting up
Result: Monitor 2 synchronizes to index 2, both show wall3.jpg
```

### 2. Synchronized "Next" Command

When you run `neowall next`, ALL outputs with matching configurations advance together.

**Before:**
```bash
neowall next  # Only one monitor advances, they go out of sync
Monitor 1: index 3 -> 4
Monitor 2: index 3 -> stays at 3 (next frame) -> 4
```

**After:**
```bash
neowall next  # All matching monitors advance simultaneously
Monitor 1: index 3 -> 4
Monitor 2: index 3 -> 4
Result: Both monitors stay perfectly synchronized
```

### 3. Per-Output Persistent State

Each output maintains its own persistent state in the state file, including:
- Current wallpaper/shader path
- Display mode
- Cycle index
- Cycle count
- Status
- Timestamp

**State File Format:**
```
[output]
name=DP-1
wallpaper=/path/to/wallpaper.jpg
mode=fill
cycle_index=5
cycle_total=10
status=active
timestamp=1234567890

[output]
name=HDMI-1
wallpaper=/path/to/wallpaper.jpg
mode=fill
cycle_index=5
cycle_total=10
status=active
timestamp=1234567890
```

### 4. Transition State Management for Multi-Monitor

OpenGL state is properly managed across multiple monitors to prevent rendering artifacts during transitions.

**Key Improvements:**
- Error clearing before each transition render
- Proper vertex attribute setup for each draw call
- State cleanup after transitions
- Independent transition timing per output

## Configuration Matching

Two outputs are considered to have "matching configuration" if:
1. Both have cycling enabled (`cycle = true`)
2. Both have the same number of wallpapers (`cycle_count`)
3. All wallpaper paths match in the same order

**Example Matching Configs:**
```toml
[output.DP-1]
cycle = true
paths = [
    "~/Pictures/wall1.jpg",
    "~/Pictures/wall2.jpg",
    "~/Pictures/wall3.jpg"
]

[output.HDMI-1]
cycle = true
paths = [
    "~/Pictures/wall1.jpg",
    "~/Pictures/wall2.jpg",
    "~/Pictures/wall3.jpg"
]
# These will stay synchronized!
```

**Example Non-Matching Configs:**
```toml
[output.DP-1]
cycle = true
paths = [
    "~/Pictures/wall1.jpg",
    "~/Pictures/wall2.jpg"
]

[output.HDMI-1]
cycle = true
paths = [
    "~/Pictures/wall1.jpg",
    "~/Pictures/wall3.jpg"  # Different wallpaper
]
# These will NOT synchronize (different paths)
```

## Technical Implementation

### Synchronization Algorithm

**At Startup/Reload:**
```c
for each output being configured:
    if output has cycling enabled:
        // Priority 1: Sync with existing outputs
        for each other output:
            for each config slot (active and inactive):
                if config matches this output's config:
                    use that output's current_cycle_index
                    break
        
        // Priority 2: Restore from state file
        if not synced:
            index = restore_cycle_index_from_state(output_name)
        
        // Priority 3: Default
        if still not set:
            index = 0
```

**During "Next" Command:**
```c
when next_requested > 0:
    first_cycleable_output = find_first_output_with_cycling()
    
    for each output:
        if output has same config as first_cycleable_output:
            cycle_wallpaper(output)
    
    decrement next_requested
```

### State File Management

The state file is managed with file locking to prevent corruption:
- Uses `pthread_mutex` for thread-safe access
- Atomic read-modify-write operations
- All outputs' states stored in a single file
- INI-style format with `[output]` sections

### Transition Context API

New high-level API for transition effects:

```c
transition_context_t ctx;

// Automatically handles:
// - Error clearing
// - Viewport setup
// - Vertex attribute management
// - Buffer binding
// - State cleanup
if (!transition_begin(&ctx, output, program)) {
    return false;
}

// Draw with automatic state management
transition_draw_textured_quad(&ctx, texture, alpha, vertices);

// Automatic cleanup
transition_end(&ctx);
```

**Benefits:**
- No manual OpenGL state management in transitions
- Consistent behavior across monitors
- Prevents state leakage between monitors
- Simplifies transition implementation

## Usage Examples

### Synchronized Dual Monitor Setup

```toml
[output.DP-1]
cycle = true
duration = 300
paths = [
    "~/Wallpapers/nature1.jpg",
    "~/Wallpapers/nature2.jpg",
    "~/Wallpapers/nature3.jpg"
]
transition = "slide_left"
mode = "fill"

[output.HDMI-1]
cycle = true
duration = 300
paths = [
    "~/Wallpapers/nature1.jpg",
    "~/Wallpapers/nature2.jpg",
    "~/Wallpapers/nature3.jpg"
]
transition = "slide_left"
mode = "fill"
```

**Result:**
- Both monitors show the same wallpaper at startup
- Both monitors transition simultaneously every 5 minutes
- `neowall next` advances both monitors together
- Transitions are smooth on both monitors

### Independent Monitor Setup

```toml
[output.DP-1]
cycle = true
paths = ["~/Wallpapers/landscape/*.jpg"]
transition = "fade"

[output.HDMI-1]
cycle = true
paths = ["~/Wallpapers/abstract/*.jpg"]
transition = "glitch"
```

**Result:**
- Each monitor has its own wallpaper set
- Each monitor cycles independently
- `neowall next` advances each independently
- Each maintains its own state

## Troubleshooting

### Monitors Not Synchronizing

**Check if configs match exactly:**
```bash
neowall current
```

Look for the cycle configuration - paths must match exactly.

**Delete state file to reset:**
```bash
rm ~/.local/state/neowall/state
neowall reload
```

### Transitions Glitching

If you see OpenGL errors during transitions:
1. Check GPU driver is up to date
2. Verify OpenGL ES support: `eglinfo | grep ES`
3. Check logs: `journalctl -u neowall`

### State File Issues

**View current state:**
```bash
cat ~/.local/state/neowall/state
```

**Expected format:**
```
[output]
name=<output-name>
wallpaper=<path>
mode=<mode>
cycle_index=<index>
cycle_total=<count>
status=active
timestamp=<unix-time>
```

## Performance Considerations

- Synchronization checks happen only during configuration (startup/reload)
- No performance impact during normal rendering
- State file I/O is minimal (only on wallpaper changes)
- Transition state management has negligible overhead

## Future Enhancements

Possible future improvements:
- Option to force desynchronization
- Group-based synchronization (sync monitors A+B, but not C)
- Network synchronization across multiple machines
- Visual indicator of synchronized outputs

## See Also

- [Transition API Documentation](TRANSITION_API.md)
- [Multi-Monitor Improvements](MULTI_MONITOR_IMPROVEMENTS.md)
- [Configuration Guide](../config/docs/CONFIG.md)