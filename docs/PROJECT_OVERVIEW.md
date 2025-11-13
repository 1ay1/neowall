# NeoWall Project Overview

**GPU-Accelerated Animated Wallpapers for Wayland**

---

## 🎯 What is NeoWall?

NeoWall is a high-performance wallpaper daemon for Wayland compositors that brings your desktop to life with GPU-accelerated animated wallpapers. It uses OpenGL ES shaders for stunning visual effects while maintaining minimal CPU usage (~0.5%) and low memory footprint (<50MB).

### Key Features

- **30+ Built-in Shaders** - Plasma waves, matrix rain, fractal noise, cyberpunk aesthetics
- **Shadertoy Compatible** - Import thousands of community shaders
- **Multi-Monitor Support** - Independent wallpapers per display
- **GPU-Accelerated** - Hardware rendering via OpenGL ES
- **CLI Control** - Full command-line interface
- **System Tray** - Optional GTK tray application
- **Per-Monitor Config** - Each display independently configurable
- **Live Shader Editing** - Hot reload without restart

---

## 🏗️ Architecture

### High-Level Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     User Layer                               │
├─────────────────────────────────────────────────────────────┤
│  neowall (CLI)  │  neowall_tray (GTK)  │  Scripts/Hotkeys  │
└────────┬─────────────────┬──────────────────────┬───────────┘
         │                 │                      │
         └─────────────────┼──────────────────────┘
                           │
                    ┌──────▼──────┐
                    │  IPC Socket │ (Unix Domain Socket)
                    │   JSON RPC  │
                    └──────┬──────┘
                           │
         ┌─────────────────▼─────────────────┐
         │      neowalld (Daemon)            │
         ├───────────────────────────────────┤
         │  • Event Loop                     │
         │  • Command Dispatcher             │
         │  • Configuration Manager          │
         │  • State Persistence              │
         └─────────────────┬─────────────────┘
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    ┌────▼────┐     ┌─────▼──────┐   ┌─────▼──────┐
    │ Wayland │     │ Compositor │   │    EGL     │
    │ Globals │     │  Abstraction│   │   Core     │
    └─────────┘     └────────────┘   └────────────┘
         │                 │                 │
    ┌────▼────┐     ┌─────▼──────┐   ┌─────▼──────┐
    │ Outputs │     │  Backends  │   │  Shaders   │
    │ Manager │     │  • Layer   │   │  • GLSL    │
    │         │     │  • Plasma  │   │  • Shadertoy│
    └─────────┘     │  • Fallback│   └────────────┘
                    └────────────┘
```

### Component Breakdown

#### 1. **Client Layer**

**neowall (CLI Client)**
- Location: `src/neowall/`
- Purpose: Command-line interface for controlling daemon
- Communication: IPC socket (JSON protocol)
- Features: 28+ commands for wallpaper control

**neowall_tray (System Tray)**
- Location: `src/neowall_tray/`
- Technology: GTK3 + libappindicator
- Purpose: Graphical control and quick access
- Features: Menu, notifications, status display

#### 2. **Daemon Layer (neowalld)**

**Main Daemon**
- Location: `src/neowalld/main.c`
- Purpose: Background service managing wallpapers
- Architecture: Event-driven with epoll
- Threading: Single-threaded event loop + background preload threads

**Event Loop**
- Location: `src/neowalld/eventloop.c`
- Technology: `epoll` for multiplexed I/O
- Handles:
  - Wayland events
  - IPC connections
  - Timer events (wallpaper cycling)
  - Signal handling
  - Background thread completion

**Command System**
- Location: `src/neowalld/commands/`
- Design: Registry-based with automatic registration
- Categories:
  - Core: `next`, `prev`, `pause`, `resume`, `status`
  - Output: Per-monitor control commands
  - Config: Configuration query/modify
  - Shader: Animation control
  - Introspection: `list-commands`, `command-stats`

**IPC System**
- Location: `src/ipc/`
- Protocol: JSON over Unix domain sockets
- Socket: `$XDG_RUNTIME_DIR/neowalld.sock`
- Features:
  - Bidirectional communication
  - Error reporting
  - Streaming responses
  - Multiple concurrent clients

#### 3. **Compositor Abstraction Layer**

**Purpose**: Support multiple Wayland compositors through unified API

**Location**: `src/neowalld/compositor/`

**Backends**:
1. **wlr-layer-shell** (Priority 100) ✅
   - For: Hyprland, Sway, River, Wayfire
   - Protocol: `zwlr_layer_shell_v1`
   - Status: Fully implemented

2. **KDE Plasma Shell** (Priority 90) 🚧
   - For: KDE Plasma
   - Protocol: `org_kde_plasma_shell`
   - Status: Stub implementation

3. **GNOME Shell** (Priority 80) 🚧
   - For: GNOME
   - Method: Subsurface workaround
   - Status: Stub implementation

4. **Fallback** (Priority 10) ✅
   - For: Any compositor
   - Method: Regular surface (limited features)
   - Status: Fully implemented

**Auto-Detection**: Compositor type detected at runtime, best backend selected automatically.

#### 4. **Output Management**

**Location**: `src/neowalld/output/`

**Per-Output State**:
```c
struct output_state {
    // Wayland objects
    struct wl_output *output;
    struct compositor_surface *surface;
    
