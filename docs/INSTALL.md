# Installation Guide for Staticwall

This guide will help you install and set up Staticwall on your system.

## Quick Start

For the impatient:

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install build-essential pkg-config libwayland-dev \
    libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev wayland-protocols

# Install tomlc99
git clone https://github.com/cktan/tomlc99.git
cd tomlc99 && make && sudo make install && cd ..

# Build and install staticwall
cd staticwall
make
sudo make install

# Create config
mkdir -p ~/.config/staticwall
cp config/staticwall.toml ~/.config/staticwall/config.toml
# Edit the config file with your wallpaper paths
nano ~/.config/staticwall/config.toml

# Run
staticwall
```

## Detailed Installation

### Step 1: Install Dependencies

#### Debian/Ubuntu/Pop!_OS

```bash
sudo apt update
sudo apt install build-essential pkg-config \
    libwayland-dev libwayland-egl-backend-dev \
    libegl1-mesa-dev libgles2-mesa-dev \
    libpng-dev libjpeg-dev \
    wayland-protocols git
```

#### Arch Linux/Manjaro

```bash
sudo pacman -S base-devel wayland wayland-protocols \
    mesa libpng libjpeg-turbo git
```

#### Fedora

```bash
sudo dnf install gcc make pkg-config git \
    wayland-devel wayland-protocols-devel \
    mesa-libEGL-devel mesa-libGLES-devel \
    libpng-devel libjpeg-turbo-devel
```

#### Void Linux

```bash
sudo xbps-install -S base-devel pkg-config \
    wayland-devel wayland-protocols \
    MesaLib-devel libpng-devel libjpeg-turbo-devel
```

### Step 2: Install libtoml (tomlc99)

Most distributions don't package libtoml, so we need to install it manually:

```bash
cd /tmp
git clone https://github.com/cktan/tomlc99.git
cd tomlc99
make
sudo make install
sudo ldconfig  # Update library cache
```

**Arch Linux users** can install from AUR:
```bash
yay -S tomlc99
# or
paru -S tomlc99
```

### Step 3: Clone and Build Staticwall

```bash
# Clone the repository
git clone <repository-url>
cd staticwall

# Build the project
make

# The binary will be at: build/bin/staticwall
```

#### Build Options

**Debug build** (with symbols and debug logging):
```bash
make debug
```

**Clean build** (remove all artifacts):
```bash
make clean
make
```

**Clean everything** (including generated protocol files):
```bash
make distclean
make
```

### Step 4: Install System-Wide

```bash
sudo make install
```

This installs:
- Binary: `/usr/local/bin/staticwall`
- Example config: `/etc/staticwall/config.toml.example`

To install to a different prefix:
```bash
sudo make install DESTDIR=/usr
```

### Step 5: Configuration

#### Create Configuration Directory

```bash
mkdir -p ~/.config/staticwall
```

#### Create Configuration File

Copy the example config:
```bash
cp config/staticwall.toml ~/.config/staticwall/config.toml
```

Or create from scratch:
```bash
cat > ~/.config/staticwall/config.toml << 'EOF'
[default]
path = "~/Pictures/wallpaper.png"
mode = "fill"
EOF
```

#### Edit Configuration

Edit the config file with your preferred editor:
```bash
nano ~/.config/staticwall/config.toml
# or
vim ~/.config/staticwall/config.toml
# or
code ~/.config/staticwall/config.toml
```

**Important**: Update the `path` values to point to your actual wallpaper images!

### Step 6: Find Your Output Names

Before configuring per-output wallpapers, you need to know your output names.

**For Sway:**
```bash
swaymsg -t get_outputs | grep name
```

**For Hyprland:**
```bash
hyprctl monitors | grep Monitor
```

**For other wlroots compositors:**
```bash
# Install wlr-randr if not already installed
# Arch: sudo pacman -S wlr-randr
# Ubuntu: sudo apt install wlr-randr

wlr-randr
```

Example output names:
- Laptop: `eDP-1`, `LVDS-1`
- HDMI: `HDMI-A-1`, `HDMI-A-2`
- DisplayPort: `DP-1`, `DP-2`

### Step 7: Run Staticwall

#### Manual Start

```bash
staticwall
```

#### Background/Daemon Mode

```bash
staticwall --daemon
```

#### With Config File Watching

```bash
staticwall --watch --daemon
```

## Autostart with Your Compositor

### Sway

Add to `~/.config/sway/config`:
```
exec staticwall --daemon
```

### Hyprland

Add to `~/.config/hypr/hyprland.conf`:
```
exec-once = staticwall --daemon
```

### river

Add to `~/.config/river/init`:
```bash
staticwall --daemon &
```

### wayfire

Add to `~/.config/wayfire.ini`:
```ini
[autostart]
staticwall = staticwall --daemon
```

## Systemd Service (Optional)

For automatic startup with systemd:

Create `~/.config/systemd/user/staticwall.service`:
```ini
[Unit]
Description=Staticwall - Wayland Wallpaper Daemon
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/local/bin/staticwall
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

Enable and start:
```bash
systemctl --user daemon-reload
systemctl --user enable staticwall.service
systemctl --user start staticwall.service
```

Check status:
```bash
systemctl --user status staticwall.service
```

View logs:
```bash
journalctl --user -u staticwall.service -f
```

## Verification

Check if staticwall is running:
```bash
pgrep -a staticwall
```

Check logs (if running in foreground):
```bash
staticwall -v  # verbose mode
```

Reload configuration:
```bash
killall -HUP staticwall
```

## Troubleshooting

### "Failed to connect to Wayland display"

Make sure `WAYLAND_DISPLAY` is set:
```bash
echo $WAYLAND_DISPLAY
```

You should see something like `wayland-0` or `wayland-1`.

### "wlr-layer-shell protocol not available"

Your compositor doesn't support wlr-layer-shell. See the README for supported compositors.

### "Failed to load configuration"

Check your config file syntax:
```bash
cat ~/.config/staticwall/config.toml
```

Make sure paths use quotes and are valid.

### Build Errors

**"toml.h: No such file or directory"**
- Install libtoml (tomlc99) as shown in Step 2
- Run `sudo ldconfig` after installing

**"wayland-scanner: command not found"**
- Install wayland-protocols package
- Ensure wayland-scanner is in PATH

**Protocol files not generating**
- Check that `/usr/share/wayland-protocols/` exists
- Try `make distclean && make`

### Permission Issues

If installation fails:
```bash
# Use sudo for system-wide install
sudo make install

# Or install to user directory
make install DESTDIR=$HOME/.local
# Add $HOME/.local/bin to PATH
```

## Uninstallation

To remove staticwall:
```bash
sudo make uninstall
```

Remove configuration:
```bash
rm -rf ~/.config/staticwall
```

If using systemd service:
```bash
systemctl --user stop staticwall.service
systemctl --user disable staticwall.service
rm ~/.config/systemd/user/staticwall.service
```

## Next Steps

- Read the [README.md](README.md) for usage examples
- Check [config/staticwall.toml](config/staticwall.toml) for all options
- Join the community for support and updates

## Getting Help

If you encounter issues not covered here:

1. Check the [README.md](README.md) troubleshooting section
2. Look for similar issues in the issue tracker
3. Ask in the community chat/forum
4. Open a new issue with detailed information

Happy wallpapering! 🎨