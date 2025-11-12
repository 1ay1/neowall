# NeoWall Command System

Modern, extensible command handling API for neowalld.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Client (neowall CLI)                      │
│  Sends JSON: {"command":"next", "args":{}}                  │
└───────────────────────────┬─────────────────────────────────┘
                            │ Unix Socket
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              IPC Layer (src/ipc/)                            │
│  • Socket transport                                         │
│  • JSON protocol parsing                                    │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│         Command Dispatcher (commands.h)                      │
│  commands_dispatch() → routes to handler                    │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│        Command Registry (registry.c)                         │
│  • Command table with metadata                              │
│  • Handler functions                                        │
│  • Statistics tracking                                      │
│  • Help generation                                          │
└─────────────────────────────────────────────────────────────┘
```

## File Structure

```
src/neowalld/commands/
├── commands.h        # Public API (header)
├── commands.c        # Compatibility wrapper (routes ipc_handle_command)
├── registry.c        # Command registry and implementations
└── README.md         # This file
```

## Key Features

### ✅ **Command Metadata**
Every command has rich metadata:
- Name, category, description
- Argument schema
- Example usage
- Capability flags
- Version information

### ✅ **Introspection API**
```c
// List all commands
const command_info_t *commands = commands_get_all(&count);

// Find specific command
const command_info_t *cmd = commands_find("next");

// Get commands by category
const command_info_t *wallpaper_cmds = commands_get_by_category("wallpaper", &count);
```

### ✅ **Statistics Tracking**
Every command execution is tracked:
- Total calls
- Success/failure counts
- Average execution time
- Last call timestamp

```c
command_stats_t stats;
commands_get_stats("next", &stats);
printf("Calls: %lu, Success: %lu, Avg time: %lu µs\n",
       stats.calls_total, stats.calls_success, stats.avg_time_us);
```

### ✅ **Auto-Generated Help**
```c
char help[4096];
commands_generate_help(help, sizeof(help));
// Outputs formatted help grouped by category

commands_generate_json_list(buffer, size);
// Outputs JSON list for API clients
```

### ✅ **Type-Safe Results**
```c
typedef enum {
    CMD_SUCCESS = 0,
    CMD_ERROR_INVALID_ARGS,
    CMD_ERROR_STATE,
    CMD_ERROR_FAILED,
    CMD_ERROR_NOT_IMPLEMENTED,
    CMD_ERROR_PERMISSION,
} command_result_t;
```

### ✅ **Helper Functions**
```c
// Build responses easily
commands_build_success(resp, "Wallpaper changed", "{\"index\":5}");
commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'speed' parameter");
```

## Command Categories

Commands are organized by category:

- **wallpaper** - Wallpaper control (next, prev, current)
- **cycling** - Automatic cycling (pause, resume)
- **config** - Configuration (reload)
- **shader** - Shader control (speed-up, speed-down)
- **info** - Information (status, version, ping, list)

## Adding New Commands

### 1. Add to Registry (`registry.c`)

```c
static const command_info_t command_registry[] = {
    // ... existing commands ...
    {
        .name = "screenshot",
        .category = "wallpaper",
        .description = "Take a screenshot of current wallpaper",
        .args_schema = "{\"path\": <string>}",
        .example = "{\"command\":\"screenshot\",\"args\":{\"path\":\"/tmp/shot.png\"}}",
        .handler = cmd_screenshot,
        .capabilities = CMD_CAP_REQUIRES_STATE,
        .version = 1,
    },
    // ...
};
```

### 2. Implement Handler

```c
static command_result_t cmd_screenshot(struct neowall_state *state,
                                       const ipc_request_t *req,
                                       ipc_response_t *resp) {
    // Parse arguments
    const char *path = parse_string_arg(req->args, "path");
    if (!path) {
        commands_build_error(resp, CMD_ERROR_INVALID_ARGS, "Missing 'path' argument");
        return CMD_ERROR_INVALID_ARGS;
    }
    
    // Execute command logic
    if (take_screenshot(state, path) != 0) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Screenshot failed");
        return CMD_ERROR_FAILED;
    }
    
    // Build success response
    char data[256];
    snprintf(data, sizeof(data), "{\"path\":\"%s\"}", path);
    commands_build_success(resp, "Screenshot saved", data);
    
    return CMD_SUCCESS;
}
```

### 3. Add Forward Declaration

At the top of `registry.c`:
```c
static command_result_t cmd_screenshot(struct neowall_state *state, 
                                       const ipc_request_t *req, 
                                       ipc_response_t *resp);