    // Configuration
    struct wallpaper_config config;
    
    // Rendering resources
    GLuint texture;           // Current wallpaper
    GLuint next_texture;      // For transitions
    GLuint preload_texture;   // Async preload
    GLuint program;           // Shader program
    
    // State
    int cycle_index;
    time_t last_cycle;
    bool needs_redraw;
};
```

**Features**:
- Independent wallpaper per monitor
- Per-output configuration
- Cycle state persistence
- Async texture preloading (zero-stall transitions)
- HiDPI support

#### 5. **EGL Core**

**Location**: `src/neowalld/egl/`

**Responsibilities**:
- EGL display and context management
- OpenGL ES capability detection
- Surface creation and management
- Context switching between outputs

**Strategy**: Shared EGL context for all outputs (resource efficient)

**Capability Detection**:
```c
typedef struct {
    bool has_gles3;
    bool has_instancing;
    bool has_vao;
    bool has_buffer_storage;
    int max_texture_size;
    int max_viewport_dims[2];
} egl_capabilities_t;
```

#### 6. **Shader System**

**Location**: `src/neowalld/shader.c`, `src/neowalld/shadertoy_compat.c`

**Features**:
- GLSL ES 2.0/3.0 support
- Shadertoy compatibility layer
- Automatic uniform injection
- Per-output shader programs
- Hot reload support

**Shadertoy Compatibility**:
- Automatic conversion of Shadertoy shaders
- Built-in uniforms: `iTime`, `iResolution`, `iMouse`, `iChannel[0-3]`
- Texture channel support

**Shader Adaptation**:
- Auto-detection of GLSL version
- Precision qualifiers
- ES 2.0 fallbacks

#### 7. **Render Pipeline**

**Location**: `src/neowalld/render/`

**Render Flow**:
```
Frame Requested
    ↓
1. EGL Make Current
    ↓
2. Clear Framebuffer
    ↓
3. Bind Texture/Shader
    ↓
4. Update Uniforms (time, resolution)
    ↓
5. Draw Quad
    ↓
6. Handle Transition (if active)
    ↓
7. Swap Buffers
    ↓
8. Update Statistics
```

**Transition System**:
- Location: `src/neowalld/transitions/`
- Types: Fade, Slide, Glitch, Pixelate
- Implementation: Fragment shader effects
- Duration: Configurable per transition

#### 8. **Configuration System**

**Location**: `src/neowalld/config/`

**Format**: VIBE (custom key-value format)

**Default Path**: `~/.config/neowall/config.vibe`

**Structure**:
```vibe
[general]
cycle_interval = 300
wallpaper_mode = fill

