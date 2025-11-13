# NeoWall Configuration & State Architecture

**Understanding how Config, State, and Settings work together**

---

## 🎯 Overview

NeoWall has three key concepts that work together:

1. **Config File** - Persistent user preferences on disk
2. **Runtime State** - Current running state in memory
3. **Settings Dialog** - GUI for editing config

Understanding how these interact is crucial for proper implementation.

---

## 📚 The Three Layers

### 1. Config File (`config.vibe`)

**Location:** `~/.config/neowall/config.vibe`

**Purpose:** Persistent storage of user preferences

**Format:** VIBE (custom configuration format)

**Contains:**
- Wallpaper folders/paths
- Cycle duration
- Display mode (fill/fit/center/etc.)
- Shader speed
- Default settings

**Example:**
```vibe
default {
    shader /home/user/.config/neowall/shaders/
    duration 60
    mode fill
    shader_speed 1.0
}

DP-1 {
    image /home/user/Pictures/Wallpapers/
    duration 30
    mode fit
}
```

**Key Points:**
- ✅ Read at daemon startup
- ✅ Read when `reload` command is issued
- ✅ Written by `set-config` commands
- ❌ NOT changed by runtime state (e.g., pressing next/prev)
- ❌ NOT changed by pause/resume (except old buggy behavior we fixed)

---

### 2. Runtime State (In-Memory)

**Location:** `struct neowall_state` in daemon memory

**Purpose:** Current running state of the daemon

**Contains:**
- Current wallpaper for each output
- Current cycle index (which wallpaper in the cycle)
- Paused state (cycling paused? animation paused?)
- Current shader speed
- Loaded textures/shaders
- Output information

**Key Points:**
- ✅ Initialized from config file at startup
- ✅ Changes during operation (next/prev/pause/resume)
- ✅ May be partially persisted to state files
- ❌ Lost when daemon stops (unless persisted)

---

### 3. State Files (Persistent State)

**Location:** `~/.cache/neowall/state/`

**Purpose:** Remember current state across daemon restarts

**Contains:**
- Current wallpaper path for each output
- Current cycle index
- Display mode
- Status info

**Example:** `~/.cache/neowall/state/DP-1.state`
```json
{
  "output": "DP-1",
  "wallpaper": "/path/to/current.jpg",
  "mode": "fill",
  "cycle_index": 5,
  "cycle_total": 20,
  "status": "active"
}
```

**Key Points:**
- ✅ Written when wallpaper changes
- ✅ Read at startup to restore previous state
- ✅ Allows daemon to remember where it left off
- ❌ NOT the same as config.vibe

---

## 🔄 How They Work Together

### Startup Flow

```
1. Daemon starts
   ↓
2. Read config.vibe → Parse user preferences
   ↓
3. Create runtime state from config
   ↓
4. Read state files (if exist) → Restore previous position
   ↓
5. Apply wallpapers and start running
```

### Configuration Change Flow

```
User edits Settings Dialog
   ↓
Settings Dialog calls: neowall set-config <key> <value>
   ↓
Daemon receives set-config command
   ↓
Daemon writes to config.vibe file
   ↓
Daemon reloads config.vibe (or user calls 'reload')
   ↓
Runtime state updated from new config
   ↓
Changes take effect
```

### Runtime Operation Flow

```
User clicks "Next Wallpaper"
   ↓
Tray executes: neowall next
   ↓
Daemon increments cycle_index in runtime state
   ↓
Daemon loads next wallpaper from cycle
   ↓
Daemon writes new state to state file
   ↓
(config.vibe unchanged - it still has same settings)
```

---

## 🏗️ Best Practice Architecture

### Rule 1: Config is for PREFERENCES, State is for CURRENT

**Config (Preferences):**
- Wallpaper folder: `/home/user/Pictures/Wallpapers/`
- Cycle duration: `60 seconds`
- Display mode: `fill`

**State (Current):**
- Current wallpaper: `/home/user/Pictures/Wallpapers/image_005.jpg`
- Current cycle index: `5`
- Currently paused: `false`

### Rule 2: User Settings → Config File → Reload → State

```
[Settings Dialog]
    ↓ (writes via set-config)
[config.vibe]
    ↓ (reload command)
[Runtime State]
    ↓ (takes effect)
[Wallpaper Display]
```

**Example:**
```bash
# User changes cycle duration in Settings Dialog to 120 seconds
neowall set-config "default.duration" 120

# Daemon writes to config.vibe
# Then daemon reloads config (automatic or manual)
neowall reload

# New duration now active in runtime state
```

### Rule 3: Runtime Changes DON'T Modify Config

```bash
# User pauses cycling
neowall cycle-pause

# Runtime state: paused = true
# config.vibe: UNCHANGED (still has duration = 60)
# State file: may record paused = true

# After daemon restart:
# - Reads config.vibe (duration = 60)
# - Reads state file (paused = true)
# - Resumes with original config but remembers paused state
```

---

## 🐛 Common Mistakes (What NOT To Do)

### ❌ Mistake 1: Modifying Config for Runtime State

**BAD:**
```c
// User presses pause
config.duration = 0;  // DON'T modify config!
write_config_file();
```

**GOOD:**
```c
// User presses pause
state->paused = true;  // Modify runtime state
write_state_file();    // Persist state
```

