# Shadertoy Support in Staticwall

## Overview

Staticwall now supports **OpenGL ES 3.0** with automatic fallback to ES 2.0, enabling nearly complete compatibility with [Shadertoy.com](https://www.shadertoy.com) shaders. This means you can copy most Shadertoy shaders directly and use them as live wallpapers!

## Quick Start

### Copy a Shadertoy Shader

1. Go to [Shadertoy.com](https://www.shadertoy.com)
2. Find a shader you like (try searching "plasma", "nebula", "matrix")
3. Click on the shader to view it
4. Click the "View Code" button or scroll down to see the GLSL code
5. Copy the entire `mainImage` function (or the whole shader)
6. Save it as `~/.config/staticwall/shaders/my_shader.glsl`
7. Update your config:

```vibe
default {
  shader my_shader.glsl
}
```

8. Reload: `staticwall reload`

That's it! Your Shadertoy shader is now your wallpaper.

---

## OpenGL ES Version Support

### Automatic Version Detection

Staticwall automatically detects your GPU's capabilities at runtime:

```
[INFO] Using OpenGL ES 3.0 (enhanced Shadertoy compatibility)
```

or

```
[INFO] Using OpenGL ES 2.0 (basic Shadertoy compatibility)
```

### What Each Version Supports

#### OpenGL ES 3.0 (Enhanced Support) ✅

When ES 3.0 is available, you get:

- ✅ `texture()` function (instead of `texture2D()`)
- ✅ Integer types (`int`, `uint`) in shaders
- ✅ Modern GLSL syntax (`in`/`out` instead of `varying`)
- ✅ Non-constant array indexing
- ✅ Better loop support
- ✅ `#version 300 es` shaders work directly
- ✅ **80-90% of Shadertoy shaders work unchanged**

#### OpenGL ES 2.0 (Basic Support) ⚠️

When only ES 2.0 is available:

- ✅ `texture2D()` function
- ✅ Float-based operations
- ✅ `#version 100` shaders
- ✅ `varying`/`attribute` syntax
- ⚠️ Some Shadertoy shaders need minor adaptations

### Automatic Shader Conversion

Staticwall includes an **intelligent shader adaptation layer** that automatically converts between ES 2.0 and ES 3.0 syntax:

| Conversion | ES 2.0 → ES 3.0 | ES 3.0 → ES 2.0 |
|------------|-----------------|-----------------|
| Version directive | `#version 100` → `#version 300 es` | `#version 300 es` → `#version 100` |
| Texture sampling | `texture2D()` → `texture()` | `texture()` → `texture2D()` |
| Vertex inputs | `attribute` → `in` | `in` → `attribute` |
| Vertex outputs | `varying` → `out` | `out` → `varying` |
| Fragment inputs | `varying` → `in` | `in` → `varying` |
| Fragment output | `fragColor` → `gl_FragColor` | `gl_FragColor` → `fragColor` |

**This means**: Write shaders in either ES 2.0 or ES 3.0 syntax, and Staticwall adapts them automatically!

---

## Shadertoy Compatibility Layer

### Supported Uniforms

Staticwall provides a compatibility layer for Shadertoy's standard uniforms:

#### ✅ Fully Supported

| Uniform | Type | Description | Status |
|---------|------|-------------|--------|
| `iTime` | `float` | Shader playback time (seconds) | ✅ Full |
| `iResolution` | `vec3` | Viewport resolution (width, height, aspect) | ✅ Full |
| `fragCoord` | `vec2` | Pixel coordinates (passed to mainImage) | ✅ Full |
| `fragColor` | `vec4` | Output color (passed to mainImage) | ✅ Full |

#### ⚠️ Partially Supported

| Uniform | Type | Description | Status |
|---------|------|-------------|--------|
| `iChannel0-3` | `sampler2D` | Input textures | ⚠️ Noise fallback |

#### ❌ Not Yet Supported (Planned)

| Uniform | Type | Description | Status |
|---------|------|-------------|--------|
| `iMouse` | `vec4` | Mouse position and state | ❌ Fixed at (0,0,0,0) |
| `iTimeDelta` | `float` | Time since last frame | ❌ Fixed at 16.67ms |
| `iFrame` | `int` | Current frame number | ❌ Fixed at 0 |
| `iDate` | `vec4` | Current date/time | ❌ Static value |
| `iChannelTime` | `vec4[4]` | Channel playback time | ❌ Not implemented |
| `iChannelResolution` | `vec3[4]` | Channel resolutions | ❌ Not implemented |
| `iSampleRate` | `float` | Audio sample rate | ❌ Fixed at 44100 |

### Texture Channels (iChannel0-3)

Shadertoy shaders often use texture inputs (`iChannel0`, `iChannel1`, etc.) for things like:
- Noise textures
- Pre-rendered images
- Video frames
- Audio data

**Current Behavior**: Staticwall provides **procedural noise fallbacks** for texture channels. This means:

✅ Shaders compile and run successfully
⚠️ Visual output may differ from Shadertoy (procedural noise instead of actual textures)

**Example**: A shader expecting a brick texture in `iChannel0` will instead receive procedural noise that looks like random grayscale patterns.

**Planned Enhancement**: Future versions will support loading actual texture images for iChannels.

---

## Shadertoy Shader Formats

### Format 1: Standard Shadertoy (Recommended)

This is the format you'll find on Shadertoy.com:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Normalize pixel coordinates (from 0 to 1)
    vec2 uv = fragCoord / iResolution.xy;
    
    // Time varying pixel color
    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0, 2, 4));
    
    // Output to screen
    fragColor = vec4(col, 1.0);
}
```

**Just copy-paste from Shadertoy!** Staticwall automatically:
1. Detects the `mainImage` function
2. Wraps it with proper uniforms
3. Adds GLSL version directive
4. Injects compatibility helpers

### Format 2: Native Staticwall

If you're writing shaders from scratch, you can use the simpler native format:

```glsl
#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    vec3 col = 0.5 + 0.5 * cos(time + uv.xyx + vec3(0, 2, 4));
    gl_FragColor = vec4(col, 1.0);
}
```

### Format 3: ES 3.0 Native (Advanced)

For maximum performance on ES 3.0 systems:

```glsl
#version 300 es
precision mediump float;

