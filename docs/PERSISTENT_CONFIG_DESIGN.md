# NeoWall Persistent Configuration Design

**Making Runtime Config Changes Persistent Across Restarts**

---

## 📋 Table of Contents

- [Overview](#overview)
- [Goals](#goals)
- [Architecture](#architecture)
- [File Structure](#file-structure)
- [Configuration Layers](#configuration-layers)
- [Data Flow](#data-flow)
- [Implementation](#implementation)
- [API Reference](#api-reference)
- [Commands](#commands)
- [Examples](#examples)
- [Migration Strategy](#migration-strategy)

---

## 🎯 Overview

This document describes the persistent configuration system for NeoWall, allowing users to modify configuration via IPC commands and have those changes persist across daemon restarts.

### Current Problem

- Config is loaded from `config.vibe` at startup
- Runtime commands modify in-memory state only
- Changes are lost on daemon restart
- Users have to manually edit `config.vibe` to make permanent changes

### Solution

**Two-File System with Override Semantics:**
- `config.vibe` - User's base configuration (read-only by daemon)
- `state.json` - Runtime state + config overrides (daemon-managed)
- Merge on load: `state.json` overrides `config.vibe`

---

## 🎯 Goals

### Functional Goals

1. ✅ **Persistent Changes** - Config changes via IPC survive daemon restarts
2. ✅ **User Safety** - Never corrupt or modify user's `config.vibe`
3. ✅ **Predictable Behavior** - Clear override semantics
4. ✅ **Resetable** - Easy to revert to base config
5. ✅ **Atomic Updates** - No corruption on crash/kill

### Non-Functional Goals

6. ✅ **Performance** - Fast config loading (<100ms)
7. ✅ **Simplicity** - Easy to understand and debug
8. ✅ **Backward Compatible** - Works with existing configs
9. ✅ **Thread-Safe** - Safe concurrent access
10. ✅ **Observable** - Users can inspect what's overridden

---

## 🏗️ Architecture

### Two-File System

```
┌─────────────────┐
│   config.vibe   │  User's base configuration
│  (user-edited)  │  - Never touched by daemon
│                 │  - Can have comments, custom formatting
└────────┬────────┘  - Version controlled
         │
         ├─────────── Load at startup
         │
         ▼
┌─────────────────┐
│  In-Memory      │  Active configuration in daemon
│  Config State   │  = config.vibe + state.json overrides
└────────┬────────┘
         │
         ├─────────── Runtime changes via IPC
         │
         ▼
┌─────────────────┐
│   state.json    │  Runtime state + config overrides
│ (daemon-managed)│  - Auto-generated JSON
│                 │  - Atomic writes (temp + rename)
└─────────────────┘  - Contains ONLY changed values
```

### Key Principles

1. **Read-Only User Config** - Daemon NEVER writes to `config.vibe`
2. **Override Semantics** - `state.json` values override `config.vibe`
3. **Sparse Storage** - Only store changed values in `state.json`
4. **Atomic Writes** - Use temp file + rename for crash safety
5. **Clear Separation** - Config (what user wants) vs State (runtime data)

---

## 📁 File Structure

### `config.vibe` - User's Base Configuration

**Location:** `~/.config/neowall/config.vibe`

**Purpose:** User's base configuration template

**Format:** VIBE (Values In Bracket Expression)

**Managed By:** User (manual editing)

**Example:**
```vibe
# My NeoWall Configuration
default {
    shader retro_wave.glsl
    shader_speed 1.0
    mode fill
}

output {
    DP-1 {
        shader matrix_real.glsl
        shader_speed 1.5
    }
}
```

**Characteristics:**
- ✅ Human-readable and editable
- ✅ Supports comments
- ✅ Can be version controlled
- ✅ Survives daemon crashes
- ❌ Never modified by daemon

---

### `state.json` - Runtime State + Overrides

**Location:** `~/.config/neowall/state.json`

**Purpose:** Runtime state and config overrides

**Format:** JSON

**Managed By:** Daemon (auto-generated)

**Example:**
```json
{
  "version": 1,
  "last_updated": "2024-01-15T10:30:00Z",
  
  "config_overrides": {
    "default": {
      "shader_speed": 2.0,
      "shader_fps": 30
    },
    "outputs": {
      "DP-1": {
        "mode": "fit",
        "duration": 600,
        "shader_speed": 2.5
      },
      "HDMI-A-1": {
        "path": "/home/user/Pictures/custom.png"
      }
    }
  },
  
  "runtime_state": {
    "daemon_started": "2024-01-15T08:00:00Z",
    "paused": false,
    "shader_paused": false,
    "global_shader_speed": 1.0,
    
    "outputs": {
      "DP-1": {
        "current_wallpaper": "/home/user/.config/neowall/shaders/matrix_real.glsl",
        "current_mode": "fill",
        "cycle_index": 5,
        "cycle_count": 20,
        "last_cycle_time": 1705314600,
        "frames_rendered": 123456
      },
      "HDMI-A-1": {
        "current_wallpaper": "/home/user/Pictures/custom.png",
        "current_mode": "fit",
        "cycle_index": 0,
        "cycle_count": 1,
        "frames_rendered": 78900
      }
    }
  },
  
  "statistics": {
    "total_frames": 202356,
    "uptime_seconds": 9000,
    "total_cycles": 25,
    "errors_count": 0
  }
}
```

**Characteristics:**
- ✅ Machine-readable JSON
- ✅ Auto-generated by daemon
- ✅ Atomic writes (crash-safe)
- ✅ Contains only changed values
- ❌ Not meant for manual editing

---

## 🔄 Configuration Layers

### Three-Layer Merge System

```
Layer 1: Built-in Defaults (lowest priority)
    ↓
Layer 2: config.vibe (user's base config)
    ↓
Layer 3: state.json overrides (highest priority)
    ↓
Final Merged Config (active in daemon)
```

### Merge Algorithm

```c
/*
 * Pseudo-code for config merge
 */
Config merge_config() {
    Config result = builtin_defaults();
    
    // Layer 2: Apply config.vibe
    if (exists("~/.config/neowall/config.vibe")) {
        Config user_config = parse_vibe("config.vibe");
        result = merge(result, user_config);
    }
    
    // Layer 3: Apply state.json overrides
    if (exists("~/.config/neowall/state.json")) {
        ConfigOverrides overrides = parse_json("state.json").config_overrides;
        result = apply_overrides(result, overrides);
    }
    
    return result;
}
```

### Merge Rules

1. **Scalar Values** - Later layers completely override earlier ones
   ```
   config.vibe:     shader_speed 1.0
   state.json:      shader_speed 2.0
   Result:          shader_speed 2.0  ← override wins
   ```

2. **Output Configs** - Per-output, per-key override
   ```
   config.vibe:     DP-1 { mode fill, duration 300 }
   state.json:      DP-1 { mode fit }
   Result:          DP-1 { mode fit, duration 300 }  ← partial override
   ```

3. **Missing Keys** - Inherit from earlier layers
   ```
   config.vibe:     shader_speed 1.5
   state.json:      (no shader_speed)
   Result:          shader_speed 1.5  ← inherit from config.vibe
   ```

4. **Null Values** - Explicit null removes override
   ```
   config.vibe:     mode fill
   state.json:      mode null
   Result:          mode fill  ← revert to config.vibe
   ```

---

## 🔄 Data Flow

### Startup Flow

```
1. Daemon starts
   ↓
2. Load built-in defaults
   ↓
3. Parse config.vibe (if exists)
   ↓
4. Parse state.json (if exists)
   ↓
5. Merge: defaults → config.vibe → state.json
   ↓
6. Apply merged config to outputs
   ↓
7. Daemon ready
```

### Runtime Config Change Flow

```
User runs: neowall set-config default.shader_speed 2.0
   ↓
1. Client sends IPC request
   ↓
2. Daemon receives command
   ↓
3. Validate new value (schema + rules)
   ↓
4. Update in-memory state
   ↓
5. Apply to affected outputs immediately
   ↓
6. Update state.json (atomic write)
   ↓
7. Return success to client
```

### State File Write Flow (Atomic)

```
1. Prepare new state data
   ↓
2. Serialize to JSON string
   ↓
3. Write to temp file: state.json.tmp
   ↓
4. fsync(state.json.tmp)
   ↓
5. rename(state.json.tmp, state.json)  ← Atomic!
   ↓
6. Done (crash-safe)
```

---

## 💻 Implementation

### Core Data Structures

#### Config Override Storage

```c
/* Runtime config override entry */
typedef struct {
    char *key;           /* Config key (e.g., "default.shader_speed") */
    char *value;         /* Override value as string */
    bool is_null;        /* Explicit null (remove override) */
    time_t set_time;     /* When this override was set */
    char *set_by;        /* Who set it (e.g., "IPC command", "CLI") */
} config_override_t;

/* Per-output config overrides */
typedef struct {
    char output_name[64];           /* "DP-1", "HDMI-A-1", etc. */
    config_override_t *overrides;   /* Array of overrides */
    size_t override_count;
} output_config_overrides_t;

/* Global config override state */
typedef struct {
    /* Default section overrides */
    config_override_t *default_overrides;
    size_t default_override_count;
    
    /* Per-output overrides */
    output_config_overrides_t *output_overrides;
    size_t output_override_count;
    
    /* Metadata */
    int version;
    time_t last_updated;
    bool dirty;          /* Needs write to disk */
} config_override_state_t;
```

#### Add to neowall_state

```c
struct neowall_state {
    /* ... existing fields ... */
    
    /* Config override system */
    config_override_state_t *config_overrides;
    pthread_mutex_t config_override_mutex;
    
    /* Auto-save timer */
    int config_save_timer_fd;   /* Save state every N seconds if dirty */
};
```

---

### Core Functions

#### 1. Config Override Management

```c
/**
 * Set a config override
 * 
 * @param state Global state
 * @param scope "default" or "output"
 * @param output_name Output name (NULL for default scope)
 * @param key Config key (e.g., "shader_speed")
 * @param value New value as string (NULL to remove override)
 * @return true on success
 */
bool config_override_set(
    struct neowall_state *state,
    const char *scope,
    const char *output_name,
    const char *key,
    const char *value
);

/**
 * Get a config value with override resolution
 * 
 * @param state Global state
 * @param scope "default" or "output"
 * @param output_name Output name (NULL for default scope)
 * @param key Config key
 * @return Value (from override or base config), NULL if not found
 */
const char *config_get_effective_value(
    struct neowall_state *state,
    const char *scope,
    const char *output_name,
    const char *key
);

/**
 * Remove a config override (revert to base config)
 * 
 * @param state Global state
 * @param scope "default" or "output"
 * @param output_name Output name (NULL for default scope)
 * @param key Config key (NULL to remove all overrides in scope)
 * @return true on success
 */
bool config_override_remove(
    struct neowall_state *state,
    const char *scope,
    const char *output_name,
    const char *key
);

/**
 * Check if a key is overridden
 * 
 * @param state Global state
 * @param scope "default" or "output"
 * @param output_name Output name (NULL for default scope)
 * @param key Config key
 * @return true if overridden
 */
bool config_is_overridden(
    struct neowall_state *state,
    const char *scope,
    const char *output_name,
    const char *key
);

/**
 * List all overrides
 * 
 * @param state Global state
 * @param overrides Output array of override entries
 * @param max_count Maximum entries to return
 * @return Number of overrides returned
 */
size_t config_override_list(
    struct neowall_state *state,
    config_override_t **overrides,
    size_t max_count
);
```

#### 2. State File I/O

```c
/**
 * Load state.json and apply overrides
 * 
 * @param state Global state
 * @param state_file_path Path to state.json
 * @return true on success
 */
bool state_file_load(
    struct neowall_state *state,
    const char *state_file_path
);

/**
 * Save state.json atomically
 * 
 * @param state Global state
 * @param state_file_path Path to state.json
 * @return true on success
 */
bool state_file_save(
    struct neowall_state *state,
    const char *state_file_path
);

/**
 * Save state.json atomically (internal implementation)
 * Uses temp file + rename for crash safety
 * 
 * @param state Global state
 * @param state_file_path Path to state.json
 * @return true on success
 */
static bool state_file_save_atomic(
    struct neowall_state *state,
    const char *state_file_path
) {
    char tmp_path[MAX_PATH_LENGTH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", state_file_path);
    
    /* Serialize to JSON */
    char *json = state_serialize_to_json(state);
    if (!json) {
        log_error("Failed to serialize state to JSON");
        return false;
    }
    
    /* Write to temp file */
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        log_error("Failed to open temp state file: %s", tmp_path);
        free(json);
        return false;
    }
    
    size_t len = strlen(json);
    if (fwrite(json, 1, len, fp) != len) {
        log_error("Failed to write state file");
        fclose(fp);
        free(json);
        unlink(tmp_path);
        return false;
    }
    
    /* Ensure data is written to disk */
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    free(json);
    
    /* Atomic rename */
    if (rename(tmp_path, state_file_path) != 0) {
        log_error("Failed to rename temp state file");
        unlink(tmp_path);
        return false;
    }
    
    return true;
}

/**
 * Auto-save state if dirty (called periodically)
 * 
 * @param state Global state
 */
void state_file_auto_save(struct neowall_state *state);

/**
 * Mark state as dirty (needs save)
 * 
 * @param state Global state
 */
void state_mark_dirty(struct neowall_state *state);
```

#### 3. JSON Serialization

```c
/**
 * Serialize entire state to JSON string
 * 
 * @param state Global state
 * @return JSON string (caller must free), NULL on error
 */
char *state_serialize_to_json(struct neowall_state *state);

/**
 * Parse state.json and extract config overrides
 * 
 * @param json_str JSON string
 * @param overrides Output override state
 * @return true on success
 */
bool state_parse_json_overrides(
    const char *json_str,
    config_override_state_t *overrides
);

/**
 * Parse state.json and extract runtime state
 * 
 * @param json_str JSON string
 * @param state Global state (to populate runtime fields)
 * @return true on success
 */
bool state_parse_json_runtime(
    const char *json_str,
    struct neowall_state *state
);
```

---

## 🎮 Commands

### New IPC Commands

#### `set-config` - Set Config Value

**Syntax:**
```bash
neowall set-config <key> <value>
neowall set-config <output>.<key> <value>
```

**Examples:**
```bash
# Set global shader speed
neowall set-config default.shader_speed 2.0

# Set per-output mode
neowall set-config output.DP-1.mode fit

# Set per-output duration
neowall set-config output.HDMI-A-1.duration 600

# Set global shader
neowall set-config default.shader plasma.glsl
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "key": "default.shader_speed",
    "old_value": "1.0",
    "new_value": "2.0",
    "source": "override",
    "persisted": true
  }
}
```

---

#### `get-config` - Get Config Value

**Syntax:**
```bash
neowall get-config <key>
neowall get-config <output>.<key>
```

**Examples:**
```bash
neowall get-config default.shader_speed
neowall get-config output.DP-1.mode
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "key": "default.shader_speed",
    "value": "2.0",
    "source": "override",
    "base_value": "1.0",
    "is_overridden": true
  }
}
```

---

#### `reset-config` - Remove Override

**Syntax:**
```bash
neowall reset-config <key>          # Reset specific key
neowall reset-config default        # Reset all default overrides
neowall reset-config output.DP-1    # Reset all DP-1 overrides
neowall reset-config --all          # Reset everything
```

**Examples:**
```bash
# Reset specific key to config.vibe value
neowall reset-config default.shader_speed

# Reset all default section overrides
neowall reset-config default

# Reset all overrides for specific output
neowall reset-config output.DP-1

# Reset everything (all overrides)
neowall reset-config --all
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "reset_count": 3,
    "keys_reset": [
      "default.shader_speed",
      "default.shader_fps",
      "output.DP-1.mode"
    ]
  }
}
```

---

#### `list-overrides` - Show All Overrides

**Syntax:**
```bash
neowall list-overrides [--scope=default|output]
```

**Examples:**
```bash
# List all overrides
neowall list-overrides

# List only default overrides
neowall list-overrides --scope=default

# List only output overrides
neowall list-overrides --scope=output
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "override_count": 5,
    "overrides": {
      "default": {
        "shader_speed": {
          "value": "2.0",
          "base_value": "1.0",
          "set_time": "2024-01-15T10:30:00Z",
          "set_by": "IPC command"
        },
        "shader_fps": {
          "value": "30",
          "base_value": "60",
          "set_time": "2024-01-15T10:32:00Z",
          "set_by": "IPC command"
        }
      },
      "outputs": {
        "DP-1": {
          "mode": {
            "value": "fit",
            "base_value": "fill",
            "set_time": "2024-01-15T11:00:00Z",
            "set_by": "IPC command"
          },
          "duration": {
            "value": "600",
            "base_value": "300",
            "set_time": "2024-01-15T11:05:00Z",
            "set_by": "IPC command"
          }
        }
      }
    }
  }
}
```

---

#### `export-config` - Export Merged Config

**Syntax:**
```bash
neowall export-config [--format=vibe|json]
```

**Examples:**
```bash
# Export as VIBE (default)
neowall export-config

# Export as JSON
neowall export-config --format=json

# Save to file
neowall export-config > my-config.vibe
```

**Output (VIBE format):**
```vibe
# Exported NeoWall Configuration
# Generated: 2024-01-15T12:00:00Z
# Includes: config.vibe + state.json overrides

default {
    shader retro_wave.glsl
    shader_speed 2.0        # OVERRIDE (base: 1.0)
    shader_fps 30           # OVERRIDE (base: 60)
    mode fill
}

output {
    DP-1 {
        shader matrix_real.glsl
        mode fit            # OVERRIDE (base: fill)
        duration 600        # OVERRIDE (base: 300)
    }
}
```

---

#### `show-config-source` - Show Config Source

**Syntax:**
```bash
neowall show-config-source <key>
```

**Examples:**
```bash
neowall show-config-source default.shader_speed
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "key": "default.shader_speed",
    "effective_value": "2.0",
    "sources": [
      {
        "layer": "builtin",
        "value": "1.0"
      },
      {
        "layer": "config.vibe",
        "value": "1.0",
        "file": "/home/user/.config/neowall/config.vibe",
        "line": 15
      },
      {
        "layer": "state.json",
        "value": "2.0",
        "file": "/home/user/.config/neowall/state.json",
        "set_time": "2024-01-15T10:30:00Z"
      }
    ],
    "effective_source": "state.json"
  }
}
```

---

### Modified Existing Commands

Existing output commands now persist changes:

```bash
# These now write to state.json
neowall set-output-mode DP-1 fit
neowall set-output-duration DP-1 600
neowall set-output-path DP-1 ~/pic.png
neowall set-output-shader DP-1 matrix.glsl
```

---

## 📖 Examples

### Example 1: Basic Config Override

```bash
# 1. Start with config.vibe
$ cat ~/.config/neowall/config.vibe
default {
    shader retro_wave.glsl
    shader_speed 1.0
}

# 2. Change shader speed via IPC
$ neowall set-config default.shader_speed 2.0
✓ Set default.shader_speed = 2.0 (persisted)

# 3. Verify it's overridden
$ neowall get-config default.shader_speed
{
  "value": "2.0",
  "source": "override",
  "base_value": "1.0",
  "is_overridden": true
}

# 4. Restart daemon
$ neowall kill
$ neowall start

# 5. Override persisted!
$ neowall get-config default.shader_speed
{
  "value": "2.0",
  "source": "override"
}
```

---

### Example 2: Per-Output Config

```bash
# Set different modes for different monitors
$ neowall set-config output.DP-1.mode fit
✓ Set output.DP-1.mode = fit

$ neowall set-config output.HDMI-A-1.mode fill
✓ Set output.HDMI-A-1.mode = fill

# Changes persist across restarts
$ neowall kill && neowall start
$ neowall list-outputs
DP-1: mode=fit (overridden)
HDMI-A-1: mode=fill (overridden)
```

---

### Example 3: Reset to Base Config

```bash
# Made some changes
$ neowall set-config default.shader_speed 3.0
$ neowall set-config default.shader_fps 30

# List overrides
$ neowall list-overrides
default.shader_speed: 3.0 (base: 1.0)
default.shader_fps: 30 (base: 60)

# Reset specific key
$ neowall reset-config default.shader_speed
✓ Reset default.shader_speed to base value (1.0)

# Reset all default overrides
$ neowall reset-config default
✓ Reset 1 override(s)

# Verify
$ neowall list-overrides
No overrides
```

---

### Example 4: Export Current Config

```bash
# Export merged config (config.vibe + overrides)
$ neowall export-config > current-config.vibe

# Review what's been overridden
$ cat current-config.vibe
# Exported NeoWall Configuration
# Generated: 2024-01-15T12:00:00Z

default {
    shader retro_wave.glsl
    shader_speed 2.0        # OVERRIDE (base: 1.0)
    shader_fps 30           # OVERRIDE (base: 60)
}

# Can use this as a new base config
$ cp current-config.vibe ~/.config/neowall/config.vibe
$ neowall reset-config --all  # Clear overrides
$ neowall reload
```

---

### Example 5: Inspect Config Source

```bash
$ neowall show-config-source default.shader_speed
Config key: default.shader_speed
Effective value: 2.0

Layer 1 (builtin):     1.0
Layer 2 (config.vibe): 1.0
Layer 3 (state.json):  2.0 ← ACTIVE
                       Set: 2024-01-15 10:30:00
                       By: IPC command

Effective source: state.json (override)
```

---

## 🔄 Migration Strategy

### Phase 1: Add Override System (No Breaking Changes)

**Week 1-2: Core Implementation**

1. Add `config_override_state_t` to `neowall_state`
2. Implement `config_override_*()` functions
3. Add `state_file_save()` and `state_file_load()`
4. Add `state.json` serialization/parsing
5. Unit tests for override system

**Week 2-3: Integrate with Config Loading**

1. Modify `config_load()` to apply overrides after parsing
2. Add state.json loading to daemon startup
3. Add auto-save timer (save every 30 seconds if dirty)
4. Integration tests

**Week 3-4: IPC Commands**

1. Implement `set-config` command
2. Implement `get-config` command
3. Implement `reset-config` command
4. Implement `list-overrides` command
5. Implement `export-config` command
6. Update existing output commands to persist

---

### Phase 2: Deploy & Test

**Week 5: Testing**

1. End-to-end testing
2. Crash safety testing (kill -9 during saves)
3. Performance testing (1000s of config changes)
4. Multi-monitor testing
5. Backward compatibility testing

**Week 6: Documentation & Release**

1. Update user manual
2. Update command reference
3. Add migration guide
4. Release notes
5. Announce feature

---

### Backward Compatibility

**100% backward compatible:**

1. ✅ Existing `config.vibe` files work without changes
2. ✅ Daemon works without `state.json` (optional)
3. ✅ No breaking changes to config format
4. ✅ Existing commands continue to work
5. ✅ New features are opt-in (use IPC commands to enable)

**Migration path for users:**

- **No action required** - Everything works as before
- **Optional:** Use new `set-config` commands for persistent changes
- **Optional:** Review overrides with `list-overrides`
- **Optional:** Export merged config with `export-config`

---

## 🔒 Safety & Reliability

### Atomic Writes

```c
/* Always use temp file + rename pattern */
1. Write to state.json.tmp
2. fsync(state.json.tmp)
3. rename(state.json.tmp, state.json)  // Atomic!
```

**Benefits:**
- ✅ Crash-safe (either old or new, never corrupt)
- ✅ No partial writes
- ✅ Atomic on all POSIX filesystems

---

### Auto-Save Strategy

```c
/* Save periodically if config changed */
- Auto-save every 30 seconds if dirty
- Immediate save on shutdown
- Immediate save on explicit commands (set-config)
- Rate-limited (max 1 save per second)
```

---

### Error Handling

```c
/* Graceful degradation */
if (!load_state_json()) {
    log_warn("Failed to load state.json, using config.vibe only");
    // Continue without overrides
}

if (!save_state_json()) {
    log_error("Failed to save state.json, changes may be lost");
    // Mark as dirty, retry later
}
```

---

### Concurrency

```c
/* Thread-safe access */
pthread_mutex_t config_override_mutex;

void config_override_set(...) {
    pthread_mutex_lock(&state->config_override_mutex);
    // ... modify overrides ...
    state_mark_dirty(state);
    pthread_mutex_unlock(&state->config_override_mutex);
}
```

---

## 📊 Performance

### Expected Performance

- **Config Load:** <100ms (including state.json parsing)
- **Override Set:** <1ms (in-memory update)
- **State Save:** <50ms (JSON serialization + atomic write)
- **Auto-Save Impact:** <0.1% CPU (happens every 30s)
- **Memory Overhead:** ~10KB per 100 overrides

### Optimization Strategies

1. **Sparse Storage** - Only store changed values
2. **Lazy Saves** - Batch writes every 30 seconds
3. **Incremental Serialization** - Only serialize changed sections
4. **Memory Pools** - Pre-allocate override structures
5. **JSON Streaming** - Stream large state files

---

## 🎯 Success Criteria

### Functional

- ✅ Config changes via IPC persist across restarts
- ✅ User's config.vibe is never modified
- ✅ Overrides can be reset individually or in bulk
- ✅ Crash-safe (no corruption on kill -9)
- ✅ Works without state.json (graceful degradation)

### Performance

- ✅ Config load <100ms
- ✅ Override set <1ms
- ✅ State save <50ms
- ✅ Memory overhead <1MB

### Reliability

- ✅ No data loss on crash
- ✅ No config corruption
- ✅ Thread-safe
- ✅ Race-condition free

### Usability

- ✅ Clear documentation
- ✅ Intuitive commands
- ✅ Helpful error messages
- ✅ Observable (can inspect overrides)

---

## 📚 References

- [Config System Architecture](CONFIG_STATE_DESIGN.md)
- [Config Rules System](CONFIG_RULES.md)
- [IPC Protocol](../src/ipc/protocol.h)
- [Command Reference](commands/COMMANDS.md)

---

**Made with ❤️ for the Linux desktop**