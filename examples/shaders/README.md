# Staticwall Shaders

Animated GPU-accelerated GLSL shaders for your desktop wallpaper. These shaders run continuously at ~60fps to create mesmerizing live backgrounds.

## Available Shaders

### 🌊 **wave.glsl** - Rippling Wave Patterns
Concentric waves ripple outward from the center with smooth color gradients. Blue, pink, and teal colors flow and mix based on wave position and angle.

**Best for:** Calm, meditative backgrounds  
**Performance:** Lightweight  
**Colors:** Blue → Pink → Teal

---

### 🎨 **plasma.glsl** - Psychedelic Plasma Effect
Multiple sine waves combine to create a flowing plasma effect with vibrant, constantly shifting colors. Red, green, and blue channels cycle at different speeds for a hypnotic result.

**Best for:** Energetic, colorful backgrounds  
**Performance:** Lightweight  
**Colors:** Full RGB spectrum, constantly cycling

---

### 🎭 **gradient.glsl** - Smooth Color Gradients
Animated diagonal gradient that rotates and shifts through smooth color transitions. Perfect for a subtle, elegant background.

**Best for:** Professional, minimalist setups  
**Performance:** Very lightweight  
**Colors:** Smooth pastel gradients

---

### 💚 **matrix.glsl** - The Matrix Digital Rain (ENHANCED)
**NEW!** Heavily upgraded Matrix-style falling characters with authentic effects:
- Falling "rain" columns with character-like patterns
- Bright white/cyan heads on each column
- Green glowing trails that fade realistically
- Random glitch flashes for authenticity
- CRT scanline effect for retro feel
- Ambient background glow from nearby columns
- Vignette darkening at screen edges
- Multiple columns falling at varied speeds
- Characters that mutate over time

**Best for:** Cyberpunk/hacker aesthetic, fans of The Matrix  
**Performance:** Medium (worth it!)  
**Colors:** Classic Matrix green with cyan/white accents

**Easter egg:** This is the most technically complex shader - it's basically a small graphics demo!

---

### 🌐 **cyber.glsl** - Cyberpunk Neon Grid (NEW!)
A futuristic cyberpunk grid with hexagonal patterns, neon colors, and digital glitches:
- Animated hexagonal and perpendicular grids
- Cyan, magenta, and yellow neon colors
- Moving glowing scan lines
- Digital noise and glitch blocks
- Chromatic aberration effects
- Pulsing vignette
- Vertical data streams
- CRT scanlines

**Best for:** Cyberpunk enthusiasts, synthwave lovers  
**Performance:** Medium-Heavy (lots of effects!)  
**Colors:** Neon cyan, magenta, yellow (classic cyberpunk palette)

---

### 💻 **terminal.glsl** - Hacker Terminal (NEW!)
A fully animated retro terminal with scrolling text and effects:
- Scrolling pseudo-ASCII characters moving upward
- Blinking cursor that follows "typing"
- Text fades as it ages (older = dimmer)
- Random "active" lines that glow brighter
- Authentic CRT phosphor glow around characters
- Screen curvature effect
- Horizontal scanline sweeps
- Screen flicker and noise grain
- Random glitch lines with color shifts

**Best for:** Developers, terminal enthusiasts, retro computing fans  
**Performance:** Medium-Heavy (complex character rendering)  
**Colors:** Classic terminal green with cyan accents

**Fun fact:** The characters are procedurally generated - not real text, but convincing pseudo-ASCII patterns!

---

## Usage

### In Your Config File

```vibe
default {
  shader matrix.glsl      # or wave.glsl, plasma.glsl, etc.
  mode fill
}
```

### Quick Test

```bash
# Matrix rain
echo "default { shader matrix.glsl }" > ~/.config/staticwall/config.vibe
staticwall reload

# Cyberpunk grid
echo "default { shader cyber.glsl }" > ~/.config/staticwall/config.vibe
staticwall reload

# Hacker terminal
echo "default { shader terminal.glsl }" > ~/.config/staticwall/config.vibe
staticwall reload
```

## Creating Your Own Shaders

Staticwall supports **two shader formats**:

### Format 1: Native Staticwall Format

```glsl
#version 100
precision mediump float;

uniform float time;        // Elapsed time in seconds
uniform vec2 resolution;   // Screen resolution (width, height)

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    
    // Your shader magic here!
    vec3 color = vec3(uv, sin(time));
    
    gl_FragColor = vec4(color, 1.0);
}
```

### Format 2: Shadertoy Format (NEW! 🎉)

