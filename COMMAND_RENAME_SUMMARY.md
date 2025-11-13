# Command Rename Summary - Config Key Alignment

## Overview

NeoWall commands have been renamed to **exactly match** their corresponding configuration keys for consistency and ease of use.

---

## ✅ Changes Made

### 1. Output Commands Renamed

| Old Command | New Command | Config Key | Status |
|-------------|-------------|------------|--------|
| `set-output-interval` | `set-output-duration` | `output.duration` | ✅ Renamed |
| `set-output-wallpaper` | `set-output-path` | `output.path` | ✅ Renamed (for images) |
| - | `set-output-shader` | `output.shader` | ✅ New (for shaders) |

### 2. New Output Commands Added

These commands now match the full config schema:

| New Command | Config Key | Description |
|-------------|------------|-------------|
| `set-output-shader` | `output.shader` | Set shader wallpaper for specific output |
| `set-output-shader-speed` | `output.shader_speed` | Set shader animation speed for specific output |
| `set-output-transition` | `output.transition` | Set transition effect for specific output |
| `set-output-transition-duration` | `output.transition_duration` | Set transition duration for specific output |

### 3. New Default (Global) Commands Added

These commands set default values that apply to all outputs:

| Command | Config Key | Description |
|---------|------------|-------------|
| `set-default-path` | `default.path` | Set default image wallpaper path |
| `set-default-shader` | `default.shader` | Set default shader wallpaper |
| `set-default-mode` | `default.mode` | Set default display mode |
| `set-default-duration` | `default.duration` | Set default cycle duration |
| `set-default-shader-speed` | `default.shader_speed` | Set default shader speed |
| `set-default-transition` | `default.transition` | Set default transition effect |
| `set-default-transition-duration` | `default.transition_duration` | Set default transition duration |

---

## 📖 Command Naming Convention

The new naming convention is **consistent and predictable**:

```
Config Key Format:       <section>.<setting>
Command Name Format:     set-<section>-<setting>

Examples:
  output.duration        → set-output-duration
  output.shader_speed    → set-output-shader-speed
  default.path           → set-default-path
  default.transition     → set-default-transition
```

**Rules**:
- Dots (`.`) become hyphens (`-`)
- Underscores (`_`) become hyphens (`-`)
- Prefix with `set-` for modification commands
- Section name comes after `set-`

---

## 🔄 Migration Guide

### Before (Old Commands)

```bash
# Set cycle interval
neowall set-output-interval DP-1 600

# Set wallpaper (ambiguous - image or shader?)
neowall set-output-wallpaper DP-1 ~/wallpaper.png
neowall set-output-wallpaper DP-1 shader.glsl
```

### After (New Commands)

```bash
# Set cycle duration (matches config key: output.duration)
neowall set-output-duration DP-1 600

# Set image wallpaper (matches config key: output.path)
neowall set-output-path DP-1 ~/wallpaper.png

# Set shader wallpaper (matches config key: output.shader)
neowall set-output-shader DP-1 shader.glsl
```

---

## 💡 Benefits

### 1. **Consistency**
Commands now **exactly match** config file keys, making it easy to remember:
```bash
# Config file
output {
  DP-1 {
    duration 600
    shader_speed 2.0
  }
}

# Equivalent commands
neowall set-output-duration DP-1 600
neowall set-output-shader-speed DP-1 2.0
```

### 2. **Clarity**
Separate commands for images vs shaders removes ambiguity:
- `set-output-path` - clearly for images
- `set-output-shader` - clearly for shaders

### 3. **Completeness**
All config keys now have corresponding commands:
- **Before**: Only 3 output commands
- **After**: 10+ output commands covering all settings

### 4. **Discoverability**
Users can easily find commands by looking at config keys:
```bash
# See config key in file
default.shader_speed 2.0

# Know exactly which command to use
neowall set-default-shader-speed 2.0
```

---

## 📋 Complete Command List

### Output Commands (Per-Monitor)

```bash
neowall set-output-path <output> <path>                    # output.path
neowall set-output-shader <output> <shader>                # output.shader
neowall set-output-mode <output> <mode>                    # output.mode
neowall set-output-duration <output> <seconds>             # output.duration
neowall set-output-shader-speed <output> <speed>           # output.shader_speed
neowall set-output-transition <output> <type>              # output.transition
neowall set-output-transition-duration <output> <ms>       # output.transition_duration
```

### Default Commands (Global)

