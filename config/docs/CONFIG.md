# NeoWall Configuration Guide

Complete reference for configuring NeoWall.

## Quick Start

Edit `~/.config/neowall/config.vibe`:

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
}
```

Save and changes apply automatically.

## Config File Location

- User config: `~/.config/neowall/config.vibe`
- System config: `/etc/neowall/config.vibe` (fallback)

Config is auto-created on first run with Matrix rain as default.

## VIBE Syntax

Simple bracket-based format:

```vibe
# Comments start with #
section {
  key value
  another_key value
}
```

- No quotes needed for simple strings
- No colons or semicolons
- Whitespace flexible
- Braces show hierarchy

## Configuration Sections

### `default` - Global Settings

Applies to all monitors unless overridden by `output`.

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
  mode fill
}
```

### `output` - Per-Monitor Settings

Override settings for specific monitors:

```vibe
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/wallpaper.png
  }
}
```

Get monitor names:
- Hyprland: `hyprctl monitors`
- Sway: `swaymsg -t get_outputs`
- Generic: `wlr-randr`

## Options Reference

### Shader Options

#### `shader` - GLSL Shader File

Run GPU shader as wallpaper:

```vibe
shader matrix_real.glsl           # From ~/.config/neowall/shaders/
shader ~/custom/shader.glsl       # Absolute path
shader /usr/share/shaders/x.glsl  # System path
```

Included shaders:
- `matrix_real.glsl` - Matrix rain with detail
- `matrix_rain.glsl` - Classic Matrix effect
- `plasma.glsl` - Flowing plasma waves
- `aurora.glsl` - Northern lights
- `2d_clouds.glsl` - Procedural clouds
- `sunrise.glsl` - Dynamic sky
- `fractal_land.glsl` - Fractal landscapes
- `mandelbrot.glsl` - Mandelbrot zoom
- And more in `~/.config/neowall/shaders/`

#### `shader_speed` - Animation Speed

Control shader animation speed:

```vibe
shader_speed 1.0   # Normal (default)
shader_speed 2.0   # 2x faster
shader_speed 0.5   # Half speed
shader_speed 0.1   # Very slow
```

Only affects shaders, not images.

### Image Options

#### `path` - Image File or Directory

Static image wallpaper:

```vibe
path ~/Pictures/wallpaper.png     # Single file
path ~/Pictures/Wallpapers/       # Directory (cycles all images)
```

Supported formats: PNG, JPEG/JPG

Directory mode:
- Add trailing slash: `~/Pictures/Wallpapers/`
- Loads all PNG/JPEG files
- Cycles alphabetically
- Use with `duration` to auto-cycle

#### `mode` - Display Mode

How image fills screen:

```vibe
mode fill      # Scale to fill, crop if needed (default, recommended)
mode fit       # Scale to fit, may show borders
mode center    # No scaling, center image
mode stretch   # Stretch to fill (may distort)
mode tile      # Repeat image as tiles
```

#### `duration` - Cycle Interval

Seconds between wallpaper changes:

```vibe
duration 300    # 5 minutes
duration 900    # 15 minutes
duration 1800   # 30 minutes
duration 3600   # 1 hour
duration 0      # No cycling (default)
```

Works with:
- Image directories (`path ~/Pictures/Wallpapers/`)
- Shader directories (`shader ~/.config/neowall/shaders/`)

#### `transition` - Transition Effect

Effect when changing wallpapers:

```vibe
transition fade         # Smooth crossfade (default)
transition slide_left   # Slide left
transition slide_right  # Slide right
transition glitch       # Digital glitch
transition pixelate     # Mosaic blocks
transition none         # Instant switch
```

Only applies to image cycling, not shaders.

#### `transition_duration` - Transition Speed

Transition length in seconds:

```vibe
transition_duration 0.3   # Default
transition_duration 0.1   # Fast
transition_duration 5     # Smooth
transition_duration 10    # Slow
```

#### `shuffle` - Randomise Cycle Order

Randomise the order of a directory cycle. Works for both image directories
and shader directories.

```vibe
shuffle true    # Random order, fresh permutation every wrap
shuffle false   # Alphabetical order (default)
```

- Applied at startup so each launch is a different sequence.
- Re-applied every time the cycle wraps, so a long-running daemon doesn't
  settle into a fixed loop. The just-shown wallpaper is held back from
  position 0 on the new pass so the same item never appears twice in a row.
- All monitors that share a config block see the **same** shuffled order,
  so multi-monitor synchronised cycling keeps working.
