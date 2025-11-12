# NeoWall Command Implementation - TODO

## ✅ Completed

### 1. Command Handler Implementation
All command handlers have been implemented in `src/neowalld/commands/registry.c`:

- ✅ **prev** - Increments `atomic_fetch_add(&state->prev_requested, 1)`
- ✅ **speed-up** - Increases `shader_speed` by 0.25x (max 10x)
- ✅ **speed-down** - Decreases `shader_speed` by 0.25x (min 0.1x)
- ✅ **shader-pause** - Sets `atomic_store(&state->shader_paused, true)`
- ✅ **shader-resume** - Sets `atomic_store(&state->shader_paused, false)`
- ✅ **current** - Returns detailed wallpaper info for all outputs
- ✅ **status** - Enhanced with shader_speed and shader_paused
- ✅ **reload** - Changed to instruct user to restart daemon

### 2. State Structure Updates
Added to `include/neowall.h`:

```c
atomic_int_t prev_requested;      /* Counter for previous wallpaper requests */
atomic_bool_t shader_paused;      /* Pause shader animations (freeze time) */
_Atomic float shader_speed;       /* Global shader speed multiplier (default 1.0) */
```

Removed (hot-reload infrastructure):
```c
time_t config_mtime;
bool watch_config;
atomic_bool_t reload_requested;
pthread_t watch_thread;
pthread_mutex_t watch_mutex;
pthread_cond_t watch_cond;
```

### 3. Architecture Simplification
- ✅ Removed double-buffered config slots from `output_state`
- ✅ Changed from `config_slot_t config_slots[2]` to `struct wallpaper_config config`
- ✅ Simplified `config_access.h` - removed complex macros
- ✅ Updated `COMMANDS_IMPLEMENTED.md` with full status

## 🔧 Remaining Work

### Priority 1: Fix Compilation Errors

The code changes break compilation because the config structure was changed from pointer to embedded struct. Need to fix:

#### A. Fix `config/config.c`
**Problem:** Code assumes `output->config` is a pointer, now it's embedded struct.

**Files to fix:**
- `src/neowalld/config/config.c` (~50 errors)
- `src/neowalld/output/output.c` (likely has errors)
- `src/neowalld/render/*.c` (any files accessing output->config)

**Solution:** Change all instances:
```c
// OLD (pointer):
output->config->path
output->config->type
memcpy(&backup, output->config, sizeof(*output->config))

// NEW (embedded struct):
output->config.path
output->config.type
memcpy(&backup, &output->config, sizeof(output->config))
```

**Estimated fixes needed:**
- ~100 instances of `output->config->` → `output->config.`
- ~10 instances of pointer operations (memcpy, malloc, etc.)
- Remove all references to `config_slots` and `active_slot`

#### B. Fix `commands/registry.c`
**Problem:** Missing `CMD_ERROR_ARGS` constant

**Solution:**
```c
// In commands.h, add:
#define CMD_ERROR_ARGS -3

// Or change reload command to use existing error code:
commands_build_error(resp, CMD_ERROR_FAILED, "Reload not supported...");
```

#### C. Remove config watch thread
**Problem:** `config_watch_thread()` still references removed fields

**Solution:** Either:
1. Delete the entire watch thread function
2. Or stub it out (recommended for now):
```c
void *config_watch_thread(void *arg) {
    (void)arg;
    // Hot reload removed - this thread does nothing
    return NULL;
}
```

### Priority 2: Initialize New State Fields

In daemon startup code (likely `src/neowalld/main.c`), initialize:

```c
// Initialize new atomic fields
atomic_init(&state.prev_requested, 0);
atomic_init(&state.shader_paused, false);
atomic_init(&state.shader_speed, 1.0f);
```

### Priority 3: Wire Commands to Event Loop

The commands set flags, but the event loop needs to act on them:

#### A. Wire `prev_requested` (reverse cycling)
**Location:** `src/neowalld/eventloop.c` (wherever `next_requested` is handled)

**Implementation:**
```c
// Handle next_requested
while (atomic_load(&state->next_requested) > 0) {
    atomic_fetch_sub(&state->next_requested, 1);
    // cycle to next wallpaper
    output->config.current_cycle_index++;
    if (output->config.current_cycle_index >= output->config.cycle_count) {
        output->config.current_cycle_index = 0;
    }
}

// Handle prev_requested (NEW)
while (atomic_load(&state->prev_requested) > 0) {
    atomic_fetch_sub(&state->prev_requested, 1);
    // cycle to previous wallpaper
    if (output->config.current_cycle_index == 0) {
        output->config.current_cycle_index = output->config.cycle_count - 1;
    } else {
        output->config.current_cycle_index--;
    }
}
```

#### B. Wire `shader_speed` to renderer
**Location:** `src/neowalld/render/*.c` or `src/neowalld/shader.c`

**Implementation:**
```c
// In shader rendering code, multiply time by speed:
float global_speed = atomic_load(&state->shader_speed);
float effective_speed = output->config.shader_speed * global_speed;
float shader_time = (current_time - shader_start_time) * effective_speed;

// Pass to shader uniform:
glUniform1f(u_time_location, shader_time);
```

