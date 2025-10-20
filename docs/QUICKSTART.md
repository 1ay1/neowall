# Staticwall Quick Start Guide

Get up and running with Staticwall in 5 minutes!

## Prerequisites

You need:
- A Wayland compositor with wlr-layer-shell support (Sway, Hyprland, river, etc.)
- Build tools (gcc, make)
- Some wallpaper images

## Installation (One-Liner)

### Debian/Ubuntu
```bash
sudo apt install -y build-essential pkg-config libwayland-dev libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev libpng-dev libjpeg-dev wayland-protocols git && cd /tmp && git clone https://github.com/cktan/tomlc99.git && cd tomlc99 && make && sudo make install && sudo ldconfig && cd - && make && sudo make install
```

### Arch Linux
```bash
yay -S tomlc99 && make && sudo make install
```

## Basic Setup

### 1. Create Config Directory
```bash
mkdir -p ~/.config/staticwall
```

### 2. Create Minimal Config
```bash
cat > ~/.config/staticwall/config.toml << 'EOF'
[default]
path = "~/Pictures/wallpaper.png"
mode = "fill"
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
```toml
[default]
path = "/home/user/wallpaper.jpg"
mode = "fill"
```

### Different Wallpapers per Monitor
```toml
[default]
path = "~/Pictures/default.png"
mode = "fill"

[output.eDP-1]
path = "~/Pictures/laptop.jpg"

[output.HDMI-A-1]
path = "~/Pictures/monitor.png"
```

### Wallpaper Cycling (Slideshow)
```toml
[default]
path = "~/Pictures/wallpaper1.png"
mode = "fill"
duration = 300
transition = "fade"
cycle = [
    "~/Pictures/wallpaper1.png",
    "~/Pictures/wallpaper2.jpg",
    "~/Pictures/wallpaper3.png"
]
```

## Display Modes

| Mode | Description |
|------|-------------|
| `fill` | Fill screen, maintain aspect, crop edges (recommended) |
| `fit` | Fit inside screen, maintain aspect, show letterboxing |
| `center` | Center without scaling |
| `stretch` | Stretch to fill (may distort) |
| `tile` | Tile/repeat the image |

## Find Your Monitor Names

**Sway:**
```bash
swaymsg -t get_outputs | grep "Output"
```

**Hyprland:**
```bash
hyprctl monitors | grep "Monitor"
```

**wlr-randr (generic):**
```bash
wlr-randr | grep "^[A-Z]"
```

## Common Tasks

### Run in Background
```bash
staticwall --daemon
```

### Auto-start with Compositor

**Sway** (`~/.config/sway/config`):
```
exec staticwall --daemon
```

**Hyprland** (`~/.config/hypr/hyprland.conf`):
```
exec-once = staticwall --daemon
```

### Reload Configuration
```bash
killall -HUP staticwall
```

### Stop Staticwall
```bash
killall staticwall
```

## Troubleshooting

### Not seeing wallpaper?
1. Check image path is correct: `ls -lh ~/Pictures/wallpaper.png`
2. Check config syntax: `cat ~/.config/staticwall/config.toml`
3. Run in verbose mode: `staticwall -v`

### "Failed to connect to Wayland display"
```bash
echo $WAYLAND_DISPLAY  # Should show wayland-0 or similar
```
Make sure you're running under a Wayland session.

### "wlr-layer-shell protocol not available"
Your compositor doesn't support it. Supported: Sway, Hyprland, river, wayfire.
Not supported: GNOME, KDE Plasma (without extensions).

### Build fails with "toml.h not found"
```bash
cd /tmp
git clone https://github.com/cktan/tomlc99.git
cd tomlc99
make
sudo make install
sudo ldconfig
```

## Next Steps

- Read [README.md](README.md) for full feature list
- See [INSTALL.md](INSTALL.md) for detailed installation
- Check [config/staticwall.toml](config/staticwall.toml) for all options
- Join community for support

## Tips

1. **Use absolute paths** or `~` in config - relative paths may not work
2. **PNG and JPEG** formats are supported
3. **Config file watch** mode: `staticwall --watch` auto-reloads on changes
4. **Test your config** before making it auto-start
5. **Check compositor compatibility** before reporting bugs

## Example: Complete Setup

```bash
# 1. Install dependencies (Ubuntu/Debian)
sudo apt install -y build-essential pkg-config libwayland-dev \
    libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols

# 2. Install tomlc99
cd /tmp
git clone https://github.com/cktan/tomlc99.git
cd tomlc99 && make && sudo make install && sudo ldconfig

# 3. Build staticwall
cd ~/Downloads/staticwall
make
sudo make install

# 4. Setup config
mkdir -p ~/.config/staticwall
cat > ~/.config/staticwall/config.toml << 'EOF'
[default]
path = "~/Pictures/nature.jpg"
mode = "fill"
EOF

# 5. Test run
staticwall -v

# 6. If works, add to compositor config
echo 'exec staticwall --daemon' >> ~/.config/sway/config

# 7. Reload compositor or reboot
swaymsg reload
```

## Getting Help

1. **Check logs**: Run with `-v` flag for verbose output
2. **Verify paths**: All image paths must exist
3. **Test protocol**: `wlr-randr` to check wlr support
4. **Read docs**: [README.md](README.md) has detailed troubleshooting
5. **Report bugs**: Include compositor, version, config, and logs

---

**That's all you need to get started!** 🎨

For more advanced features like transitions, multiple monitors, and wallpaper cycling, check out the full documentation.