# iChannel Texture System

## Overview

Staticwall provides a comprehensive iChannel texture system that allows shaders to access texture inputs, just like Shadertoy. The system includes 5 default procedural textures and supports custom image textures.

## What are iChannels?

In Shadertoy and other shader environments, **iChannels** are texture inputs that shaders can sample. They're accessed through uniform samplers:

```glsl
vec4 color = texture(iChannel0, uv);
vec4 noise = textureLod(iChannel1, uv, 0.0);
```

Staticwall supports **iChannel0 through iChannel4** (5 channels total), which matches most Shadertoy shader requirements.

## Default Procedural Textures

When no custom textures are specified, staticwall automatically provides high-quality procedural textures for each channel:

### iChannel0: RGBA Noise (rgba_noise)

**Most commonly used texture in Shadertoy**

- **Description**: Independent noise in all 4 channels (R, G, B, A)
- **Size**: 256×256 pixels
- **Use Cases**:
  - General-purpose noise effects
  - Random number generation in shaders
  - Texture variation and detail
  - Multi-octave noise lookups
- **Characteristics**:
  - Tileable pattern
  - Different frequencies per channel
  - Smooth interpolation between values

**Example Usage:**
```glsl
vec4 n = texture(iChannel0, uv);
float r = n.r; // Red channel noise
float g = n.g; // Green channel noise (different pattern)
```

### iChannel1: Gray Noise (gray_noise)

**High-quality grayscale noise**

- **Description**: Multi-octave fractal Brownian motion (fBM) in grayscale
- **Size**: 256×256 pixels
- **Use Cases**:
  - Displacement maps
  - Height maps
  - Single-channel effects
  - Cloud/fog generation
- **Characteristics**:
  - 5 octaves of detail
  - Smooth, natural-looking patterns
  - Tileable

**Example Usage:**
```glsl
float height = texture(iChannel1, uv).r;
vec3 displaced = position + normal * height;
```

### iChannel2: Blue Noise (blue_noise)

**Better distributed than white noise**

- **Description**: Blue noise using void-and-cluster algorithm
- **Size**: 256×256 pixels
- **Use Cases**:
  - Dithering
  - Reducing banding artifacts
  - Temporal anti-aliasing
  - Screen-space effects
- **Characteristics**:
  - Superior frequency distribution
  - Minimizes visual artifacts
  - Better than random noise for human perception

**Example Usage:**
```glsl
float dither = texture(iChannel2, gl_FragCoord.xy / 256.0).r;
if (value + dither * 0.1 > threshold) {
    // Pass
}
```

### iChannel3: Wood Grain (wood)

**Realistic wood texture**

- **Description**: Procedural wood grain with natural variation
- **Size**: 256×256 pixels
- **Use Cases**:
  - Natural backgrounds
  - Organic patterns
  - Material textures
  - Artistic effects
- **Characteristics**:
  - Realistic ring patterns
  - Fine grain detail
  - Brown color tones
  - Natural variation

**Example Usage:**
```glsl
vec3 woodColor = texture(iChannel3, uv).rgb;
```

### iChannel4: Abstract Pattern (abstract)

**Colorful artistic texture**

- **Description**: Voronoi-based pattern with color variations
- **Size**: 256×256 pixels
- **Use Cases**:
  - Abstract backgrounds
  - Artistic effects
  - Color variation
  - Cell-based patterns
- **Characteristics**:
  - Voronoi cells with unique colors
  - HSV-based color generation
  - Multi-scale patterns
  - Vibrant and varied

**Example Usage:**
```glsl
vec3 pattern = texture(iChannel4, uv).rgb;
```

## Configuration

### Using Default Textures

By default, all shaders automatically get the 5 procedural textures assigned to iChannel0-4:

```vibe
default {
    shader examples/shaders/my_shader.glsl
    mode fill
}
```

