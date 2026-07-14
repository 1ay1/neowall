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
│   ├── XCreateWindow() (managed or override-redirect, per WM detection)
│   ├── EWMH property setup
│   └── XRandR multi-monitor
├── EGL Integration
│   ├── EGL_PLATFORM_X11_KHR
│   ├── Native window handle
│   └── GPU-accelerated rendering
└── Event Handling
    ├── ConfigureNotify (resize)
    ├── Expose (redraw)
    ├── PropertyNotify on root (_NET_SUPPORTING_WM_CHECK: a WM started/exited)
    └── RRScreenChangeNotify (monitor changes)
```

### Window Properties Set

Every wallpaper window carries these properties, set before it is mapped
(`x11_set_wallpaper_properties`, `x11_core.c`):

```c
_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
_NET_WM_STATE = [
    _NET_WM_STATE_BELOW,
    _NET_WM_STATE_STICKY,        /* viewport-fixed; NOT "all desktops" */
    _NET_WM_STATE_SKIP_TASKBAR,
    _NET_WM_STATE_SKIP_PAGER
]
_NET_WM_DESKTOP = 0xFFFFFFFF     /* all desktops (EWMH 5.5 / 9.2) */
WM_CLASS        = "neowall", "NeoWall"   /* instance, class */
WM_NAME         = "NeoWall Wallpaper"
_NET_WM_NAME    = "NeoWall Wallpaper"    /* UTF8_STRING */
WM_NORMAL_HINTS = { PPosition, PSize }   /* our geometry, not WM policy */
WM_HINTS        = { input: False }       /* never takes keyboard focus */
```

`WM_CLASS` is what a WM rule keys on, so it is also the handle you need to
override our stacking if your WM gets it wrong — see
[Wallpaper Appears Above Windows](#wallpaper-appears-above-windows).

`WM_NORMAL_HINTS` matters on multi-monitor: ICCCM 4.1.2.3 makes a managed
window's requested position merely advisory without `PPosition`, which would let
the WM place a per-monitor wallpaper window on the wrong monitor.

### Window Stacking: Managed vs Override-Redirect

An override-redirect window is by definition **not managed**, so no WM ever reads
its `_NET_WM_WINDOW_TYPE` and the `DESKTOP` hint above has no effect. The backend
therefore picks per-display:

| Situation | Window is | Stacked by |
|---|---|---|
| EWMH WM that advertises `_NET_WM_WINDOW_TYPE_DESKTOP` | managed | the WM, as the desktop |
| EWMH WM that does not advertise it (dwm, i3) | override-redirect | `XLowerWindow` + raising the root's other children |
| No WM (bare X, or a non-EWMH WM) | override-redirect | `XLowerWindow` + raising the root's other children |

The WM is detected with the two-step `_NET_SUPPORTING_WM_CHECK` handshake (EWMH
1.5): the root property names a window, and that window must carry the same
property naming *itself*. A WM that exited without cleaning up leaves the root
property behind, so only the self-reference proves one is actually alive.

Liveness alone does not earn a managed window: the WM must also list
`_NET_WM_WINDOW_TYPE_DESKTOP` in `_NET_SUPPORTED` (which EWMH says "MUST" name
every supported hint). dwm and i3 pass the handshake but omit it, and neither
honours the hint — dwm tiles every managed window, and i3 floats a window
carrying `_NET_WM_DESKTOP=0xFFFFFFFF` above the tiling layer — so a managed
wallpaper there would become an ordinary client window. Such WMs keep the
override-redirect path; a WM that honours `DESKTOP` without advertising it can
be forced managed with `NEOWALL_X11_OVERRIDE_REDIRECT=0` (below).

**A window manager that starts after us is handled.** A wallpaper daemon is
autostarted at login and routinely wins the race against the WM. The backend
selects `PropertyChangeMask` on the root, and when `_NET_SUPPORTING_WM_CHECK`
or `_NET_SUPPORTED` changes it re-runs the probe (a starting WM may write those
two properties in either order); if a WM has appeared, the wallpaper windows are
destroyed and recreated on the managed path. They must be *recreated* —
`override_redirect` is fixed at `XCreateWindow` time, so re-setting properties on
the existing window would not hand it to the WM. The shared `EGLContext` outlives
the swap, so textures and compiled shaders are not rebuilt.

**A window manager that dies is handled too.** When the WM goes away the window is
rebuilt on the override-redirect path, restoring exactly the setup the daemon
would have had if it had started with no WM. This is not cosmetic: when the WM
dies the wallpaper window loses its contents, and for a static image
`needs_redraw` has already been cleared after the first frame, so nothing ever
repaints it — the wallpaper goes black and stays black. Rebuilding repaints it and
re-arms the `XLowerWindow` path.

A WM *restart* (`--replace`) flaps the property down and straight back up, so the
WM-went-away edge is **debounced** (`X11_WM_GONE_DEBOUNCE_MS`, 1.5 s): the daemon
waits, re-probes, and only rebuilds if no replacement has appeared. A restart
therefore costs zero window rebuilds. Both edges are idempotent — the action is
derived from the window state actually in use, so a repeated `PropertyNotify`
does nothing.

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

**Hotplug.** The backend selects `RRScreenChangeNotifyMask` and reconciles the
output list on every `RRScreenChangeNotify` (plug/unplug, mode change, layout
edit). It re-enumerates monitors and diffs them against the live outputs by
connector name + geometry: vanished or moved monitors are torn down (surface +
EGL + GL state) and unref'd, new monitors are created and brought fully online
(surface → EGL → render → per-output config) in place. The reconcile runs on the
event-loop thread, which owns the EGL context, and is idempotent — a spurious
notify with an unchanged layout is a no-op.

---

## 🐛 Troubleshooting

### Wallpaper Appears Above Windows

**Cause:** Window manager not respecting EWMH hints, or the backend picked the
wrong window type for your WM.

**Solution:**
1. Check if WM advertises desktop windows: `xprop -root _NET_SUPPORTED | grep -o _NET_WM_WINDOW_TYPE_DESKTOP`
2. Check what the backend actually chose — run with `-f -v` and look for one of:
   ```
   X11: EWMH window manager with _NET_WM_WINDOW_TYPE_DESKTOP support detected - using managed wallpaper windows
   X11: EWMH window manager detected, but its _NET_SUPPORTED does not advertise _NET_WM_WINDOW_TYPE_DESKTOP - using override-redirect wallpaper windows
   X11: no EWMH window manager - using override-redirect wallpaper windows
   ```
3. Override the choice with `NEOWALL_X11_OVERRIDE_REDIRECT` (see below)
4. Add manual rules keyed on `WM_CLASS` (see WM-specific sections above), e.g.
   i3: `for_window [class="NeoWall"] ...`
5. Try a compositor like `picom` for better stacking

#### `NEOWALL_X11_OVERRIDE_REDIRECT`

Escape hatch for a WM that mishandles the auto-detected choice. Accepts exactly
two values; anything else logs a warning and is ignored.

| Value | Effect |
|---|---|
| `1` | Force **override-redirect**. The WM never manages or stacks the wallpaper; `XLowerWindow` keeps it at the bottom. Use if your WM stacks the desktop window *above* other windows. |
| `0` | Force a **managed** window. Use if auto-detection fails to see your WM but it does honour `_NET_WM_WINDOW_TYPE_DESKTOP`. |
| unset | Auto-detect (the default), and switch to managed if a WM appears later. |

```bash
NEOWALL_X11_OVERRIDE_REDIRECT=1 neowall -f -v
```

Setting either value **pins** the choice: the backend will not revise it if a
window manager starts or exits later.

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