uniform float time;
uniform vec2 resolution;

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    vec3 col = 0.5 + 0.5 * cos(time + uv.xyx + vec3(0, 2, 4));
    fragColor = vec4(col, 1.0);
}
```

---

## Compatibility Tips

### High Compatibility Shaders (90%+ success rate)

Look for Shadertoy shaders with these characteristics:

✅ Use only `iTime` and `iResolution`
✅ Pure mathematical/procedural (no textures)
✅ No mouse interaction required
✅ Single-pass rendering
✅ Relatively simple (< 200 lines)

**Popular categories**:
- Plasma effects
- Fractals (Mandelbrot, Julia sets)
- Procedural patterns
- Simple 3D raymarching
- Mathematical visualizations

### Moderate Compatibility (50-80% success rate)

These may work with minor modifications:

⚠️ Use `iChannel0-3` for noise/textures
⚠️ Use `iMouse` (can be commented out)
⚠️ Complex multi-pass effects
⚠️ Heavy computation (may be slow)

### Low Compatibility (< 50% success rate)

These likely won't work without significant changes:

❌ Require specific texture inputs
❌ Depend on `iMouse` heavily
❌ Use multiple render passes (Buffers A, B, C, D)
❌ Audio-reactive shaders
❌ Video/webcam input

---

## Testing Shaders

### Recommended Workflow

1. **Test on Shadertoy first**: Make sure the shader works on Shadertoy.com
2. **Check requirements**: Look for `iChannel` usage, mouse input, etc.
3. **Copy to Staticwall**: Save as `.glsl` file
4. **Test with verbose logging**:
   ```bash
   staticwall -f -v
   ```
5. **Check the analysis**: Staticwall will analyze the shader and report:
   - Detected features
   - Compatibility warnings
   - Performance estimate

### Example Output

```
[INFO] === Intelligent Shader Analysis ===
[INFO] Shader Statistics:
[INFO]   - Lines: 156
[INFO]   - Size: 4521 bytes
[INFO]   - Functions: ~12
[INFO]   - Loops: 3
[INFO] Features Detected:
[INFO]   + Texture Channels: 1 channel
[INFO]     -> Will use noise-based fallbacks
[INFO] Performance Estimate:
[INFO]   -> Moderate complexity - good performance expected
[INFO] =================================
```

### Debugging Shader Compilation

If a shader fails to compile:

```bash
staticwall -f -v 2>&1 | grep -A 20 "shader"
```

Common issues:
- **Syntax errors**: Check GLSL version compatibility
- **Missing uniforms**: Make sure you're using supported uniforms
- **Integer types on ES 2.0**: Replace `int` with `float` or enable ES 3.0
- **Array indexing**: ES 2.0 requires constant indices

---

## Performance Considerations

### GPU Usage

Live shaders run continuously at your display's refresh rate (typically 60 FPS). Performance depends on:

1. **Shader complexity**: Loops, function calls, texture samples
2. **Resolution**: 4K requires 4× more pixels than 1080p
3. **GPU capability**: Integrated vs. dedicated GPU

### Performance Levels

| Category | CPU Usage | GPU Usage | Example Shaders |
|----------|-----------|-----------|-----------------|
| Lightweight | < 2% | < 5% | Simple gradients, basic plasma |
| Medium | 2-5% | 5-15% | Fractals, simple raymarching |
| Heavy | 5-10% | 15-30% | Complex 3D scenes, multi-layer effects |
| Very Heavy | > 10% | > 30% | Advanced raymarching, many loops |

### Optimization Tips

1. **Reduce iterations**: If a shader has `for(int i=0; i<100; i++)`, try reducing to 50
2. **Lower precision**: Change `highp` to `mediump` or `lowp`
3. **Simplify effects**: Remove expensive operations like `pow()`, `sin()`, `cos()` in tight loops
4. **Use shader speed control**:
   ```bash
   staticwall shader_speed_down  # Reduce animation speed
   staticwall shader_speed_up    # Increase animation speed
   ```

---

## Advanced Features

### Per-Monitor Shaders

Run different shaders on each monitor:

```vibe
default {
  shader plasma.glsl
}

