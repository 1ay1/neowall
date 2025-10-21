# Staticwall

**Sets wallpapers until it... doesn't.**

A lightweight, reliable Wayland wallpaper daemon written in C. Perfect for tiling window managers and minimal setups. (Mostly reliable. See tagline.)

![Staticwall Default Wallpaper](assets/default.png)

> **Why "Staticwall" if it cycles wallpapers?** Because it's *static*ally compiled C code that sets wallpapers. The fact that it can also change them dynamically is just a happy accident. We're committed to the bit.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Features

- 🎨 **Multiple Display Modes**: center, fill, fit, stretch, tile
- 🔄 **Automatic Cycling**: Rotate through wallpapers on a timer
- 🖥️ **Multi-Monitor Support**: Configure each monitor independently
- ⚡ **Hardware Accelerated**: OpenGL ES rendering via EGL
- 🔥 **Hot Reload**: Update configuration without restarting (`SIGHUP`)
- 🎬 **Smooth Transitions**: Fade and slide effects between wallpapers
- 📁 **Directory Support**: Point to a folder and automatically cycle through all images - just set `path` to a directory!
- 🪶 **Minimal Dependencies**: Pure C with no heavy frameworks (because who needs bloated libraries when you have raw pointers and existential dread?)
- 💀 **Memory Safety**: We free our allocations. Most of the time. Okay, all of the time, but the joke was funnier before we fixed the bugs.
- 🔧 **VIBE Config Format**: Simple, human-readable configuration

## Why Staticwall?

Because you have wallpapers and we have... strong opinions on how to display them. Also, we were bored on a Tuesday.

Built as a reliable replacement for `wpaperd` and similar tools, Staticwall focuses on:
- **Stability**: Robust error handling and clean resource management (sets wallpapers until it doesn't crash—which is hopefully never, but we're realistic about our place in the universe)
- **Performance**: Direct Wayland protocol usage with minimal overhead (your CPU has better things to worry about)
- **Simplicity**: Easy configuration with sensible defaults (we assume you want your wallpaper visible, which is a pretty bold assumption)
- **Compatibility**: Works with any wlroots-based compositor (Sway, Hyprland, River, etc.)
- **Single Instance**: Prevents multiple instances from running (because one wallpaper daemon is enough chaos for anyone)
- **Name Irony**: Yes, we know it cycles wallpapers dynamically. The name stays. It's static in spirit, dynamic in practice, and confusing in marketing.

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

**Note:** Staticwall runs as a normal user and doesn't require root privileges to run! Only the binary installation requires sudo. On first run, config and default wallpaper will be automatically copied to your user directories. (We promise not to do anything weird with your home folder. We're just here to set wallpapers, not start a revolution.)

### Usage

On first run, Staticwall will automatically:
- Create config at `~/.config/staticwall/config.vibe`
- Copy default wallpaper to `~/.local/share/staticwall/default.png`

No root/sudo needed to run!

```bash
# Start daemon (default - runs in background)
staticwall

# Run in foreground (for debugging/viewing logs)
staticwall -f

# Run with verbose logging (daemon mode)
staticwall -v

# Run in foreground with verbose logging
staticwall -f -v

# Use custom config
staticwall -c /path/to/config.vibe

# Watch config for changes (auto-reload on config edit)
staticwall -w

# Combine options (foreground + watch + verbose)
staticwall -f -w -v
```

### Controlling the Daemon

Once the daemon is running, use these commands to control it:

```bash
# Stop the daemon
staticwall kill

# Skip to next wallpaper immediately
staticwall next

# Pause wallpaper cycling
staticwall pause

# Resume wallpaper cycling
staticwall resume

# Reload configuration without restarting
staticwall reload

# Show current wallpaper
staticwall current
```

**Note:** Staticwall prevents multiple instances from running. If you try to start it while a daemon is already running, you'll get an error. Use `staticwall kill` first to stop the existing instance.

### Quick Examples

