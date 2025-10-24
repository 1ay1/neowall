# Shadertoy Quick Reference for Staticwall

> **TL;DR:** Copy Shadertoy shaders directly. They just work. Most common shortcuts and uniforms are automatically provided.

---

## Quick Start

1. Go to [Shadertoy.com](https://www.shadertoy.com)
2. Find a shader you like
3. Copy the code
4. Save as `~/.config/staticwall/shaders/myshader.glsl`
5. Run: `staticwall reload`

Done! 🎉

---

## What's Supported

### ✅ Fully Working
- `iTime` - Animation time
- `iResolution` - Screen resolution
- `mainImage()` - Standard entry point
- Mathematical functions (sin, cos, smoothstep, etc.)
- All GLSL ES 2.0/3.0 features
- **100+ shorthand macros** (see below)

### ⚠️ Partial Support
- `iChannel0-3` - Textures (returns procedural noise)
- `iMouse` - Mouse input (fixed at 0,0,0,0)
- `iDate` - Date/time (static value)
- `iFrame` - Frame number (fixed at 0)

### ❌ Not Yet Supported
- `mainSound()` - Sound shaders
- `mainVR()` - VR shaders
- Buffer passes (Buffer A/B/C/D)
- Keyboard input
- Webcam/video input

---

## Essential Uniforms

```glsl
uniform vec3 iResolution;        // Screen size (width, height, aspect)
uniform float iTime;             // Time in seconds
uniform vec4 iMouse;             // Mouse (xy: current, zw: click)
uniform vec4 iDate;              // (year, month, day, seconds)
uniform sampler2D iChannel0;     // Texture channel 0
uniform sampler2D iChannel1;     // Texture channel 1
uniform sampler2D iChannel2;     // Texture channel 2
uniform sampler2D iChannel3;     // Texture channel 3
```

---

## Automatic Shorthand Macros

Staticwall automatically provides these shortcuts (commonly used on Shadertoy):

### Constants
```glsl
PI      // 3.14159265359
TAU     // 6.28318530718 (2π)
PHI     // 1.61803398875 (golden ratio)
E       // 2.71828182846
```

### Time & Resolution
```glsl
T       // iTime
t       // iTime
R       // iResolution
FC      // gl_FragCoord
M       // iMouse
```

### Common Functions
```glsl
S(a,b,x)        // smoothstep(a,b,x)
sat(x)          // clamp(x,0.,1.)
saturate(x)     // clamp(x,0.,1.)
n(x)            // normalize(x)
l(x)            // length(x)
d(a,b)          // dot(a,b)
x(a,b)          // cross(a,b)
a(x)            // abs(x)
f(x)            // floor(x)
c(x)            // ceil(x)
fr(x)           // fract(x)
p(x,y)          // pow(x,y)
sgn(x)          // sign(x)
mn(a,b)         // min(a,b)
mx(a,b)         // max(a,b)
dst(a,b)        // distance(a,b)
rfl(i,n)        // reflect(i,n)
rfr(i,n,e)      // refract(i,n,e)
```

### Rotation
```glsl
rot(a)          // mat2(cos(a),sin(a),-sin(a),cos(a))
r(v,a)          // Inline rotation: rotates vector v by angle a
```

### Type Shortcuts
```glsl
v2(x)           // vec2(x)
v3(x)           // vec3(x)
v4(x)           // vec4(x)
```

### Texture Shortcuts
```glsl
T0(uv)          // texture(iChannel0, uv)
T1(uv)          // texture(iChannel1, uv)
T2(uv)          // texture(iChannel2, uv)
T3(uv)          // texture(iChannel3, uv)
```

### Hash Constants (for noise)
```glsl
MOD3            // vec3(.1031,.1030,.0973)
MOD4            // vec4(.1031,.1030,.0973,.1099)
```

### Advanced
```glsl
smin(a,b,k)     // Smooth minimum
B(a,b,x,t)      // Box/band function
mnc(x)          // min(min(x.x,x.y),x.z) - minimum component
mxc(x)          // max(max(x.x,x.y),x.z) - maximum component
mul(a,b)        // (b*a) - HLSL-style multiplication
```

---

## Common Patterns

### Normalize Coordinates
```glsl
vec2 uv = fragCoord / iResolution.xy;  // 0 to 1
vec2 uv = fragCoord / R.xy;            // Using shortcut
```

### Center Coordinates
```glsl
vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
```

### Time-based Animation
```glsl
float pulse = 0.5 + 0.5 * sin(iTime);
float pulse = 0.5 + 0.5 * sin(T);     // Using shortcut
```

### Using Shortcuts
```glsl
// Verbose version
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = vec3(smoothstep(0.4, 0.6, length(uv - 0.5)));
    fragColor = vec4(col, 1.0);
}

// With shortcuts
void mainImage(out vec4 o, in vec2 U)
{
    vec2 uv = U / R.xy;
    vec3 col = v3(S(0.4, 0.6, l(uv - 0.5)));
    o = v4(col, 1.0);
}
```

---

## GLSL Quick Reference

### Types
```glsl
float, vec2, vec3, vec4              // Floats
int, ivec2, ivec3, ivec4             // Integers
uint, uvec2, uvec3, uvec4            // Unsigned (ES 3.0+)
bool, bvec2, bvec3, bvec4            // Booleans
mat2, mat3, mat4                     // Matrices
sampler2D, sampler3D, samplerCube    // Textures
```

### Operators
```glsl
+ - * / %           // Arithmetic
< > <= >= == !=     // Comparison
&& || !             // Logical
& | ^ ~ << >>       // Bitwise (ES 3.0+)
```

### Swizzling
```glsl
vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
v.x, v.y, v.z, v.w               // Access components
v.rgb, v.xyz, v.xy               // Multiple components
v.bgr, v.zyx                     // Reorder
v.xxxx                           // Replicate
```

### Common Functions
```glsl
sin, cos, tan, asin, acos, atan
exp, log, exp2, log2, pow, sqrt
abs, sign, floor, ceil, fract, mod
min, max, clamp, mix, step, smoothstep
length, distance, dot, cross, normalize
reflect, refract
dFdx, dFdy, fwidth (derivatives)
```

---

## GLSL Gotchas

### ❌ Common Mistakes

```glsl
// NO 'f' suffix!
float x = 1.0f;      // ❌ ILLEGAL - Use: 1.0

// NO saturate()!
float x = saturate(v);  // ❌ HLSL only - Use: clamp(v, 0.0, 1.0)

// Initialize variables!
float x;             // ❌ Undefined - Use: float x = 0.0;

// Protect sqrt/pow!
sqrt(-1.0);          // ❌ Undefined - Use: sqrt(max(0.0, x))
pow(-2.0, 3.0);      // ❌ Undefined - Use: pow(abs(x), 3.0)

// Don't divide by zero!
mod(x, 0.0);         // ❌ Undefined
```

### ✅ Best Practices

```glsl
// Always use decimal point for floats
float x = 1.0;       // ✅ Good

// Initialize all variables
float x = 0.0;       // ✅ Good
vec3 col = vec3(0.0);

// Use const for constants
const float PI = 3.14159265359;

// Add precision qualifiers (ES 2.0)
precision mediump float;
```

---

## Compatibility

### OpenGL ES 3.0 (Recommended)
- Full Shadertoy support
- Integer types, bitwise ops
- Modern GLSL syntax
- 80-90% of Shadertoy shaders work unchanged

### OpenGL ES 2.0 (Fallback)
- Basic Shadertoy support
- Automatic shader conversion
- Some limitations with integers, arrays
- 60-70% of Shadertoy shaders work

**Staticwall automatically detects and uses the best version available!**

---

## Troubleshooting

### Shader looks different from Shadertoy
- **Textures:** iChannel0-3 return procedural noise (not actual images)
- **Mouse:** Fixed at (0, 0, 0, 0) - comment out mouse code
- **Multipass:** Not supported yet - single-pass shaders only

### Shader won't compile
- Remove `1.0f` (use `1.0`)
- Replace `saturate()` with `clamp(x, 0.0, 1.0)`
- Initialize all variables
- Check for ES 2.0 vs ES 3.0 features

### Shader is slow
- Reduce loop iterations
- Lower precision (highp → mediump)
- Simplify distance functions
- Remove pow/sin/cos from tight loops

---

## Examples

### Simple Gradient
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5, 1.0);
}
```

### Animated Circle
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float d = length(uv) - 0.3;
    float c = smoothstep(0.01, 0.0, d);
    c *= 0.5 + 0.5 * sin(iTime * 2.0);
    fragColor = vec4(vec3(c), 1.0);
}
```

