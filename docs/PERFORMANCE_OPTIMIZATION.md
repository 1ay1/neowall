# Performance Optimization Guide

This document describes performance considerations, known bottlenecks, and optimization strategies for NeoWall, particularly when running complex shaders on high-resolution displays.

## Table of Contents

1. [Understanding Performance Factors](#understanding-performance-factors)
2. [Common Performance Issues](#common-performance-issues)
3. [Immediate Workarounds](#immediate-workarounds)
4. [Configuration Options](#configuration-options)
5. [Shader Complexity Guide](#shader-complexity-guide)
6. [Proposed Optimizations](#proposed-optimizations)
7. [Debugging Performance Issues](#debugging-performance-issues)

---

## Understanding Performance Factors

### Pixel Count Matters

The primary factor affecting GPU performance is the total number of pixels being rendered per frame. NeoWall renders shaders at the **full buffer resolution** of each output.

| Resolution | Pixels per Frame | Relative Load |
|------------|------------------|---------------|
| 1920×1080 (1080p) | 2.07M | 1.0× (baseline) |
| 2560×1080 (Ultrawide) | 2.76M | 1.3× |
| 2560×1440 (1440p) | 3.69M | 1.8× |
| 3840×2160 (4K) | 8.29M | 4.0× |
| 6144×3456 (4K @ scale 2) | 21.2M | **10.2×** |

**Important:** When using HiDPI scaling (e.g., scale factor 2), NeoWall renders at the **scaled resolution**, not the logical resolution. A 4K display at scale 2 renders at 6144×3456 pixels, which is **10× more work** than a 1080p display.

### Multi-Monitor Multiplication

With multiple monitors, the GPU must render each output independently:

```
Total GPU Work = Σ (pixels_per_output × shader_complexity × target_fps)
```

For example:
- 4K @ scale 2 (6144×3456) + Ultrawide 1080p (2560×1080)
- Total: ~24 million pixels per frame
- At 60 FPS: **1.44 billion pixels per second**

### Shader Complexity

Not all shaders are equal. Complexity factors include:

| Factor | Impact | Examples |
|--------|--------|----------|
| Raymarching loops | Very High | `train_journey.glsl`, `fractal_land.glsl` |
| Reflections/Refractions | High | Any shader with `reflect()` + secondary rays |
| Ambient Occlusion | High | Multiple distance field samples |
| Noise functions | Medium | `fbm()`, multi-octave noise |
| Texture lookups | Low-Medium | `iChannel` sampling |
| Simple math | Low | `retro_wave.glsl`, `synthwave.glsl` |

---

## Common Performance Issues

### Issue: 100% GPU Usage with Low FPS

**Symptoms:**
- GPU utilization at 100%
- Actual FPS much lower than target (e.g., 15 FPS instead of 60)
- Frame timer overrun messages in logs:
  ```
  DEBUG: Frame timer expired 4 times (frame overrun)
  INFO: FPS [Monitor]: 15.7 FPS (target: 60, frame_time: 0ms)
  ```

**Cause:** The shader is too complex for the resolution and target FPS.

**Diagnosis:** Run NeoWall in verbose mode:
```bash
neowall -f -v
```

Look for:
1. Render buffer size (e.g., `render buffer 6144x3456`)
2. Shader complexity warnings during analysis
3. Frame timer overrun messages

### Issue: High Power Consumption

**Symptoms:**
- Laptop fans spinning constantly
- High battery drain
- GPU running hot

**Cause:** Rendering at high FPS with no frame limiting.

### Issue: Stuttering/Dropped Frames

**Symptoms:**
- Animation appears jerky
- Inconsistent frame pacing

**Cause:** GPU cannot maintain target FPS consistently.

---

## Immediate Workarounds

### 1. Use a Simpler Shader

The easiest fix is to switch to a less demanding shader:

```vibe
default {
  shader retro_wave.glsl    # Simple, runs well at any resolution
  shader_speed 1.0
}
```

**Lightweight shaders (good for 4K+):**
- `retro_wave.glsl` - Simple grid + sun
- `synthwave.glsl` - 2D gradient effects
- `neonwave_sunrise.glsl` - Minimal raymarching
- `plasma.glsl` - Pure math, no raymarching

**Heavy shaders (avoid at 4K):**
- `train_journey.glsl` - Complex raymarcher with reflections
- `fractal_land.glsl` - Fractal terrain raymarching
- `cross_galactic_ocean.glsl` - Volumetric effects
- `retro_wave.glsl` - Wait, this one varies

### 2. Lower the Target FPS

Reducing FPS linearly reduces GPU load:

```vibe
default {
  shader train_journey.glsl
  shader_fps 30              # Half the GPU work of 60 FPS
}
```

| FPS | GPU Load | Visual Quality |
|-----|----------|----------------|
| 60 | 100% | Smooth |
| 30 | 50% | Good for static scenes |
| 24 | 40% | Cinematic feel |
| 15 | 25% | Minimal, still animated |

### 3. Use Different Shaders Per Monitor

Configure heavy shaders only on lower-resolution displays:

```vibe
output {
  DP-1 {
    # 4K monitor - use simple shader
    shader retro_wave.glsl
    shader_fps 60
  }
  
  HDMI-A-1 {
    # 1080p monitor - can handle complex shader
    shader train_journey.glsl
    shader_fps 60
  }
}
```

### 4. Disable HiDPI Scaling for Wallpaper

If your compositor supports it, you may be able to render the wallpaper layer at native resolution without scaling. This is compositor-specific and not directly controllable by NeoWall.

---

## Configuration Options

### Current Options

| Option | Description | Default | Impact |
|--------|-------------|---------|--------|
| `shader_fps` | Target frames per second | 60 | Direct GPU load |
| `shader_speed` | Animation speed multiplier | 1.0 | None on GPU |
| `vsync` | Sync to monitor refresh | false | May limit FPS |

### Example Optimized Config

```vibe
# Optimized for 4K + 1080p dual monitor setup

default {
  shader retro_wave.glsl
  shader_fps 30
  shader_speed 1.0
}

output {
  # 4K monitor - conservative settings
  DP-2 {
    shader synthwave.glsl     # Simple shader
    shader_fps 30             # Lower FPS
    shader_speed 0.8          # Slower animation
  }
  
  # 1080p ultrawide - can afford more
  DP-3 {
    shader train_journey.glsl
    shader_fps 45
    shader_speed 1.0
  }
}
```

---

## Shader Complexity Guide

### How to Evaluate Shader Complexity

NeoWall provides shader analysis during loading. Run with `-v` to see:

```
DEBUG: === Intelligent Shader Analysis ===
DEBUG: Shader Statistics:
DEBUG:   - Lines: 255
DEBUG:   - Size: 6776 bytes
DEBUG:   - Functions: ~102
DEBUG:   - Loops: 5
DEBUG: Performance Estimate:
DEBUG:   -> Complex shader - may impact performance on lower-end GPUs
```

### Complexity Indicators

**High Complexity (avoid at 4K):**
- `for` loops with many iterations (raymarching)
- Multiple `map()` or distance function calls
- Reflection/refraction calculations
- Ambient occlusion (AO) calculations
- Volumetric effects (god rays, fog marching)

**Medium Complexity:**
- Fractal noise (`fbm()` with 4-8 octaves)
- Multiple texture lookups
- Complex trigonometric operations

**Low Complexity (safe for 4K):**
- Simple color gradients
- Basic UV manipulation
- Single texture lookup
- Minimal branching

### Example: train_journey.glsl Breakdown

```glsl
// EXPENSIVE: 64-iteration raymarching loop
for(int i=0; i<64; i++) {
    float d = map(ro+rd*t);  // map() called 64 times per pixel
    ...
}

// EXPENSIVE: Normal calculation (3 additional map() calls)
vec3 normal(vec3 p) {
    n.x = d - map(p-eps.xyy);
    n.y = d - map(p-eps.yxy);
    n.z = d - map(p-eps.yyx);
}

// EXPENSIVE: AO (4 more map() calls)
for(int i=1; i<=4; i++) {
    float d = map(p + n * dist);
}

// EXPENSIVE: Reflection pass (another 64 iterations)
float t2 = trace(p + rrd * 0.1, rrd, vec2(0., 150.));

// EXPENSIVE: God rays (3 more map() calls)
for(float i = 0.25; i < 1.0; i += 0.25) {
    float d = map(p + SUNDIR * 3.0);
}
```

**Total map() calls per pixel:** ~150-200 (not including the map() internals)

At 21M pixels × 60 FPS × 150 calls = **189 billion map() evaluations per second**

---

## Proposed Optimizations

The following optimizations are planned or under consideration for future NeoWall versions:

### 1. Resolution Scale Factor (Planned)

Render shaders at a fraction of native resolution, then upscale:

```vibe
default {
  shader train_journey.glsl
  shader_scale 0.5           # Render at 50% resolution
  shader_fps 60
}
```

| Scale | 4K Actual Render | GPU Load Reduction |
|-------|------------------|-------------------|
| 1.0 | 6144×3456 | 0% |
| 0.75 | 4608×2592 | 44% |
| 0.5 | 3072×1728 | 75% |
| 0.25 | 1536×864 | 94% |

**Implementation notes:**
- Render to FBO at reduced resolution
- Upscale with bilinear or Lanczos filtering
- Some shaders may look fine at 50%, others may need higher

### 2. Automatic FPS Throttling (Planned)

Detect frame overruns and automatically reduce FPS:

```vibe
default {
  shader train_journey.glsl
  shader_fps auto            # Start at 60, reduce if needed
  shader_fps_min 15          # Never go below 15 FPS
}
```

**Algorithm:**
1. Track frame time over rolling window
2. If average > target, reduce target FPS
3. If consistently under budget, slowly increase FPS
4. Clamp to configured min/max

### 3. Shader Complexity Categories (Planned)

Tag shaders with performance tiers:

```glsl
// At top of shader file:
// #pragma neowall complexity: high
// #pragma neowall recommended_fps: 30
// #pragma neowall max_resolution: 2560x1440
```

NeoWall would read these pragmas and warn/auto-configure.

### 4. Per-Output Resolution Override (Proposed)

Allow manual resolution override per output:

```vibe
output {
  DP-2 {
    shader train_journey.glsl
    render_width 1920
    render_height 1080
    shader_fps 60
  }
}
```

### 5. Lazy Rendering Mode (Proposed)

Only re-render when time advances significantly:

```vibe
default {
  shader slow_evolution.glsl
  render_mode lazy           # Skip frames if shader output unchanged
  time_step 0.1              # Only update every 100ms
}
```

### 6. GPU Power Management Hints (Proposed)

Allow users to hint at power preferences:

```vibe
default {
  shader retro_wave.glsl
  power_mode efficiency      # Prefer lower clocks, accept lower FPS
}
```

---

## Debugging Performance Issues

### Step 1: Get Verbose Output

```bash
neowall -f -v 2>&1 | tee neowall.log
```

### Step 2: Check Render Buffer Sizes

Look for lines like:
```
INFO: Output DP-2: render buffer 6144x3456 (logical 3072x1728 @ scale 2)
```

Calculate total pixels:
```
6144 × 3456 = 21,233,664 pixels
```

### Step 3: Check FPS Reports

```
INFO: FPS [XV273K]: 15.7 FPS (target: 60, frame_time: 0ms)
```

If actual FPS << target FPS, the GPU is bottlenecked.

### Step 4: Check for Frame Overruns

```
DEBUG: Frame timer expired 4 times (frame overrun)
```

This means the GPU took 4× longer than the frame budget.

### Step 5: Monitor GPU Usage

```bash
# NVIDIA
nvidia-smi -l 1

# AMD
watch -n 1 cat /sys/class/drm/card0/device/gpu_busy_percent

# Intel
intel_gpu_top
```

If GPU is at 100% and FPS is low, the shader is too complex.

### Step 6: Test with Simple Shader

```bash
# Edit config to use retro_wave.glsl
neowall -f -v
```

If FPS improves dramatically, the original shader was the issue.

### Step 7: Test Single Monitor

Disconnect one monitor or configure only one output to isolate the problem.

---

## Hardware Recommendations

### Minimum for 4K @ 60 FPS (Simple Shaders)

- NVIDIA GTX 1660 / AMD RX 580 or better
- 4GB VRAM

### Recommended for 4K @ 60 FPS (Complex Shaders)

- NVIDIA RTX 3070 / AMD RX 6800 or better
- 8GB VRAM

### Multi-4K Setup with Complex Shaders

- NVIDIA RTX 4080 / AMD RX 7900 XT or better
- 12GB+ VRAM
- Consider `shader_fps 30` or resolution scaling

---

## FAQ

### Q: Why does NeoWall use 100% GPU?

A: Fragment shaders run on every pixel, every frame. Complex shaders with raymarching can require hundreds of operations per pixel. At 4K resolution with HiDPI scaling, this becomes billions of operations per second.

### Q: Why is my RTX 2080 Ti struggling?

A: Even high-end GPUs have limits. The `train_journey.glsl` shader at 6144×3456 × 60 FPS requires processing ~1.3 billion pixels per second, each with ~150+ distance function evaluations. That's ~200 billion shader operations per second.

### Q: Should I disable HiDPI scaling?

A: NeoWall renders at the compositor's buffer size. If you can configure your compositor to use a smaller buffer for the wallpaper layer, that would help. This is compositor-specific.

### Q: Can I use the GPU for other things while NeoWall runs?

A: Yes, but if NeoWall is using 100% GPU, other GPU tasks will compete for resources. Consider:
- Lower `shader_fps` to 30 or less
- Use a simpler shader
- Resolution scaling (when implemented)

### Q: Why not just detect 4K and auto-reduce quality?

A: This is a planned feature. Currently, NeoWall trusts the user's configuration. Future versions will include smarter defaults based on resolution and shader complexity.

---

## See Also

- [Configuration Guide](../config/docs/CONFIG.md)
- [Shader Documentation](../examples/shaders/README.md)
- [Multi-Monitor Synchronization](MULTI_MONITOR_SYNC.md)

---

## Contributing

If you have ideas for performance optimizations, please:

1. Open an issue describing the optimization
2. Include benchmarks if possible
3. Consider edge cases (multi-monitor, HiDPI, various GPUs)

Performance improvements are always welcome!