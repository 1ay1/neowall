# Current Config Commands in NeoWall

**Documentation of existing configuration command capabilities**

---

## 📋 Overview

NeoWall currently has a comprehensive set of configuration commands for runtime control. These commands allow modifying settings via IPC, but **changes are NOT currently persisted** across daemon restarts.

---

## 🎯 Current Commands

### Generic Config Commands

#### `get-config` - Get Configuration Value

**Syntax:**
```bash
neowall get-config [key]
```

**Examples:**
```bash
# Get specific config value
neowall get-config general.cycle_interval

# Get all config (no key provided)
neowall get-config
```

**Status:** ✅ Implemented (read-only, temporary)

---

#### `set-config` - Set Configuration Value

**Syntax:**
```bash
neowall set-config <key> <value>
```

**Examples:**
```bash
# Set global settings
neowall set-config general.cycle_interval 600
neowall set-config general.wallpaper_mode fill
neowall set-config performance.shader_speed 1.5
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on daemon restart

---

#### `reset-config` - Reset Configuration

**Syntax:**
```bash
neowall reset-config <key|--all>
```

**Examples:**
```bash
# Reset specific key
neowall reset-config general.cycle_interval

# Reset all configuration
neowall reset-config --all
```

**Status:** ✅ Implemented
**Note:** Resets to built-in defaults, not config.vibe values

---

#### `list-config-keys` - List All Config Keys

**Syntax:**
```bash
neowall list-config-keys
```

**Status:** ✅ Implemented

---

#### `reload` - Reload Configuration

**Syntax:**
```bash
neowall reload
```

**Status:** ⚠️ Deprecated
**Note:** Returns error: "Config reload is deprecated. Use 'set-config' for runtime updates."

---

### Per-Output Commands

These commands modify output-specific settings:

#### `set-output-mode` - Set Display Mode

**Syntax:**
```bash
neowall set-output-mode <output> <mode>
```

**Config Key:** `output.<name>.mode`

**Modes:** `center`, `stretch`, `fit`, `fill`, `tile`

**Examples:**
```bash
neowall set-output-mode DP-1 fill
neowall set-output-mode HDMI-A-1 fit
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-duration` - Set Cycle Duration

**Syntax:**
```bash
neowall set-output-duration <output> <seconds>
```

**Config Key:** `output.<name>.duration`

**Examples:**
```bash
neowall set-output-duration DP-1 600    # 10 minutes
neowall set-output-duration HDMI-A-1 300  # 5 minutes
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-path` - Set Image Wallpaper

**Syntax:**
```bash
neowall set-output-path <output> <path>
```

**Config Key:** `output.<name>.path`

**Examples:**
```bash
neowall set-output-path DP-1 ~/Pictures/wallpaper.png
neowall set-output-path HDMI-A-1 ~/Pictures/Wallpapers/  # Directory
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-shader` - Set Shader Wallpaper

**Syntax:**
```bash
neowall set-output-shader <output> <shader>
```

**Config Key:** `output.<name>.shader`

**Examples:**
```bash
neowall set-output-shader DP-1 matrix_rain.glsl
neowall set-output-shader HDMI-A-1 plasma.glsl
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-shader-speed` - Set Shader Speed

**Syntax:**
```bash
neowall set-output-shader-speed <output> <speed>
```

**Config Key:** `output.<name>.shader_speed`

**Range:** 0.1 - 10.0

**Examples:**
```bash
neowall set-output-shader-speed DP-1 2.0    # 2x speed
neowall set-output-shader-speed HDMI-A-1 0.5  # Half speed
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-transition` - Set Transition Effect

**Syntax:**
```bash
neowall set-output-transition <output> <type>
```

**Config Key:** `output.<name>.transition`

**Types:** `none`, `fade`, `slide-left`, `slide-right`, `glitch`, `pixelate`

**Examples:**
```bash
neowall set-output-transition DP-1 fade
neowall set-output-transition HDMI-A-1 slide-left
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-output-transition-duration` - Set Transition Duration

**Syntax:**
```bash
neowall set-output-transition-duration <output> <milliseconds>
```

**Config Key:** `output.<name>.transition_duration`

**Examples:**
```bash
neowall set-output-transition-duration DP-1 500   # 0.5 seconds
neowall set-output-transition-duration HDMI-A-1 1000  # 1 second
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `list-outputs` - List All Outputs

**Syntax:**
```bash
neowall list-outputs
```

**Examples:**
```bash
neowall list-outputs
neowall --json list-outputs
```

**Status:** ✅ Implemented

---

### Default Section Commands

These commands set global defaults (apply to all outputs):

#### `set-default-path` - Set Default Image Path

**Syntax:**
```bash
neowall set-default-path <path>
```

**Config Key:** `default.path`

**Examples:**
```bash
neowall set-default-path ~/Pictures/wallpaper.png
neowall set-default-path ~/Pictures/Wallpapers/  # Directory
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-shader` - Set Default Shader

