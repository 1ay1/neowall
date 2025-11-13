# NeoWall Command Design

## Philosophy: Runtime State vs Persistent Configuration

NeoWall follows a clear separation between **runtime commands** and **configuration commands**:

### Runtime Commands (Output Control)
These commands affect only the **current session state** and do NOT persist to disk:

- `list-outputs` - List all connected outputs and their current state
- `output-info <output>` - Show detailed information about a specific output
- `next-output <output>` - Cycle to next wallpaper in the current output's cycle list
- `prev-output <output>` - Cycle to previous wallpaper
- `jump-to-output <output> <index>` - Jump to specific wallpaper by index in cycle
- `reload-output <output>` - Reload the current wallpaper/shader
- `pause-output <output>` - Pause automatic wallpaper cycling (runtime only)
- `resume-output <output>` - Resume automatic wallpaper cycling

**Behavior:**
- Changes are immediate and temporary
- State is lost on daemon restart
- Perfect for quick experimentation and temporary adjustments
- Do NOT write to `config.vibe`

**Example workflow:**
```bash
# Temporarily switch to next wallpaper
neowall next-output DP-1

# Pause cycling for this session
neowall pause-output DP-1

# On daemon restart, settings revert to what's in config.vibe
```

### Configuration Commands (Persistent Settings)
These commands modify `config.vibe` and make changes **permanent**:

- `get-config <key>` - Read a config value using VIBE path notation
- `set-config <key> <value>` - Set a config value and persist to disk
- `reset-config <key|--all>` - Delete a config key (or all keys) and revert to defaults
- `list-config-keys` - List all configuration keys in the VIBE file

**Behavior:**
- Changes are written atomically to `config.vibe` (via temp file + rename)
- Daemon automatically reloads configuration after write
- Changes survive across restarts
- Use VIBE path notation (dot-separated paths like `output.DP-1.mode`)

**Example workflow:**
```bash
# Permanently set default shader for all outputs
neowall set-config default.shader "/path/to/shader.glsl"

# Configure specific output permanently
neowall set-config output.DP-1.path "/path/to/wallpaper.jpg"
neowall set-config output.DP-1.mode "fill"

# View current persistent setting
neowall get-config output.DP-1.mode

# Remove output-specific setting (reverts to default)
neowall reset-config output.DP-1.mode
```

## VIBE Path Notation

Configuration commands use **dot-notation** to address nested config values:

```
default.shader               → config.default.shader
default.mode                 → config.default.mode
output.DP-1.path             → config.output["DP-1"].path
output.HDMI-1.shader_speed   → config.output["HDMI-1"].shader_speed
```

Leaf values can be:
- **Strings:** `"value"` or `value` (quotes optional for simple strings)
- **Integers:** `42`
- **Floats:** `3.14`
- **Booleans:** `true`, `false`

## Design Benefits

### 1. **Single Source of Truth**
- `config.vibe` is the canonical persistent state
- No duplication between command definitions and config keys
- Generic commands operate on any config path

### 2. **Clear Mental Model**
Users understand:
- "Output commands" = temporary, runtime-only changes
- "Config commands" = permanent, persisted changes

### 3. **Safe Experimentation**
Users can:
- Try different settings with runtime commands
- Only persist when satisfied using `set-config`
- Easily revert by restarting daemon

### 4. **Simplified Command Surface**
- Before: ~19 specific commands like `set-output-mode`, `set-default-shader`, etc.
- After: 4 generic config commands that work with any VIBE path
- Easier to maintain and extend

## Implementation Notes

### Automatic Reload
When `set-config` or `reset-config` modifies `config.vibe`, the daemon:
1. Writes changes atomically (temp file → fsync → rename)
2. Calls `config_load(state, config_path)` to reload
3. Applies new configuration to all outputs
4. Returns success/error to client

No manual `reload-config` command is needed - it happens automatically.

### Runtime State Drift
Runtime and persistent state can diverge:

```bash
# config.vibe says: output.DP-1.path = "/wallpaper1.jpg"
# User runs: neowall next-output DP-1
# Runtime state now shows /wallpaper2.jpg

# On daemon restart → reverts to /wallpaper1.jpg (from config.vibe)
```

This is **intentional** - runtime commands are for temporary overrides.

### Pause/Resume Duration Handling
**Current limitation:** `resume-output` hardcodes a 300s default duration instead of reading from config.

**Recommended fix:**
```c
command_result_t cmd_resume_output(...) {
    // ... find output ...
    
    if (output->config.cycle && output->config.duration == 0.0f) {
        // Read duration from config.vibe instead of hardcoding
        // Ideally: call config_load or query config.vibe for this output's duration
        // For now: keep simple hardcoded default
        output->config.duration = 300.0f;
    }
    // ...
}
```

Better approach: Store original duration in output state before pause, restore on resume.

## Future Enhancements

### 1. Schema Validation (Recommended)
Currently `set-config` does basic type parsing but doesn't enforce:
- Mutual exclusivity rules (e.g., `path` XOR `shader`)
- Value constraints (e.g., `mode` only valid for images)
- Range validation (e.g., `shader_speed > 0`)

**TODO:** Integrate `config_validate_rules()` before writing to reject invalid changes.

### 2. Config Backups
Consider writing `config.vibe.bak` before each `set-config` for easy rollback:
```bash
neowall set-config output.DP-1.shader "/new/shader.glsl"  # creates backup
# If broken, restore: cp ~/.config/neowall/config.vibe.bak ~/.config/neowall/config.vibe
```

### 3. Transaction Support
For atomic multi-key updates:
```bash
neowall begin-transaction
neowall set-config output.DP-1.shader "/shader.glsl"
neowall set-config output.DP-1.shader_speed "2.0"
neowall commit-transaction  # Single atomic write + reload
```

### 4. Runtime-Only Override Layer (Optional)
If desired, add a separate override mechanism:
- `config.vibe` = base persistent config
- `overrides.vibe` = runtime-only overrides (ephemeral, not committed)
- Allows "save this session" vs "save permanently" distinction

**Note:** Current design deliberately avoids this complexity in favor of simplicity.

## Migration from Old Commands

Old specific commands have been **removed**:

| Old Command | New Equivalent |
|-------------|----------------|
| `set-output-mode <output> <mode>` | `set-config output.<output>.mode <mode>` |
| `set-output-duration <output> <dur>` | `set-config output.<output>.duration <dur>` |
| `set-default-shader <path>` | `set-config default.shader <path>` |
| `set-default-mode <mode>` | `set-config default.mode <mode>` |

**Why removed?**
- Redundant with generic `set-config`
- Creates maintenance burden (duplicate logic)
- Harder to discover (need to know specific command names)
- Generic commands work for ALL config keys, not just predefined ones

## Summary

| Aspect | Runtime Commands | Config Commands |
|--------|------------------|-----------------|
| **Persistence** | Session only | Survives restarts |
| **Scope** | Output control | Any VIBE path |
| **Examples** | `next-output`, `pause-output` | `set-config`, `get-config` |
| **File I/O** | None | Writes `config.vibe` |
| **Use Case** | Quick experiments | Permanent settings |

This design provides:
✅ Clear separation of concerns  
✅ Predictable behavior (no surprise persistence)  
✅ Flexibility (VIBE paths work for any config)  
✅ Simplicity (fewer commands to remember)  
✅ Safety (runtime changes are temporary by default)