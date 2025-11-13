# Building NeoWall

Complete guide to building NeoWall from source.

## Quick Start

```bash
# Configure and build
meson setup builddir
ninja -C builddir

# Run without installing
builddir/bin/neowalld &
builddir/bin/neowall status
builddir/bin/neowall_tray &
```

## Dependencies

### Required

- **Build tools:**
  - `meson` (≥0.60)
  - `ninja`
  - `gcc` or `clang`

- **Wayland:**
  - `wayland-client` (≥1.20)
  - `wayland-protocols` (≥1.25)
  - `wayland-egl`

- **Graphics:**
  - `egl`
  - `glesv2` (OpenGL ES 3.x)

- **System tray:**
  - `gtk+-3.0` (≥3.22)
  - `libappindicator3-0.1`

### Optional

- `libpng` - PNG image support
- `libjpeg` - JPEG image support

### Install Dependencies

**Arch Linux:**
```bash
sudo pacman -S meson ninja gcc wayland wayland-protocols gtk3 libappindicator-gtk3 mesa libpng libjpeg
```

**Ubuntu/Debian:**
```bash
sudo apt install meson ninja-build gcc libwayland-dev wayland-protocols \
  libwayland-egl1 libegl1-mesa-dev libgles2-mesa-dev libgtk-3-dev \
  libappindicator3-dev libpng-dev libjpeg-dev
```

**Fedora:**
```bash
sudo dnf install meson ninja-build gcc wayland-devel wayland-protocols-devel \
  mesa-libEGL-devel mesa-libGLES-devel gtk3-devel libappindicator-gtk3-devel \
  libpng-devel libjpeg-turbo-devel
```

## Build Configuration

### Standard Build (Release)

```bash
meson setup builddir
ninja -C builddir
```

### Debug Build

```bash
meson setup builddir -Dbuildtype=debug
ninja -C builddir
```

### Custom Build Options

```bash
# Debug build with verbose output
meson setup builddir -Dbuildtype=debug -Dwarning_level=3

# Release build with optimizations
meson setup builddir -Dbuildtype=release -Doptimization=3
```

## Build Output

Binaries are generated in `builddir/bin/`:

```
builddir/bin/
├── neowalld       # Daemon (wallpaper renderer)
├── neowall        # Client CLI (control interface)
└── neowall_tray   # System tray application
```

Additionally, a version header is auto-generated:

```
builddir/version.h  # Generated from meson.build version
```

## Installation

### System-wide Installation

```bash
sudo ninja -C builddir install
```

This installs:
- Binaries to `/usr/local/bin/`
- Icons to `/usr/local/share/pixmaps/`
- Desktop files to `/usr/local/share/applications/`
- Headers to `/usr/local/include/neowall/`

### Custom Install Prefix

```bash
meson setup builddir --prefix=/usr
sudo ninja -C builddir install
```

### Uninstall

```bash
sudo ninja -C builddir uninstall
```

## Development Workflow

### Clean Rebuild

```bash
# Wipe build directory and reconfigure
meson setup builddir --wipe --reconfigure
ninja -C builddir
```

### Incremental Build

```bash
# Just rebuild what changed
ninja -C builddir
```

### Build Only Specific Components

```bash
ninja -C builddir src/neowalld/neowalld   # Just daemon
ninja -C builddir src/neowall/neowall     # Just client
ninja -C builddir src/neowall_tray/neowall_tray  # Just tray
```

## Running from Build Directory

No installation needed for development:

```bash
# Start daemon
builddir/bin/neowalld &

# Control daemon
builddir/bin/neowall next
builddir/bin/neowall status
builddir/bin/neowall --json list

# Launch tray
builddir/bin/neowall_tray &

# Or use the convenient shortcut
builddir/bin/neowall tray &
```

## Troubleshooting

### Missing Dependencies

If meson fails with dependency errors:

```bash
# Check which dependencies are found
meson setup builddir

# Look for "FOUND: YES/NO" in output
```

### Build Failures

```bash
# Clean and rebuild
rm -rf builddir
meson setup builddir
ninja -C builddir
```

### Wayland Protocols Not Found

```bash
# Arch Linux
sudo pacman -S wayland-protocols

# Ubuntu/Debian
sudo apt install wayland-protocols

# Fedora
sudo dnf install wayland-protocols-devel
```

### GTK/AppIndicator Issues

```bash
# Check GTK3 version
pkg-config --modversion gtk+-3.0

# Must be ≥3.22
```

### Icon Generation

If icons are missing:

```bash
# Requires ImageMagick
sudo pacman -S imagemagick  # Arch
sudo apt install imagemagick  # Debian/Ubuntu

# Generate icons
scripts/generate_icons.sh
```

## Verification

### Check Version

```bash
builddir/bin/neowall version
```

Expected output:
```
NeoWall 0.4.0
GPU-Accelerated Shader Wallpapers for Wayland

License: MIT
Website: https://github.com/1ay1/neowall
Author: github.com/1ay1
```

### Check Build Info

```bash
builddir/bin/neowall help
```

### Test Daemon

```bash
# Start daemon
builddir/bin/neowalld &

# Check status
builddir/bin/neowall status

# Should output:
# OK
# ● neowalld is running
```

## Version Management

Version is centrally defined in `meson.build`:

```meson
project('neowall', 'c',
  version: '0.4.0',  # ← Edit here
  ...
)
```

After changing version:

```bash
meson setup builddir --reconfigure
ninja -C builddir
```

All binaries, headers, and about dialogs automatically use the new version.

## Build Flags

Meson automatically sets appropriate flags based on build type:

- **Debug:** `-g -O0` (debug symbols, no optimization)
- **Release:** `-O3 -DNDEBUG` (full optimization)
- **Warnings:** `-Wall -Wextra -Werror` (all warnings as errors)

## Platform Support

- **Linux:** Full support (Wayland required)
- **BSD:** Should work (untested)
- **X11:** Not supported (Wayland-only)

## Performance

Release builds are optimized for performance:

- Client commands: ~1-2ms execution time
- Stop/restart: <100ms
- Low memory footprint
- GPU-accelerated rendering

## Contributing

When building for development:

1. Use debug builds: `meson setup builddir -Dbuildtype=debug`
2. Enable verbose output: `ninja -C builddir -v`
3. Run from build directory (no install needed)
4. All warnings treated as errors (`-Werror`)

## Additional Resources

- [Main README](README.md) - Project overview
- [docs/USAGE.md](docs/USAGE_GUIDE.md) - Usage guide
- [docs/TRAY.md](docs/TRAY.md) - System tray documentation
- [GitHub Issues](https://github.com/1ay1/neowall/issues) - Bug reports

## License

MIT License - See [LICENSE](LICENSE) file for details.