**Syntax:**
```bash
neowall set-default-shader <shader>
```

**Config Key:** `default.shader`

**Examples:**
```bash
neowall set-default-shader retro_wave.glsl
neowall set-default-shader plasma.glsl
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-mode` - Set Default Display Mode

**Syntax:**
```bash
neowall set-default-mode <mode>
```

**Config Key:** `default.mode`

**Modes:** `center`, `stretch`, `fit`, `fill`, `tile`

**Examples:**
```bash
neowall set-default-mode fill
neowall set-default-mode fit
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-duration` - Set Default Cycle Duration

**Syntax:**
```bash
neowall set-default-duration <seconds>
```

**Config Key:** `default.duration`

**Examples:**
```bash
neowall set-default-duration 600    # 10 minutes
neowall set-default-duration 300    # 5 minutes
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-shader-speed` - Set Default Shader Speed

**Syntax:**
```bash
neowall set-default-shader-speed <speed>
```

**Config Key:** `default.shader_speed`

**Range:** 0.1 - 10.0

**Examples:**
```bash
neowall set-default-shader-speed 2.0
neowall set-default-shader-speed 0.5
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-transition` - Set Default Transition

**Syntax:**
```bash
neowall set-default-transition <type>
```

**Config Key:** `default.transition`

**Types:** `none`, `fade`, `slide-left`, `slide-right`, `glitch`, `pixelate`

**Examples:**
```bash
neowall set-default-transition fade
neowall set-default-transition slide-left
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

#### `set-default-transition-duration` - Set Default Transition Duration

**Syntax:**
```bash
neowall set-default-transition-duration <milliseconds>
```

**Config Key:** `default.transition_duration`

**Examples:**
```bash
neowall set-default-transition-duration 500
neowall set-default-transition-duration 1000
```

**Status:** ✅ Implemented
**Persistence:** ❌ Changes lost on restart

---

## 📊 Command Summary Table

| Command | Scope | Config Key | Persistence | Status |
|---------|-------|------------|-------------|--------|
| `get-config` | Generic | Any | N/A | ✅ |
| `set-config` | Generic | Any | ❌ | ✅ |
| `reset-config` | Generic | Any | N/A | ✅ |
| `list-config-keys` | Generic | N/A | N/A | ✅ |
| `reload` | Generic | N/A | N/A | ⚠️ Deprecated |
| `set-output-mode` | Output | `output.mode` | ❌ | ✅ |
| `set-output-duration` | Output | `output.duration` | ❌ | ✅ |
| `set-output-path` | Output | `output.path` | ❌ | ✅ |
| `set-output-shader` | Output | `output.shader` | ❌ | ✅ |
| `set-output-shader-speed` | Output | `output.shader_speed` | ❌ | ✅ |
| `set-output-transition` | Output | `output.transition` | ❌ | ✅ |
| `set-output-transition-duration` | Output | `output.transition_duration` | ❌ | ✅ |
| `set-default-path` | Default | `default.path` | ❌ | ✅ |
| `set-default-shader` | Default | `default.shader` | ❌ | ✅ |
| `set-default-mode` | Default | `default.mode` | ❌ | ✅ |
| `set-default-duration` | Default | `default.duration` | ❌ | ✅ |
| `set-default-shader-speed` | Default | `default.shader_speed` | ❌ | ✅ |
| `set-default-transition` | Default | `default.transition` | ❌ | ✅ |
| `set-default-transition-duration` | Default | `default.transition_duration` | ❌ | ✅ |
| `list-outputs` | Output | N/A | N/A | ✅ |

---

## 🔍 Implementation Details

### Config Command Handler

Located in: `src/neowalld/commands/config_commands.c`

**Key Functions:**

```c
/* Generic config commands */
command_result_t cmd_get_config(struct neowall_state *state, ...);
command_result_t cmd_set_config(struct neowall_state *state, ...);
command_result_t cmd_reset_config(struct neowall_state *state, ...);
command_result_t cmd_list_config_keys(struct neowall_state *state, ...);

/* Default section commands */
command_result_t cmd_set_default_path(struct neowall_state *state, ...);
command_result_t cmd_set_default_shader(struct neowall_state *state, ...);
command_result_t cmd_set_default_mode(struct neowall_state *state, ...);
command_result_t cmd_set_default_duration(struct neowall_state *state, ...);
command_result_t cmd_set_default_shader_speed(struct neowall_state *state, ...);
command_result_t cmd_set_default_transition(struct neowall_state *state, ...);
command_result_t cmd_set_default_transition_duration(struct neowall_state *state, ...);
```

### Output Command Handler

Located in: `src/neowalld/commands/output_commands.c`

**Key Functions:**

```c
command_result_t cmd_set_output_mode(struct neowall_state *state, ...);
command_result_t cmd_set_output_duration(struct neowall_state *state, ...);
command_result_t cmd_set_output_path(struct neowall_state *state, ...);
command_result_t cmd_set_output_shader(struct neowall_state *state, ...);
command_result_t cmd_set_output_shader_speed(struct neowall_state *state, ...);
command_result_t cmd_set_output_transition(struct neowall_state *state, ...);
command_result_t cmd_set_output_transition_duration(struct neowall_state *state, ...);
command_result_t cmd_list_outputs(struct neowall_state *state, ...);
```

---

## 🚧 Current Limitations

### 1. No Persistence

**Problem:** All runtime changes are lost when daemon restarts

```bash
# Make changes
neowall set-output-shader DP-1 matrix.glsl
neowall set-output-shader-speed DP-1 2.0

