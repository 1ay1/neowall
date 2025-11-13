# Commands Quick Reference - Config Key Alignment

## Output Commands (Per-Monitor)

| Config Key | Command | Example |
|------------|---------|---------|
| `output.path` | `set-output-path` | `neowall set-output-path DP-1 ~/pic.jpg` |
| `output.shader` | `set-output-shader` | `neowall set-output-shader DP-1 matrix.glsl` |
| `output.mode` | `set-output-mode` | `neowall set-output-mode DP-1 fill` |
| `output.duration` | `set-output-duration` | `neowall set-output-duration DP-1 600` |
| `output.shader_speed` | `set-output-shader-speed` | `neowall set-output-shader-speed DP-1 2.0` |
| `output.transition` | `set-output-transition` | `neowall set-output-transition DP-1 fade` |
| `output.transition_duration` | `set-output-transition-duration` | `neowall set-output-transition-duration DP-1 500` |

## Default Commands (Global)

| Config Key | Command | Example |
|------------|---------|---------|
| `default.path` | `set-default-path` | `neowall set-default-path ~/wallpaper.png` |
| `default.shader` | `set-default-shader` | `neowall set-default-shader plasma.glsl` |
| `default.mode` | `set-default-mode` | `neowall set-default-mode fill` |
| `default.duration` | `set-default-duration` | `neowall set-default-duration 300` |
| `default.shader_speed` | `set-default-shader-speed` | `neowall set-default-shader-speed 1.5` |
| `default.transition` | `set-default-transition` | `neowall set-default-transition fade` |
| `default.transition_duration` | `set-default-transition-duration` | `neowall set-default-transition-duration 300` |

## Renamed Commands

| Old Command | New Command | Reason |
|-------------|-------------|--------|
| `set-output-interval` | `set-output-duration` | Match config key `output.duration` |
| `set-output-wallpaper` | `set-output-path` + `set-output-shader` | Clarify image vs shader, match config keys |