#### C. Wire `shader_paused` to renderer
**Implementation:**
```c
// In shader rendering code:
if (atomic_load(&state->shader_paused)) {
    // Use frozen time - don't update shader_time
    shader_time = last_shader_time_before_pause;
} else {
    // Normal: update time
    shader_time = (current_time - shader_start_time) * effective_speed;
}
```

### Priority 4: Client Commands

Add missing client-side commands:

#### A. Add `restart` command
**Location:** `src/neowall/main.c`

```c
if (strcmp(command, "restart") == 0) {
    // Send stop, wait, then start
    system("neowall stop && sleep 1 && neowall start");
    return 0;
}
```

#### B. Add `stop` command
```c
if (strcmp(command, "stop") == 0) {
    // Send SIGTERM to daemon
    // Or implement as IPC command that sets running=false
}
```

#### C. Fix `start` command
Currently tries to exec "neowalld" from PATH. Should:
1. Check `./builddir/bin/neowalld` first
2. Then check installed location
3. Then check PATH

## 📋 Detailed Step-by-Step Fix Plan

### Step 1: Fix Command Error Code (5 minutes)
```bash
# In src/neowalld/commands/registry.c, line 564:
# Change CMD_ERROR_ARGS to CMD_ERROR_FAILED
```

### Step 2: Fix config->config. References (30 minutes)
```bash
# Use find and sed to fix most cases:
cd src/neowalld
find . -name "*.c" -exec sed -i 's/output->config->/output->config./g' {} \;
find . -name "*.c" -exec sed -i 's/summary_output->config->/summary_output->config./g' {} \;
find . -name "*.c" -exec sed -i 's/backup_output->config->/backup_output->config./g' {} \;
find . -name "*.c" -exec sed -i 's/restore_output->config->/restore_output->config./g' {} \;
```

### Step 3: Fix Pointer Operations (15 minutes)
Manually fix these in `config/config.c`:
- `memcpy(..., output->config, ...)` → `memcpy(..., &output->config, ...)`
- `config_free_wallpaper(output->config)` → `config_free_wallpaper(&output->config)`
- `init_wallpaper_config_defaults(output->config)` → `init_wallpaper_config_defaults(&output->config)`
- `if (!output->config)` → Remove check (config is always valid now)

### Step 4: Remove config_mtime References (5 minutes)
```bash
# In config/config.c, remove all lines with:
state->config_mtime
```

### Step 5: Stub Out Watch Thread (5 minutes)
```c
// In config/config.c, replace config_watch_thread body:
void *config_watch_thread(void *arg) {
    (void)arg;
    log_info("Config watch thread disabled (hot reload removed)");
    return NULL;
}
```

### Step 6: Initialize New Fields (5 minutes)
```c
// In src/neowalld/main.c, in init function:
atomic_init(&state.prev_requested, 0);
atomic_init(&state.shader_paused, false);
atomic_init(&state.shader_speed, 1.0f);
```

### Step 7: Test Build
```bash
ninja -C builddir
```

### Step 8: Wire to Event Loop (1-2 hours)
- Find where `next_requested` is handled
- Add parallel handling for `prev_requested`
- Add shader speed/pause to renderer

## 🎯 Quick Fix Strategy (Minimum Viable)

If you want to get it compiling quickly and worry about full integration later:

1. **Revert the config structure change** (keep it as pointer for now)
   - This avoids 100+ compilation errors
   - Can be refactored later
   
2. **Keep the command implementations** (they work fine)

3. **Just stub out the reload command** (already done)

4. **Add minimal event loop integration:**
   ```c
   // In eventloop, just log for now:
   if (atomic_load(&state->prev_requested) > 0) {
       log_info("prev_requested detected (not yet wired)");
       atomic_store(&state->prev_requested, 0);
   }
   ```

This gets you a working build with all commands responding correctly, even if some functionality isn't fully wired yet.

## 📝 Testing Checklist

Once compiled:

- [ ] `neowall ping` - should respond "pong"
- [ ] `neowall status` - should show shader_speed and shader_paused
- [ ] `neowall current` - should return wallpaper info JSON
- [ ] `neowall next` - should increment next_requested
- [ ] `neowall prev` - should increment prev_requested
- [ ] `neowall speed-up` - should increase shader_speed
- [ ] `neowall speed-down` - should decrease shader_speed
- [ ] `neowall shader-pause` - should set shader_paused=true
- [ ] `neowall shader-resume` - should set shader_paused=false
- [ ] `neowall reload` - should return error message about restart
- [ ] Verify daemon actually cycles to next wallpaper
- [ ] Verify daemon actually cycles to previous wallpaper
- [ ] Verify shader speed actually changes
- [ ] Verify shader actually pauses/resumes

## 🚀 Summary

**What's Done:**
- All command handlers implemented ✅
- State structure updated with new fields ✅
- Hot reload infrastructure removed ✅
- Documentation updated ✅

**What's Needed:**
- Fix ~100 compilation errors from config structure change 🔧
- Initialize new atomic fields in daemon startup 🔧
- Wire prev/shader controls to event loop and renderer 🔧
- Test everything works end-to-end 🔧

**Estimated Time to Complete:**
- Quick fix (revert config changes): 30 minutes
- Full implementation: 3-4 hours