- Saved cycle position (the bookmark used to resume where you left off after
  a daemon restart) is disabled under `shuffle true` — the saved index
  refers to a different random permutation than the one you'd get on the
  next launch, so resuming at it is meaningless.

### Terminal Options

Run any terminal program as the wallpaper. neowall has its own in-tree,
dependency-free VT/xterm-class terminal emulator — a real PTY running the
command, a spec-correct ANSI/DEC parser, and a cell grid (SGR truecolour,
scroll regions, alternate screen, mouse reporting, colour emoji) rendered on the
GPU. TUIs like `btop`, `htop`, `cava` and `vim` run live behind your windows.

#### `terminal` - Command to Run

Names the command launched under the PTY. Mutually exclusive with `path` and
`shader`.

```vibe
terminal btop                    # a bare command
terminal "journalctl -f"         # quote if it has arguments
terminal "htop -d 10"
```

The command runs via `$SHELL -c`, so pipelines and arguments work. If it exits,
neowall relaunches it after a short backoff (so a transient crash resumes the
wallpaper instead of freezing on the last frame).

#### `term_cols` / `term_rows` - Grid Size

The terminal grid in character cells. `0` (the default) means **auto-fit the
whole display** from its resolution and the font's cell size.

```vibe
term_cols 0     # auto-fit width  (default)
term_rows 0     # auto-fit height (default)
term_cols 120   # or pin an explicit grid
term_rows 40
```

#### `term_font_size` - Font Size

Cell font size in pixels. Larger = bigger text, fewer cells when auto-fitting.

```vibe
term_font_size 16    # default is a mid-size cell
term_font_size 24    # chunkier, fewer rows/cols
```

#### `term_font` / `term_font_bold` / `term_font_italic` - Fonts

Paths to TrueType/OpenType font files. If unset, neowall picks a sensible
monospace face it finds on the system. Bold/italic fall back to the regular
face synthesised if not given.

```vibe
term_font ~/.fonts/JetBrainsMono-Regular.ttf
term_font_bold ~/.fonts/JetBrainsMono-Bold.ttf
term_font_italic ~/.fonts/JetBrainsMono-Italic.ttf
```

Colour emoji fonts (CBDT/CBLC or sbix bitmap strikes, e.g.
`NotoColorEmoji.ttf`) are detected automatically and rendered in colour.

#### `term_fg` / `term_bg` - Default Colours

Hex colours for the default foreground and background (cells that don't set
their own via SGR). `term_bg` also sets the surface behind the text.

```vibe
term_fg #d0d0d0
term_bg #101018
```

#### `term_cwd` - Working Directory

Directory the command starts in (defaults to inherited cwd).

```vibe
term_cwd ~/projects
```

#### `term_env` - TERM Value

The value of `$TERM` seen by the child (default `xterm-256color`).
`COLORTERM=truecolor` is always exported so 24-bit colour works.

```vibe
term_env xterm-256color
```

#### `term_shader` - Styling Pass

Optional GLSL shader that post-processes the rendered terminal (a CRT curve,
glow, scanlines, …). The shader samples the terminal via `nwTerm()`.

```vibe
term_shader crt.glsl
```

`shader_fps` and `vsync` also apply in terminal mode (they pace the redraw).

## Global Options

These sit at the top level of `config.vibe`, **outside** any `default {}` or
`output {}` block. They configure the daemon process as a whole, not a single
wallpaper surface.

### `mouse_interaction` - Pointer Input

Whether the wallpaper surface receives pointer events.

```vibe
mouse_interaction true     # default — pointer enters wallpaper, iMouse fed to shaders
mouse_interaction false    # wallpaper is invisible to the pointer
```

When `false`:

- Wayland: `wl_pointer` is not bound (or is released if already bound). The
  compositor routes pointer events to whatever is underneath the wallpaper.
- X11: `XQueryPointer` polling and Button/Motion events are skipped.
- Shaders: `iMouse` stays at its initial fallback (screen center).
- No themed cursor is set on the wallpaper.

Use cases for turning it off:
- You don't want neowall to override the cursor theme over the wallpaper.
- Your shader doesn't use `iMouse` and you want to avoid the per-motion lock
  traffic from pointer events.
- Principle: a wallpaper shouldn't grab input focus.

### Performance Options

#### `pause_on_fullscreen` - Pause When Occluded

Automatically pause rendering when the wallpaper is covered:

```vibe
pause_on_fullscreen true    # Pause rendering (default)
pause_on_fullscreen false   # Keep rendering behind covering windows
```