**Example 1: Cycle through wallpapers in a directory (most common)**
```vibe
default {
  path ~/Pictures/wallpapers/    # ← Directory with trailing slash!
  duration 300                    # Change every 5 minutes
  mode fill
  transition fade
}
```

**Example 2: Static wallpaper (no cycling)**
```vibe
default {
  path ~/Pictures/my-wallpaper.png    # ← Single file, no trailing slash
  mode fill
}
```

**Example 3: Different wallpapers per monitor**
```vibe
output {
  eDP-1 {
    path ~/Pictures/laptop-walls/    # Laptop: cycle through directory
    duration 600
  }
  
  HDMI-A-1 {
    path ~/Pictures/desk.jpg         # External monitor: static image
    mode center
  }
}
```

**Example 4: Fast cycling with transitions**
```vibe
default {
  path ~/Pictures/slideshow/
  duration 60                         # Change every minute
  transition slide_left
  transition_duration 500             # Smooth 500ms transition
  mode fill
}
```

### Configuration

Edit `~/.config/staticwall/config.vibe`:

```vibe
# Simple single wallpaper for all monitors
default {
  path ~/Pictures/wallpaper.png
  mode fill
}

# 🔥 Cycle through ALL images in a directory (recommended!)
# Just point path to a directory and Staticwall automatically loads all PNG/JPEG files
default {
  path ~/Pictures/wallpapers/    # ← Note the trailing slash for directories!
  duration 300                    # Change every 5 minutes (in seconds)
  transition fade                 # Smooth fade between wallpapers
}

# Configure specific monitor
output {
  HDMI-A-1 {
    path ~/Pictures/monitor1/
    mode fill
    duration 600
    transition fade
  }
  
  DP-1 {
    path ~/Pictures/monitor2.jpg
    mode center
  }
}
```

#### Configuration Options

**`path`** (required) - The most important setting!

**📁 DIRECTORY MODE (Recommended for cycling):**
- Point to a folder to automatically cycle through ALL images inside
- Example: `path ~/Pictures/wallpapers/`
- **IMPORTANT:** Must end with `/` slash for directories!
- Staticwall auto-detects and loads all PNG/JPEG files in the directory
- Images cycle in alphabetical order by filename
- Subdirectories are NOT scanned (only top-level files)
- Hidden files (starting with `.`) are ignored

**📄 SINGLE FILE MODE:**
- Path to one specific image file (PNG or JPEG)
- Example: `path ~/Pictures/wallpaper.png`
- Wallpaper stays static unless manually changed

**`mode`** (optional, default: `fill`)
- Display mode for the wallpaper

**`duration`** (optional, default: `0` - disabled)
- Time in seconds before switching to next wallpaper
- Only works when `path` is a directory or when cycling is configured
- Set to `0` to disable automatic cycling
- Examples: `300` (5 min), `600` (10 min), `1800` (30 min), `3600` (1 hour)

**`transition`** (optional, default: `none`)
- Animation effect when changing wallpapers
- Only visible when cycling between wallpapers

**`transition_duration`** (optional, default: `300` ms)
- How long the transition animation takes
- Value in milliseconds
- Examples: `100` (fast), `500` (smooth), `1000` (slow)

#### Display Modes

- **fill** - Scale to fill screen, crop if needed (recommended for most wallpapers)
- **fit** - Scale to fit inside screen, maintain aspect ratio (may show black bars)
- **center** - Center image without scaling (good for logos or patterns)
- **stretch** - Stretch to fill screen (may distort image)
- **tile** - Repeat/tile the image to fill screen (good for patterns)

#### Transitions

- **none** - Instant change (no animation)
- **fade** - Smooth fade/crossfade between images
- **slide_left** - New wallpaper slides in from right to left
- **slide_right** - New wallpaper slides in from left to right

#### Configuration Sections

**`default { }`**
- Applies to all monitors unless overridden
- Required if you don't specify per-monitor configs

**`output { monitor-name { } }`**
- Per-monitor configuration
- Overrides `default` settings for specific monitors
- Find monitor names with: `swaymsg -t get_outputs` or `hyprctl monitors`

