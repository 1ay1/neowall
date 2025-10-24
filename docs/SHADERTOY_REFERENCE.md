# Shadertoy Complete Reference for Staticwall

> **Complete reference based on the official [Shadertoy.com](https://www.shadertoy.com) specification**

This document provides a comprehensive guide to using Shadertoy shaders in Staticwall, including all uniforms, built-in functions, and compatibility information.

---

## Table of Contents

1. [Shadertoy Shader Types](#shadertoy-shader-types)
2. [Input Uniforms](#input-uniforms)
3. [Shader Entry Points](#shader-entry-points)
4. [GLSL ES Language Reference](#glsl-es-language-reference)
5. [Built-in Functions](#built-in-functions)
6. [Shorthand Macros (Staticwall Extension)](#shorthand-macros-staticwall-extension)
7. [Common Patterns](#common-patterns)
8. [Compatibility Matrix](#compatibility-matrix)
9. [Best Practices](#best-practices)

---

## Shadertoy Shader Types

Shadertoy supports three types of shaders. Staticwall currently focuses on **Image Shaders**.

### Image Shaders (Supported ✅)

Image shaders implement the `mainImage()` function to generate procedural images:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // fragCoord: pixel coordinates (0.5 to resolution-0.5)
    // fragColor: output color (rgba, alpha often ignored)
    
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

**Staticwall Support:** ✅ Full support with automatic wrapping

### Sound Shaders (Not Supported ❌)

Sound shaders implement `mainSound()` to generate audio:

```glsl
vec2 mainSound(float time)
{
    // time: current sample time in seconds
    // return: vec2(left_channel, right_channel)
    
    float wave = sin(2.0 * 3.14159 * 440.0 * time);
    return vec2(wave, wave);
}
```

**Staticwall Support:** ❌ Not implemented (planned for future)

### VR Shaders (Not Supported ❌)

VR shaders implement `mainVR()` for virtual reality rendering:

```glsl
void mainVR(out vec4 fragColor, in vec2 fragCoord, 
            in vec3 fragRayOri, in vec3 fragRayDir)
{
    // fragRayOri: ray origin in tracker space
    // fragRayDir: ray direction in tracker space
}
```

**Staticwall Support:** ❌ Not implemented

---

## Input Uniforms

### Official Shadertoy Uniforms

All Shadertoy shaders can access these uniforms:

| Uniform | Type | Description | Staticwall Support |
|---------|------|-------------|-------------------|
| `iResolution` | `vec3` | Viewport resolution (width, height, aspect ratio) | ✅ Full |
| `iTime` | `float` | Shader playback time in seconds | ✅ Full |
| `iTimeDelta` | `float` | Time to render last frame (seconds) | ⚠️ Fixed at 0.0166 (60 FPS) |
| `iFrame` | `int` | Current frame number | ⚠️ Fixed at 0 |
| `iFrameRate` | `float` | Frames per second | ⚠️ Fixed at 60.0 |
| `iChannelTime[4]` | `float[4]` | Playback time for each channel | ❌ Not implemented |
| `iChannelResolution[4]` | `vec3[4]` | Resolution of each input channel | ⚠️ Returns default 256x256 |
| `iMouse` | `vec4` | Mouse position (xy: current, zw: click) | ⚠️ Fixed at (0,0,0,0) |
| `iDate` | `vec4` | Current date/time (year, month, day, seconds) | ⚠️ Static value |
| `iSampleRate` | `float` | Audio sample rate (typically 44100) | ⚠️ Fixed at 44100.0 |
| `iChannel0` | `sampler2D` | Input texture channel 0 | ⚠️ Procedural noise fallback |
| `iChannel1` | `sampler2D` | Input texture channel 1 | ⚠️ Procedural noise fallback |
| `iChannel2` | `sampler2D` | Input texture channel 2 | ⚠️ Procedural noise fallback |
| `iChannel3` | `sampler2D` | Input texture channel 3 | ⚠️ Procedural noise fallback |

### Uniform Details

#### `iResolution` - Viewport Resolution

```glsl
uniform vec3 iResolution;
// .x = width in pixels
// .y = height in pixels  
// .z = pixel aspect ratio (usually 1.0)

// Common usage:
vec2 uv = fragCoord / iResolution.xy;  // Normalized coordinates (0-1)
```

#### `iTime` - Shader Time

```glsl
uniform float iTime;
// Time in seconds since shader started
// Increases continuously, never resets

// Common usage:
float pulse = 0.5 + 0.5 * sin(iTime * 2.0);
vec3 color = vec3(cos(iTime), sin(iTime), 0.5);
```

#### `iMouse` - Mouse Input

```glsl
uniform vec4 iMouse;
// .xy = current mouse position (if left button is down)
// .zw = mouse position at last click
// All in pixel coordinates

// Common usage:
vec2 mouse = iMouse.xy / iResolution.xy;  // Normalized mouse position
bool clicked = iMouse.z > 0.5;
```

**Staticwall Note:** Mouse input is not yet supported. Use fixed values or comment out mouse-dependent code.

#### `iDate` - Date and Time

```glsl
uniform vec4 iDate;
// .x = year (e.g., 2024)
// .y = month (0-11)
// .z = day (1-31)
// .w = time of day in seconds

// Common usage:
float dayTime = mod(iDate.w, 86400.0);  // Seconds since midnight
```

#### `iChannelN` - Input Textures

```glsl
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;

// Common usage:
vec4 tex = texture(iChannel0, uv);
vec4 noise = textureLod(iChannel1, uv, 0.0);
```

**Staticwall Note:** Texture channels currently return procedural noise. Actual texture loading is planned.

---

## Shader Entry Points

### Image Shader Entry Point

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
```

**Parameters:**
- `fragColor` (out): Output pixel color (RGBA, alpha usually set to 1.0)
- `fragCoord` (in): Pixel coordinates in range [0.5, resolution-0.5]

**Coordinate System:**
- Origin (0, 0) is at bottom-left
- fragCoord is in pixel units, not normalized
- Center of screen: `iResolution.xy * 0.5`

**Example:**
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Normalize coordinates to 0-1 range
    vec2 uv = fragCoord / iResolution.xy;
    
    // Center coordinates around origin
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    
    // Output color
    fragColor = vec4(uv, 0.5, 1.0);
}
```

---

## GLSL ES Language Reference

### Language Version

**Shadertoy:** WebGL 2.0 (GLSL ES 3.00)  
**Staticwall:** Supports GLSL ES 1.00 (ES 2.0) and 3.00 (ES 3.0) with automatic conversion

### Data Types

#### Scalar Types
```glsl
void      // No return value
bool      // Boolean (true/false)
int       // Signed integer
uint      // Unsigned integer (ES 3.0+)
float     // Floating point
```

#### Vector Types
```glsl
vec2, vec3, vec4      // Float vectors
bvec2, bvec3, bvec4   // Boolean vectors
ivec2, ivec3, ivec4   // Integer vectors
uvec2, uvec3, uvec4   // Unsigned integer vectors (ES 3.0+)
```

#### Matrix Types
```glsl
mat2, mat3, mat4      // Square matrices
mat2x2, mat2x3, mat2x4
mat3x2, mat3x3, mat3x4
mat4x2, mat4x3, mat4x4
```

#### Sampler Types
```glsl
sampler2D       // 2D texture
sampler3D       // 3D texture (ES 3.0+)
samplerCube     // Cube map texture
```

### Operators

#### Arithmetic
```glsl
+ - * / %       // Addition, subtraction, multiplication, division, modulo
++ --           // Increment, decrement
```

#### Logical/Relational
```glsl
< > <= >= == != // Comparison
&& || !         // Logical AND, OR, NOT
```

#### Bitwise (ES 3.0+)
```glsl
& | ^ ~ << >>   // AND, OR, XOR, NOT, left shift, right shift
```

### Literals

```glsl
float a = 1.0;      // Float (must have decimal point)
int b = 1;          // Integer
uint c = 1U;        // Unsigned integer (ES 3.0+)
int d = 0x1A;       // Hexadecimal
```

⚠️ **Important:** The `f` suffix (`1.0f`) is **illegal** in GLSL. Use `1.0` instead.

### Vector Components

Vectors support swizzling with multiple naming schemes:

```glsl
vec4 v = vec4(1.0, 2.0, 3.0, 4.0);

// Positional (x, y, z, w)
v.x, v.y, v.z, v.w
v.xyz, v.xy, v.zw

// Color (r, g, b, a)
v.r, v.g, v.b, v.a
v.rgb, v.rg, v.ba

// Texture (s, t, p, q)
v.s, v.t, v.p, v.q
v.st, v.sp

// Swizzling and reordering
v.zyxw    // Reverse order
v.xxxx    // Replicate x
v.bgr     // Reorder as blue, green, red
```

### Control Flow

```glsl
// If-else
if (condition) {
    // code
} else if (other_condition) {
    // code
} else {
    // code
}

// For loop
for (int i = 0; i < 10; i++) {
    // code
}

// While loop
while (condition) {
    // code
}

// Switch (ES 3.0+)
switch (value) {
    case 0:
        // code
        break;
    case 1:
        // code
        break;
    default:
        // code
}

// Control statements
break;       // Exit loop
continue;    // Next iteration
return;      // Exit function
```

### Functions

```glsl
// Function declaration
float myFunction(float x, vec2 y) {
    return x + y.x;
}

// Parameter qualifiers
void func(in float x,      // Input (default, can be omitted)
          out float y,     // Output
          inout float z)   // Input and output
{
    y = x * 2.0;
    z *= 2.0;
}
```

### Structs

```glsl
struct Material {
    vec3 color;
    float roughness;
    float metallic;
};

Material mat = Material(vec3(1.0, 0.0, 0.0), 0.5, 0.1);
vec3 col = mat.color;
```

### Arrays

```glsl
// Declaration
float arr[5];
float init[3] = float[](1.0, 2.0, 3.0);

// Access
float x = arr[0];
arr[2] = 5.0;

// Multi-dimensional (ES 3.0+)
float matrix[4][4];
```

⚠️ **ES 2.0 Limitation:** Array indices must be constant expressions or loop counters in ES 2.0.

### Preprocessor

```glsl
#define NAME value          // Define macro
#undef NAME                 // Undefine macro
#if expression             // Conditional compilation
#ifdef NAME                // If defined
#ifndef NAME               // If not defined
#else                      // Else branch
#elif expression           // Else if
#endif                     // End conditional
#error message             // Compiler error
#pragma directive          // Implementation-specific directive
#line number               // Set line number
```

---

## Built-in Functions

### Trigonometric Functions

| Function | Description |
|----------|-------------|
| `radians(degrees)` | Convert degrees to radians |
| `degrees(radians)` | Convert radians to degrees |
| `sin(angle)` | Sine |
| `cos(angle)` | Cosine |
| `tan(angle)` | Tangent |
| `asin(x)` | Arc sine |
| `acos(x)` | Arc cosine |
| `atan(y, x)` | Arc tangent (two arguments) |
| `atan(y_over_x)` | Arc tangent (one argument) |
| `sinh(x)` | Hyperbolic sine |
| `cosh(x)` | Hyperbolic cosine |
| `tanh(x)` | Hyperbolic tangent |
| `asinh(x)` | Inverse hyperbolic sine |
| `acosh(x)` | Inverse hyperbolic cosine |
| `atanh(x)` | Inverse hyperbolic tangent |

### Exponential Functions

| Function | Description |
|----------|-------------|
| `pow(x, y)` | x raised to the power y |
| `exp(x)` | Natural exponentiation (e^x) |
| `log(x)` | Natural logarithm |
| `exp2(x)` | 2 raised to the power x |
| `log2(x)` | Base 2 logarithm |
| `sqrt(x)` | Square root |
| `inversesqrt(x)` | Inverse square root (1/√x) |

⚠️ **Warning:** Don't feed `sqrt()` and `pow()` with negative numbers. Use `abs()` or `max(0.0, x)`.

### Common Functions

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `sign(x)` | Sign (-1, 0, or 1) |
| `floor(x)` | Round down to nearest integer |
| `ceil(x)` | Round up to nearest integer |
| `trunc(x)` | Truncate to integer |
| `fract(x)` | Fractional part (x - floor(x)) |
| `mod(x, y)` | Modulo operation |
| `modf(x, i)` | Split into integer and fractional parts |
| `min(x, y)` | Minimum value |
| `max(x, y)` | Maximum value |
| `clamp(x, minVal, maxVal)` | Clamp x between minVal and maxVal |
| `mix(x, y, a)` | Linear interpolation: x*(1-a) + y*a |
| `step(edge, x)` | 0 if x < edge, else 1 |
| `smoothstep(a, b, x)` | Smooth Hermite interpolation |

⚠️ **Warning:** Don't use `mod(x, 0.0)` - it's undefined on some platforms.

### Geometric Functions

| Function | Description |
|----------|-------------|
| `length(x)` | Length of vector |
| `distance(p0, p1)` | Distance between two points |
| `dot(x, y)` | Dot product |
| `cross(x, y)` | Cross product (vec3 only) |
| `normalize(x)` | Normalize vector to length 1 |
| `faceforward(N, I, Nref)` | Orient vector to face forward |
| `reflect(I, N)` | Reflect incident vector |
| `refract(I, N, eta)` | Refract incident vector |

### Matrix Functions

| Function | Description |
|----------|-------------|
| `matrixCompMult(x, y)` | Component-wise matrix multiplication |
| `outerProduct(c, r)` | Outer product of two vectors |
| `transpose(m)` | Transpose matrix |
| `determinant(m)` | Matrix determinant |
| `inverse(m)` | Matrix inverse |

### Texture Functions

| Function | Description |
|----------|-------------|
| `texture(sampler, coord)` | Sample texture |
| `texture(sampler, coord, bias)` | Sample with LOD bias |
| `textureLod(sampler, coord, lod)` | Sample at specific LOD |
| `textureLodOffset(sampler, coord, lod, offset)` | Sample at LOD with offset |
| `textureGrad(sampler, coord, dPdx, dPdy)` | Sample with explicit gradients |
| `textureProj(sampler, coord)` | Sample with projection |
| `texelFetch(sampler, coord, lod)` | Fetch single texel |
| `textureSize(sampler, lod)` | Get texture dimensions |

### Derivative Functions

| Function | Description |
|----------|-------------|
| `dFdx(p)` | Derivative in x direction |
| `dFdy(p)` | Derivative in y direction |
| `fwidth(p)` | Sum of absolute derivatives: abs(dFdx) + abs(dFdy) |

⚠️ **ES 2.0:** Requires `#extension GL_OES_standard_derivatives : enable`

### Bit Manipulation (ES 3.0+)

| Function | Description |
|----------|-------------|
| `floatBitsToInt(x)` | Reinterpret float as int |
| `floatBitsToUint(x)` | Reinterpret float as uint |
| `intBitsToFloat(x)` | Reinterpret int as float |
| `uintBitsToFloat(x)` | Reinterpret uint as float |

### Packing Functions (ES 3.0+)

| Function | Description |
|----------|-------------|
| `packSnorm2x16(v)` | Pack normalized vec2 into uint |
| `packUnorm2x16(v)` | Pack unsigned normalized vec2 into uint |
| `unpackSnorm2x16(p)` | Unpack to normalized vec2 |
| `unpackUnorm2x16(p)` | Unpack to unsigned normalized vec2 |

### Vector Relational Functions

| Function | Description |
|----------|-------------|
| `lessThan(x, y)` | Component-wise less than |
| `lessThanEqual(x, y)` | Component-wise less than or equal |
| `greaterThan(x, y)` | Component-wise greater than |
| `greaterThanEqual(x, y)` | Component-wise greater than or equal |
| `equal(x, y)` | Component-wise equal |
| `notEqual(x, y)` | Component-wise not equal |
| `any(x)` | True if any component is true |
| `all(x)` | True if all components are true |
| `not(x)` | Component-wise logical NOT |

### Type Testing (ES 3.0+)

| Function | Description |
|----------|-------------|
| `isnan(x)` | True if x is NaN |
| `isinf(x)` | True if x is infinity |

---

## Shorthand Macros (Staticwall Extension)

Staticwall automatically injects common shorthand macros used in Shadertoy shaders. These are **not** part of the official Shadertoy specification but are commonly used by shader artists.

### Mathematical Constants

```glsl
#define PI 3.14159265359
#define TAU 6.28318530718    // 2 * PI
#define PHI 1.61803398875    // Golden ratio
#define E 2.71828182846      // Euler's number
```

### Shadertoy Input Shortcuts

```glsl
#define T iTime
#define t iTime
#define R iResolution
#define FC gl_FragCoord
#define fc gl_FragCoord
#define M iMouse
#define m iMouse
```

### Function Shortcuts

```glsl
// Smoothstep and clamping
#define S(a,b,x) smoothstep(a,b,x)
#define sat(x) clamp(x,0.,1.)
#define saturate(x) clamp(x,0.,1.)

// Vector operations
#define n(x) normalize(x)
#define N(x) normalize(x)
#define l(x) length(x)
#define L(x) length(x)
#define d(a,b) dot(a,b)
#define D(a,b) dot(a,b)
#define x(a,b) cross(a,b)
#define X(a,b) cross(a,b)

// Math functions
#define a(x) abs(x)
#define A(x) abs(x)
#define f(x) floor(x)
#define F(x) floor(x)
#define c(x) ceil(x)
#define C(x) ceil(x)
#define fr(x) fract(x)
#define p(x,y) pow(x,y)
#define P(x,y) pow(x,y)
#define sgn(x) sign(x)
#define mn(a,b) min(a,b)
#define mx(a,b) max(a,b)
#define stp(e,x) step(e,x)
#define md(x,y) mod(x,y)

// Other
#define dst(a,b) distance(a,b)
#define rfl(i,n) reflect(i,n)
#define rfr(i,n,e) refract(i,n,e)
#define eq(a,b) (abs((a)-(b))<0.001)
```

### Rotation Macros

```glsl
// 2D rotation matrix
#define rot(a) mat2(cos(a),sin(a),-sin(a),cos(a))
#define ROT(a) mat2(cos(a),sin(a),-sin(a),cos(a))

// Inline rotation (modifies vector in place)
#define r(v,a) { float _c=cos(a),_s=sin(a); v=vec2(_c*v.x-_s*v.y,_s*v.x+_c*v.y); }
```

### Type Shortcuts

```glsl
#define v2(x) vec2(x)
#define v3(x) vec3(x)
#define v4(x) vec4(x)
#define m2(a,b,c,d) mat2(a,b,c,d)
#define m3(...) mat3(...)
```

### Texture Shortcuts

```glsl
#define T0(u) texture(iChannel0,u)
#define T1(u) texture(iChannel1,u)
#define T2(u) texture(iChannel2,u)
#define T3(u) texture(iChannel3,u)
```

### Hash Constants

```glsl
#define MOD3 vec3(.1031,.1030,.0973)
#define MOD4 vec4(.1031,.1030,.0973,.1099)
```

### Advanced Functions

```glsl
// Smooth minimum
#define smin(a,b,k) (-(log(exp(-k*a)+exp(-k*b))/k))

// Min/max component of vector
#define mnc(x) min(min((x).x,(x).y),(x).z)
#define mxc(x) max(max((x).x,(x).y),(x).z)

// HLSL-style matrix multiplication
#define mul(a,b) (b*a)

// Box/band function
#define B(a,b,x,t) (smoothstep(a-t,a+t,x)*smoothstep(b+t,b-t,x))
```

---

## Common Patterns

### Normalizing Coordinates

```glsl
// Standard normalization (0 to 1)
vec2 uv = fragCoord / iResolution.xy;

// Centered coordinates (-0.5 to 0.5)
vec2 uv = fragCoord / iResolution.xy - 0.5;

// Aspect-corrected centered coordinates
vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
```

### Time-based Animation

```glsl
// Pulsing effect
float pulse = 0.5 + 0.5 * sin(iTime * 2.0);

// Rotation over time
vec2 p = fragCoord - iResolution.xy * 0.5;
p *= rot(iTime);

// Oscillation
float wave = sin(iTime * 3.0 + uv.x * 10.0);
```

### Distance Fields

```glsl
// Circle distance field
float circle(vec2 p, float r) {
    return length(p) - r;
}

// Box distance field
float box(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
```

### Raymarching Template

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    
    // Camera
    vec3 ro = vec3(0, 0, -3);  // Ray origin
    vec3 rd = normalize(vec3(uv, 1));  // Ray direction
    
    float t = 0.0;  // Distance traveled
    for (int i = 0; i < 100; i++) {
        vec3 p = ro + rd * t;
        float d = sceneSDF(p);  // Distance to scene
        if (d < 0.001) break;
        t += d;
    }
    
    vec3 col = vec3(t * 0.1);
    fragColor = vec4(col, 1.0);
}
```

### Hash Functions

```glsl
// 1D hash
float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

// 2D hash
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Using MOD3 constant
vec3 hash33(vec3 p3) {
    p3 = fract(p3 * MOD3);
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}
```

---

## Compatibility Matrix

### Staticwall Support Status

| Feature | ES 2.0 | ES 3.0 | Support Status |
|---------|--------|--------|----------------|
| `mainImage()` | ✅ | ✅ | Full |
| `iTime` | ✅ | ✅ | Full |
| `iResolution` | ✅ | ✅ | Full |
| `iMouse` | ✅ | ✅ | Partial (fixed at 0,0,0,0) |
| `iDate` | ✅ | ✅ | Partial (static value) |
| `iFrame` | ✅ | ✅ | Partial (fixed at 0) |
| `iTimeDelta` | ✅ | ✅ | Partial (fixed at 16.67ms) |
| `iFrameRate` | ✅ | ✅ | Partial (fixed at 60) |
| `iChannel0-3` | ✅ | ✅ | Partial (noise fallback) |
| `iChannelTime` | ❌ | ❌ | Not implemented |
| `iChannelResolution` | ⚠️ | ⚠️ | Returns 256x256 |
| `iSampleRate` | ⚠️ | ⚠️ | Fixed at 44100 |
| Integer types | ⚠️ | ✅ | Limited in ES 2.0 |
| `texture()` | ❌ | ✅ | Auto-converted to `texture2D()` |
| Derivatives | ⚠️ | ✅ | Requires extension in ES 2.0 |
| Bit operations | ❌ | ✅ | ES 3.0 only |
| Array indexing | ⚠️ | ✅ | Constant only in ES 2.0 |

### Shadertoy Feature Support

| Feature | Staticwall Support | Notes |
|---------|-------------------|-------|
| Single-pass image shaders | ✅ Full | Primary use case |
| Multipass (Buffer A/B/C/D) | ❌ Not yet | Planned |
| Cubemap textures | ❌ Not yet | Planned |
| 3D textures | ❌ Not yet | Planned |
| Sound shaders | ❌ Not yet | Planned |
| VR shaders | ❌ Not yet | Not planned |
| Keyboard input | ❌ Not yet | Possible future feature |
| Video/webcam input | ❌ Not yet | Possible future feature |
| Audio input | ❌ Not yet | Planned |

---

## Best Practices

### DO ✅

```glsl
// Initialize variables
float x = 0.0;
vec3 color = vec3(0.0);

// Use decimal point for floats
float a = 1.0;

// Protect sqrt and pow from negative values
float safe_sqrt = sqrt(max(0.0, x));
float safe_pow = pow(abs(x), 2.0);

// Use const for constants
const float PI = 3.14159265359;

// Normalize coordinates properly
vec2 uv = fragCoord / iResolution.xy;
```

### DON'T ❌

```glsl
// Don't use 'f' suffix
float a = 1.0f;  // ❌ ILLEGAL in GLSL

// Don't use saturate() (it's HLSL, not GLSL)
float x = saturate(value);  // ❌ Use clamp(value, 0.0, 1.0)

// Don't pass negative values to sqrt/pow
float bad = sqrt(-1.0);  // ❌ Undefined behavior

// Don't divide by zero in mod
float bad = mod(x, 0.0);  // ❌ Undefined

// Don't assume variables are initialized
float x;  // ❌ Could be any value
vec3 col;  // ❌ Initialize to vec3(0.0)

// Don't name functions same as variables
float myValue = 1.0;
float myValue() { return 2.0; }  // ❌ Name conflict
```

### Performance Tips

```glsl
// Use built-in constants when possible
const float TAU = 6.28318530718;

// Precompute values outside loops
float scale = 1.0 / iResolution.y;
for (int i = 0; i < 100; i++) {
    vec2 p = fragCoord * scale;  // scale is constant
}

// Use smoothstep instead of if/else for smooth transitions
float edge = smoothstep(0.4, 0.6, x);
// Instead of:
// float edge = (x > 0.5) ? 1.0 : 0.0;

// Minimize texture lookups
vec4 tex = texture(iChannel0, uv);  // Fetch once
float r = tex.r;  // Use components
float g = tex.g;
// Instead of:
// float r = texture(iChannel0, uv).r;  // Fetches twice
// float g = texture(iChannel0, uv).g;

// Use integer loops with constant bounds
for (int i = 0; i < 64; i++) {  // Good
    // ...
}
// Not:
for (int i = 0; i < int(iResolution.x); i++) {  // May unroll badly
```

### Precision Qualifiers

```glsl
// ES 2.0 requires precision qualifiers
precision highp float;   // High precision (may be slower)
precision mediump float; // Medium precision (good balance)
precision lowp float;    // Low precision (fastest)

// Per-variable precision
highp vec3 position;
mediump vec2 texCoord;
lowp vec4 color;
```

### Code Organization

```glsl
// Group related code
// 1. Constants and macros at top
const float PI = 3.14159265359;
#define STEPS 64

// 2. Helper functions next
float hash(vec2 p) { /* ... */ }
float noise(vec2 p) { /* ... */ }

// 3. Scene definition
float sceneSDF(vec3 p) { /* ... */ }

// 4. Main shader last
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Implementation
}
```

---

## Common Issues and Solutions

### Issue: Shader doesn't compile

**Symptoms:** Error messages about undefined functions or syntax errors

**Solutions:**
1. Check GLSL version compatibility
2. Remove `f` suffix from floats: `1.0f` → `1.0`
3. Replace `saturate()` with `clamp(x, 0.0, 1.0)`
4. Ensure all variables are initialized
5. Add ES 2.0 precision qualifiers if needed

### Issue: Shader looks different from Shadertoy

**Symptoms:** Colors, patterns, or textures don't match

**Solutions:**
1. Texture channels use procedural noise (not actual textures)
2. Mouse input is fixed at (0,0,0,0)
3. iFrame, iDate might have different values
4. Check for multipass rendering (not supported yet)

### Issue: Shader is slow

**Symptoms:** Low FPS, stuttering, high GPU usage

**Solutions:**
1. Reduce iteration counts in loops
2. Simplify distance functions
3. Lower precision: `highp` → `mediump`
4. Remove expensive operations (pow, sin, cos) from inner loops
5. Reduce shader resolution in config

### Issue: Texture sampling doesn't work

**Symptoms:** Black screen or noise patterns

**Solutions:**
1. iChannel textures return procedural noise by default
2. Check texture coordinates are in 0-1 range
3. Ensure sampler is declared as uniform
4. Wait for texture loading support in future versions

---

## Additional Resources

- **Official Shadertoy:** https://www.shadertoy.com
- **GLSL ES 3.00 Specification:** https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
- **Staticwall Shader Examples:** `examples/shaders/`
- **Staticwall Shorthand Macros:** [SHADERTOY_MACROS.md](SHADERTOY_MACROS.md)
- **Implementation Status:** [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)

---

## License and Attribution

When using shaders from Shadertoy:
- Always credit the original author
- Respect the shader's license (usually CC BY-NC-SA 3.0)
- Shadertoy shaders are typically "Public + API" licensed
- Commercial use may require permission from the author

Staticwall's Shadertoy compatibility layer is released under the MIT license.

---

**Last Updated:** December 2024  
**Staticwall Version:** 0.2.0+  
**Based on:** Shadertoy.com official documentation