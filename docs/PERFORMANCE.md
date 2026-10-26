# Performance

How fast a shader wallpaper runs comes down to three numbers multiplied
together: how many pixels it draws, how much work each pixel does, and how
many frames a second. This page covers how to see where you are, what neowall
does about it, and what you can do.

## What neowall does for you

- **Every pass is drawn at full resolution, every frame.** neowall used to
  scale shaders down, render buffer passes at half resolution and skip
  alternate buffer updates to hit a frame budget. It misjudged often enough
  (garden's feedback buffer ran at 50% on a fast GPU) that all of it is now
  off. Adaptive resolution is disabled by default and the scale is pinned to
  1.0. If a shader looks wrong, it isn't neowall degrading it.
- **The one skip left is a frame identical to the last.** A shader that never
  reads `iTime`, the mouse, audio or a reactive uniform paints once and then
  sleeps. An idle terminal wallpaper sleeps until its child prints.
- **Hidden wallpapers stop.** With `pause_on_fullscreen` (on by default) an
  output covered by a fullscreen window, or by tiled windows past
  `pause_coverage_threshold` on Hyprland, stops rendering.
- **Monitors don't hold each other up.** With vsync on, each Wayland output is
  paced by its own frame callback, so one slow or hidden monitor can't stall
  the others or the event loop.
- **Nothing slow runs on the render thread.** Sensor reads (hwmon,
  `gpu_busy_percent`, `/proc`), audio capture and `nvidia-smi` all run on
  their own threads and hand over a snapshot.

## See where you are

```bash
neowall preview garden -v        # or: neowall -f -v
```

Every two seconds per output:

```
FPS [DP-1]: 60.0 FPS (vsync: monitor sync, frame_time: 1ms)
```

`frame_time` is CPU time spent in the draw call, not GPU time. If FPS sits
below your refresh rate while `frame_time` is small, the GPU is the limit.
`nvidia-smi` / `radeontop` / `intel_gpu_top` will show it pegged.

## Pixel count

neowall draws at the output's buffer size. On a scaled HiDPI output that's
the physical size, not the logical one.

| Output | Pixels per frame | vs 1080p |
|--------|------------------|----------|
| 1920×1080 | 2.1 M | 1.0× |
| 2560×1440 | 3.7 M | 1.8× |
| 3440×1440 | 5.0 M | 2.4× |
| 3840×2160 | 8.3 M | 4.0× |

Several monitors add up: every output draws its own frames.

## Per-pixel work

What's expensive, roughly in order:

- **Raymarch loops.** A `for` loop calling `map()` 64–128 times per pixel,
  plus normals (3–6 more calls), soft shadows and AO. `train_journey.glsl`
  and `fractal_land.glsl` are in this class.
- **Reflections and volumetrics.** A second march per pixel, or a march
  through fog.
- **Deep noise.** `nwFbm` with 6+ octaves, especially nested inside a loop.
  Hoist anything that doesn't depend on the pixel out of the per-pixel path:
  garden's frame cost halved at 3440×1440 by computing a stem's wind
  turbulence once per plant instead of per sample point.
- **Many texture reads** are cheap by comparison; plain math is cheaper still.

## What you can do

1. **Pick shaders per monitor.** Put the heavy one on the smaller display:

   ```vibe
   output {
     DP-1     { shader retro_wave.glsl }      # 4K: something light
     HDMI-A-1 { shader train_journey.glsl }   # 1080p: can afford it
   }
   ```

2. **Lower `shader_fps`** with `vsync false`. GPU load drops about linearly:

   ```vibe
   default {
     shader train_journey.glsl
     vsync false
     shader_fps 30
   }
   ```

   With `vsync true` (the default) the display's refresh paces the shader.

3. **Use a lighter shader.** `retro_wave`, `synthwave`, `neonwave_sunrise`
   and most of the reactive examples run at 4K on modest GPUs.

4. **Profile your own shader** with `neowall watch myshader.glsl`: it
   recompiles on save, so you can comment out a pass or cut an octave and
   watch the FPS line.

## Timing

Smoothness is a separate question from speed. On Wayland each frame's `iTime`
is stamped with the vblank it will be shown at, so it advances by exactly one
refresh per refresh even when the draw start jitters. Long-running
wallpapers reset their clock before float32 precision gets coarse. See
[ARCHITECTURE.md](ARCHITECTURE.md) §8, "Shader time".