The shader can immediately use any of the channels:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // All these work automatically:
    vec4 noise = texture(iChannel0, uv);
    float gray = texture(iChannel1, uv).r;
    float dither = texture(iChannel2, uv).r;
    vec3 wood = texture(iChannel3, uv).rgb;
    vec3 abstract = texture(iChannel4, uv).rgb;
    
    fragColor = vec4(noise.rgb, 1.0);
}
```

### Custom Texture Assignment (Coming Soon)

Future versions will support custom texture assignment:

```vibe
default {
    shader examples/shaders/my_shader.glsl
    mode fill
    
    # Custom texture for specific channel
    channel0 /path/to/image.png
    
    # Or use named default textures
    channel1 gray_noise
    channel2 blue_noise
}
```

## Texture Access Methods

Staticwall's compatibility layer supports all common Shadertoy texture access patterns:

### Standard texture() - GLSL ES 3.0 style

```glsl
vec4 color = texture(iChannel0, uv);
vec4 color = texture(iChannel0, vec3(uv, layer)); // 3D coords
```

### textureLod() - With mipmap level

```glsl
vec4 color = textureLod(iChannel0, uv, 0.0);
vec4 blurry = textureLod(iChannel0, uv, 3.0); // Higher mip level
```

### texelFetch() - Integer coordinates

```glsl
ivec2 pixel = ivec2(100, 50);
vec4 color = texelFetch(iChannel0, pixel, 0);
```

## Technical Details

### Texture Properties

All default textures are generated with:

- **Format**: RGBA8 (GL_RGBA, GL_UNSIGNED_BYTE)
- **Filtering**: 
  - Min: `GL_LINEAR_MIPMAP_LINEAR` (trilinear)
  - Mag: `GL_LINEAR`
- **Wrapping**: `GL_REPEAT` (tileable)
- **Mipmaps**: Automatically generated

### Memory Usage

Each 256×256 RGBA texture with mipmaps uses approximately:
- Base level: 256 KB
- Full mipmap chain: ~341 KB
- **Total for all 5 channels**: ~1.7 MB

This is negligible compared to video memory available on modern GPUs.

### Performance

- Textures are generated once at startup
- Cached and reused across all shaders
- GPU texture sampling is highly optimized
- No runtime overhead

## Common Use Cases

### Rain on Glass Effect

```glsl
// Using noise for rain drops
float n = texture(iChannel0, uv * 10.0).r;
float drops = smoothstep(0.7, 0.8, n);

// Using blue noise for dithering
float dither = texture(iChannel2, fragCoord.xy / 256.0).r;
```

### Procedural Patterns

```glsl
// Combine multiple noise channels
float pattern = texture(iChannel0, uv).r * 0.5 +
                texture(iChannel1, uv * 2.0).r * 0.3 +
                texture(iChannel2, uv * 4.0).r * 0.2;
```

### Material Textures

```glsl
// Use wood for organic backgrounds
vec3 background = texture(iChannel3, uv).rgb;

// Use abstract for colorful effects
vec3 colors = texture(iChannel4, uv * 0.5).rgb;
```

### Displacement Mapping

```glsl
// Use gray noise for height
float height = texture(iChannel1, uv).r;
vec2 displaced = uv + vec2(height) * 0.1;
vec3 color = texture(iChannel0, displaced).rgb;
```

## Shadertoy Compatibility

### What Works

✅ **Texture sampling**: `texture()`, `textureLod()`, `texelFetch()`  
✅ **Multiple channels**: iChannel0-4  
✅ **All coordinate types**: vec2, vec3, ivec2  
✅ **Mipmapping**: LOD parameter respected  
✅ **Wrapping**: Textures tile correctly

### What Doesn't Work (Yet)

❌ **Custom images**: Can't load specific images yet  
❌ **Video textures**: No video input support  
❌ **Cubemaps**: Only 2D textures  
❌ **Buffer passes**: Multi-pass rendering not supported  
❌ **Audio input**: No FFT or waveform data

### Fallback Behavior

If a shader tries to use more than 5 channels (iChannel5+), the preprocessor will still generate fallback code, but the texture will be empty (black).

## Troubleshooting

### Texture appears black

**Problem**: `texture(iChannel0, uv)` returns black

**Solutions**:
1. Check UV coordinates are in valid range
2. Verify the shader is in Shadertoy format (uses `mainImage`)
3. Check for shader compilation errors in logs

### Texture looks different from Shadertoy

**Problem**: Shader doesn't look like the original

**Cause**: Shadertoy often uses specific images as iChannel inputs

**Solution**: 
- Original Shadertoy shaders may rely on specific images
- Default procedural textures won't match exactly
- This is expected behavior for complex shaders
- Custom texture loading (coming soon) will help

### Performance issues

**Problem**: Shader runs slowly

**Solutions**:
1. Reduce texture sampling frequency
2. Use lower mipmap levels: `textureLod(iChannel0, uv, 2.0)`
3. Cache texture samples instead of repeated lookups
4. Consider shader optimization

### Compilation errors

**Problem**: "iChannel0 undeclared"

**Cause**: Shader preprocessing failed

**Solutions**:
1. Ensure shader has `mainImage` function
2. Check for syntax errors
3. Run with `-v` flag to see preprocessing logs

## Examples

### Basic Noise

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Simple noise lookup
    vec4 noise = texture(iChannel0, uv * 10.0);
    
    fragColor = noise;
}
```