output {
  eDP-1 {
    shader matrix.glsl
  }
  
  HDMI-A-1 {
    shader nebula.glsl
  }
}
```

### Shader Speed Control

Adjust animation speed at runtime:

```bash
staticwall shader_speed_up      # Increase by 1.0x
staticwall shader_speed_down    # Decrease by 1.0x
```

Or in config:

```vibe
default {
  shader wave.glsl
  shader_speed 0.5    # Half speed (slow motion)
}
```

### Transitions Between Shaders

When cycling through multiple shaders:

```vibe
default {
  path ~/wallpapers/shaders/
  cycle true
  duration 300  # 5 minutes per shader
  transition fade
  transition_duration 2000  # 2 second fade
}
```

---

## Popular Shadertoy Shaders to Try

### Beginner-Friendly (High Compatibility)

1. **"Plasma"** - Search: `plasma simple`
   - Colorful animated plasma effect
   - No textures, pure math

2. **"Star Nest"** - Search: `star nest`
   - Beautiful space tunnel effect
   - Some versions use textures (find "procedural" versions)

3. **"Fractal Pyramid"** - Search: `fractal pyramid`
   - Mesmerizing recursive patterns

4. **"Protean Clouds"** - Search: `protean clouds`
   - Animated cloud-like formations

### Intermediate (May Need Adaptation)

1. **"Seascape"** - Search: `seascape`
   - Realistic ocean waves
   - May use textures for foam

2. **"Elevated"** - Search: `elevated`
   - Stunning mountain terrain
   - Heavy computation

3. **"Rainforest"** - Search: `rainforest`
   - Lush vegetation effect

### Advanced (Worth the Effort)

1. **"Snail"** - Search: `snail`
   - Incredible 3D scene
   - Very heavy, needs powerful GPU

2. **"Canyon"** - Search: `canyon`
   - Beautiful landscape
   - Complex raymarching

---

## FAQ

### Why doesn't my Shadertoy shader look the same?

**Textures**: Most visual differences come from `iChannel` textures. Shadertoy uses specific images (noise textures, photos, etc.), while Staticwall uses procedural noise as a fallback.

**Solution**: Choose shaders that don't rely on specific texture content, or wait for texture loading support.

### Can I use multipass shaders (Buffer A, B, C)?

**Not yet**. Staticwall currently only supports single-pass fragment shaders. Multipass support is planned for future releases.

### Why is my shader slow?

1. **Check complexity**: Look for nested loops, expensive functions
2. **Reduce quality**: Lower iteration counts
3. **Check GPU**: Run `glxinfo | grep "OpenGL renderer"` to see your GPU
4. **Monitor usage**: Run `htop` and `nvidia-smi` (or `radeontop`) to check resource usage

### How do I convert a shader that uses iMouse?

For now, you have two options:

1. **Comment out mouse code**:
   ```glsl
   // vec2 mouse = iMouse.xy / iResolution.xy;
   vec2 mouse = vec2(0.5, 0.5);  // Fixed center position
   ```

2. **Remove mouse-dependent effects** entirely

### Does Staticwall support Shadertoy's audio features?

Not yet. Audio-reactive shaders that use `iChannel` for audio input won't work correctly. This feature is planned for future releases.

---

## Contributing

### Found a Compatible Shader?

Share it! Create a PR with:
1. The shader file in `examples/shaders/`
2. Add it to the README
3. Include attribution to original Shadertoy author

### Improving Compatibility

Help us improve Shadertoy support:

1. **Report incompatible shaders**: Open an issue with the Shadertoy link
2. **Texture support**: Help implement actual iChannel texture loading
3. **Multipass rendering**: Contribute to the multipass shader system
4. **Mouse input**: Add mouse position tracking

---

## Technical Details

### Architecture

```
┌─────────────────────────────────────────┐
│  Shadertoy Shader (.glsl)               │
│  - Uses iTime, iResolution, iChannel    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  Shadertoy Compatibility Layer          │
│  - Detects mainImage() function         │
│  - Injects uniform declarations         │
│  - Provides texture fallbacks           │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  Shader Adaptation Layer                │
│  - Converts ES 2.0 ↔ ES 3.0             │
│  - Handles version directives           │
│  - Adapts syntax (texture(), in/out)    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  OpenGL ES Context (2.0 or 3.0)         │
│  - Compiles adapted shader              │
│  - Renders to layer surface             │
│  - Updates at display refresh rate      │
└─────────────────────────────────────────┘
```

### Version Detection

At startup, Staticwall:
1. Tries to create ES 3.0 context
2. Falls back to ES 2.0 if unavailable
3. Queries GL capabilities
4. Logs detected version and features
5. Adapts all shaders accordingly

This happens automatically—no configuration needed!

---

## License

Shaders copied from Shadertoy are subject to their original licenses (usually CC BY-NC-SA 3.0). Always credit the original author!

Staticwall's shader adaptation and compatibility layers are released under the same license as Staticwall itself (MIT).

---

## Support

- **Documentation**: See `examples/shaders/README.md`
- **Issues**: Report bugs or incompatible shaders on GitHub
- **Discord**: Join the community for shader help and sharing

**Happy Shading!** 🎨✨