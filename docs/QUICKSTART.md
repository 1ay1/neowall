# Staticwall Quick Start Guide

**Sets wallpapers until it... doesn't.**

Get up and running with Staticwall in minutes.

## Prerequisites

- **Wayland compositor** that supports `wlr-layer-shell` (Sway, Hyprland, River, etc.)
- **Linux distribution** with build tools

## Quick Install

### Debian/Ubuntu
```bash
sudo apt install -y build-essential pkg-config libwayland-dev libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev libpng-dev libjpeg-dev wayland-protocols git && make && sudo make install
```

### Arch Linux
```bash
make && sudo make install
```

## Basic Setup

### 1. Create Config Directory
```bash
mkdir -p ~/.config/staticwall
```

### 2. Create Minimal Config
```bash
cat > ~/.config/staticwall/config.vibe << 'EOF'
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
EOF
```

**Important**: Replace `~/Pictures/wallpaper.png` with your actual wallpaper path!

### 3. Run It
```bash
staticwall
```

That's it! Your wallpaper should now be displayed.

## Quick Configuration Examples

### Single Wallpaper for All Monitors
```vibe
default {
  path ~/Pictures/wallpaper.jpg
  mode fill
}
```

### Different Wallpapers per Monitor
```vibe
default {
  path ~/Pictures/default.png
  mode fill
}

output {
  eDP-1 {
    path ~/Pictures/laptop.jpg
  }
  
  HDMI-A-1 {
    path ~/Pictures/monitor.png
  }
}
```

### Wallpaper Cycling (Slideshow)
```vibe
default {
  path ~/Pictures/wallpaper1.png
  mode fill
  duration 300
  transition fade
  cycle [
    ~/Pictures/wallpaper1.png
    ~/Pictures/wallpaper2.jpg
    ~/Pictures/wallpaper3.png
  ]
}
```

### Automatic Directory Cycling
```vibe
default {
  path ~/Pictures/Wallpapers/
  mode fill
  duration 300
  transition fade
}
```

This automatically loads all PNG and JPEG files from the directory!

## Essential Commands

```bash
# Run in foreground (see logs)
staticwall

# Run in background (daemon)
staticwall -d

# Run with verbose logging
staticwall -v

# Reload configuration
killall -HUP staticwall

# Stop daemon
killall staticwall

# Auto-reload on config changes
staticwall --watch --daemon
```

## Finding Your Monitor Names

### Sway
```bash
swaymsg -t get_outputs | grep "Output"
```

### Hyprland
```bash
hyprctl monitors | grep "Monitor"
```

### Generic (wlr-randr)
```bash
wlr-randr
```

Common monitor names:
- Laptop: `eDP-1`, `LVDS-1`
- HDMI: `HDMI-A-1`, `HDMI-A-2`
- DisplayPort: `DP-1`, `DP-2`, `DP-3`

## Display Modes

- **fill** - Scale to fill screen, crop if needed (recommended)
- **fit** - Scale to fit inside screen, may have black bars
- **center** - Center image without scaling
- **stretch** - Stretch to fill screen (may distort)
- **tile** - Repeat image to fill screen

## Common Issues

### Not seeing wallpaper?
1. Check image path is correct: `ls -lh ~/Pictures/wallpaper.png`
2. Check config syntax: `cat ~/.config/staticwall/config.vibe`
3. Run in verbose mode: `staticwall -v`

### "Failed to connect to Wayland display"
Your compositor doesn't support it. Supported: Sway, Hyprland, river, wayfire.
Not supported: GNOME, KDE Plasma (without extensions).

### "Configuration file not found"
Create the config directory: `mkdir -p ~/.config/staticwall`
Then create the config file as shown above.

### Build fails with missing headers

Make sure all dependencies are installed:
```bash
# Debian/Ubuntu
sudo apt install -y build-essential pkg-config libwayland-dev \
    libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols

# Arch Linux
sudo pacman -S base-devel wayland wayland-protocols egl-wayland mesa libpng libjpeg-turbo
```

## Next Steps

- Read [README.md](README.md) for full feature list
- See [INSTALL.md](INSTALL.md) for detailed installation
- Check [config/staticwall.vibe](config/staticwall.vibe) for all options
- Join community for support

## Tips

- Use absolute paths if `~` doesn't work: `/home/username/Pictures/wallpaper.png`
- Organize wallpapers in folders and use directory cycling
- Test config before daemonizing: `staticwall -v`
- Use `--watch` flag for auto-reload during development

## Complete Setup Example

```bash
# 1. Install dependencies
sudo apt install -y build-essential pkg-config libwayland-dev \
    libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols

# 2. Build staticwall
cd ~/Downloads/staticwall
make
sudo make install

# 3. Create config
mkdir -p ~/.config/staticwall
cat > ~/.config/staticwall/config.vibe << 'EOF'
default {
  path ~/Pictures/nature.jpg
  mode fill
}
EOF

# 4. Test run
staticwall -v

# 5. Run as daemon
staticwall -d
```

---

**You're all set!** Your wallpaper should now be displayed. Press Ctrl+C to stop if running in foreground, or use `killall staticwall` to stop the daemon.