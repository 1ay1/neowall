# NeoWall System Tray

The NeoWall system tray provides a convenient GUI interface to control the wallpaper daemon from your system tray.

## Features

- **Quick Access**: Control all NeoWall commands from a single menu
- **Status Monitoring**: Visual indicator showing daemon status
- **Organized Menu**: Commands grouped by category for easy access
- **Auto-refresh**: Menu updates every 2 seconds to reflect daemon status

## Installation

The tray application is built automatically with NeoWall:

```bash
meson setup builddir
ninja -C builddir
sudo ninja -C builddir install
```

## Usage

### Starting the Tray

Start the tray application in the background:

```bash
neowall tray &
```

Or directly:

```bash
neowall-tray &
```

### Menu Structure

The tray menu is organized into the following sections:

#### Status Indicator
- Shows whether the daemon is running or stopped
- Updates automatically every 2 seconds

#### Wallpaper Controls
- **Next**: Switch to next wallpaper
- **Previous**: Switch to previous wallpaper
- **Show Current**: Display current wallpaper path

#### Cycling Controls
- **Pause**: Pause automatic wallpaper cycling
- **Resume**: Resume automatic wallpaper cycling

#### Shader Effects
- **Pause Animation**: Freeze shader animation
- **Resume Animation**: Resume shader animation
- **Speed Up**: Increase shader animation speed
- **Speed Down**: Decrease shader animation speed

#### System Controls
- **Show Status**: Display detailed daemon status
- **Edit Configuration**: Open config file in default editor
- **Restart Daemon**: Restart the wallpaper daemon
- **Stop Daemon**: Stop the wallpaper daemon (tray remains active)
- **Start Daemon**: Start the daemon (only shown when stopped)

#### Information
- **About**: Show version and project information
- **Quit Tray**: Close the tray application

## Autostart

To launch the tray automatically at login:

### Method 1: Desktop Entry (GNOME, KDE, etc.)

Copy the desktop entry to autostart:

```bash
mkdir -p ~/.config/autostart
cp /usr/share/applications/neowall-tray.desktop ~/.config/autostart/
```

### Method 2: Systemd User Service

Create `~/.config/systemd/user/neowall-tray.service`:

```ini
[Unit]
Description=NeoWall System Tray
After=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/neowall-tray
Restart=on-failure

[Install]
WantedBy=default.target
```

Enable and start:

```bash
systemctl --user enable neowall-tray.service
systemctl --user start neowall-tray.service
```

### Method 3: Window Manager Config

Add to your WM startup script (e.g., `~/.config/i3/config`):

```
exec --no-startup-id neowall-tray
```

## Requirements

- GTK+ 3.22 or later
- libappindicator3 (for system tray support)
- A desktop environment or window manager with system tray support

## Supported Desktop Environments

The tray application works on:

- ✅ GNOME (with TopIcons Plus or AppIndicator extension)
- ✅ KDE Plasma
- ✅ XFCE
- ✅ MATE
- ✅ Cinnamon
- ✅ i3 (with i3bar)
- ✅ Sway (with waybar + tray module)
- ✅ Most other DEs with system tray support

## Troubleshooting

### Tray icon not showing

**GNOME Users**: Install the AppIndicator extension:
```bash
# Ubuntu/Debian
sudo apt install gnome-shell-extension-appindicator

# Arch Linux
sudo pacman -S gnome-shell-extension-appindicator
```

Then enable it:
```bash
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com
```

**Sway/Wayland Users**: Ensure your bar supports tray (e.g., waybar):
```json
// waybar config.json
{
    "modules-right": ["tray"],
    "tray": {
        "icon-size": 16,
        "spacing": 10
    }
}
```

### Commands not working

- Ensure `neowall` binary is in your PATH
- Check that the daemon is running: `neowall status`
- Verify socket permissions: `ls -l /run/user/$UID/neowall.sock`

### Menu not updating

The menu refreshes every 2 seconds. If it's not updating:
- Check if the tray process is running: `pidof neowall-tray`
- Restart the tray: `pkill neowall-tray && neowall-tray &`

## Command Equivalents

Tray menu actions correspond to these CLI commands:

| Menu Action | CLI Command |
|------------|-------------|
| Next | `neowall next` |
| Previous | `neowall prev` |
| Show Current | `neowall current` |
| Pause | `neowall pause` |
| Resume | `neowall resume` |
| Pause Animation | `neowall shader-pause` |
| Resume Animation | `neowall shader-resume` |
| Speed Up | `neowall speed-up` |
| Speed Down | `neowall speed-down` |
| Show Status | `neowall status` |
| Edit Configuration | `neowall config` |
| Restart Daemon | `neowall restart` |
| Stop Daemon | `neowall stop` |
| Start Daemon | `neowall start` |

## Tips

- **Right-click** on the tray icon to open the menu
- The tray icon color changes when the daemon is stopped (attention state)
- You can keep the tray running even when the daemon is stopped
- The tray automatically detects when you start/stop the daemon via CLI
- Use the tray for quick wallpaper switching without opening a terminal

## See Also

- [Main Documentation](../README.md)
- [Configuration Guide](CONFIG.md)
- [IPC Documentation](IPC_MIGRATION.md)