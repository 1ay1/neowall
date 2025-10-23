# iChannel Configuration Guide

This guide explains how to configure iChannel textures for your Shadertoy-style shaders in Staticwall.

## Quick Start

Add a `channels` array to your shader configuration:

```vibe
[outputs.eDP-1]
shader = "examples/shaders/heartfelt.glsl"
channels = ["rgba_noise", "gray_noise", "blue_noise"]
```

## How It Works

- The `channels` array maps to iChannel samplers in your shader
- Index 0 → `iChannel0`, Index 1 → `iChannel1`, etc.
- You can specify up to any number of channels (most shaders use 0-4)
- If not specified, defaults are automatically assigned

## Default Procedural Textures

Staticwall provides 5 built-in procedural textures (256×256 with mipmaps):

| Name | iChannel | Description | Use Cases |
|------|----------|-------------|-----------|
| `rgba_noise` | 0 | Multi-octave RGBA noise | General-purpose noise, clouds, turbulence |
| `gray_noise` | 1 | Grayscale fBM | Heightmaps, displacement, single-channel effects |
| `blue_noise` | 2 | Blue noise pattern | Dithering, anti-aliasing, sampling |
| `wood` | 3 | Wood grain texture | Organic patterns, natural materials |
| `abstract` | 4 | Colorful Voronoi | Backgrounds, abstract art effects |

These are **generated at runtime** (no image files needed) and optimized for tiling/repeating.

## Configuration Examples

### Example 1: Use All Defaults

```vibe
[outputs.eDP-1]
shader = "my_shader.glsl"
# No channels specified - automatically gets all 5 defaults
```

Result:
- iChannel0 = rgba_noise
- iChannel1 = gray_noise
- iChannel2 = blue_noise
- iChannel3 = wood
- iChannel4 = abstract

### Example 2: Override Specific Channels

```vibe
[outputs.eDP-1]
shader = "my_shader.glsl"
channels = ["blue_noise"]
```

Result:
- iChannel0 = blue_noise
- iChannel1-4 = defaults (gray_noise, blue_noise, wood, abstract)

### Example 3: Custom Image Files

```vibe
[outputs.HDMI-A-1]
shader = "photo_shader.glsl"
channels = [
    "/home/user/Pictures/background.png",
    "/home/user/Pictures/overlay.jpg"
]
```

Result:
- iChannel0 = background.png (loaded from file)
- iChannel1 = overlay.jpg (loaded from file)
- iChannel2-4 = defaults

### Example 4: Mix Defaults and Custom

```vibe
[outputs.DP-1]
shader = "mixed_shader.glsl"
channels = [
    "rgba_noise",                        # Default procedural
    "/path/to/custom_texture.png",      # Custom image
    "wood",                              # Default procedural
    "_",                                 # Skip (no texture)
    "/path/to/another.jpg"              # Custom image
]
```

### Example 5: Skip Channels with Underscore

```vibe
[outputs.eDP-1]
shader = "minimal_shader.glsl"
channels = ["_", "gray_noise", "_", "_", "_"]
```

Result:
- iChannel0 = (empty/skipped)
- iChannel1 = gray_noise
- iChannel2-4 = (empty/skipped)

Use `"_"` to explicitly skip a channel (no texture will be bound).

## Using Channels in Your Shader

Access the textures in your GLSL code:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Sample iChannel0
    vec4 noise = texture(iChannel0, uv * 5.0);
    
    // Sample iChannel1 with LOD
    float detail = textureLod(iChannel1, uv, 2.0).r;
    
    // Sample iChannel2 with screen coordinates
    vec4 dither = texture(iChannel2, fragCoord.xy / 256.0);
    
    fragColor = vec4(noise.rgb * detail, 1.0);
}
```

## File Paths

### Relative Paths
Relative to the config file location:

```vibe
channels = ["textures/noise.png"]
```

### Absolute Paths
Full system paths:

```vibe
channels = ["/home/user/Pictures/texture.jpg"]
```

### Environment Variables
Expand using shell:

```bash
# In your shell startup:
export TEXTURE_DIR="$HOME/Pictures/textures"
```

Then in config:
```vibe
# Note: staticwall doesn't expand env vars directly
# Use absolute paths instead
channels = ["/home/user/Pictures/textures/noise.png"]
```

## Supported Image Formats

- **PNG** - Recommended (supports transparency)
- **JPEG** - Good for photos (no alpha channel)

Images are automatically:
- Converted to RGBA format
- Uploaded to GPU as textures
- Mipmapped for efficient sampling

## Performance Tips

1. **Use defaults when possible** - Procedural textures are tiny in memory
2. **Smaller images = faster loading** - 512×512 or 1024×1024 is usually enough
3. **Power-of-2 dimensions** - Better GPU performance (256, 512, 1024, 2048)
4. **Reuse textures** - Use the same texture across multiple channels if needed

## Troubleshooting

### Texture not loading

Check logs for error messages:
```bash
staticwall -f -v
```

Common issues:
- **File not found** - Check path (use absolute paths)
- **Format not supported** - Use PNG or JPEG
- **Permission denied** - Check file permissions

### Texture appears wrong

- **Upside down** - GL textures use bottom-left origin (flip UV: `uv.y = 1.0 - uv.y`)
- **Stretched** - Check aspect ratio in shader
- **Blurry** - Use `texelFetch()` for pixel-perfect sampling

### Performance issues

- Reduce texture resolution
- Use fewer channels
- Optimize shader code (reduce texture samples)

## Advanced Usage

### Dynamic Channel Count

Staticwall dynamically adjusts the number of iChannels based on your config:

```vibe
# Only declares iChannel0 and iChannel1 in shader
channels = ["rgba_noise", "gray_noise"]
```

This saves GPU resources for shaders that don't need all 5 channels.

### Wrapping and Filtering

All textures use:
- **Wrap mode**: `GL_REPEAT` (tileable)
- **Min filter**: `GL_LINEAR_MIPMAP_LINEAR` (trilinear)
- **Mag filter**: `GL_LINEAR`
- **Mipmaps**: Automatically generated

## Examples

See:
- [examples/configs/shadertoy_ichannels.vibe](../examples/configs/shadertoy_ichannels.vibe) - Comprehensive examples
- [examples/configs/simple_ichannels.vibe](../examples/configs/simple_ichannels.vibe) - Minimal example
- [examples/shaders/heartfelt.glsl](../examples/shaders/heartfelt.glsl) - Shader using iChannel0

## Further Reading

- [ICHANNELS.md](ICHANNELS.md) - Complete technical documentation
- [Shadertoy.com](https://www.shadertoy.com) - Shader examples and inspiration
- [CONFIG_GUIDE.md](CONFIG_GUIDE.md) - General configuration guide

---

**Tip**: Start with default textures and only add custom images when you need specific content. The defaults are designed to work well with most Shadertoy shaders!