#### Complete Configuration Example

```vibe
# Default config for all monitors
default {
  path ~/Pictures/Wallpapers/
  mode fill
  duration 900
  transition fade
  transition_duration 500
}

# Per-monitor overrides
output {
  # Laptop display - slower cycle
  eDP-1 {
    path ~/Pictures/Laptop/
    mode fit
    duration 1800
    transition fade
  }
  
  # Main monitor - different wallpaper set
  DP-1 {
    path ~/Pictures/Desktop/
    mode fill
    duration 600
    transition slide_left
    transition_duration 300
  }
  
  # Vertical monitor - static wallpaper
  HDMI-A-1 {
    path ~/Pictures/vertical.png
    mode fit
    # No duration = no cycling
  }
}
```

**Pro Tips:**
- Organize wallpapers in subdirectories: `~/Pictures/Wallpapers/Nature/`, `~/Pictures/Wallpapers/Abstract/`
- Point `path` to different directories for different moods/themes
- Use `mode fit` for ultrawide monitors to avoid excessive cropping
- Set longer `duration` for work hours, shorter for leisure
- Use `transition none` for minimal resource usage
- Hot-reload config with `killall -HUP staticwall` or `staticwall --watch` to test settings live

## Documentation

- [Installation Guide](docs/INSTALL.md) - Detailed installation instructions
- [Quick Start Guide](docs/QUICKSTART.md) - Get up and running fast
- [Configuration Guide](docs/CONFIG_GUIDE.md) - Complete configuration reference
- [Directory Cycling Guide](docs/DIRECTORY_CYCLING.md) - How to cycle through wallpaper folders
- [VIBE Tutorial](docs/VIBE_TUTORIAL.md) - Learn the VIBE config format

## Dependencies

