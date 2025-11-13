# NeoWall System Tray - Menu Structure

**Status:** Complete and Tested  
**Date:** 2024  
**Branch:** `icon-tray`

---

## 📋 Menu Structure

The NeoWall system tray provides a clean, organized menu for controlling the wallpaper daemon.

### Complete Menu Layout

```
┌─────────────────────────────────────┐
│ ● NeoWall Running                   │  ← Status indicator
├─────────────────────────────────────┤
│ Wallpaper                         ▶ │  ← Submenu
│   ├─ Next Wallpaper                 │
│   ├─ Previous Wallpaper             │
│   └─ Show Current                   │
│                                     │
│ Cycling                           ▶ │  ← Submenu
│   ├─ Pause Cycling                  │
│   └─ Resume Cycling                 │
│                                     │
│ Live Animation                    ▶ │  ← Submenu
│   ├─ Pause Animation                │
│   ├─ Resume Animation               │
│   ├─────────────────────            │
│   ├─ Speed Up                       │
│   └─ Speed Down                     │
├─────────────────────────────────────┤
│ Show Full Status                    │
├─────────────────────────────────────┤
│ Reload Configuration                │
│ Edit Configuration...               │
│ Restart Daemon                      │
│ Stop Daemon                         │
├─────────────────────────────────────┤
│ About NeoWall                       │
│ Quit Tray                           │
└─────────────────────────────────────┘
```

---

## 🎯 Menu Sections

### 1. Status Indicator
- **● NeoWall Running** - Shows current daemon state (disabled/non-clickable)
- Updates automatically every 2 seconds
- Changes to **○ NeoWall Stopped** when daemon is not running

### 2. Wallpaper Control Submenu
Controls wallpaper cycling on all outputs:

| Menu Item | Command | Description |
|-----------|---------|-------------|
| Next Wallpaper | `next` | Switch to next wallpaper on all outputs |
| Previous Wallpaper | `prev` | Switch to previous wallpaper on all outputs |
| Show Current | `current` | Display current wallpaper information |

### 3. Cycling Control Submenu
Controls automatic wallpaper cycling:

| Menu Item | Command | Description |
|-----------|---------|-------------|
| Pause Cycling | `cycle-pause` | Pause automatic wallpaper cycling on all outputs |
| Resume Cycling | `cycle-resume` | Resume automatic wallpaper cycling on all outputs |

### 4. Live Animation Submenu
Controls shader animation and speed:

| Menu Item | Command | Description |
|-----------|---------|-------------|
| Pause Animation | `live-pause` | Pause shader/live wallpaper animation |
| Resume Animation | `live-resume` | Resume shader/live wallpaper animation |
| Speed Up | `speed-up` | Increase animation speed (1.0x → 1.5x → 2.0x, etc.) |
| Speed Down | `speed-down` | Decrease animation speed |

### 5. System Control Section
Daemon and configuration management:

| Menu Item | Command | Description |
|-----------|---------|-------------|
| Show Full Status | `status` | Display detailed daemon status dialog |
| Reload Configuration | `reload` | Reload config.vibe without restarting |
| Edit Configuration... | Interactive | Choose how to edit configuration |
| Restart Daemon | `restart` | Restart the daemon (preserves state) |
| Stop Daemon | `stop` | Stop the daemon (asks for confirmation) |

### 6. Info Section
About and quit options:

| Menu Item | Description |
|-----------|-------------|
| About NeoWall | Show version and project information |
| Quit Tray | Exit the system tray application (daemon keeps running) |

---

## 🔄 Dynamic Menu Behavior

### When Daemon is Running
Full menu is displayed with all controls available.

### When Daemon is Stopped
Menu adapts to show:
```
┌─────────────────────────────────────┐
│ ○ NeoWall Stopped                   │
├─────────────────────────────────────┤
│ Start Daemon                        │  ← Only system control shown
├─────────────────────────────────────┤
│ Edit Configuration...               │
├─────────────────────────────────────┤
│ About NeoWall                       │
│ Quit Tray                           │
└─────────────────────────────────────┘
```