### Animated Noise

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Scroll noise over time
    vec2 animatedUV = uv + vec2(iTime * 0.1, 0.0);
    vec4 noise = texture(iChannel0, animatedUV * 5.0);
    
    fragColor = noise;
}
```

### Multi-Channel Composition

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Combine different noise channels
    float r = texture(iChannel0, uv * 8.0).r;
    float g = texture(iChannel1, uv * 4.0).r;
    float b = texture(iChannel2, uv * 2.0).r;
    
    fragColor = vec4(r, g, b, 1.0);
}
```

### Displacement Effect

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Get displacement from gray noise
    float disp = texture(iChannel1, uv).r;
    
    // Apply displacement to UV
    vec2 displaced = uv + vec2(disp - 0.5) * 0.1;
    
    // Sample color with displaced coordinates
    vec3 color = texture(iChannel0, displaced * 10.0).rgb;
    
    fragColor = vec4(color, 1.0);
}
```

## Advanced Topics

### Texture Coordinates

- **Normalized [0,1]**: Use `uv = fragCoord / iResolution.xy`
- **Pixel coordinates**: Use `fragCoord` directly
- **Tiling**: Multiply UVs to create repeating patterns
- **Aspect ratio**: Account for with `iResolution`

### Mipmap Levels

```glsl
// Sharp (level 0)
vec4 sharp = textureLod(iChannel0, uv, 0.0);

// Blurry (higher levels)
vec4 blurry = textureLod(iChannel0, uv, 3.0);

// Auto-select based on derivatives
vec4 auto = texture(iChannel0, uv);
```

### Tileable Patterns

All default textures are tileable:

```glsl
// These will tile seamlessly
vec4 tile1 = texture(iChannel0, uv * 10.0);
vec4 tile2 = texture(iChannel1, uv * 5.0);
```

### Performance Tips

1. **Cache lookups**: Store texture samples in variables
2. **Reduce frequency**: Lower UV multipliers
3. **Use mipmaps**: Higher LOD = faster sampling
4. **Avoid dependent reads**: Minimize texture lookups in loops

## Future Enhancements

Planned features for iChannel system:

- 📁 **Custom image loading**: Use your own images
- 🎬 **Video textures**: Animated inputs
- 🎵 **Audio input**: FFT and waveform data  
- 🔄 **Buffer passes**: Multi-pass rendering
- 🎮 **Keyboard input**: Interactive textures
- 📊 **Texture resolution**: Per-channel size control

## API Reference

### Texture Access Functions (Auto-generated)

These functions are automatically available in all shaders:

```glsl
// Standard texture sampling
vec4 texture(sampler2D channel, vec2 uv);
vec4 texture(sampler2D channel, vec3 uvw);

// LOD sampling
vec4 textureLod(sampler2D channel, vec2 uv, float lod);
vec4 textureLod(sampler2D channel, vec3 uvw, float lod);

// Integer pixel fetch
vec4 texelFetch(sampler2D channel, ivec2 coord, int lod);
```

### Available Samplers

```glsl
uniform sampler2D iChannel0; // RGBA Noise
uniform sampler2D iChannel1; // Gray Noise
uniform sampler2D iChannel2; // Blue Noise
uniform sampler2D iChannel3; // Wood
uniform sampler2D iChannel4; // Abstract
```

## See Also

- [SHADERS.md](SHADERS.md) - General shader documentation
- [SHADERTOY.md](SHADERTOY.md) - Shadertoy compatibility guide
- [README.md](../README.md) - Main documentation