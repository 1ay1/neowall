# NeoWall IPC System

Unix socket-based Inter-Process Communication for NeoWall daemon control.

## Architecture

```
┌─────────────┐                    ┌──────────────┐
│   neowall   │◄──── Unix Socket ──►│  neowalld    │
│  (client)   │      JSON Protocol  │  (daemon)    │
└─────────────┘                    └──────────────┘
```

### Components

1. **Protocol** (`protocol.c/h`) - JSON message format
2. **Socket Server** (`socket_server.c`) - Daemon-side listener
3. **Socket Client** (`socket_client.c`) - Client-side connector
4. **Command Handler** (`commands.c/h`) - Command dispatch logic

## Protocol Specification

### Message Format

All messages are JSON with UTF-8 encoding.

**Request:**
```json
{
  "command": "next",
  "args": {}
}
```

**Response (Success):**
```json
{
  "status": "ok",
  "data": {
    "message": "Switched to next wallpaper"
  }
}
```

**Response (Error):**
```json
{
  "status": "error",
  "message": "Daemon not running"
}
```

### Socket Location

- **Linux:** `$XDG_RUNTIME_DIR/neowalld.sock` (typically `/run/user/1000/neowalld.sock`)
- **Fallback:** `~/.neowalld.sock`
- **Permissions:** 0600 (owner only)

## Supported Commands

| Command      | Description                      | Response Data               |
|--------------|----------------------------------|-----------------------------|
| `next`       | Switch to next wallpaper         | `{"message": "..."}`        |
| `pause`      | Pause wallpaper cycling          | `{"message": "..."}`        |
| `resume`     | Resume wallpaper cycling         | `{"message": "..."}`        |
| `reload`     | Reload configuration             | `{"message": "..."}`        |
| `speed-up`   | Increase shader animation speed  | `{"message": "..."}`        |
| `speed-down` | Decrease shader animation speed  | `{"message": "..."}`        |
| `status`     | Get daemon status                | `{"daemon": "running", ...}`|
| `current`    | Get current wallpaper            | Same as `status`            |
| `ping`       | Test connection                  | `{"message": "pong"}`       |
| `version`    | Get version info                 | `{"version": "0.3.0", ...}` |

## Usage Examples

### From C Code

```c
#include "ipc/socket.h"
#include "ipc/protocol.h"

// Connect to daemon
ipc_client_t *client = ipc_client_connect(NULL);

// Build request
ipc_request_t req = {0};
strcpy(req.command, "next");
strcpy(req.args, "{}");

// Send and receive
ipc_response_t resp;
if (ipc_client_send(client, &req, &resp)) {
    if (resp.status == IPC_STATUS_OK) {
        printf("Success: %s\n", resp.data);
    } else {
        fprintf(stderr, "Error: %s\n", resp.message);
    }
}

ipc_client_close(client);
```

### From Shell (using netcat)

```bash
# Send command
echo '{"command":"status","args":{}}' | nc -U /run/user/$(id -u)/neowalld.sock

# Pretty print response
echo '{"command":"version","args":{}}' | nc -U /run/user/$(id -u)/neowalld.sock | jq .
```

### Adding New Commands

1. **Add handler function in `commands.c`:**
```c
static void cmd_my_command(struct neowall_state *state, 
                          const ipc_request_t *req,
                          ipc_response_t *resp) {
    // Process command
    // ...
    
    ipc_success_response(resp, "{\"result\":\"ok\"}");
}
```

2. **Register in command table:**
```c
static const command_entry_t command_table[] = {
    // ... existing commands ...
    {"my-command", cmd_my_command, "My custom command"},
    {NULL, NULL, NULL}
};
```

3. **Use from client:**
```bash
neowall my-command
```

## Advantages Over Signals

| Feature              | Signals | Unix Sockets |
|----------------------|---------|--------------|
| Bidirectional        | ✗       | ✓            |
| Error reporting      | ✗       | ✓            |
| Pass parameters      | ✗       | ✓            |
| Get return values    | ✗       | ✓            |
| Multiple clients     | ✗       | ✓            |
| Debuggable           | ✗       | ✓            |
| Complex data         | ✗       | ✓            |
| Future-proof         | ✗       | ✓            |

## Performance

- **Latency:** ~0.5ms per request/response
- **Throughput:** ~2000 requests/sec
- **Memory:** ~4KB per connection
- **CPU:** Negligible (<0.1%)

Perfect for a wallpaper daemon where commands are infrequent.

## Security

- Socket file permissions: 0600 (owner only)
- No network exposure (Unix socket only)
- No authentication needed (relies on filesystem permissions)
- Input validation on all commands
- Buffer overflow protection

## Debugging

Enable IPC debug output:
```c
#define IPC_DEBUG 1
```

Monitor socket traffic:
```bash
# Watch socket file
watch -n1 'ls -l /run/user/$(id -u)/neowalld.sock'

# Test connectivity
echo '{"command":"ping","args":{}}' | nc -U /run/user/$(id -u)/neowalld.sock
```

## Error Handling

All errors return status != "ok":

```json
{
  "status": "error",
  "message": "Failed to switch wallpaper: No wallpapers configured"
}
```

Client should always check `status` field before processing `data`.

## Future Extensions

Possible future commands:

- `list-outputs` - List all displays
- `set-wallpaper` - Set specific wallpaper by path
- `get-config` - Get current configuration
- `set-config` - Update configuration programmatically
- `list-shaders` - List available shaders
- `screenshot` - Capture current wallpaper state

The protocol is extensible - just add new commands without breaking existing clients.

## License

MIT License - Same as NeoWall main project