[paths]
wallpaper_dir = ~/Pictures/Wallpapers
shader_dir = /usr/share/neowall/shaders

[performance]
fps_limit = 60
vsync = true

[outputs.DP-1]
cycle_interval = 600
```

**Features**:
- Global defaults
- Per-output overrides
- Hot reload (via `SIGHUP`)
- Validation

#### 9. **State Persistence**

**Current Implementation**:
- Location: `$XDG_RUNTIME_DIR/neowall.state`
- Format: Plain text
- Content: Current wallpaper per output
- Limitation: Ephemeral (lost on reboot)

**Planned Enhancement** (See [CONFIG_STATE_DESIGN.md](CONFIG_STATE_DESIGN.md)):
- Location: `~/.config/neowall/state.json`
- Format: JSON
- Content: Full daemon state (cycle positions, statistics)
- Persistence: Survives reboots

---

## 📂 Project Structure

```
neowall/
├── src/
│   ├── ipc/                    # IPC communication layer
│   │   ├── protocol.c          # JSON protocol parser
│   │   ├── socket_server.c     # Daemon-side server
│   │   └── socket_client.c     # Client-side connector
│   │
│   ├── neowalld/               # Main daemon
│   │   ├── main.c              # Entry point
│   │   ├── eventloop.c         # Event loop
│   │   ├── commands/           # Command handlers
│   │   │   ├── commands.c      # Registry & dispatcher
│   │   │   ├── core_commands.c # Basic commands
│   │   │   ├── output_commands.c # Per-output control
│   │   │   └── config_commands.c # Config queries
│   │   ├── compositor/         # Compositor abstraction
│   │   │   ├── compositor_registry.c
│   │   │   └── backends/
│   │   │       ├── wlr_layer_shell.c
│   │   │       ├── kde_plasma.c
│   │   │       ├── gnome_shell.c
│   │   │       └── fallback.c
│   │   ├── config/             # Configuration management
│   │   │   ├── config.c
│   │   │   └── vibe.c          # VIBE parser
│   │   ├── egl/                # EGL/OpenGL setup
│   │   │   ├── egl_core.c
│   │   │   └── capability.c
│   │   ├── output/             # Output management
│   │   │   └── output.c
│   │   ├── render/             # Rendering engine
│   │   │   └── render.c
│   │   ├── shader.c            # Shader compilation
│   │   ├── shadertoy_compat.c  # Shadertoy adaptation
│   │   ├── image/              # Image loading
│   │   │   └── image.c         # PNG/JPEG support
│   │   ├── transitions/        # Transition effects
│   │   └── utils.c             # Utilities
│   │
│   ├── neowall/                # CLI client
│   │   └── main.c
│   │
│   └── neowall_tray/           # System tray app
│       └── main.c
│
├── include/                    # Public headers
│   ├── neowall.h               # Main state structure
│   ├── compositor.h            # Compositor abstraction API
│   └── version/
│       └── version.h.in        # Version template
│
├── protocols/                  # Wayland protocols
│   ├── plasma-shell.xml
│   ├── wlr-layer-shell-unstable-v1.xml
│   └── tearing-control-v1.xml
│
├── data/                       # Application data
│   ├── shaders/                # Built-in shaders
│   └── icons/                  # Application icons
│
├── docs/                       # Documentation
│   ├── README.md               # Main documentation
│   ├── BUILD.md                # Build instructions
│   ├── IPC_MIGRATION.md        # IPC architecture
│   ├── COMMAND_REGISTRY_IMPROVEMENTS.md
│   ├── CONFIG_STATE_DESIGN.md  # Config/state design
│   └── commands/               # Command references
│
├── scripts/                    # Utility scripts
│
├── meson.build                 # Build configuration
└── README.md                   # Project overview
```

---

## 🔄 Data Flow Examples

### Starting the Daemon

```
1. User: neowall start
    ↓
2. CLI forks neowalld daemon
    ↓