### Required
- Wayland client library (because we're not savages living in X11)
- EGL (OpenGL ES interface)
- GLESv2 (OpenGL ES 2.0)
- pthread (for that sweet, sweet concurrency)

### Optional (for image loading)
- libpng (PNG support—recommended unless you enjoy staring at error messages)
- libjpeg (JPEG support—because your phone camera outputs these, apparently)

## Supported Compositors

Staticwall works with any Wayland compositor that implements `wlr-layer-shell`:

- ✅ Sway
- ✅ Hyprland
- ✅ River
- ✅ Wayfire
- ✅ Newm
- ✅ Other wlroots-based compositors

## Commands

### Starting Staticwall

```bash
staticwall [OPTIONS]

OPTIONS:
  -c, --config PATH    Specify config file path
  -f, --foreground     Run in foreground (for debugging)
  -w, --watch          Watch config file for changes
  -v, --verbose        Enable verbose logging
  -h, --help           Show help message
  -V, --version        Show version information
```

By default, `staticwall` runs as a daemon (background process). Use `-f` to run in foreground.

### Daemon Control Commands

When the daemon is running, control it with these commands:

```bash
staticwall kill      # Stop the daemon gracefully
staticwall next      # Skip to next wallpaper immediately
staticwall pause     # Pause wallpaper cycling
staticwall resume    # Resume wallpaper cycling
staticwall reload    # Reload configuration without restarting
staticwall current   # Show current wallpaper and status
```

### Signals (Advanced)

You can also control the daemon using signals:

- `SIGUSR1` - Skip to next wallpaper
- `SIGUSR2` - Pause wallpaper cycling
- `SIGCONT` - Resume wallpaper cycling
- `SIGHUP` - Reload configuration
- `SIGTERM/SIGINT` - Graceful shutdown

```bash
# Examples using killall
killall -USR1 staticwall    # Next wallpaper
killall -USR2 staticwall    # Pause cycling
killall -CONT staticwall    # Resume cycling
killall -HUP staticwall     # Reload config
killall -TERM staticwall    # Stop daemon
```

**Note:** Staticwall automatically prevents multiple instances from running. If you try to start it while it's already running, you'll get a helpful error message with the PID of the running instance. Use `staticwall kill` to stop it first.

## Troubleshooting

### Already running error
If you get "Staticwall is already running", this means an instance is already active:
1. Stop it with: `staticwall kill`
2. Or check if it's a stale PID: the error shows the PID, verify with `ps -p <PID>`
3. If PID doesn't exist, remove the PID file: `rm $XDG_RUNTIME_DIR/staticwall.pid`

### Wallpaper not showing
1. Check compositor supports `wlr-layer-shell`
2. Verify image path exists: `ls -la ~/Pictures/wallpaper.png`
3. Run with `-v` flag to see detailed logs
4. Check permissions on image files

### Cycling not working
1. **Check path is a directory:** Make sure path ends with `/` (e.g., `~/Pictures/wallpapers/`)
2. **Ensure duration is set:** `duration 300` (or any value > 0) must be in your config
3. **Verify directory has images:** 
   ```bash
   ls ~/Pictures/wallpapers/*.{png,jpg,jpeg}
   ```
4. **Check supported formats:** Only PNG and JPEG/JPG files are loaded
5. **Run with verbose logging:** `staticwall -v` to see cycling messages
6. **Verify directory permissions:** Ensure you have read access to the directory and images
7. **Check for hidden files:** Files starting with `.` are ignored

**Example working config:**
```vibe
default {
  path ~/Pictures/wallpapers/    # Note the trailing slash!
  duration 300                    # 5 minutes
  mode fill
}
```

### Directory not being detected as directory
- **Problem:** Staticwall treats your directory as a single file
- **Solution:** Add a trailing slash: `~/Pictures/wallpapers/` not `~/Pictures/wallpapers`
- **Verify:** Run `staticwall -v` and check if it says "Loading images from directory" or "Loading single image"

### No images found in directory
1. **Check file extensions:** Only `.png`, `.jpg`, `.jpeg` are supported (case-insensitive)
2. **Check subdirectories:** Staticwall only scans the specified directory, not subdirectories
3. **Test manually:** 
   ```bash
   find ~/Pictures/wallpapers/ -maxdepth 1 -type f \( -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" \)
   ```
4. **File permissions:** Ensure image files are readable: `chmod 644 ~/Pictures/wallpapers/*`

### Config not loading
1. Verify config syntax (no quotes on values)
2. Check file permissions
3. Run `staticwall -v` to see config parsing errors

## Development

### Building

```bash
# Debug build with symbols (for when things inevitably go wrong)
make debug

# Clean rebuild (the "have you tried turning it off and on again" of compilation)
make clean && make

# Run tests (we have tests! They even pass sometimes!)
make test
```

### Project Structure

```
staticwall/
├── src/           # Source files (where the magic happens)
├── include/       # Header files (where the magic is declared)
├── protocols/     # Wayland protocol definitions (generated, do not touch unless you enjoy pain)
├── config/        # Example configuration (copy-paste friendly)
├── assets/        # Default wallpaper (a suspiciously large PNG)
├── docs/          # Documentation (yes, we wrote docs. shocking, we know.)
└── Makefile       # Build configuration (artisanal, hand-crafted build scripts)
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

If Staticwall doesn't meet your needs (or you just don't appreciate our humor), check out:
- [wpaperd](https://github.com/danyspin97/wpaperd) - Rust-based wallpaper daemon (memory-safe but written in a language with opinions)
- [swaybg](https://github.com/swaywm/swaybg) - Simple wallpaper tool for Sway (does one thing well, unlike us who do several things adequately)
- [mpvpaper](https://github.com/GhostNaN/mpvpaper) - Video wallpapers with mpv (for when static images aren't draining your battery fast enough)
- [hyprpaper](https://github.com/hyprwm/hyprpaper) - Wallpaper utility for Hyprland (by people who actually know what they're doing)

## FAQ

### Why is it called "Staticwall" if it changes wallpapers dynamically?

Because it's written in statically compiled C code. The fact that your wallpaper cycles is merely a dynamic side effect of our static determinism. We're committed to the name, even if it doesn't make sense anymore.

Also, "Dynamicwall" was taken and "Sometimesstaticwall" didn't have the same ring to it.

### Does it really set wallpapers until it doesn't?

Yes. It sets wallpapers reliably until something goes wrong (like you unplugging all your monitors, or the heat death of the universe). We're being honest about our limitations.

### Why another wallpaper daemon?

Because the existing ones didn't crash the way we wanted them to. Just kidding – we actually focused on *not* crashing, which turned out to be surprisingly effective. Turns out proper error handling is better than artistic segfaults. Who knew?

### How do I stop the daemon?

Just run `staticwall kill`. It'll gracefully stop the running daemon, clean up resources, and remove the PID file. Much more civilized than `killall`, though that works too if you're feeling nostalgic for the old ways.

### How do I cycle through all wallpapers in a folder?

Just point `path` to a directory with a trailing slash! Staticwall automatically detects all PNG/JPEG files and cycles through them.

```vibe
default {
  path ~/Pictures/wallpapers/    # ← Trailing slash is important!
  duration 300                    # Change every 5 minutes
}
```

That's it! No need to list each file manually. Organize your wallpapers in folders and point to the folder.

### Why isn't my directory being recognized?

Make sure you include the trailing slash: `~/Pictures/wallpapers/` not `~/Pictures/wallpapers`. Without the slash, Staticwall thinks it's a single file, not a directory.

Run `staticwall -v` to see if it says "Loading images from directory" or trying to load a single file.

### Can I cycle through subdirectories?

No, Staticwall only scans the immediate directory you specify (non-recursive). If you want to include subdirectories, you'll need to move or symlink the images to a single folder.

This is intentional to keep things simple and predictable.

### Can I run multiple instances?

No, and you don't want to. Staticwall prevents multiple instances from running simultaneously because having multiple wallpaper daemons fighting over your display is a recipe for confusion (and possibly an avant-garde art installation). If you need to restart it, use `staticwall kill` first.

Once the daemon is running, any subsequent calls to `staticwall` with control commands (`kill`, `next`, `pause`, `resume`, `reload`) will control the running daemon instead of starting a new instance.

### Is this production-ready?

It sets wallpapers. On Wayland. Until it doesn't. You decide what "production-ready" means to you. We've been using it for months without issues, but then again, we wrote it, so we know exactly which buttons not to press.

### Will this eat my RAM?

Staticwall uses **intelligent display-aware scaling** that optimizes images based on your actual display resolution and wallpaper mode. Images are automatically downscaled (never upscaled) to save memory while maintaining quality.

**How it works:**
- **FILL mode**: Scales to match display dimensions (may crop)
- **FIT mode**: Scales to fit entirely within display
- **STRETCH mode**: Scales to exact display size
- **CENTER/TILE modes**: Only scales down if larger than display

**Example scaling on 3440x1440 display:**
- 5000×2500 image in FILL mode → 3440×1720 (~24 MB uncompressed)
- 5000×2500 image in FIT mode → 2880×1440 (~17 MB uncompressed)
- 5669×2835 image in FILL mode → 3440×1720 (~24 MB uncompressed)

**Memory usage:** Two 5000×2500 images on a 3440×1440 display uses ~245 MB total:
- Image data: ~50 MB (scaled down from ~114 MB)
- GPU textures: ~50 MB (mirrors image data)
- OpenGL/EGL/Wayland libraries: ~100 MB
- Process overhead: ~45 MB

**Quality:** Uses high-quality bilinear interpolation for smooth, artifact-free scaling.

Your browser tabs are still doing far worse things behind your back.

**Tip:** Staticwall automatically optimizes everything. Just use whatever images you want - they'll be scaled intelligently!

### Can I trust C code written by someone with this sense of humor?

Bold of you to assume correlation between humor quality and code quality. But yes, we take the actual code seriously. The jokes are just coping mechanisms.

## Support

- **Issues**: [GitHub Issues](https://github.com/1ay1/staticwall/issues)
- **Discussions**: [GitHub Discussions](https://github.com/1ay1/staticwall/discussions)

---

Made with ❤️ for the Wayland community