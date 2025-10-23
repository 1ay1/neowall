# Staticwall Example Shaders

This directory contains example GLSL fragment shaders for live animated wallpapers in Staticwall.

## Available Shaders

### plasma.glsl
Colorful plasma effect using multiple sine waves. Creates flowing, organic color patterns.

**Usage:**
```vibe
default {
  shader ~/path/to/staticwall/examples/shaders/plasma.glsl
}
```

### wave.glsl
Radial wave patterns emanating from the center with color gradients based on distance and angle.

**Usage:**
```vibe
default {
  shader ~/path/to/staticwall/examples/shaders/wave.glsl
}
```

### gradient.glsl
Smooth animated diagonal gradient that shifts colors and angle over time.

**Usage:**
```vibe
default {
  shader ~/path/to/staticwall/examples/shaders/gradient.glsl
}
```

### matrix.glsl
Matrix-style digital rain effect with falling green characters.

**Usage:**
```vibe
default {
  shader ~/path/to/staticwall/examples/shaders/matrix.glsl
}
```

## Installation

Copy shaders to your local config directory:

```bash
mkdir -p ~/.local/share/staticwall/shaders
cp *.glsl ~/.local/share/staticwall/shaders/
```

Then reference them in your config:

```vibe
default {
  shader ~/.local/share/staticwall/shaders/plasma.glsl
}
```

## Writing Your Own Shaders

All shaders are GLSL ES 1.0 fragment shaders. Here's the minimal template:

```glsl
#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    
    // Your shader code here
    vec3 color = vec3(uv.x, uv.y, 0.5);
    
    gl_FragColor = vec4(color, 1.0);
}
```

### Available Uniforms

- `uniform float time;` - Elapsed time in seconds (continuously incrementing)
- `uniform vec2 resolution;` - Screen resolution in pixels (width, height)

### Tips

1. **Normalize coordinates:** `vec2 uv = gl_FragCoord.xy / resolution.xy;` gives you 0-1 coordinates
2. **Center coordinates:** `vec2 center = uv - 0.5;` centers around (0,0)
3. **Animate with time:** Use `time` in sine/cosine functions for smooth animation
4. **Use distance:** `length(center)` for radial effects
5. **Use angles:** `atan(center.y, center.x)` for rotational patterns

### Converting Shadertoy Shaders

Many Shadertoy shaders can be easily converted:

**Shadertoy:**
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5, 1.0);
}
```

**Staticwall:**
```glsl
#version 100
precision mediump float;

uniform float time;       // iTime
uniform vec2 resolution;  // iResolution.xy

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    gl_FragColor = vec4(uv, 0.5, 1.0);
}
```

## Resources

- [Shadertoy](https://www.shadertoy.com/) - Browse thousands of shader examples
- [The Book of Shaders](https://thebookofshaders.com/) - Learn GLSL programming
- [GLSL Sandbox](https://glslsandbox.com/) - Simple shader playground

## Performance

These shaders are designed to run efficiently on most GPUs:
- All computation happens on the GPU
- Minimal CPU usage
- Suitable for laptop use (though battery impact may vary)

For best performance:
- Use `mediump` or `lowp` precision when possible
- Avoid expensive operations in tight loops
- Test on your hardware and adjust complexity as needed

## License

These example shaders are released into the public domain (CC0). Use them however you want!