3. Daemon initializes:
   • Load config.vibe
   • Connect to Wayland
   • Detect compositor type
   • Initialize EGL context
   • Enumerate outputs
   • Create compositor surfaces
    ↓
4. For each output:
   • Apply configuration
   • Load wallpaper/shader
   • Compile shader programs
   • Start render loop
    ↓
5. Start IPC server
   • Listen on Unix socket
    ↓
6. Enter event loop:
   • Wayland events
   • Timer events (cycling)
   • IPC connections
```

### Changing Wallpaper

```
1. User: neowall next
    ↓
2. CLI connects to IPC socket
    ↓
3. Send JSON command:
   {"command":"next","args":{}}
    ↓
4. Daemon command dispatcher:
   • Lookup "next" in registry
   • Call cmd_next handler
    ↓
5. cmd_next implementation:
   • Lock state mutex
   • Increment cycle index
   • Schedule preload of next wallpaper
   • Unlock mutex
    ↓
6. Background preload thread:
   • Decode image from disk
   • Signal main thread
    ↓
7. Main thread (next frame):
   • Upload texture to GPU
   • Start transition
    ↓
8. Render loop:
   • Interpolate transition (0.0 → 1.0)
   • Blend old and new textures
   • Display result
    ↓
9. Transition complete:
   • Free old texture
   • Update state file
    ↓
10. Send IPC response:
    {"status":"ok","data":"Switched to next wallpaper"}
    ↓
11. CLI displays: "✓ Switched to next wallpaper"
```

### Multi-Monitor Support

```
For each connected output:
    ↓
1. Wayland announces new wl_output
    ↓
2. Output manager creates output_state
    ↓
3. Query xdg-output for connector name (DP-1, HDMI-1)
    ↓
4. Load per-output config (if exists)
    ↓
5. Create compositor surface for output
    ↓
6. Create EGL window tied to surface
    ↓
7. Make context current for this output
    ↓
8. Initialize textures and shaders for output
    ↓
9. Add to output list
    ↓
10. Start independent render loop for output
    ↓