```

That's it! The command is automatically:
- Registered and discoverable
- Tracked in statistics
- Included in help generation
- Available via IPC

## Usage Examples

### From C Code (Daemon Side)

```c
// Initialize command system
commands_init();

// Dispatch commands
ipc_request_t req = {
    .command = "next",
    .args = "{}"
};
ipc_response_t resp;
commands_dispatch(&req, &resp, daemon_state);

// Cleanup
commands_shutdown();
```

### From Client (JSON over socket)

```json
{"command":"next","args":{}}
{"command":"speed-up","args":{"amount":0.5}}
{"command":"status","args":{}}
```

### Introspection

```c
// List all commands
size_t count;
const command_info_t *cmds = commands_get_all(&count);
for (size_t i = 0; i < count; i++) {
    printf("%s: %s\n", cmds[i].name, cmds[i].description);
}

// Check if command exists
if (commands_exists("next")) {
    printf("'next' command is available\n");
}

// Get command metadata
const command_info_t *cmd = commands_find("speed-up");
if (cmd) {
    printf("Args: %s\n", cmd->args_schema);
    printf("Example: %s\n", cmd->example);
}
```

## Command Capabilities

```c
CMD_CAP_NONE               // No special requirements
CMD_CAP_REQUIRES_STATE     // Needs daemon state
CMD_CAP_MODIFIES_STATE     // Changes daemon state
CMD_CAP_ADMIN_ONLY         // Requires admin privileges (future)
CMD_CAP_ASYNC              // Can run asynchronously (future)
```

## Best Practices

### ✅ DO:
- Return proper `command_result_t` codes
- Use `commands_build_success()` / `commands_build_error()` helpers
- Validate arguments before processing
- Update command metadata when changing behavior
- Add meaningful descriptions and examples

### ❌ DON'T:
- Build JSON strings manually (use helpers)
- Ignore return values
- Forget to check for NULL state when needed
- Skip argument validation

## Migration from Old System

### Before (Old):
```c
extern void request_next_wallpaper(struct neowall_state *state);

static void cmd_next(...) {
    request_next_wallpaper(state);
    ipc_success_response(resp, "{\"message\":\"OK\"}");
}
```

### After (New):
```c
static command_result_t cmd_next(struct neowall_state *state,
                                 const ipc_request_t *req,
                                 ipc_response_t *resp) {
    if (daemon_wallpaper_next(state) != 0) {
        commands_build_error(resp, CMD_ERROR_FAILED, "Failed to switch");
        return CMD_ERROR_FAILED;
    }
    commands_build_success(resp, "Switched to next wallpaper", NULL);
    return CMD_SUCCESS;
}
```

## Performance

- Command lookup: O(n) linear search (acceptable for ~10-20 commands)
- Statistics: Minimal overhead (~100ns per call)
- Memory: Static allocation, no heap usage during dispatch
- Thread-safety: Not thread-safe (daemon is single-threaded)

## Future Enhancements

- [ ] JSON schema validation for arguments
- [ ] Command aliases (e.g., "n" → "next")
- [ ] Async command support
- [ ] Command versioning and deprecation
- [ ] Rate limiting per command
- [ ] Hash table for O(1) command lookup
- [ ] Plugin/dynamic command registration

---

**Status:** ✅ Fully implemented and tested  
**Version:** 1.0  
**Last Updated:** 2024-11-12