- Wallpaper, Cycling, and Live Animation controls are hidden
- **Start Daemon** button becomes available
- Restart and Stop options are removed

---

## 🎨 Design Principles

### Clean Text Labels
- **No emojis** - Ensures consistent alignment across all GTK themes
- **Clear, descriptive names** - No ambiguity about what each item does
- **Ellipsis for dialogs** - "Edit Configuration..." indicates it will open a dialog

### Logical Grouping
- Related controls grouped into submenus
- Separators between major sections
- Most common actions (wallpaper control) at the top

### Visual Hierarchy
1. Status indicator (always visible)
2. Primary controls (wallpaper, cycling, animation)
3. System controls (configuration, daemon management)
4. Info/exit options (about, quit)

---

## 🔌 Command Mapping

All menu items execute standard `neowall` CLI commands:

```bash
# Wallpaper submenu
neowall next
neowall prev
neowall current

# Cycling submenu
neowall cycle-pause
neowall cycle-resume

# Live Animation submenu
neowall live-pause
neowall live-resume
neowall speed-up
neowall speed-down

# System controls
neowall status
neowall reload
neowall restart
neowall stop
neowall start
```

This ensures consistency between CLI and GUI control.

---

## 📱 User Experience Features

### Automatic Updates
- Status indicator refreshes every 2 seconds
- Menu structure changes automatically when daemon starts/stops
- No manual refresh needed

### Confirmation Dialogs
- **Stop Daemon** - Asks for confirmation before stopping
- Prevents accidental shutdowns

### Feedback Dialogs
- **Reload Configuration** - Shows success/error message
- **Start/Restart/Stop** - Shows progress with auto-close (2 seconds)
- Non-blocking notifications

### Edit Configuration Dialog
When clicking "Edit Configuration...", user gets three options:
1. **Open Editor** - Opens config.vibe in default editor
2. **Show Path** - Displays the configuration file path
3. **Open Folder** - Opens ~/.config/neowall in file manager

---

## 🧪 Testing

All menu commands have been tested and verified:

```bash
./tests/test_tray_menu_commands.sh
```

**Test Results:**
- ✅ All 15 commands working
- ✅ State changes verified (pause/resume/speed)
- ✅ Daemon control tested (start/stop/restart)
- ✅ Configuration reload working

---

## 🚀 Usage

### Starting the Tray
```bash
# Method 1: Direct
neowall_tray

# Method 2: Via neowall CLI
neowall tray

# Method 3: With debug logging
neowall_tray --debug
```

### Interacting with Menu
1. **Right-click** the tray icon to open menu
2. **Left-click** for quick status
3. Use keyboard navigation:
   - Arrow keys to navigate
   - Enter to select
   - Escape to close

---

## 📝 Implementation Details

### Files
- `src/neowall_tray/menu/menu_items.h` - Menu label definitions
- `src/neowall_tray/menu/menu_items.c` - Menu item structures
- `src/neowall_tray/menu/menu_callbacks.c` - Command execution
- `src/neowall_tray/menu/menu_builder.c` - Menu construction logic

### Architecture
- **Component-based** - Each subsystem (menu, dialogs, indicator) is separate
- **Callback-driven** - GTK signals trigger command execution
- **State-aware** - Menu adapts based on daemon state
- **Async commands** - Non-blocking command execution

---

## 🎉 Summary

The NeoWall system tray provides a **clean, intuitive interface** for controlling the wallpaper daemon:

✅ **Simple** - No complex hierarchies  
✅ **Consistent** - Matches CLI command names  
✅ **Adaptive** - Changes based on daemon state  
✅ **Responsive** - Non-blocking operations  
✅ **Professional** - Clean text labels, proper alignment  

Perfect for both casual users and power users! 🚀