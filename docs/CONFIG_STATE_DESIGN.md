# NeoWall Configuration & State Management Design

**Comprehensive design for runtime configuration and persistent state**

---

## 📋 Table of Contents

- [Overview](#overview)
- [Current State Analysis](#current-state-analysis)
- [Proposed Architecture](#proposed-architecture)
- [Configuration Management](#configuration-management)
- [State Management](#state-management)
- [Implementation Plan](#implementation-plan)
- [API Reference](#api-reference)
- [Examples](#examples)

---

## 🎯 Overview

### Goals

1. **Full CLI Control** - Every configuration option settable via `neowall` client
2. **Persistent Configuration** - Settings survive daemon restarts
3. **Persistent State** - Wallpaper positions, cycle indices, runtime state preserved
4. **Live Updates** - Changes apply immediately without restart
5. **Atomic Operations** - No partial updates or race conditions
6. **Rollback Support** - Can revert to previous configuration
7. **Per-Output Config** - Each monitor independently configurable

### Design Principles

- **Separation of Concerns**: Configuration (user preferences) vs State (runtime data)
- **Single Source of Truth**: One canonical config file, one state file
- **Layered Defaults**: Global → Per-Output → Runtime overrides
- **Thread-Safe**: All operations safe for concurrent access
- **Crash-Resilient**: Atomic file writes with rename + fsync

---

## 📊 Current State Analysis

### What Exists ✅

1. **Config File System**
   - Location: `~/.config/neowall/config.vibe`
   - Parser: VIBE format (custom key-value)
   - Loading: At daemon startup only
   - Reloading: Manual via `neowall reload` (requires restart)

2. **State File System**
   - Location: `$XDG_RUNTIME_DIR/neowall.state`
   - Format: Plain text (output=wallpaper=mode=index)
   - Usage: Tracks current wallpaper per output
   - Persistence: Lost on reboot (runtime dir cleared)

3. **IPC Commands**
   - `get-config` - Read configuration values
   - `list-config-keys` - List available keys
   - Output commands - Set wallpaper, mode, interval per output
   - No `set-config` command exists

### What's Missing ❌

1. **Runtime Configuration Updates**
   - No way to change config values via CLI
   - No live reload without restart
   - No validation of config values

2. **Persistent State Across Reboots**
   - State file in runtime dir (ephemeral)
   - No restoration of cycle positions
   - No memory of user preferences

3. **Configuration Hierarchy**
   - Global defaults not clearly defined
   - Per-output overrides not well structured
   - No environment-specific configs

4. **Validation & Schemas**
   - No type checking on config values
   - No bounds checking (e.g., negative intervals)
   - No error reporting for invalid configs

---

## 🏗️ Proposed Architecture

### Dual-File System

```
~/.config/neowall/
├── config.vibe              # USER CONFIGURATION (persistent)
├── config.vibe.backup       # Automatic backup of last working config
└── state.json               # RUNTIME STATE (persistent)

$XDG_RUNTIME_DIR/
└── neowalld.sock            # IPC socket
```

### Configuration Layers

```
┌─────────────────────────────────────┐
│   Runtime Overrides (temporary)     │  Highest priority
├─────────────────────────────────────┤
│   Per-Output Config                 │
├─────────────────────────────────────┤
│   Global User Config                │
├─────────────────────────────────────┤
│   Compiled-in Defaults              │  Lowest priority
└─────────────────────────────────────┘
```

### Data Flow

```
User Command
    │
    ├─> neowall set-config general.cycle_interval 600
    │       │
    │       ├─> IPC → neowalld
    │       │       │
    │       │       ├─> Validate value
    │       │       ├─> Update in-memory config
    │       │       ├─> Write to config.vibe (atomic)
    │       │       ├─> Apply to affected outputs
    │       │       └─> Send success response
    │       │
    │       └─> Display confirmation
    │
    ├─> Daemon Restart
    │       │
    │       ├─> Load config.vibe
    │       ├─> Load state.json
    │       ├─> Restore wallpapers to last positions
    │       └─> Resume cycling
    │
    └─> System Reboot
            │
            ├─> config.vibe persists (user config)
            ├─> state.json persists (last state)
            └─> Daemon autostart restores everything
```

---

## ⚙️ Configuration Management

### Configuration Schema

```c
/* Configuration value types */
typedef enum {
    CONFIG_TYPE_INTEGER,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_BOOLEAN,
    CONFIG_TYPE_ENUM,
    CONFIG_TYPE_PATH,
} config_value_type_t;

/* Configuration entry metadata */
typedef struct {
    const char *key;              /* e.g., "general.cycle_interval" */
    config_value_type_t type;     /* Value type */
    const char *description;      /* Human-readable description */
    const char *default_value;    /* Default as string */
    bool per_output;              /* Can be overridden per-output? */
    
    /* Validation */
    bool (*validate)(const char *value, char *error, size_t error_len);
    
    /* Min/max for numeric types */
    union {
        struct { int64_t min; int64_t max; } int_range;
        struct { double min; double max; } float_range;
        struct { const char **values; } enum_values;
    } constraints;
} config_schema_entry_t;

/* In-memory configuration */
typedef struct {
    /* General settings */
    int cycle_interval;           /* Default: 300 seconds */
    enum wallpaper_mode mode;     /* Default: MODE_FILL */
    bool shader_enabled;          /* Default: true */
    
    /* Paths */
    char wallpaper_dir[PATH_MAX]; /* Default: ~/Pictures/Wallpapers */
    char shader_dir[PATH_MAX];    /* Default: /usr/share/neowall/shaders */
    
    /* Performance */
    int fps_limit;                /* Default: 60 */
    bool vsync;                   /* Default: true */
    float shader_speed;           /* Default: 1.0 */
    
    /* Transitions */
    enum transition_type default_transition;  /* Default: TRANSITION_FADE */
    float transition_duration;    /* Default: 1.0 seconds */
    
    /* Behavior */
    bool pause_on_battery;        /* Default: false */
    bool pause_when_fullscreen;   /* Default: false */
    bool restore_on_startup;      /* Default: true */
    
    /* Per-output overrides */
    GHashTable *output_configs;   /* Key: output_name, Value: wallpaper_config */
    
    /* Metadata */
    time_t last_modified;
    char config_file_path[PATH_MAX];
} neowall_config_t;
```

### Configuration Commands

#### 1. Set Configuration Value

```bash
# Set global config
neowall set-config <key> <value>

# Examples
neowall set-config general.cycle_interval 600
neowall set-config general.wallpaper_mode fill
neowall set-config performance.fps_limit 120
neowall set-config paths.wallpaper_dir ~/Pictures/Walls

# Set per-output config
neowall set-config outputs.DP-1.cycle_interval 300
neowall set-config outputs.HDMI-1.shader_enabled false
```

#### 2. Get Configuration Value

```bash
# Get specific value
neowall get-config general.cycle_interval
# Output: 600

# Get all config (JSON)
neowall get-config --json
# Output: {"general": {...}, "paths": {...}, ...}

# Get per-output config
neowall get-config outputs.DP-1
```

#### 3. Reset Configuration

```bash
# Reset to defaults
neowall reset-config

# Reset specific key
neowall reset-config general.cycle_interval

# Reset output-specific config
neowall reset-config outputs.DP-1
```

#### 4. Validate Configuration

```bash
# Validate current config
neowall validate-config

# Validate config file before loading
neowall validate-config ~/.config/neowall/config.vibe
```

#### 5. Export/Import Configuration

```bash
# Export current config
neowall export-config > my-config.vibe

# Import config
neowall import-config my-config.vibe
```

### Configuration File Format

**VIBE Format (Enhanced)**

```vibe
# NeoWall Configuration
# Auto-generated - Edit with 'neowall set-config' or manually

[general]
cycle_interval = 300              # seconds
wallpaper_mode = fill             # center | stretch | fit | fill | tile
shader_enabled = true

[paths]
wallpaper_dir = ~/Pictures/Wallpapers
shader_dir = /usr/share/neowall/shaders

[performance]
fps_limit = 60
vsync = true
shader_speed = 1.0

[transitions]
default_transition = fade         # none | fade | slide_left | slide_right | glitch | pixelate
transition_duration = 1.0         # seconds

[behavior]
pause_on_battery = false
pause_when_fullscreen = false
restore_on_startup = true

# Per-output configuration
[outputs.DP-1]
cycle_interval = 600              # Override global setting
wallpaper_mode = center
shader_enabled = true

[outputs.HDMI-1]
cycle_interval = 300
wallpaper_mode = fill
```

### Atomic Configuration Updates

```c
bool config_set_value(neowall_state_t *state, const char *key, 
                      const char *value) {
    char error[256];
    
    /* 1. Validate value */
    if (!config_validate_value(key, value, error, sizeof(error))) {
        log_error("Invalid config value: %s", error);
        return false;
    }
    
    /* 2. Backup current config */
    config_backup(state->config_path);
    
    /* 3. Update in-memory config */
    pthread_mutex_lock(&state->config_mutex);
    config_update_in_memory(state, key, value);
    
    /* 4. Write to temp file */
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", state->config_path);
    
    if (!config_write_to_file(state, temp_path)) {
        pthread_mutex_unlock(&state->config_mutex);
        config_restore_backup(state->config_path);
        return false;
    }
    
    /* 5. Atomic rename (POSIX guarantees atomicity) */
    if (rename(temp_path, state->config_path) != 0) {
        pthread_mutex_unlock(&state->config_mutex);
        unlink(temp_path);
        config_restore_backup(state->config_path);
        return false;
    }
    
    /* 6. Fsync to ensure durability */
    int fd = open(state->config_path, O_RDONLY);
    fsync(fd);
    close(fd);
    
    pthread_mutex_unlock(&state->config_mutex);
    
    /* 7. Apply changes to runtime */
    config_apply_changes(state, key, value);
    
    log_info("Config updated: %s = %s", key, value);
    return true;
}
```

---

## 💾 State Management

### State Schema

```c
/* Runtime state (persists across restarts) */
typedef struct {
    /* Daemon state */
    bool paused;                  /* Is cycling paused? */
    bool shader_paused;           /* Are shaders paused? */
    float shader_speed;           /* Current shader speed multiplier */
    
    /* Per-output state */
    GHashTable *output_states;    /* Key: output_name, Value: output_state_data */
    
    /* Statistics */
    uint64_t total_frames_rendered;
    uint64_t total_cycles;
    time_t daemon_started;
    
    /* Metadata */
    int version;                  /* State file format version */
    time_t last_updated;
} neowall_state_persistent_t;

/* Per-output state data */
typedef struct {
    char output_name[64];                     /* e.g., "DP-1" */
    char current_wallpaper[PATH_MAX];         /* Currently displayed */
    enum wallpaper_mode current_mode;
    
    /* Cycling state */
    bool cycling_enabled;
    int cycle_index;                          /* Current position in cycle */
    int cycle_count;                          /* Total wallpapers in cycle */
    char cycle_list[MAX_WALLPAPERS][PATH_MAX]; /* All wallpapers in cycle */
    time_t last_cycle_time;                   /* When last cycled */
    int next_cycle_in;                        /* Seconds until next cycle */
    
    /* Shader state */
    char shader_path[PATH_MAX];
    bool shader_active;
    
    /* Statistics */
    uint64_t frames_rendered;
    uint64_t cycles_performed;
} output_state_data_t;
```

### State File Format (JSON)

```json
{
  "version": 1,
  "last_updated": "2024-01-15T10:30:45Z",
  "daemon": {
    "paused": false,
    "shader_paused": false,
    "shader_speed": 1.0,
    "started": "2024-01-15T08:00:00Z",
    "statistics": {
      "total_frames": 1234567,
      "total_cycles": 42,
      "uptime_seconds": 9045
    }
  },
  "outputs": {
    "DP-1": {
      "current_wallpaper": "/home/user/Pictures/Wallpapers/neon_city.jpg",
      "current_mode": "fill",
      "cycling": {
        "enabled": true,
        "index": 5,
        "count": 12,
        "list": [
          "/home/user/Pictures/Wallpapers/img1.jpg",
          "/home/user/Pictures/Wallpapers/img2.jpg",
          "..."
        ],
        "last_cycle": "2024-01-15T10:25:00Z",
        "next_cycle_in": 240
      },
      "shader": {
        "path": "/usr/share/neowall/shaders/plasma.glsl",
        "active": true
      },
      "statistics": {
        "frames_rendered": 456789,
        "cycles_performed": 5
      }
    },
    "HDMI-1": {
      "current_wallpaper": "/home/user/Pictures/Wallpapers/matrix.glsl",
      "current_mode": "fill",
      "cycling": {
        "enabled": false
      },
      "shader": {
        "path": "/home/user/Pictures/Wallpapers/matrix.glsl",
        "active": true
      }
    }
  }
}
```

### State Persistence Operations

#### Save State (Triggered by)
- Wallpaper change
- Configuration change
- Daemon pause/resume
- Output added/removed
- Every N minutes (auto-checkpoint)
- Graceful shutdown

#### Load State (Triggered by)
- Daemon startup
- After crash recovery
- Manual restore command

#### State Commands

```bash
# Show current state
neowall state
neowall state --json

# Show state for specific output
neowall state DP-1

# Save current state
neowall save-state

# Restore from saved state
neowall restore-state

# Clear state (reset to defaults)
neowall clear-state

# Export state
neowall export-state > saved-state.json

# Import state
neowall import-state saved-state.json
```

### State Persistence Implementation

```c
/* Auto-save state on changes */
void state_mark_dirty(neowall_state_t *state) {
    atomic_store(&state->state_dirty, true);
    
    /* Wake up state saver thread */
    eventfd_write(state->state_saver_fd, 1);
}

/* Background state saver thread */
void *state_saver_thread(void *arg) {
    neowall_state_t *state = arg;
    
    while (atomic_load(&state->running)) {
        /* Wait for dirty flag or timeout (auto-checkpoint) */
        struct pollfd pfd = {
            .fd = state->state_saver_fd,
            .events = POLLIN,
        };
        
        int ret = poll(&pfd, 1, STATE_AUTOSAVE_INTERVAL_MS);
        
        if (ret > 0 || atomic_load(&state->state_dirty)) {
            /* Clear eventfd */
            uint64_t val;
            eventfd_read(state->state_saver_fd, &val);
            
            /* Save state atomically */
            state_save_atomic(state);
            atomic_store(&state->state_dirty, false);
        }
    }
    
    return NULL;
}

/* Atomic state save */
bool state_save_atomic(neowall_state_t *state) {
    char temp_path[PATH_MAX];
    char state_path[PATH_MAX];
    
    get_state_file_path(state_path, sizeof(state_path));
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", state_path);
    
    /* Serialize state to JSON */
    cJSON *root = state_serialize_json(state);
    char *json_str = cJSON_Print(root);
    
    /* Write to temp file */
    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        cJSON_Delete(root);
        free(json_str);
        return false;
    }
    
    fprintf(fp, "%s\n", json_str);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    
    /* Atomic rename */
    if (rename(temp_path, state_path) != 0) {
        unlink(temp_path);
        cJSON_Delete(root);
        free(json_str);
        return false;
    }
    
    cJSON_Delete(root);
    free(json_str);
    
    log_debug("State saved to %s", state_path);
    return true;
}
```

---

## 🚀 Implementation Plan

### Phase 1: Configuration Commands (Week 1)

**Goal**: Enable runtime configuration via CLI

1. **Add Configuration Schema**
   - Define all config keys with types and constraints
   - Implement validation functions
   - Add default values

2. **Implement set-config Command**
   - Parse and validate input
   - Update in-memory config
   - Write to file atomically
   - Apply changes to runtime

3. **Implement reset-config Command**
   - Restore defaults
   - Support partial resets

4. **Testing**
   - Unit tests for validation
   - Integration tests for persistence
   - Concurrency tests

**Files to Modify/Create:**
```
src/neowalld/config/
├── config_schema.h         # NEW: Schema definitions
├── config_schema.c         # NEW: Schema implementation
├── config_write.c          # NEW: Atomic write operations
└── config_commands.c       # MODIFY: Add set/reset commands

src/neowalld/commands/
└── config_commands.c       # MODIFY: Register new commands

docs/
└── CONFIG_COMMANDS.md      # NEW: User documentation
```

### Phase 2: State Persistence (Week 2)

**Goal**: Preserve state across restarts

1. **Migrate State File**
   - Move from `$XDG_RUNTIME_DIR` to `~/.config/neowall/`
   - Change format from plain text to JSON
   - Add versioning for compatibility

2. **Implement State Serialization**
   - Serialize daemon state to JSON
   - Serialize per-output state
   - Include cycle positions, wallpaper lists

3. **Implement State Restoration**
   - Load state on daemon startup
   - Restore wallpapers to last positions
   - Resume cycling from saved index

4. **Background State Saver**
   - Auto-save on changes (debounced)
   - Periodic checkpoints every 5 minutes
   - Save on graceful shutdown

**Files to Modify/Create:**
```
src/neowalld/
├── state_persistence.h     # NEW: State persistence API
├── state_persistence.c     # NEW: Save/load implementation
└── main.c                  # MODIFY: Load state on startup

include/
└── state.h                 # NEW: State structures

docs/
└── STATE_FORMAT.md         # NEW: State file format spec
```

### Phase 3: Live Configuration Updates (Week 3)

**Goal**: Apply config changes without restart

1. **Hot Reload Infrastructure**
   - Detect which configs changed
   - Apply changes selectively
   - Notify affected outputs

2. **Per-Output Config Override**
   - Support output-specific settings
   - Implement config hierarchy
   - Test precedence rules

3. **Configuration Validation UI**
   - Show validation errors clearly
   - Preview changes before applying
   - Rollback on error

**Files to Modify/Create:**
```
src/neowalld/config/
├── config_apply.c          # NEW: Apply config changes
└── config_reload.c         # MODIFY: Hot reload logic

src/neowalld/output/
└── output_config.c         # MODIFY: Per-output config

docs/
└── CONFIG_HIERARCHY.md     # NEW: Config precedence docs
```

### Phase 4: Import/Export & Backup (Week 4)

**Goal**: Enable config portability and recovery

1. **Export Functionality**
   - Export config to file
   - Export state to file
   - Bundle config + state

2. **Import Functionality**
   - Import and validate
   - Preview before applying
   - Merge vs replace options

3. **Automatic Backups**
   - Backup before changes
   - Keep last N backups
   - Restore from backup

**Files to Modify/Create:**
```
src/neowalld/config/
├── config_export.c         # NEW: Export operations
├── config_import.c         # NEW: Import operations
└── config_backup.c         # NEW: Backup management

scripts/
└── neowall-migrate-config  # NEW: Migration helper script
```

---

## 📚 API Reference

### Configuration Commands

#### set-config

Set a configuration value.

```bash
neowall set-config <key> <value>
```

**Examples:**
```bash
neowall set-config general.cycle_interval 600
neowall set-config outputs.DP-1.mode fill
neowall set-config performance.vsync true
```

**IPC Protocol:**
```json
{
  "command": "set-config",
  "args": {
    "key": "general.cycle_interval",
    "value": "600"
  }
}
```

**Response:**
```json
{
  "status": "ok",
  "data": {
    "key": "general.cycle_interval",
    "old_value": "300",
    "new_value": "600",
    "applied": true
  }
}
```

#### get-config

Get configuration value(s).

```bash
neowall get-config [key]
```

**Examples:**
```bash
neowall get-config general.cycle_interval
neowall get-config outputs.DP-1
neowall get-config --all
```

#### reset-config

Reset configuration to defaults.

```bash
neowall reset-config [key]
```

**Examples:**
```bash
neowall reset-config                    # Reset all
neowall reset-config general            # Reset section
neowall reset-config outputs.DP-1       # Reset output
```

### State Commands

#### state

Show current runtime state.

```bash
neowall state [output]
```

**Output:**
```
● NeoWall Daemon State
  Running: Yes
  Paused: No
  Uptime: 2h 30m

● Output DP-1
  Wallpaper: /home/user/Pictures/neon_city.jpg
  Mode: fill
  Cycling: Yes (5/12)
  Next cycle: in 4m 23s

● Output HDMI-1
  Shader: /usr/share/neowall/shaders/matrix.glsl
  Mode: fill
  Cycling: No
```

#### save-state

Manually save current state.

```bash
neowall save-state
```

#### restore-state

Restore from saved state.

```bash
neowall restore-state [file]
```

---

## 💡 Examples

### Basic Configuration

```bash
# Set global cycle interval
neowall set-config general.cycle_interval 600

# Set wallpaper directory
neowall set-config paths.wallpaper_dir ~/Pictures/Wallpapers

# Enable shader animations
neowall set-config general.shader_enabled true

# Set default transition
neowall set-config transitions.default_transition fade
```

### Per-Output Configuration

```bash
# Configure primary monitor
neowall set-config outputs.DP-1.cycle_interval 300
neowall set-config outputs.DP-1.mode fill

# Configure secondary monitor  
neowall set-config outputs.HDMI-1.cycle_interval 600
neowall set-config outputs.HDMI-1.mode center
neowall set-config outputs.HDMI-1.shader_enabled false
```

### State Management

```bash
# View current state
neowall state

# Save state before making changes
neowall save-state

# Make changes...
neowall set-output-wallpaper DP-1 ~/cool.jpg

# Export state for backup
neowall export-state > ~/neowall-backup.json

# Later: restore from backup
neowall import-state ~/neowall-backup.json
```

### Configuration Profiles

```bash
# Export work profile
neowall export-config > ~/.config/neowall/profiles/work.vibe

# Export gaming profile  
neowall set-config performance.fps_limit 144
neowall set-config transitions.default_transition none
neowall export-config > ~/.config/neowall/profiles/gaming.vibe

# Switch profiles
neowall import-config ~/.config/neowall/profiles/work.vibe
neowall import-config ~/.config/neowall/profiles/gaming.vibe
```

---

## 🔒 Security Considerations

1. **File Permissions**
   - Config file: `0600` (user read/write only)
   - State file: `0600` (user read/write only)
   - Backup files: `0600`

2. **Path Validation**
   - Validate all file paths
   - Prevent directory traversal
   - Check file existence before loading

3. **Input Sanitization**
   - Validate all config values
   - Bounds checking for numbers
   - Escape special characters in paths

4. **Race Condition Prevention**
   - Use atomic file operations (rename)
   - Lock files during write operations
   - Fsync before considering write complete

---

## 📊 Testing Strategy

### Unit Tests

```c
// Test configuration validation
test_config_validate_integer();
test_config_validate_enum();
test_config_validate_path();

// Test atomic operations
test_config_atomic_write();
test_config_rollback_on_error();

// Test state serialization
test_state_serialize_json();
test_state_deserialize_json();
```

### Integration Tests

```bash
# Test configuration persistence
neowall set-config general.cycle_interval 600
neowall restart
assert_config_value general.cycle_interval 600

# Test state restoration
neowall set-output-wallpaper DP-1 ~/test.jpg
neowall restart
assert_output_wallpaper DP-1 ~/test.jpg

# Test multi-output state
neowall set-output-wallpaper DP-1 ~/img1.jpg
neowall set-output-wallpaper HDMI-1 ~/img2.jpg
neowall restart
assert_all_outputs_restored
```

### Stress Tests

```bash
# Concurrent config updates
for i in {1..100}; do
  neowall set-config test.value $i &
done
wait
assert_no_corruption

# Rapid restarts
for i in {1..10}; do
  neowall restart
  sleep 1
done
assert_state_consistent
```

---

## 🎯 Success Criteria

### Functionality
- ✅ All config values settable via CLI
- ✅ Config persists across daemon restarts
- ✅ State persists across system reboots
- ✅ Changes apply immediately (no restart needed)
- ✅ Multi-output state preserved independently

### Reliability
- ✅ No config corruption on crash
- ✅ No state loss on sudden shutdown
- ✅ Atomic operations prevent partial updates
- ✅ Automatic backups enable recovery

### Performance
- ✅ Config changes apply in <100ms
- ✅ State save completes in <50ms
- ✅ Startup with state restore <200ms
- ✅ No frame drops during config updates

### Usability
- ✅ Clear error messages for invalid values
- ✅ Config validation before applying
- ✅ Export/import for portability
- ✅ Comprehensive documentation

---

## 📖 References

- [VIBE Config Format](vibe.h)
- [IPC Protocol](../src/ipc/README.md)
- [Command System](COMMAND_REGISTRY_IMPROVEMENTS.md)
- [Output Management](../src/neowalld/output/README.md)

---

**Document Version**: 1.0  
**Last Updated**: 2024-01-15  
**Status**: Design Complete - Ready for Implementation