11. Each output now cycles independently
```

---

## 🔧 Technology Stack

### Core Technologies

- **Language**: C11
- **Build System**: Meson + Ninja
- **IPC**: Unix Domain Sockets + JSON
- **GUI**: GTK3 + libappindicator (tray only)

### Wayland Stack

- **wayland-client** (≥1.20)
- **wayland-protocols** (≥1.25)
- **wayland-egl**

### Graphics Stack

- **EGL** - Display/context management
- **OpenGL ES 2.0/3.0** - GPU rendering
- **GLSL ES** - Shader language

### Image Support

- **libpng** - PNG loading
- **libjpeg** - JPEG loading

### Protocols Used

- `wl_compositor` - Surface creation
- `wl_output` - Monitor enumeration
- `zwlr_layer_shell_v1` - Background layer (wlroots)
- `org_kde_plasma_shell` - KDE integration
- `xdg_output` - Connector names
- `wp_tearing_control_v1` - Immediate presentation

---

## 🎮 Command System

### Command Categories

1. **Core** (8 commands)
   - Basic: `start`, `stop`, `restart`, `status`
   - Control: `next`, `prev`, `pause`, `resume`

2. **Output** (9 commands)
   - Query: `list-outputs`, `output-info`
   - Control: `next-output`, `prev-output`, `reload-output`
   - Config: `set-output-mode`, `set-output-wallpaper`, `set-output-interval`

3. **Shader** (4 commands)
   - `shader-pause`, `shader-resume`
   - `speed-up`, `speed-down`

4. **Config** (3 commands)
   - `get-config`, `list-config-keys`, `reload`

5. **Introspection** (4 commands)
   - `list-commands`, `command-stats`, `version`, `help`

**Total**: 28 commands

### Command Registry Design

**Auto-Registration**:
```c
static const command_info_t core_commands[] = {
    COMMAND_ENTRY(next, "wallpaper", "Switch to next wallpaper",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    COMMAND_ENTRY(pause, "wallpaper", "Pause cycling",
                  CMD_CAP_REQUIRES_STATE | CMD_CAP_MODIFIES_STATE),
    {NULL} // Sentinel
};
```

**Statistics Tracking**:
- Execution count
- Success/failure ratio
- Average execution time
- Min/max execution time
- Last error

---

## 🚀 Performance Characteristics

### Resource Usage

- **CPU**: ~0.5% (idle), ~2-5% (animated shaders)
- **Memory**: <50MB (typical), <100MB (with preloaded textures)
- **GPU**: Minimal (simple quad rendering)

### Rendering Performance

- **Target FPS**: 60 (configurable)
- **Vsync**: Optional
- **Frame Time**: ~1-2ms per output
- **Texture Upload**: Async (background threads)
- **Transition Overhead**: <5% additional GPU time

### Startup Time

- **Cold Start**: ~200ms
- **With State Restore**: ~300ms
- **Per-Output Init**: ~50ms

### Command Latency

- **IPC Round-Trip**: ~1-2ms
- **Config Update**: ~10-50ms
- **Wallpaper Switch**: ~100-200ms (with transition)

---

## 🔐 Security Considerations

### IPC Security

- **Socket Permissions**: `0600` (user-only)
- **Socket Location**: `$XDG_RUNTIME_DIR` (user-specific)
- **No Authentication**: Trust model (same user)

### File Access

- **Config File**: `0600` (user read/write)
- **State File**: `0600` (user read/write)
- **Shader Files**: Read-only access
- **Image Files**: Read-only access

### Input Validation

- All IPC commands validated
- File paths checked for existence
- No shell command execution
- No arbitrary code execution

---

## 🐛 Known Limitations

1. **Wayland-Only**: No X11 support (by design)
2. **Compositor Support**: Best on wlroots-based compositors
3. **GNOME**: Limited support (no official protocol)
4. **State Persistence**: Currently ephemeral (fix in progress)
5. **Hot-Plugging**: Monitors must be connected at startup

---

## 🛣️ Roadmap

### Short-Term (v0.5)

- ✅ Multi-monitor support
- ✅ Command introspection
- ✅ Compositor abstraction
- 🚧 Persistent state across reboots
- 🚧 Runtime configuration updates

### Mid-Term (v1.0)

- ⏳ Hot-plug monitor support
- ⏳ Configuration profiles
- ⏳ Shader marketplace integration
- ⏳ Video wallpaper support
- ⏳ Advanced transition effects

### Long-Term (v2.0)

- ⏳ Wayland extension for wallpaper protocol
- ⏳ Performance profiler
- ⏳ GUI configuration tool
- ⏳ Plugin system

---

## 📚 Documentation Index

- [README.md](../README.md) - Project overview and quick start
- [BUILD.md](../BUILD.md) - Build instructions
- [USAGE_GUIDE.md](USAGE_GUIDE.md) - User guide
- [IPC_MIGRATION.md](IPC_MIGRATION.md) - IPC architecture
- [COMMAND_REGISTRY_IMPROVEMENTS.md](COMMAND_REGISTRY_IMPROVEMENTS.md) - Command system
- [CONFIG_STATE_DESIGN.md](CONFIG_STATE_DESIGN.md) - Configuration design
- [compositor/README.md](../src/neowalld/compositor/README.md) - Compositor abstraction
- [commands/COMMANDS.md](commands/COMMANDS.md) - Command reference

---

## 🤝 Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

### Areas Needing Help

1. **Compositor Backends**: KDE Plasma, GNOME implementations
2. **Shader Library**: Community-contributed shaders
3. **Documentation**: Tutorials, examples
4. **Testing**: Multi-compositor testing
5. **Packaging**: Distribution packages

---

## 📜 License

**GNU General Public License v3.0**

See [LICENSE](../LICENSE) for full text.

---

**Project Maintainer**: [@1ay1](https://github.com/1ay1)  
**Repository**: https://github.com/1ay1/neowall  
**Documentation Version**: 1.0  
**Last Updated**: 2024-01-15