### Using Shortcuts
```glsl
void mainImage(out vec4 o, in vec2 U)
{
    vec2 uv = (U - 0.5 * R.xy) / R.y;
    float d = l(uv) - 0.3;
    float c = S(0.01, 0.0, d);
    c *= 0.5 + 0.5 * sin(T * 2.0);
    o = v4(v3(c), 1.0);
}
```

---

## Finding Compatible Shaders

### High Compatibility (90%+)
Look for shaders with:
- ✅ Pure math/procedural
- ✅ No textures or simple noise textures
- ✅ Single-pass rendering
- ✅ No mouse interaction

**Categories:** Fractals, plasma, patterns, simple raymarching

### Moderate Compatibility (50-80%)
- ⚠️ Uses iChannel for noise
- ⚠️ Light mouse usage (can be removed)
- ⚠️ Moderate complexity

### Low Compatibility (<50%)
- ❌ Requires specific textures
- ❌ Heavy mouse interaction
- ❌ Multipass rendering (Buffer A/B/C/D)
- ❌ Audio/video input

---

## Documentation

- **Full Reference:** [SHADERTOY_REFERENCE.md](SHADERTOY_REFERENCE.md)
- **Macro List:** [SHADERTOY_MACROS.md](SHADERTOY_MACROS.md)
- **General Guide:** [SHADERTOY_SUPPORT.md](SHADERTOY_SUPPORT.md)
- **Implementation:** [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)

---

## Resources

- **Official Shadertoy:** https://www.shadertoy.com
- **Browse Shaders:** https://www.shadertoy.com/browse
- **GLSL Reference:** https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
- **Shadertoy Examples:** `staticwall/examples/shaders/`

---

**Happy Shading!** 🎨✨

*All shortcuts are automatically injected - no configuration needed!*