**Why:** Config is user preferences. Pause is runtime state. Don't confuse them!

### ❌ Mistake 2: Not Reloading After Config Change

**BAD:**
```c
// Settings dialog writes to config.vibe
write_config_file();
// ... nothing happens, old config still active
```

**GOOD:**
```c
// Settings dialog writes to config.vibe
write_config_file();
// Reload to apply changes
reload_config();
```

### ❌ Mistake 3: Losing State on Reload

**BAD:**
```c
// Reload config
load_config();
cycle_index = 0;  // Reset to start! User loses position!
```

**GOOD:**
```c
// Reload config
old_index = cycle_index;  // Remember current position
load_config();
cycle_index = old_index;  // Restore position
```

---

## 📋 Implementation Checklist

### Config Management ✅
- [x] Parse config.vibe at startup
- [x] Reload config on demand (`reload` command)
- [x] Write config via `set-config` commands
- [x] Validate config before applying

### State Management ✅
- [x] Initialize state from config
- [x] Update state during runtime (next/prev/pause/resume)
- [x] Write state files when wallpaper changes
- [x] Read state files at startup to restore position

### Settings Dialog 🚧
- [x] Create GUI with tabs (Wallpaper/Animation/Advanced)
- [x] Display current settings
- [ ] **TODO: Read current config values from daemon**
- [ ] **TODO: Write changes via set-config commands**
- [ ] **TODO: Trigger reload after applying changes**

---

## 🔧 Settings Dialog Implementation Plan

### Phase 1: Read Current Config (TODO)

```c
void settings_dialog_show(void) {
    // 1. Query daemon for current config
    char output[4096];
    command_execute_with_output("get-config default.duration", output, sizeof(output));
    
    // 2. Parse response and populate widgets
    int duration = parse_duration(output);
    gtk_spin_button_set_value(duration_spin, duration);
    
    // ... repeat for all settings
}
```

### Phase 2: Apply Changes (TODO)

```c
void apply_settings(SettingsWidgets *widgets) {
    // 1. Get values from widgets
    double duration = gtk_spin_button_get_value(widgets->duration_spin);
    
    // 2. Build set-config commands
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "set-config \"default.duration\" %.0f", duration);
    
    // 3. Execute commands
    command_execute(cmd);
    
    // 4. Reload config to apply changes
    command_execute("reload");
    
    // 5. Show confirmation
    dialog_show_info("Settings Applied", "Configuration has been updated.", 2000);
}
```

### Phase 3: Handle Errors (TODO)

```c
void apply_settings(SettingsWidgets *widgets) {
    // Validate before applying
    if (duration < 0 || duration > 3600) {
        dialog_show_error("Invalid Value", "Duration must be 0-3600 seconds");
        return;
    }
    
    // Apply with error handling
    if (!command_execute(cmd)) {
        dialog_show_error("Failed", "Could not apply settings. Check logs.");
        return;
    }
    
    // Success
    dialog_show_info("Success", "Settings applied successfully.", 2000);
}
```

---

## 🎓 Understanding The Difference

### Example Scenario: User Changes Cycle Duration

**Via Config File (Manual Edit):**
```bash
# 1. User edits ~/.config/neowall/config.vibe
vim ~/.config/neowall/config.vibe
# Changes: duration 60 → duration 120

# 2. User reloads config
neowall reload

# 3. New duration active
# Old: Next wallpaper in 60 seconds
# New: Next wallpaper in 120 seconds
```

**Via Settings Dialog (GUI):**
```bash
# 1. User opens Settings → Wallpaper tab
# 2. Changes "Cycle Duration" from 60 to 120
# 3. Clicks "Apply"

# Behind the scenes:
neowall set-config "default.duration" 120
neowall reload

# 4. New duration active immediately
```

**Via Runtime Command (Temporary):**
```bash
# User pauses cycling
neowall cycle-pause

# This is RUNTIME state, not config!
# - Runtime: paused = true
# - Config: duration = 60 (unchanged)
# - After restart: unpaused (unless state file says otherwise)
```

---

## 🚀 Summary

### The Golden Rules

1. **Config = What user wants** (preferences, settings)
2. **State = What's currently happening** (current wallpaper, position)
3. **Settings Dialog = Easy way to edit config**
4. **Runtime commands = Temporary state changes**

### Data Flow

```
User Preferences (config.vibe)
    ↓ [startup/reload]
Runtime State (in memory)
    ↓ [operations]
Current Display (screen)
    ↓ [persist]
State Files (.cache)
```

### When To Use What

| Action | Modifies | Persisted In |
|--------|----------|--------------|
| Set wallpaper folder | Config | config.vibe |
| Set cycle duration | Config | config.vibe |
| Set display mode | Config | config.vibe |
| Next wallpaper | State | state file |
| Pause cycling | State | state file (optional) |
| Change shader speed | State | state file (optional) |

---

## 📖 Further Reading

- `src/neowalld/config/config.c` - Config loading/parsing
- `src/neowalld/commands/config_commands.c` - set-config/get-config
- `src/neowalld/main.c` - State initialization
- `src/neowalld/output/output.c` - State file writing
- `docs/VIBE.md` - Config file format

---

**Last Updated:** 2024  
**Status:** Settings dialog UI complete, config integration pending