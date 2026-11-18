# Configuration

neowall reads one file, `~/.config/neowall/config.vibe` (or
`$XDG_CONFIG_HOME/neowall/config.vibe`). It is created on first run with the
default wallpaper. `neowall reload` applies changes; a file with any error is
rejected as a whole and the running configuration stays.

A fully commented example of every option is installed next to it as
`neowall.vibe`. The same reference is available offline as `man neowall.vibe`.

- [The format](#the-format)
- [What to show](#what-to-show)
- [Shaders](#shaders)
- [Images and cycling](#images-and-cycling)
- [Terminal wallpapers](#terminal-wallpapers)
- [Multiple monitors](#multiple-monitors)
- [Top-level keys](#top-level-keys)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)

## The format

The file is written in VIBE: nested blocks of `key value` lines and `#`
comments. Quote a value only if it contains spaces.

```
# every monitor
default {
  shader tokyo_night
}

# per monitor, by connector name (shown by `neowall current`)
output {
  DP-1     { shader moon_ocean }
  HDMI-A-1 { path ~/Pictures/Wallpapers/  duration 600 }
}
```

A `default` block applies to every monitor. An `output` block holds one block
per connector; a monitor with its own block ignores `default` entirely.

## What to show

Each block shows exactly one of these:

| Key | Value | |
|-----|-------|-|
| `shader` | name or path | A GLSL shader. A bare name (`tokyo_night`) is looked up in `~/.config/neowall/shaders/`, then in the installed shaders, with or without `.glsl` / `.neowall`. A directory cycles through its shaders. |
| `path` | file or `dir/` | A PNG or JPEG, or a directory of them to cycle through. |
| `terminal` | command | Runs the command in a pseudo-terminal and draws it as the wallpaper (`btop`, `cmatrix`, a dashboard). It is restarted if it exits. |

## Shaders

| Key | Default | |
|-----|---------|-|
| `shader_speed` | `1.0` | Animation speed. |
| `shader_fps` | refresh rate | Cap the frame rate, e.g. `30` to halve the GPU cost of a heavy shader. Unset, frames follow the monitor's refresh. |
| `vsync` | `true` | Present on the display's refresh, tear-free. `false` paces frames by `shader_fps` alone and allows tearing. |
| `palette` | built-in | Your colour theme, passed to shaders as `iPalette`. Inline (`palette "#1a1b26 #c0caf5 #7aa2f7 #bb9af7"`) or a path to any file that contains `#rrggbb` colours, read in order, so a kitty, ghostty or CSS theme works as is. Order: background, foreground, then accents (up to 16). |
| `channels` | | Up to four textures for `iChannel0..3`, as an array: image paths or the built-ins `rgba_noise`, `gray_noise`, `blue_noise`, `wood`, `abstract`, `font`. Only fills channels the shader's own manifest leaves open. |
| `pause_on_fullscreen` | `true` | Stop rendering while a fullscreen window covers the monitor. |
| `pause_coverage_threshold` | `0.8` | On Hyprland, also stop when tiled windows cover this fraction of the screen. |
| `show_fps` | `false` | Draw the frame rate in a corner. |

Writing your own shaders is covered in [SHADERS.md](SHADERS.md).

## Images and cycling

| Key | Default | |
|-----|---------|-|
| `mode` | `fill` | `fill`, `fit`, `center`, `stretch` or `tile`. |
| `duration` | `0` | Seconds between wallpapers in a directory. `0` doesn't cycle. Works for shader directories too. |
| `shuffle` | `false` | Random order, reshuffled on every pass. |
| `transition` | `fade` | `fade`, `slide_left`, `slide_right`, `glitch`, `pixelate` or `none`. |
| `transition_duration` | `0.3` | Seconds. |

`neowall next` skips ahead, `neowall set <n>` jumps to an entry, `neowall list`
shows the cycle, and `neowall pause` / `resume` stop and restart it. The
position is remembered across restarts (unless `shuffle` is on).

## Terminal wallpapers

| Key | |
|-----|-|
| `term_font`, `term_font_bold`, `term_font_italic` | Font files. Unset picks a monospace face through fontconfig. |
| `term_font_size` | Cell size in pixels. |
| `term_cols`, `term_rows` | Grid size. `0` (default) fits the screen. |
| `term_fg`, `term_bg` | Default colours, quoted: `term_fg "#c0caf5"`. |
| `term_cwd`, `term_env` | Working directory, and `$TERM` for the program. |
| `term_shader` | A shader that post-processes the terminal image (it reads the terminal with `nwTerm()`). |
| `term_fade`, `term_bloom`, `term_scanline`, `term_crt`, `term_chroma` | Built-in effect strengths, `0`–`1`. |

`neowall set-terminal "htop"` swaps the program without editing the file.

## Multiple monitors

Each monitor gets its own block from `output`, or else `default`. Connector
names (`DP-1`, `HDMI-A-1`, `eDP-1`) are listed by `neowall current`; a
monitor's model name works too.

To stretch one wallpaper across several monitors instead, set `span true`, or
give the monitors that should share one a common `span_group name`.

## Top-level keys

These go outside any block.

| Key | Default | |
|-----|---------|-|
| `mouse_interaction` | `true` | Let shaders see the pointer (`iMouse`). |
| `term_raw_input` | `false` | Pass Ctrl-C and similar keys to a focused terminal wallpaper. |
| `allow_exec` | `false` | Allow shader manifests to run commands for live values (`exec("...")`, see [SHADERS.md](SHADERS.md#live-values-from-anywhere)). Also enabled by `neowall --allow-exec`. |
| `shader_pause_ease_ms` | `0` | Milliseconds (0 to 60000) that `pause-shader` and `resume-shader` ease the animation to a stop and back. `--ease-ms` on either command overrides it. |

## Examples

A slideshow that changes every ten minutes:

```
default {
  path ~/Pictures/Wallpapers/
  duration 600
  shuffle true
  transition glitch
}
```

A heavy shader at 30 fps, in your terminal's colours:

```
default {
  shader tokyo_night
  shader_fps 30
  palette ~/.config/kitty/current-theme.conf
}
```

A different wallpaper on each monitor:

```
default { shader tokyo_night }

output {
  eDP-1    { path ~/Pictures/laptop.jpg  mode fit }
  HDMI-A-1 { terminal btop  term_font_size 18 }
}
```

## Troubleshooting

Run `neowall -f -v` in a terminal to see the daemon's log.

**`reload` says the config was rejected.** The log names the key and the line.
The previous configuration keeps running until the file is fixed.

**Shader not found.** A bare name is searched in `~/.config/neowall/shaders/`
and the installed shaders. Use a path for anything else.

**Black screen with a shader.** It failed to compile or produced invalid
pixels. `neowall render <shader> --check` shows the compiler errors, or which
pass produces NaN.

**A monitor isn't picked up.** Check its connector name with `neowall
current`; an `output` block with a misspelled name falls back to `default`.
