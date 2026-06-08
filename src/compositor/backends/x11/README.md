# X11 Backend for Tiling Window Managers

**Desktop wallpapers for i3, bspwm, dwm, and other X11 tiling WMs**

This backend provides NeoWall functionality on X11 systems, specifically optimized for tiling window managers that respect EWMH (Extended Window Manager Hints) standards.

---

## 🎯 Overview

The X11 backend enables GPU-accelerated live wallpapers on X11-based systems without requiring Wayland. It creates desktop windows that sit below all other windows using standard X11 protocols.

### Key Features

- ✅ **Desktop Window Type** - Uses `_NET_WM_WINDOW_TYPE_DESKTOP`
- ✅ **Proper Stacking** - `_NET_WM_STATE_BELOW` keeps wallpaper behind windows
- ✅ **Multi-Monitor** - XRandR support for multiple displays
- ✅ **EGL Rendering** - GPU-accelerated via `EGL_PLATFORM_X11_KHR`
- ✅ **Sticky Windows** - Visible across all workspaces
- ✅ **Clean Integration** - Skip taskbar/pager hints

---

## 🖥️ Supported Window Managers

Tested and confirmed working:

| Window Manager | Status | Notes |
|----------------|--------|-------|
| **i3/i3-gaps** | ✅ Full Support | Perfect stacking behavior |
| **bspwm** | ✅ Full Support | Desktop type respected |
| **dwm** | ✅ Full Support | Works out of the box |
| **awesome** | ✅ Full Support | Lua config not needed |
| **xmonad** | ✅ Full Support | No Haskell config required |
| **qtile** | ✅ Full Support | Python config not needed |
| **herbstluftwm** | ✅ Full Support | Auto-configured |
| **openbox** | ⚠️ Partial | May need manual rules |
| **fluxbox** | ⚠️ Partial | May need manual rules |

### Requirements

All listed window managers **must support**:
- EWMH (Extended Window Manager Hints)
- `_NET_WM_WINDOW_TYPE_DESKTOP` atom
- `_NET_WM_STATE_BELOW` atom

---

## 🚀 Quick Start

### Installation

The X11 backend is automatically built if X11 development libraries are detected:

**Arch Linux:**
```bash
sudo pacman -S libx11 libxrandr
```

**Debian/Ubuntu:**
```bash
sudo apt install libx11-dev libxrandr-dev
```

**Fedora:**
```bash
sudo dnf install libX11-devel libXrandr-devel
```

### Building

```bash
cd neowall
meson setup build
ninja -C build
sudo ninja -C build install
```

Meson will automatically:
1. Detect X11 libraries via `pkg-config`
2. Enable `HAVE_X11_BACKEND` flag
3. Build X11 backend if available

### Running

NeoWall automatically selects the X11 backend when:
1. `DISPLAY` environment variable is set
2. X11 display connection succeeds
3. No Wayland compositor is detected

Just run:
```bash
neowall
```

---

## ⚙️ Configuration

X11 backend uses the same configuration as Wayland. Edit `~/.config/neowall/config.vibe`:

```vibe
default {
  shader retro_wave.glsl
  shader_fps 60
}
```

### i3 Integration

**No configuration needed!** NeoWall automatically appears below all windows.

If you experience stacking issues, add to `~/.config/i3/config`:
```
for_window [class="NeoWall"] floating enable, border none, move to scratchpad
```

### bspwm Integration

**No configuration needed!** Desktop type is respected automatically.

For manual control, add to `~/.config/bspwm/bspwmrc`:
```bash
bspc rule -a NeoWall state=floating layer=below
```

### dwm Integration

**Works out of the box!** dwm respects `_NET_WM_WINDOW_TYPE_DESKTOP` by default.

---

## 🔧 Technical Details

### Architecture

```
X11 Backend
├── Display Connection (Xlib)
├── Window Management
│   ├── XCreateSimpleWindow()
│   ├── EWMH property setup
│   └── XRandR multi-monitor
├── EGL Integration
│   ├── EGL_PLATFORM_X11_KHR
│   ├── Native window handle
│   └── GPU-accelerated rendering
└── Event Handling
    ├── ConfigureNotify (resize)
    ├── Expose (redraw)
    └── RRScreenChangeNotify (monitor changes)
```

### Window Properties Set

The backend automatically sets these X11 properties:

```c
_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
_NET_WM_STATE = [
    _NET_WM_STATE_BELOW,
    _NET_WM_STATE_STICKY,
    _NET_WM_STATE_SKIP_TASKBAR,
    _NET_WM_STATE_SKIP_PAGER
]
WM_CLASS = "neowall\0NeoWall"
WM_NAME = "NeoWall Wallpaper"
```