**Copy shaders directly from Shadertoy.com** - they just work!

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Normalize pixel coordinates (from 0 to 1)
    vec2 uv = fragCoord/iResolution.xy;
    
    // Time varying pixel color
    vec3 color = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0,2,4));
    
    // Output to screen
    fragColor = vec4(color, 1.0);
}
```

Staticwall **automatically detects** which format you're using and wraps Shadertoy shaders with a compatibility layer. This means you can:
- Copy shaders directly from [Shadertoy.com](https://www.shadertoy.com)
- Use the same shader on both platforms
- Learn from thousands of existing Shadertoy examples!

### Available Uniforms

#### Native Format:
- `time` - Continuous time since shader loaded (in seconds, as float)
- `resolution` - Screen resolution as vec2(width, height)

#### Shadertoy Format:
- `iTime` - Current time in seconds
- `iResolution` - Viewport resolution as vec3(width, height, aspect_ratio)
- `fragCoord` - Pixel coordinates (passed as function parameter)
- `fragColor` - Output color (passed as function parameter)

**Note:** Other Shadertoy uniforms like `iMouse`, `iChannel0-3`, `iFrame`, etc. are not yet supported but are planned for future releases.

### Tips for Great Shaders

1. **Use time for animation** - Use `time` (native) or `iTime` (Shadertoy) and multiply by different values for varied speeds
2. **Normalize coordinates** - 
   - Native: `uv = gl_FragCoord.xy / resolution.xy`
   - Shadertoy: `uv = fragCoord / iResolution.xy`
3. **Aspect ratio** - Multiply `uv.x` by width/height ratio for square patterns
   - Native: `uv.x *= resolution.x / resolution.y`
   - Shadertoy: `uv.x *= iResolution.x / iResolution.y`
4. **Performance** - Avoid expensive operations in tight loops; modern GPUs are fast but not infinite!
5. **Smooth animations** - Use `sin()`, `cos()`, and `smoothstep()` for smooth motion
6. **Hash functions** - For randomness, use deterministic hash functions (see existing shaders)
7. **Test on Shadertoy first** - If using Shadertoy format, you can test and iterate quickly on [Shadertoy.com](https://www.shadertoy.com), then copy the final shader to staticwall

### Debugging

Run staticwall in foreground with verbose logging to see shader compilation errors:

```bash
staticwall -f -v
```

Shader errors will show the line number and error message.

## Performance Notes

- **Lightweight** (wave, plasma, gradient): ~1-2% CPU usage on most GPUs
- **Medium** (matrix): ~3-5% CPU usage
- **Heavy** (cyber, terminal): ~5-10% CPU usage on most GPUs

Modern GPUs handle all of these easily at 1080p-4K resolutions. The shaders use fragment shaders only and run at your display's refresh rate (typically 60fps).

## Installation Locations

Shaders are loaded from (in order):
1. `$XDG_CONFIG_HOME/staticwall/shaders/` (usually `~/.config/staticwall/shaders/`)
2. `~/.config/staticwall/shaders/`
3. `/usr/share/staticwall/shaders/`
4. `/usr/local/share/staticwall/shaders/`

On first run, example shaders are automatically copied to `~/.config/staticwall/shaders/`.

## Shadertoy Examples

Check out these example Shadertoy-format shaders included with staticwall:
- `shadertoy_test.glsl` - Simple animated circles to test compatibility
- `aurora_shadertoy.glsl` - Beautiful aurora borealis effect
- `fractal_julia.glsl` - Animated Julia set fractal with rainbow colors

You can find thousands more on [Shadertoy.com](https://www.shadertoy.com) - just copy and paste!

### Popular Shadertoy Shaders to Try:
- Search for "seascape" - beautiful ocean waves
- Search for "star nest" - amazing space tunnel
- Search for "elevated" - stunning 3D terrain
- Browse "Popular" or "Top" sections for inspiration

## Contributing

Want to create an awesome shader? Fork the repo and submit a PR! We'd love to see:
- More cyberpunk/sci-fi themes
- Nature-inspired animations (fire, water, aurora)
- Abstract mathematical visualizations
- Retro computer/game console effects
- Audio-reactive shaders (coming soon!)
- Your favorite Shadertoy conversions!

**Pro tip:** If you're converting a Shadertoy shader, you usually don't need to change anything - just copy it directly!

## License

All shaders are released under the same license as Staticwall (check the main repository).

---

**Pro tip:** Mix and match on different monitors! Each output can have its own shader:

```vibe
default {
  shader plasma.glsl
}

output {
  eDP-1 {
    shader terminal.glsl  # Terminal on laptop screen
  }
  
  HDMI-A-1 {
    shader matrix.glsl    # Matrix on external monitor
  }
}
```

Have fun and happy shading! 🎨✨