# Performance

A shader wallpaper's cost is three numbers multiplied together: how many
pixels it draws, how much work each pixel does, and how many frames a second.

## What neowall does for you

- **Hidden wallpapers stop.** A monitor covered by a fullscreen window stops
  rendering (`pause_on_fullscreen`, on by default), and so does one covered by
  tiled windows past `pause_coverage_threshold` on Hyprland. A covered
  wallpaper uses about 0.1% CPU.
- **Static content sleeps.** A shader that reads no time, mouse, audio or live
  input draws once. An idle terminal wallpaper sleeps until its program prints.
- **Monitors don't hold each other up.** On Wayland each monitor is paced by
  its own frame callbacks, so a slow or hidden one can't stall the others.
- **Nothing slow runs on the render thread.** Sensors, audio capture and
  `nvidia-smi` run on their own threads; audio analysis is skipped in silence
  and while no visible shader reads it.
- **Every pass runs at the resolution you ask for**, every frame. neowall never
  lowers quality behind your back; you decide with `scale` and `shader_fps`.

## Measure

```bash
neowall render myshader --size 3440x1440 --bench
```

```
GPU time per frame (3440x1440, mean of 117 frames):
  Clouds            0.03 ms
  Buffer A          0.62 ms
  Buffer B          0.18 ms
  Buffer C          0.14 ms
  Buffer D          0.02 ms
  Halo              0.01 ms
  Image             0.20 ms
  total             1.21 ms
  busy:    4% of the GPU at 30 fps,    7% at 60 fps
```

This is real GPU time per pass, measured with timer queries, at the size you
pass (use your monitor's). GPU utilisation graphs such as `nvidia-smi`
overstate a wallpaper's cost: a light job keeps the GPU at power-saving
clocks, where a small amount of work shows as a high percentage.

`neowall -f -v` also logs the achieved frame rate of each monitor every two
seconds.

## Reduce the cost

1. **Cap the frame rate.** Slow-moving scenes look the same at 30 fps and cost
   half:

   ```
   default {
     shader tokyo_night
     shader_fps 30
   }
   ```

2. **Render soft passes at lower resolution.** Blur, bloom, glow, clouds and
   fog look the same at a fraction of the size. In the shader's manifest:

   ```
   bufferC { ch0 bufferB  scale 0.25 }
   ```

   The default `tokyo_night` renders its clouds and bloom at a quarter and its
   halo at an eighth of the screen, which took it from 2.0 ms to 1.2 ms a frame
   at 3440×1440 with no visible difference.

3. **Skip work that can't show.** Return early for pixels a feature can't
   reach (the sky above the tallest building, a band where cars drive), and
   compute per-object values once instead of per pixel.

4. **Use fewer noise octaves.** `nwFbm(p, 4)` instead of six is often
   indistinguishable, and noise inside a loop multiplies quickly.

5. **Put heavy shaders on smaller monitors.** Every monitor draws its own
   frames, and a 4K screen has four times the pixels of a 1080p one.

| Resolution | Pixels | vs 1080p |
|------------|--------|----------|
| 1920×1080 | 2.1 M | 1.0× |
| 2560×1440 | 3.7 M | 1.8× |
| 3440×1440 | 5.0 M | 2.4× |
| 3840×2160 | 8.3 M | 4.0× |

On a scaled HiDPI monitor neowall draws at the physical resolution.

## What is expensive

Roughly in order: raymarch loops (64–128 scene evaluations per pixel, plus
normals, shadows and ambient occlusion), a second march for reflections or
fog, deep noise (especially inside loops), then texture reads. Plain
arithmetic is cheapest.
