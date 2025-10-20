# Staticwall

**Sets wallpapers until it... doesn't.**

A lightweight, reliable Wayland wallpaper daemon written in C. Perfect for tiling window managers and minimal setups.

> **Why "Staticwall" if it cycles wallpapers?** Because it's *static*ally compiled C code that sets wallpapers. The fact that it can also change them dynamically is just a happy accident. We're committed to the bit.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Features

- 🎨 **Multiple Display Modes**: center, fill, fit, stretch, tile
- 🔄 **Automatic Cycling**: Rotate through wallpapers on a timer
- 🖥️ **Multi-Monitor Support**: Configure each monitor independently
- ⚡ **Hardware Accelerated**: OpenGL ES rendering via EGL
- 🔥 **Hot Reload**: Update configuration without restarting (`SIGHUP`)
- 🎬 **Smooth Transitions**: Fade and slide effects between wallpapers
- 📁 **Directory Support**: Point to a folder and cycle through all images
- 🪶 **Minimal Dependencies**: Pure C with no heavy frameworks
- 🔧 **VIBE Config Format**: Simple, human-readable configuration

## Why Staticwall?

Built as a reliable replacement for `wpaperd` and similar tools, Staticwall focuses on:
- **Stability**: Robust error handling and clean resource management (sets wallpapers until it doesn't crash)
- **Performance**: Direct Wayland protocol usage with minimal overhead
- **Simplicity**: Easy configuration with sensible defaults
- **Compatibility**: Works with any wlroots-based compositor (Sway, Hyprland, River, etc.)
- **Name Irony**: Yes, we know it cycles wallpapers dynamically. The name stays. It's static in spirit.

## Quick Start

### Installation

#### Arch Linux
```bash
# Using yay or your preferred AUR helper
yay -S staticwall-git
```

#### Build from Source
```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install build-essential libwayland-dev libegl-dev libgles2-mesa-dev libpng-dev libjpeg-dev

# Install dependencies (Arch Linux)
sudo pacman -S base-devel wayland wayland-protocols egl-wayland mesa libpng libjpeg-turbo

# Clone and build
git clone https://github.com/1ay1/staticwall.git
cd staticwall
make
sudo make install
```

### Usage

On first run, Staticwall will create a default configuration file at `~/.config/staticwall/config.vibe`:

```bash
# Run in foreground (see logs)
staticwall

# Run in background (daemon mode)
staticwall -d

# Run with verbose logging
staticwall -v

# Use custom config
staticwall -c /path/to/config.vibe

# Watch config for changes
staticwall --watch
```

### Configuration

Edit `~/.config/staticwall/config.vibe`:

```vibe
# Simple single wallpaper for all monitors
[default]
path ~/Pictures/wallpaper.png
mode fill

# Cycle through a directory of wallpapers
[default]
path ~/Pictures/wallpapers/
duration 300  # Change every 5 minutes
transition fade

# Configure specific monitor
[output "HDMI-A-1"]
path ~/Pictures/monitor1/
mode fill
duration 600
transition fade

[output "DP-1"]
path ~/Pictures/monitor2.jpg
mode center
```

#### Display Modes

- **fill** - Scale to fill screen, crop if needed (recommended)
- **fit** - Scale to fit inside screen, may have black bars
- **center** - Center image without scaling
- **stretch** - Stretch to fill screen, may distort
- **tile** - Tile the image

#### Transitions

- **none** - Instant change
- **fade** - Smooth fade between images
- **slide_left** - Slide from right to left
- **slide_right** - Slide from left to right

## Documentation

- [Installation Guide](docs/INSTALL.md) - Detailed installation instructions
- [Quick Start Guide](docs/QUICKSTART.md) - Get up and running fast
- [Configuration Guide](docs/CONFIG_GUIDE.md) - Complete configuration reference
- [VIBE Tutorial](docs/VIBE_TUTORIAL.md) - Learn the VIBE config format

## Dependencies

### Required
- Wayland client library
- EGL (OpenGL ES interface)
- GLESv2 (OpenGL ES 2.0)
- pthread

### Optional (for image loading)
- libpng (PNG support)
- libjpeg (JPEG support)

## Supported Compositors

Staticwall works with any Wayland compositor that implements `wlr-layer-shell`:

- ✅ Sway
- ✅ Hyprland
- ✅ River
- ✅ Wayfire
- ✅ Newm
- ✅ Other wlroots-based compositors

## Commands

```bash
staticwall [OPTIONS]

OPTIONS:
  -c, --config PATH    Specify config file path
  -d, --daemon         Run in background
  -f, --foreground     Run in foreground (default)
  -w, --watch          Watch config file for changes
  -v, --verbose        Enable verbose logging
  -h, --help           Show help message
  -V, --version        Show version information
```

### Signals

- `SIGHUP` - Reload configuration
- `SIGTERM/SIGINT` - Graceful shutdown

```bash
# Reload config
killall -HUP staticwall

# Stop daemon
killall staticwall
```

## Troubleshooting

### Wallpaper not showing
1. Check compositor supports `wlr-layer-shell`
2. Verify image path exists: `ls -la ~/Pictures/wallpaper.png`
3. Run with `-v` flag to see detailed logs
4. Check permissions on image files

### Cycling not working
1. Ensure `duration` is set in config
2. Verify directory contains supported images (PNG, JPEG)
3. Check logs for cycling messages with `-v` flag

### Config not loading
1. Verify config syntax (no quotes on values)
2. Check file permissions
3. Run `staticwall -v` to see config parsing errors

## Development

### Building

```bash
# Debug build with symbols
make debug

# Clean rebuild
make clean && make

# Run tests
make test
```

### Project Structure

```
staticwall/
├── src/           # Source files
├── include/       # Header files
├── protocols/     # Wayland protocol definitions (generated)
├── config/        # Example configuration
├── assets/        # Default wallpaper
├── docs/          # Documentation
└── Makefile       # Build configuration
```

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Credits

- VIBE parser by [1ay1/vibe](https://github.com/1ay1/vibe)
- Inspired by wpaperd, swaybg, and other Wayland wallpaper tools

## Alternatives

If Staticwall doesn't meet your needs, check out:
- [wpaperd](https://github.com/danyspin97/wpaperd) - Rust-based wallpaper daemon
- [swaybg](https://github.com/swaywm/swaybg) - Simple wallpaper tool for Sway
- [mpvpaper](https://github.com/GhostNaN/mpvpaper) - Video wallpapers with mpv
- [hyprpaper](https://github.com/hyprwm/hyprpaper) - Wallpaper utility for Hyprland

## FAQ

### Why is it called "Staticwall" if it changes wallpapers dynamically?

Because it's written in statically compiled C code. The fact that your wallpaper cycles is merely a dynamic side effect of our static determinism. We're committed to the name, even if it doesn't make sense anymore.

Also, "Dynamicwall" was taken and "Sometimesstaticwall" didn't have the same ring to it.

### Does it really set wallpapers until it doesn't?

Yes. It sets wallpapers reliably until something goes wrong (like you unplugging all your monitors, or the heat death of the universe). We're being honest about our limitations.

### Why another wallpaper daemon?

Because the existing ones didn't crash the way we wanted them to. Just kidding – we actually focused on *not* crashing, which turned out to be surprisingly effective.

### Is this production-ready?

It sets wallpapers. On Wayland. Until it doesn't. You decide what "production-ready" means to you.

## Support

- **Issues**: [GitHub Issues](https://github.com/1ay1/staticwall/issues)
- **Discussions**: [GitHub Discussions](https://github.com/1ay1/staticwall/discussions)

---

Made with ❤️ for the Wayland community