# Restart daemon
neowall kill
neowall start

# Changes are GONE - reverts to config.vibe
```

**Workaround:** Manually edit `~/.config/neowall/config.vibe`

---

### 2. No Override Tracking

**Problem:** Can't tell what's been changed vs base config

```bash
# No way to see what's overridden
neowall get-config default.shader_speed
# Returns: 2.0
# But is this from config.vibe or a runtime change? Unknown!
```

**Workaround:** None - must remember what you changed

---

### 3. No Override Reset

**Problem:** `reset-config` resets to built-in defaults, not config.vibe

```bash
# config.vibe has: shader_speed 1.5
# Runtime change:
neowall set-config default.shader_speed 2.0

# Want to revert to config.vibe value (1.5):
neowall reset-config default.shader_speed
# Actually resets to built-in default (1.0) instead!
```

**Workaround:** Manually set back to config.vibe value

---

### 4. No Export

**Problem:** Can't export current merged config

```bash
# Want to save current runtime config to a file
neowall export-config > my-config.vibe
# Command doesn't exist!
```

**Workaround:** Manually reconstruct config

---

### 5. No Introspection

**Problem:** Can't see what's been overridden

```bash
# Want to see all runtime changes
neowall list-overrides
# Command doesn't exist!
```

**Workaround:** None

---

## 📝 Usage Examples

### Example 1: Multi-Monitor Setup

```bash
# Configure laptop display
neowall set-output-path eDP-1 ~/Pictures/laptop.png
neowall set-output-mode eDP-1 fit

# Configure external monitor
neowall set-output-shader DP-1 matrix_rain.glsl
neowall set-output-shader-speed DP-1 2.0

# Configure HDMI monitor
neowall set-output-path HDMI-A-1 ~/Pictures/Nature/
neowall set-output-duration HDMI-A-1 300
neowall set-output-transition HDMI-A-1 fade
```

**Result:** Changes apply immediately
**Problem:** All lost on daemon restart!

---

### Example 2: Changing Default Settings

```bash
# Set defaults for all monitors
neowall set-default-shader plasma.glsl
neowall set-default-shader-speed 1.5
neowall set-default-mode fill
```

**Result:** New defaults apply to all outputs without specific config
**Problem:** Lost on restart!

---

### Example 3: Quick Tweaking

```bash
# Speed up all shaders
neowall set-default-shader-speed 3.0

# Daemon restart
neowall kill && neowall start

# Speed is back to config.vibe value
# Must run command again!
```

---

## 🎯 What's Needed: Persistence Layer

To make these commands truly useful, we need:

1. **State File (`state.json`)** - Store runtime overrides
2. **Override Tracking** - Know what's been changed
3. **Merge on Load** - `config.vibe` + `state.json` overrides
4. **New Commands:**
   - `list-overrides` - Show all runtime changes
   - `reset-config <key>` - Revert to config.vibe (not built-in default)
   - `export-config` - Export merged config
   - `show-config-source` - Show where value comes from

See: [PERSISTENT_CONFIG_DESIGN.md](PERSISTENT_CONFIG_DESIGN.md) for full design.

---

## 🔧 Config Write API

Commands use: `src/neowalld/config/config_write.h`

**Key Functions:**

```c
/* Set config value (in-memory only, not persisted) */
bool config_set_value(
    struct neowall_state *state,
    const char *key,
    const char *value,
    char *error,
    size_t error_len
);

/* Get config value */
const char *config_get_value(
    struct neowall_state *state,
    const char *key
);

/* Reset to default */
bool config_reset_value(
    struct neowall_state *state,
    const char *key
);
```

**Note:** These only modify in-memory state, no disk writes!

---

## 📚 Related Documentation

- [Persistent Config Design](PERSISTENT_CONFIG_DESIGN.md) - Proposed persistence system
- [Config Rules System](CONFIG_RULES.md) - Validation rules
- [Config Schema](../src/neowalld/config/config_schema.h) - Config types and validation
- [Command Reference](commands/COMMANDS.md) - All available commands
- [IPC Protocol](../src/ipc/protocol.h) - Communication protocol

---

**Summary:** NeoWall has excellent runtime config command coverage, but lacks persistence. All changes are temporary and lost on restart. The persistence layer design in PERSISTENT_CONFIG_DESIGN.md addresses this.

---

**Made with ❤️ for the Linux desktop**