```bash
neowall set-default-path <path>                            # default.path
neowall set-default-shader <shader>                        # default.shader
neowall set-default-mode <mode>                            # default.mode
neowall set-default-duration <seconds>                     # default.duration
neowall set-default-shader-speed <speed>                   # default.shader_speed
neowall set-default-transition <type>                      # default.transition
neowall set-default-transition-duration <ms>               # default.transition_duration
```

### Generic Command (Still Available)

```bash
# For any config key (useful for scripting or advanced keys)
neowall set-config <key> <value> [--output <name>]
```

---

## 🎯 Usage Examples

### Example 1: Configure a Multi-Monitor Setup

```bash
# Laptop display: Static image with custom mode
neowall set-output-path eDP-1 ~/Pictures/laptop.png
neowall set-output-mode eDP-1 fit

# Monitor 1: Animated shader with faster speed
neowall set-output-shader DP-1 matrix_rain.glsl
neowall set-output-shader-speed DP-1 2.0

# Monitor 2: Cycling wallpapers with smooth transitions
neowall set-output-path HDMI-A-1 ~/Pictures/Nature/
neowall set-output-duration HDMI-A-1 300
neowall set-output-transition HDMI-A-1 fade
neowall set-output-transition-duration HDMI-A-1 500
```

### Example 2: Set Global Defaults

```bash
# Set defaults that apply to all monitors
neowall set-default-shader plasma.glsl
neowall set-default-shader-speed 1.5
neowall set-default-mode fill
```

### Example 3: Mix Old and New Commands

```bash
# Generic set-config still works for any key
neowall set-config output.vsync true --output DP-1
neowall set-config default.show_fps false

# New specific commands are more intuitive
neowall set-output-shader DP-1 cyberpunk.glsl
```

---

## 🔧 Implementation Details

### Files Modified

1. **`src/neowalld/commands/output_commands.c`**
   - Renamed: `cmd_set_output_interval` → `cmd_set_output_duration`
   - Renamed: `cmd_set_output_wallpaper` → `cmd_set_output_path`
   - Added: `cmd_set_output_shader`
   - Added: `cmd_set_output_shader_speed`
   - Added: `cmd_set_output_transition`
   - Added: `cmd_set_output_transition_duration`

2. **`src/neowalld/commands/config_commands.c`**
   - Added: `cmd_set_default_path`
   - Added: `cmd_set_default_shader`
   - Added: `cmd_set_default_mode`
   - Added: `cmd_set_default_duration`
   - Added: `cmd_set_default_shader_speed`
   - Added: `cmd_set_default_transition`
   - Added: `cmd_set_default_transition_duration`

3. **`README.md`**
   - Updated examples to use new command names
   - Added notes about config key alignment

4. **`docs/CONFIG_COMMAND_MAPPING.md`** (NEW)
   - Complete reference mapping config keys to commands
   - Usage examples
   - Migration guide

### Command Registry

All new commands are registered in the command registry with:
- Proper category tags (`output`, `config`)
- Clear descriptions mentioning config key alignment
- JSON argument schemas
- Example invocations

---

## ⚠️ Backward Compatibility

### Deprecated Commands

| Deprecated Command | Use Instead |
|-------------------|-------------|
| `set-output-interval` | `set-output-duration` |
| `set-output-wallpaper` (generic) | `set-output-path` (images) or `set-output-shader` (shaders) |

**Note**: Old commands may be removed in a future version. Update your scripts!

### Generic `set-config` Command

The generic `set-config` command **remains available** and works with all config keys:

```bash
# This still works
neowall set-config output.duration 600 --output DP-1

# But this is now clearer and matches the config key
neowall set-output-duration DP-1 600
```

---

## 📚 Related Documentation

- [CONFIG_COMMAND_MAPPING.md](docs/CONFIG_COMMAND_MAPPING.md) - Complete config-to-command reference
- [CONFIG_RULES.md](docs/CONFIG_RULES.md) - Configuration validation rules
- [commands/COMMANDS.md](docs/commands/COMMANDS.md) - All available commands
- [USAGE_GUIDE.md](docs/USAGE_GUIDE.md) - User manual

---

## ✨ Summary

**Before**: Commands had inconsistent naming and incomplete coverage of config options.

**After**: Every config key has a corresponding command with **matching naming**, making NeoWall more intuitive and scriptable.

**Result**: Users can now easily translate between config file syntax and CLI commands without guessing or consulting documentation.

---

**Questions?** See [CONFIG_COMMAND_MAPPING.md](docs/CONFIG_COMMAND_MAPPING.md) or open an issue on GitHub.