Saves GPU/CPU when the wallpaper isn't visible (fullscreen games, videos, maximized apps).

Works per-output — if only one monitor is covered, the others keep rendering.

**What triggers a pause:**
- A fullscreen window on the output
- A maximized window on the output
- The compositor stops requesting frames for the wallpaper surface (500ms watchdog)
- On Hyprland: tiled/floating windows that together cover ≥ `pause_coverage_threshold` of the output (via Hyprland's IPC socket; default 80%)

#### `pause_coverage_threshold` - Tiled-Mosaic Threshold (Hyprland)

Fraction of the wallpaper region that tiled windows must cover before the
output counts as occluded. Only consulted on Hyprland (other compositors
don't expose the geometry needed to compute this).

```vibe
pause_coverage_threshold 0.8    # Default — pause at 80% coverage
pause_coverage_threshold 0.95   # Conservative — only pause when nearly fully tiled
pause_coverage_threshold 0.5    # Aggressive — pause as soon as half is tiled
```

Range: `0.0` to `1.0`. The wallpaper region excludes Hyprland's reserved
zones (waybar etc.), so those don't count toward "uncovered wallpaper".

**Compositor support:**
- **Hyprland**: full coverage detection (fullscreen, maximized, tiled mosaic via IPC)
- **Sway / River / other wlroots**: fullscreen + maximized (via `wlr-foreign-toplevel-management`) + frame-callback watchdog
- **KDE Plasma (KWin)**: frame-callback watchdog (pauses when KWin throttles obscured surfaces)
- **GNOME (Mutter)**: frame-callback watchdog (pauses when Mutter throttles obscured surfaces)
- **Other / unknown Wayland**: frame-callback watchdog (best-effort)
- **X11**: Any EWMH-compliant window manager (i3, bspwm, dwm, etc.)

## Example Configurations

### Matrix Rain (Default)

```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
}
```

### Static Image

```vibe
default {
  path ~/Pictures/wallpaper.png
  mode fill
}
```

### Cycling Images

```vibe
default {
  path ~/Pictures/Wallpapers/
  duration 300
  transition fade
  mode fill
}
```

### Shuffled Cycle

```vibe
default {
  path ~/Pictures/Wallpapers/
  duration 300
  shuffle true
  transition fade
  mode fill
}
```

### Multi-Monitor

```vibe
output {
  eDP-1 {
    shader matrix_real.glsl
  }
  HDMI-A-1 {
    path ~/Pictures/monitor.png
    mode fill
  }
}
```

### Cycling Shaders

```vibe
default {
  shader ~/.config/neowall/shaders/
  duration 600
  shader_speed 1.0
}
```

### Terminal (TUI) Wallpaper

```vibe
default {
  terminal btop
  term_cols 0        # auto-fit the whole display
  term_rows 0
  term_font_size 16
}
```

With a CRT styling pass and a scrolling log:

```vibe
default {
  terminal "journalctl -f"
  term_shader crt.glsl
  term_fg #33ff66
  term_bg #001100
}
```

### Mixed Setup

```vibe
default {
  shader plasma.glsl
  shader_speed 0.5
}

output {
  eDP-1 {
    shader matrix_real.glsl
    shader_speed 2.0
  }
  HDMI-A-1 {
    path ~/Pictures/Wallpapers/
    duration 300
    transition fade
  }
  DP-1 {
    path ~/Pictures/static.png
    mode fit
  }
}
```

## Mutually Exclusive Options

Don't mix these in the same section:

**Images vs Shaders vs Terminal:**
- Use exactly one of `path`, `shader`, or `terminal` per section
- Use `mode` / `transition` with images, not shaders or terminals
- Use `shader_speed` with shaders, not images
- `term_*` keys apply only with `terminal`
- `shader_fps` / `vsync` apply to shaders and terminals (both animate)

**Valid:**
```vibe
default {
  shader matrix_real.glsl
  shader_speed 1.0
  duration 0
}
```

**Invalid:**
```vibe
default {
  path ~/Pictures/wallpaper.png
  shader matrix_real.glsl  # ERROR: Can't use both
}
```

## Daemon Commands

Control running daemon:

```bash
neowall              # Start daemon
neowall kill         # Stop daemon
neowall next         # Skip to next wallpaper/shader
neowall pause        # Pause cycling (stops advancing between wallpapers)
neowall resume       # Resume cycling
neowall pause-shader   # Freeze the shader animation in place
neowall resume-shader  # Resume a frozen shader animation (continues from the same frame)
neowall current      # Show current wallpaper
```

`pause`/`resume` stop the slideshow from advancing between wallpapers;
`pause-shader`/`resume-shader` freeze the animation of the current shader (its
time uniform) and stop drawing frames, leaving the last frame on screen. They
are independent — pausing cycling does not freeze the animation, and vice versa.

(There is no `neowall reload` — see [Reloading Config](#reloading-config).)

## Reloading Config

Config is read once at startup. **There is no hot-reload** — changes to
`~/.config/neowall/config.vibe` take effect on the next daemon start.

To apply a config change:

```bash
neowall kill        # Stop the running daemon
neowall             # Start it again
```

Or in one line:

```bash
neowall kill && neowall
```

## Custom Shaders

### Writing Shaders

Create `~/.config/neowall/shaders/myshader.glsl`:

```glsl
#version 100
precision highp float;

uniform float time;        // Seconds since start
uniform vec2 resolution;   // Screen dimensions

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    vec3 color = vec3(uv, sin(time));
    gl_FragColor = vec4(color, 1.0);
}
```

Use in config:
```vibe
default {
  shader myshader.glsl
}
```

### Shadertoy Compatibility

Most Shadertoy shaders work with minimal changes. NeoWall provides:

- `iTime` - Time in seconds
- `iResolution` - Screen resolution
- `iChannel0` through `iChannel4` - Texture samplers
- `iChannelTime[4]` - Per-channel time
- `iChannelResolution[4]` - Per-channel resolution
- `iMouse` - Mouse position (always vec4(0))
- `iDate` - Date vector
- `iFrame` - Frame counter (always 0)

To convert Shadertoy shader:
1. Copy shader code
2. Save to `~/.config/neowall/shaders/name.glsl`
3. Use in config: `shader name.glsl`

Most shaders work as-is. Some may need minor adjustments.

Browse shaders: [shadertoy.com](https://www.shadertoy.com/)

## Troubleshooting

### Config not reloading
- Neowall does **not** hot-reload. Restart the daemon: `neowall kill && neowall`
- Check: `~/.config/neowall/config.vibe` exists
- Check logs: `neowall -fv` (foreground, verbose)

### Shader not found
- Shaders in: `~/.config/neowall/shaders/`
- Use filename only: `shader matrix_real.glsl`
- Or full path: `shader ~/.config/neowall/shaders/matrix_real.glsl`

### Black screen with shader
- Check logs: `neowall -fv`
- Verify GPU supports OpenGL ES 2.0+
- Try different shader: `shader plasma.glsl`
- Check shader syntax errors in logs

### Image not showing
- Verify file exists: `ls -la ~/Pictures/wallpaper.png`
- Check format: PNG or JPEG only
- Try absolute path: `/home/user/Pictures/wallpaper.png`

### Monitor not recognized
- Get exact name: `hyprctl monitors` or `swaymsg -t get_outputs`
- Check spelling in config
- Monitor must be active

### Cycling not working
- Set `duration > 0`
- For directories, add trailing slash: `path ~/Pictures/Wallpapers/`
- Verify directory contains images: `ls ~/Pictures/Wallpapers/`

## Performance

### CPU Usage
- Shaders: ~2% CPU at 60 FPS (GPU accelerated)
- Static images: ~0% CPU (after load)
- Image cycling: Brief spike during transition
- Fullscreen apps: 0% CPU/GPU (auto-paused by default)

### Memory Usage
- Base: ~10-20 MB
- Per shader: +5-10 MB
- Per image: +image file size (uncompressed)

### GPU Usage
- Shaders render at 60 FPS
- Uses OpenGL ES 2.0+ for compatibility
- Automatic fallback for older GPUs

## Advanced Topics

### Environment Variables

Override config location:
```bash
NEOWALL_CONFIG=~/custom/config.vibe neowall
```

### Multiple Configs

Switch between configs:
```bash
neowall -c ~/.config/neowall/work.vibe
neowall -c ~/.config/neowall/gaming.vibe
```

### Debug Mode

Run in foreground with verbose logging:
```bash
neowall -fv
```

Output shows:
- Config parsing
- Shader compilation
- Image loading
- Monitor detection
- Frame timing

## See Also

- Main README: `../README.md`
- Example config: `../config/neowall.vibe`
- Shader directory: `~/.config/neowall/shaders/`
- GitHub: [github.com/1ay1/neowall](https://github.com/1ay1/neowall)
