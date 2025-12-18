# Multipass Shader Optimization for Multi-Monitor Setups

## The Challenge

NeoWall supports per-monitor shader configuration, meaning each monitor can run a different shader with different settings. When multiple monitors run multipass shaders (Shadertoy-style with BufferA/B/C/D + Image passes), the GPU workload scales dramatically:

```
Example: 3 monitors, each with a 4-pass shader @ 4K 60Hz

Current naive approach:
  Monitor 1 (DP-1):    BufferA → BufferB → BufferC → BufferD → Image
  Monitor 2 (HDMI-A-1): BufferA → BufferB → BufferC → BufferD → Image  
  Monitor 3 (DP-2):    BufferA → BufferB → BufferC → BufferD → Image
  
  = 15 render passes × 8.3M pixels × 60 FPS
  = 7.5 BILLION pixels/second
```

This document outlines optimization strategies to make multi-monitor multipass shaders viable.

---

## Table of Contents

1. [Per-Monitor Configuration Context](#per-monitor-configuration-context)
2. [Optimization Strategies](#optimization-strategies)
   - [Shared Buffer Rendering](#1-shared-buffer-rendering)
   - [Smart Buffer Resolution](#2-smart-buffer-resolution)
   - [Temporal Techniques](#3-temporal-techniques)
   - [Monitor Priority Rendering](#4-monitor-priority-rendering)
   - [Pass Culling & Merging](#5-pass-culling--merging)
   - [Compute Shader Conversion](#6-compute-shader-conversion)
   - [Variable Rate Shading](#7-variable-rate-shading-vrs)
3. [Architecture Design](#architecture-design)
4. [Implementation Phases](#implementation-phases)
5. [Configuration Options](#configuration-options)

---

## Per-Monitor Configuration Context

NeoWall's config allows different shaders per monitor:

```vibe
output {
  DP-1 {
    shader cyberpunk_city.glsl
    shader_fps 60
  }
  
  HDMI-A-1 {
    shader calm_waves.glsl
    shader_fps 30
  }
  
  DP-2 {
    shader matrix_rain.glsl
    shader_fps 60
  }
}
```

This per-monitor flexibility creates optimization scenarios:

| Scenario | Optimization Opportunity |
|----------|-------------------------|
| Same shader on all monitors | Maximum sharing (buffers + compiled programs) |
| Different shaders, same buffers | Partial sharing (common noise/utility buffers) |
| Completely different shaders | Per-monitor optimization only |
| Mixed shader + image monitors | Reduce shader monitor priority |

---

## Optimization Strategies

### 1. Shared Buffer Rendering

**Impact: 40-60% reduction for same-shader scenarios**

#### The Insight

Buffer passes (A/B/C/D) in ~90% of Shadertoy shaders are resolution-independent. They compute global state (noise fields, simulations, procedural patterns) that doesn't depend on which monitor displays the result.

#### Before vs After

```
BEFORE: Each monitor renders ALL passes independently

  Monitor 1: [BufferA] → [BufferB] → [BufferC] → [BufferD] → [Image]
  Monitor 2: [BufferA] → [BufferB] → [BufferC] → [BufferD] → [Image]
  Monitor 3: [BufferA] → [BufferB] → [BufferC] → [BufferD] → [Image]
  
  Total: 15 render passes

AFTER: Buffers rendered ONCE, shared across monitors with same shader

  Shared Pool: [BufferA] → [BufferB] → [BufferC] → [BufferD]
                    │           │           │           │
  Monitor 1: ───────┴───────────┴───────────┴───────────┴──→ [Image]
  Monitor 2: ───────────────────────────────────────────────→ [Image]
  Monitor 3: ───────────────────────────────────────────────→ [Image]
  
  Total: 7 render passes (53% reduction!)
```

#### Detection: Can Buffers Be Shared?

Analyze buffer shader source for resolution-dependent code:

| Pattern | Shareable? | Reason |
|---------|-----------|--------|
| `fragCoord / iResolution.xy` | ✅ Yes | Normalized UV (0-1), resolution-independent |
| `fragCoord` without iResolution | ❌ No | Pixel-specific, resolution-dependent |
| `iResolution.x / iResolution.y` | ✅ Yes | Aspect ratio only |
| `iResolution.xy` for pixel math | ❌ No | Depends on actual resolution |
| No iResolution usage | ✅ Yes | Purely procedural |

#### Sharing Groups

When multiple monitors use the same shader, they form a "sharing group":

```c
typedef struct {
    char shader_hash[64];           /* SHA256 of shader source */
    GLuint shared_buffers[4][2];    /* BufferA-D × ping-pong */
    int buffer_width, buffer_height;
    int reference_count;            /* Number of monitors using this */
    bool buffers_valid_this_frame;
    struct output_state *members[MAX_OUTPUTS];
} shader_sharing_group_t;
```

#### Per-Monitor Config Considerations

- **Same shader, different `shader_fps`**: Use highest FPS for shared buffers
- **Same shader, different `shader_speed`**: Cannot share (time differs per monitor)
- **Same shader, same settings**: Full sharing possible

---

### 2. Smart Buffer Resolution

**Impact: 20-50% reduction**

Not all buffers need full output resolution. Analyze what each buffer computes:

#### Resolution Requirements by Use Case

| Buffer Content | Optimal Resolution | Rationale |
|---------------|-------------------|-----------|
| Noise / randomness | 256×256 (tiled) | Random data tiles seamlessly |
| Blur / glow | 1/4 output | Blur destroys detail anyway |
| Fluid simulation | 1/2 output | Simulation doesn't need pixel precision |
| Particle systems | 1/2 output | Particles are point-sampled |
| Feedback effects | 1/2 output | Temporal blur masks low res |
| SDF / raymarching | Full or 1/2 | Depends on detail level |
| Sharp geometry | Full | Edges need precision |

#### Auto-Detection Heuristics

```c
typedef enum {
    BUFFER_RES_FULL,        /* 100% - sharp/precise content */
    BUFFER_RES_HIGH,        /* 75% - moderate detail */
    BUFFER_RES_MEDIUM,      /* 50% - blur/simulation */
    BUFFER_RES_LOW,         /* 25% - noise/glow */
    BUFFER_RES_TINY         /* 64-256px - pure procedural */
} buffer_resolution_hint_t;

/* Detection rules */
buffer_resolution_hint_t detect_buffer_resolution(const char *source) {
    if (contains(source, "textureLod") && extract_lod(source) > 2.0)
        return BUFFER_RES_LOW;  /* High LOD = low frequency */
    
    if (contains(source, "blur") || contains(source, "glow"))
        return BUFFER_RES_MEDIUM;
    
    if (contains(source, "noise") && !contains(source, "fragCoord"))
        return BUFFER_RES_TINY;  /* Tileable noise */
    
    if (contains(source, "smoothstep") && count(source, "smoothstep") > 5)
        return BUFFER_RES_MEDIUM;  /* Heavy smoothing */
    
    return BUFFER_RES_FULL;  /* Default: full resolution */
}
```

#### Per-Monitor Config: Buffer Scale Override

```vibe
output {
  DP-1 {
    shader heavy_shader.glsl
    buffer_scale 0.5          # Force all buffers to 50%
    # OR per-buffer control:
    buffer_a_scale 0.25       # Noise buffer at 25%
    buffer_b_scale 0.5        # Blur buffer at 50%
    buffer_c_scale 1.0        # Detail buffer at 100%
  }
}
```

---

### 3. Temporal Techniques

**Impact: 50-75% reduction for suitable shaders**

#### 3a. Checkerboard Rendering

Render half the pixels each frame in a checkerboard pattern:

```
Frame N:   ██░░██░░██░░    (render dark squares)
           ░░██░░██░░██
           ██░░██░░██░░
           
Frame N+1: ░░██░░██░░██    (render light squares)
           ██░░██░░██░░
           ░░██░░██░░██

Reconstruction: Temporal filter blends both frames
```

**Benefits:**
- 2× performance for buffer passes
- Minimal quality loss for smooth content
- Perfect for wallpapers (no fast motion)

**Implementation:**
- Stencil buffer masks checkerboard pattern
- Temporal accumulation blends frames
- Jittered sampling for anti-aliasing

#### 3b. Frame Interpolation

For slow-moving wallpaper animations:

```
Frame 0: Full render ─────────────────────┐
Frame 1: Reproject from Frame 0 ──────────┤ (motion compensated)
Frame 2: Full render ─────────────────────┤
Frame 3: Reproject from Frame 2 ──────────┘

Result: 2× FPS with one render
```

**Requirements:**
- Store previous frame texture
- Compute or estimate motion vectors
- Blend/warp for interpolated frames

#### 3c. Temporal Accumulation

For static or very slow scenes:

```c
/* Only re-render when needed */
bool should_render = 
    (time_delta > threshold) ||
    (mouse_moved) ||
    (scene_changed);

if (!should_render) {
    /* Reuse previous frame */
    blit(previous_frame, output);
} else {
    render_frame();
    copy(output, previous_frame);
}
```

#### Per-Monitor Config: Temporal Settings

```vibe
output {
  DP-1 {
    shader chill_shader.glsl
    temporal_mode interpolate   # none | accumulate | interpolate | checkerboard
    temporal_quality 0.8        # 0.0-1.0, higher = more accurate
  }
}
```

---

### 4. Monitor Priority Rendering

**Impact: 25-50% reduction for secondary monitors**

Users focus on one monitor at a time. Secondary/tertiary monitors can run at reduced quality without noticeable degradation.

#### Priority Levels

| Priority | Quality | Resolution Scale | Techniques |
|----------|---------|-----------------|------------|
| Primary (focused) | 100% | 1.0 | Full quality |
| Secondary | 75% | 0.85 | Checkerboard buffers |
| Tertiary | 50% | 0.7 | Lower buffer res + checkerboard |
| Background | 25% | 0.5 | Temporal accumulation, skip frames |

#### Auto-Detection

```c
/* Determine monitor priority */
monitor_priority_t get_monitor_priority(struct output_state *output) {
    if (output->has_focused_window)
        return PRIORITY_PRIMARY;
    
    if (output->has_any_window)
        return PRIORITY_SECONDARY;
    
    if (output->is_primary_display)
        return PRIORITY_SECONDARY;
    
    return PRIORITY_TERTIARY;
}
```

#### Per-Monitor Config: Priority Override

```vibe
output {
  DP-1 {
    shader main_shader.glsl
    priority primary            # Always full quality
  }
  
  HDMI-A-1 {
    shader ambient_shader.glsl
    priority low                # Always reduced quality
    quality_scale 0.5           # 50% quality cap
  }
}
```

---

### 5. Pass Culling & Merging

**Impact: 10-30% reduction**

#### Pass Culling

Skip passes that aren't contributing:

| Condition | Action |
|-----------|--------|
| Mouse-dependent pass, mouse inactive for 5s | Skip pass, reuse last result |
| Audio-reactive pass, no audio playing | Skip pass, use silence fallback |
| Buffer unchanged from last frame | Reuse previous buffer |
| Time-based pass, time delta < threshold | Reuse previous result |

```c
bool should_render_pass(multipass_pass_t *pass, render_context_t *ctx) {
    /* Skip mouse-dependent passes when mouse idle */
    if (pass->uses_mouse && ctx->mouse_idle_time > 5.0)
        return false;
    
    /* Skip if buffer content unchanged */
    if (pass->type != PASS_TYPE_IMAGE && 
        pass->content_hash == pass->prev_content_hash)
        return false;
    
    return true;
}
```

#### Pass Merging

Combine sequential passes when possible:

```glsl
/* Instead of separate horizontal + vertical blur passes: */

// Pass 1: Horizontal blur → BufferA
// Pass 2: Vertical blur (reads BufferA) → BufferB

/* Merge into single pass with approximation: */

// Combined Pass: Diagonal/box blur → BufferA
// (Slightly lower quality, but half the work)
```

---

### 6. Compute Shader Conversion

**Impact: 20-40% reduction (advanced)**

Fragment shaders have rasterization overhead. Compute shaders offer:

- Direct control over thread dispatch
- Shared memory for local operations (blur kernels)
- No rasterization pipeline overhead
- Persistent threads for complex workloads

#### Conversion Strategy

```glsl
/* Fragment shader (current) */
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = texture(iChannel0, uv);
}

/* Compute shader (optimized) */
layout(local_size_x = 16, local_size_y = 16) in;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = vec2(pixel) / vec2(imageSize(outputImage));
    vec4 color = texture(iChannel0, uv);
    imageStore(outputImage, pixel, color);
}
```

#### When to Convert

- Heavy per-pixel computation (raymarching, fluid sim)
- Local operations (blur, edge detection)
- Parallel reductions (histogram, statistics)

---

### 7. Variable Rate Shading (VRS)

**Impact: 30-50% reduction (hardware-dependent)**

Modern GPUs (Nvidia Turing+, AMD RDNA2+, Intel Xe) support Variable Rate Shading:

```
┌──────────────────────────────────────────────────┐
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  4×4 shading (edges)
│ ░░░░░░░▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░░░ │  2×2 shading (mid)
│ ░░░░░░░▒▒▒▒▒▒▒████████████████████▒▒▒▒░░░░░░░░ │  1×1 shading (center)
│ ░░░░░░░▒▒▒▒▒▒▒████████████████████▒▒▒▒░░░░░░░░ │
│ ░░░░░░░▒▒▒▒▒▒▒████████████████████▒▒▒▒░░░░░░░░ │
│ ░░░░░░░▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
└──────────────────────────────────────────────────┘
```

**For buffer passes**: Use 2×2 or 4×4 everywhere (buffers aren't directly visible)
**For image pass**: Use screen-position-based VRS map

#### Requirements

- OpenGL: `GL_NV_shading_rate_image` extension
- Vulkan: `VK_KHR_fragment_shading_rate`
- Check hardware support at runtime

---

## Architecture Design

### Unified Rendering Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SHADER REGISTRY                              │
│  • Parses shaders once, caches compiled programs                     │
│  • Computes shader hash for sharing detection                        │
│  • Analyzes buffer resolution requirements                           │
│  • Tracks which monitors use which shaders                           │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       SHARING GROUP MANAGER                          │
│  • Groups monitors by shader + compatible settings                   │
│  • Manages shared buffer pools                                       │
│  • Coordinates buffer rendering (once per group)                     │
│  • Handles per-monitor overrides                                     │
└─────────────────────────────────────────────────────────────────────┘
                                   │
           ┌───────────────────────┼───────────────────────┐
           ▼                       ▼                       ▼
┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐
│   SHARING GROUP 1  │  │   SHARING GROUP 2  │  │   SHARING GROUP 3  │
│   (shader_a.glsl)  │  │   (shader_b.glsl)  │  │   (shader_a.glsl)  │
│                    │  │                    │  │   different speed  │
│  Shared Buffers:   │  │  Shared Buffers:   │  │                    │
│  [A][B][C][D]      │  │  [A][B]            │  │  Own Buffers:      │
│                    │  │                    │  │  [A][B][C][D]      │
│  Members:          │  │  Members:          │  │                    │
│  - Monitor DP-1    │  │  - Monitor DP-2    │  │  Members:          │
│  - Monitor HDMI-1  │  │                    │  │  - Monitor DP-3    │
└────────────────────┘  └────────────────────┘  └────────────────────┘
           │                       │                       │
           ▼                       ▼                       ▼
┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐
│  Buffer Pass Once  │  │  Buffer Pass Once  │  │  Buffer Pass Once  │
│  (serves 2 mons)   │  │  (serves 1 mon)    │  │  (serves 1 mon)    │
└────────────────────┘  └────────────────────┘  └────────────────────┘
           │                       │                       │
           ▼                       ▼                       ▼
┌────────────────────────────────────────────────────────────────────┐
│                      IMAGE PASS RENDERER                            │
│  • Renders Image pass per-monitor (different resolutions/outputs)   │
│  • Samples from shared or per-monitor buffers                       │
│  • Applies per-monitor quality settings                             │
└────────────────────────────────────────────────────────────────────┘
           │                       │                       │
           ▼                       ▼                       ▼
      [Monitor 1]            [Monitor 2]            [Monitor 3]
```

### Data Structures

```c
/* Shader entry in registry */
typedef struct {
    char path[PATH_MAX];
    char hash[65];                      /* SHA256 */
    GLuint programs[MULTIPASS_MAX_PASSES];
    buffer_resolution_hint_t buffer_hints[4];
    bool buffers_shareable[4];          /* Per-buffer shareability */
    int reference_count;
} shader_registry_entry_t;

/* Sharing group for monitors with same shader + settings */
typedef struct {
    shader_registry_entry_t *shader;
    
    /* Shared buffer pool */
    GLuint shared_buffers[4][2];        /* BufferA-D × ping-pong */
    int shared_width, shared_height;
    float buffer_scales[4];             /* Per-buffer resolution scale */
    bool buffers_rendered_this_frame;
    
    /* Temporal state */
    GLuint temporal_accumulator;
    int temporal_frame_count;
    
    /* Members */
    struct output_state *members[MAX_OUTPUTS];
    int member_count;
    
    /* Compatibility key (for grouping) */
    float shader_speed;                 /* Must match to share */
    bool uses_mouse;                    /* Mouse-using shaders can't share */
} sharing_group_t;

/* Per-monitor rendering state */
typedef struct {
    sharing_group_t *group;             /* NULL if not sharing */
    
    /* Per-monitor overrides */
    monitor_priority_t priority;
    float quality_scale;
    temporal_mode_t temporal_mode;
    
    /* Own buffers (if not sharing) */
    GLuint own_buffers[4][2];
    bool has_own_buffers;
} monitor_render_state_t;
```

---

## Implementation Phases

### Phase 1: Shared Buffer Architecture (Highest Impact)

**Goal**: Render buffer passes once per shader, share across monitors

**Tasks**:
1. Add shader registry with hash-based lookup
2. Implement sharing group detection and management
3. Modify multipass renderer to use shared buffers
4. Add shareability analysis for buffers

**Expected Result**: 40-60% reduction for same-shader multi-monitor setups

### Phase 2: Smart Buffer Sizing (Medium Impact, Low Effort)

**Goal**: Automatically size buffers based on content analysis

**Tasks**:
1. Implement shader source analysis for buffer hints
2. Add per-buffer resolution scaling
3. Expose config options for manual override
4. Validate quality with test shaders

**Expected Result**: 20-50% additional reduction

### Phase 3: Monitor Priority System (Low Effort, Good Impact)

**Goal**: Reduce quality on non-focused monitors

**Tasks**:
1. Implement focus tracking
2. Add priority-based quality scaling
3. Integrate with adaptive resolution system
4. Add config options for priority override

**Expected Result**: 25-50% reduction on secondary monitors

### Phase 4: Temporal Techniques (High Impact, High Effort)

**Goal**: Reduce per-frame work with temporal reuse

**Tasks**:
1. Implement checkerboard rendering for buffers
2. Add temporal accumulation for static scenes
3. Implement frame interpolation for slow animations
4. Add motion compensation

**Expected Result**: Up to 50% additional reduction for suitable shaders

### Phase 5: Advanced Optimizations (Long Term)

**Goals**: Hardware-specific optimizations

**Tasks**:
1. VRS support for supported hardware
2. Compute shader conversion for heavy shaders
3. Async compute for parallel rendering
4. Shader specialization and simplification

---

## Configuration Options

### Proposed Config Syntax

```vibe
# Global optimization settings
optimization {
  share_buffers true                    # Enable cross-monitor buffer sharing
  smart_buffer_scale true               # Auto-detect optimal buffer resolutions
  temporal_mode auto                    # none | auto | checkerboard | interpolate
  monitor_priority_scaling true         # Reduce quality on non-focused monitors
}

# Default settings (applied to all outputs)
default {
  shader default.glsl
  shader_fps 60
  quality_scale 1.0                     # 0.1 - 1.0
  buffer_scale auto                     # auto | 0.25 | 0.5 | 0.75 | 1.0
  priority auto                         # auto | primary | secondary | low | minimal
}

# Per-monitor overrides
output {
  DP-1 {
    shader hero_shader.glsl
    shader_fps 60
    priority primary                    # Always highest quality
    buffer_scale 1.0                    # Full resolution buffers
  }
  
  HDMI-A-1 {
    shader ambient.glsl
    shader_fps 30
    priority low                        # Reduced quality OK
    quality_scale 0.75                  # 75% quality cap
    temporal_mode checkerboard          # Use checkerboard rendering
  }
  
  DP-2 {
    shader same_as_dp1.glsl             # Will share buffers with DP-1
    shader_fps 60
    shader_speed 1.0                    # Must match DP-1 to share
  }
}
```

---

## Performance Projections

### Scenario: 3 × 4K Monitors, 4-Pass Shader, 60 FPS

| Configuration | Passes/Frame | Pixels/Second | Reduction |
|--------------|--------------|---------------|-----------|
| Baseline (no optimization) | 15 | 7.5B | - |
| Shared buffers (same shader) | 7 | 3.5B | 53% |
| + Smart buffer sizing | 7 | 2.1B | 72% |
| + Monitor priority | 7 | 1.6B | 79% |
| + Checkerboard buffers | 7 | 1.0B | 87% |

### Scenario: 3 Monitors, Different Shaders

| Configuration | Reduction |
|--------------|-----------|
| Baseline | - |
| Smart buffer sizing only | 30% |
| + Monitor priority | 50% |
| + Temporal techniques | 65% |

---

## Conclusion

Multi-monitor multipass shader wallpapers are achievable with the right optimizations. The key insights are:

1. **Share what can be shared**: Buffer passes are often resolution-independent
2. **Right-size the work**: Not every buffer needs full resolution
3. **Prioritize what matters**: Users focus on one monitor at a time
4. **Reuse across time**: Wallpapers change slowly, exploit temporal coherence

With these optimizations, a 3-4× performance improvement is achievable, making multi-monitor setups with complex shaders viable on mid-range hardware.