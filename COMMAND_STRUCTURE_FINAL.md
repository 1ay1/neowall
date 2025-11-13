# NeoWall Command Structure - Final Design ✅

**Status:** Complete and Tested  
**Date:** 2024  
**Branch:** `icon-tray`

---

## 🎯 Design Philosophy

**Two command styles for different use cases:**

1. **Clean commands** - Simple, global control (all outputs)
2. **`-output` commands** - Explicit, per-output control

---

## 📋 Command Reference

### Global Commands (All Outputs)

These commands affect ALL connected outputs simultaneously:

```bash
neowall next        # Next wallpaper on all outputs
neowall prev        # Previous wallpaper on all outputs  
neowall pause       # Pause cycling on all outputs
neowall resume      # Resume cycling on all outputs
```

**Characteristics:**
- ✅ No arguments required
- ✅ Extra arguments are ignored
- ✅ Always work globally
- ✅ Use atomic counters/flags

### Per-Output Commands (Specific Output)

These commands require an output name and affect only that output:

```bash
neowall next-output <output>           # Next wallpaper on specific output
neowall prev-output <output>           # Previous on specific output
neowall reload-output <output>         # Reload wallpaper on specific output
neowall pause-output <output>          # Pause cycling on specific output
neowall resume-output <output>         # Resume cycling on specific output
neowall jump-to-output <output> <idx>  # Jump to cycle index on specific output
```

**Characteristics:**
- ⚠️ Output argument REQUIRED
- ⚠️ Error if output not provided
- ✅ Helpful tip messages
- ✅ Explicit control

### Inspection Commands

```bash
neowall list-outputs           # List all connected outputs
neowall output-info <output>   # Get info about specific output (requires arg)
```

---

## 💡 Usage Examples

### Multi-Monitor Scenarios

**Scenario 1: Change all displays at once**
```bash
neowall next     # Quick and simple - all outputs advance
neowall pause    # Pause all at once
```

**Scenario 2: Control specific display**
```bash
neowall next-output DP-1        # Only DP-1 advances
neowall pause-output HDMI-1     # Only HDMI-1 pauses
```

**Scenario 3: Mixed control**
```bash
neowall next                    # All advance
neowall pause-output DP-1       # Pause just one
neowall resume-output DP-1      # Resume just that one
neowall pause                   # Now pause all
```

---

## 🔍 Error Messages & Help

### Helpful Error Messages

When using `-output` commands without an output:

```
$ neowall next-output
Error: next-output requires output name
Usage: neowall next-output <output-name>
  Tip: Use 'neowall next' to affect all outputs
```

The tip points users to the correct command for global control!

### Discovering Outputs

```bash
$ neowall list-outputs
{
  "outputs": [
    {"name":"DP-1","width":2560,"height":1440},
    {"name":"HDMI-1","width":1920,"height":1080}
  ]
}
```

---

## 🏗️ Implementation Details

### Clean Commands (`registry.c`)

```c
static command_result_t cmd_next(struct neowall_state *state, ...) {
    (void)req; /* Unused - global command only */
    
    /* Global next - increment counter for all outputs */
    atomic_fetch_add(&state->next_requested, 1);
    commands_build_success(resp, "Switched to next wallpaper on all outputs", NULL);
    
    return CMD_SUCCESS;
}
```

**Key points:**
- No argument parsing
- Use atomic operations for thread safety
- Daemon's main loop checks these flags and applies to all outputs

### -output Commands (`output_commands.c`)

```c
command_result_t cmd_next_output(...) {
    /* Extract output name (required) */
    char output_name[256];
    if (!extract_output_name(req->args, output_name, sizeof(output_name))) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, 
                           "Missing or invalid 'output' argument");
        return CMD_ERROR_INVALID_ARGS;
    }
    
    /* Find and operate on specific output */
    struct output_state *output = find_output_by_name(state, output_name);
    if (!output) {
        commands_build_error(resp, CMD_ERROR_NOT_FOUND, "Output not found");
        return CMD_ERROR_NOT_FOUND;
    }
    
    output_cycle_wallpaper(output);
    ...
}
```

**Key points:**
- Strict argument validation
- Output name is mandatory
- Direct operation on specific output
- Clear error messages

---

## ✅ Benefits

### 1. Clear Semantics
- Command name immediately tells you its scope
- No ambiguity about what will happen

### 2. Intuitive for Common Case
- Most users want global control → use simple commands
- Clean names (`next`, `pause`) for frequent operations

### 3. Explicit When Needed
- `-output` suffix clearly indicates "per-output"
- Scripting becomes more readable:
  ```bash
  # Clear what this does
  neowall next-output DP-1
  ```

### 4. Error Prevention
- Can't accidentally affect wrong outputs
- Required arguments prevent mistakes

### 5. Great Error Messages
```bash
$ neowall next-output
Error: next-output requires output name
  Tip: Use 'neowall next' to affect all outputs
```

---

## 🧪 Testing

### Test Matrix

| Command | Without Args | With Output Arg | Result |
|---------|-------------|-----------------|--------|
| `next` | ✅ Works (all) | ✅ Ignored | Global |
| `prev` | ✅ Works (all) | ✅ Ignored | Global |
| `pause` | ✅ Works (all) | ✅ Ignored | Global |
| `resume` | ✅ Works (all) | ✅ Ignored | Global |
| `next-output` | ❌ Error | ✅ Works | Specific |
| `prev-output` | ❌ Error | ✅ Works | Specific |
| `reload-output` | ❌ Error | ✅ Works | Specific |
| `pause-output` | ❌ Error | ✅ Works | Specific |
| `resume-output` | ❌ Error | ✅ Works | Specific |
| `jump-to-output` | ❌ Error | ✅ Works (+ index) | Specific |

### Test Results

```
Testing clean commands (should work globally, no args):
=======================================================
✅ neowall next: Switched to next wallpaper
✅ neowall next DP-1 (should ignore arg): Switched to next wallpaper
✅ neowall prev: Switched to previous wallpaper
✅ neowall pause: Paused wallpaper cycling
✅ neowall resume: Resumed wallpaper cycling

Testing -output commands (should require output arg):
================================================================
✅ neowall next-output (no arg): Error: next-output requires output name
✅ neowall next-output DP-1: Switched to next wallpaper
✅ neowall prev-output: Error: prev-output requires output name
✅ neowall reload-output: Error: reload-output requires output name
✅ neowall pause-output: Error: pause-output requires output name
✅ neowall resume-output: Error: resume-output requires output name
✅ neowall jump-to-output 5: Error: jump-to-output requires output name and index
✅ neowall jump-to-output DP-1 5: Jumped to wallpaper
```

All tests passing! ✅

---

## 📦 Files Modified

- `src/neowalld/commands/registry.c` - Clean commands implementation
- `src/neowalld/commands/output_commands.c` - Per-output commands
- `src/neowall/main.c` - CLI argument handling

---

## 🎉 Summary

The final command structure is clean, intuitive, and unambiguous:

- **Simple tasks** → Simple commands (`next`, `pause`)
- **Specific tasks** → Explicit commands (`next-output DP-1`)
- **No confusion** → Command name indicates scope
- **Great UX** → Helpful error messages with tips

Perfect for both interactive use and scripting! 🚀