### EGL Context Creation

Uses `EGL_PLATFORM_X11_KHR` for direct hardware rendering:

```c
EGLDisplay display = eglGetPlatformDisplay(
    EGL_PLATFORM_X11_KHR,
    x11_display,
    NULL
);
```

### Multi-Monitor Support

The backend creates **one wallpaper window per active monitor**, enumerated via
XRandR (`XRRGetMonitors`, RandR 1.5; falls back to active CRTCs, then to the
whole screen). Each window is:

- Positioned at its monitor's `+x+y` origin within the root window and sized to
  that monitor's resolution — so a 3840×1200 dual-head screen gets two
  1920×1200 windows, not one stretched surface.
- Exposed as a separate `output_state` named after the real RandR connector
  (e.g. `DVI-D-0`, `VGA-0`), so per-output config blocks match and
  `neowall current` lists each monitor.
- Independently occluded: a fullscreen window pauses only the monitor it covers,
  leaving the others animating.

The root-window background pixmap (Conky-style pseudo-transparency) is a single
screen-wide concept, so only the monitor anchored at the origin (0,0) drives it.

---

## 🐛 Troubleshooting

### Wallpaper Appears Above Windows

**Cause:** Window manager not respecting EWMH hints

**Solution:**
1. Check if WM supports EWMH: `xprop -root | grep _NET_SUPPORTED`
2. Add manual rules (see WM-specific sections above)
3. Try a compositor like `picom` for better stacking

### Black Screen / No Rendering

**Cause:** EGL initialization failure

**Solution:**
```bash
# Check EGL/X11 support
eglinfo | grep X11

# Check OpenGL drivers
glxinfo | grep "OpenGL version"

# Run with verbose logging
neowall -v -f
```

### Multiple Wallpapers Overlapping

**Cause:** Multiple instances running

**Solution:**
```bash
neowall kill    # Stop all instances
neowall         # Start fresh
```

### Monitor Not Detected

**Cause:** XRandR not available or misconfigured

**Solution:**
```bash
# Check XRandR
xrandr --listmonitors

# Install XRandR
sudo pacman -S xorg-xrandr  # Arch
sudo apt install x11-xserver-utils  # Debian/Ubuntu
```

---

## 📊 Performance

### Expected Performance

| System | FPS | CPU Usage | GPU Usage |
|--------|-----|-----------|-----------|
| Desktop (RTX 3060) | 60 | ~2% | ~5% |
| Laptop (Intel iGPU) | 60 | ~5% | ~10% |
| Raspberry Pi 4 | 30 | ~15% | ~20% |

### Optimization Tips

**For Battery Life:**
```vibe
shader_fps 30
vsync true
```

**For Performance:**
```vibe
shader_fps 144
vsync false
```

---

## 🔄 Backend Priority

When both Wayland and X11 are available, backend selection order:

1. **wlr-layer-shell** (priority: 100) - Wayland compositors
2. **KDE Plasma Shell** (priority: 90) - KDE Wayland
3. **GNOME Shell** (priority: 80) - GNOME Wayland
4. **X11 Tiling WM** (priority: 50) - **This backend**
5. **Fallback** (priority: 10) - Generic Wayland

Force X11 backend:
```bash
unset WAYLAND_DISPLAY
neowall
```

---

## 🤝 Contributing

### Testing Checklist

When testing X11 backend:

- [ ] Window appears below all other windows
- [ ] Wallpaper visible on all workspaces (sticky)
- [ ] Not appearing in taskbar/pager
- [ ] Multi-monitor displays correctly
- [ ] Resizing works on monitor changes
- [ ] No rendering artifacts
- [ ] GPU acceleration enabled (check with `nvidia-smi` or `intel_gpu_top`)

### Adding WM Support

To add support for a new tiling WM:

1. Test if WM respects `_NET_WM_WINDOW_TYPE_DESKTOP`
2. Document any manual configuration needed
3. Add to supported WMs table above
4. Submit PR with test results

---

## 📚 References

- [EWMH Specification](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html)
- [Xlib Programming Manual](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html)
- [XRandR Extension](https://www.x.org/releases/current/doc/libXrandr/libXrandr.txt)
- [EGL X11 Platform](https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_platform_x11.txt)

---

## 📜 License

Same as NeoWall - MIT License

---

<div align="center">
  <strong>Bring live wallpapers to your tiling WM setup!</strong>
  <br/>
  <br/>
  Questions? <a href="https://github.com/1ay1/neowall/issues">Open an issue</a>
</div>