# Shadertoy Compatibility Reference

Staticwall provides comprehensive Shadertoy compatibility, automatically injecting uniforms, macros, and helperthand Macros

Staticwall automatically injects common Shadertoy shorthand macros and abbreviations into shaders during preprocessing. This allows you to use terse, compact shader code commonly found on Shadertoy without modification.

## Overview

Many Shadertoy authors use single-letter macros and abbreviations to keep their code within the 280-character tweet limit or simply for brevity. Staticwall recognizes this pattern and automatically defines these macros so your shaders work out of the box.

## Mathematical Constants

| Macro | Value | Description |
|-------|-------|-------------|
| `PI` | 3.14159265359 | Pi constant |
| `TAU` | 6.28318530718 | Tau (2π) constant |
| `PHI` | 1.61803398875 | Golden ratio |
| `E` | 2.71828182846 | Euler's number |

## Shadertoy Built-in Shortcuts

| Macro | Expands To | Description |
|-------|------------|-------------|
| `T` or `t` | `iTime` | Current time in seconds |
| `R` or `r` | `iResolution` | Viewport resolution (vec3) |
| `FC` or `fc` | `gl_FragCoord` | Fragment coordinate |

## Common Function Shortcuts

### Smoothstep and Clamping

| Macro | Expands To | Description |
|-------|------------|-------------|
| `S(a,b,x)` | `smoothstep(a,b,x)` | Smooth Hermite interpolation |
| `sat(x)` | `clamp(x,0.,1.)` | Saturate (clamp to 0-1) |
| `saturate(x)` | `clamp(x,0.,1.)` | Saturate (alternative) |
| `B(a,b,x,t)` | `smoothstep(a-t,a+t,x)*smoothstep(b+t,b-t,x)` | Box/band function |

### Vector Operations

| Macro | Expands To | Description |
|-------|------------|-------------|
| `n(x)` or `N(x)` | `normalize(x)` | Normalize vector |
| `l(x)` or `L(x)` | `length(x)` | Vector length |
| `d(a,b)` or `D(a,b)` | `dot(a,b)` | Dot product |
| `x(a,b)` or `X(a,b)` | `cross(a,b)` | Cross product |
| `dst(a,b)` | `distance(a,b)` | Distance between points |
| `rfl(i,n)` | `reflect(i,n)` | Reflect vector |
| `rfr(i,n,e)` | `refract(i,n,e)` | Refract vector |

### Math Functions

| Macro | Expands To | Description |
|-------|------------|-------------|
| `a(x)` or `A(x)` | `abs(x)` | Absolute value |
| `f(x)` or `F(x)` | `floor(x)` | Floor function |
| `p(x,y)` or `P(x,y)` | `pow(x,y)` | Power function |
| `sgn(x)` | `sign(x)` | Sign function |
| `mn(a,b)` | `min(a,b)` | Minimum |
| `mx(a,b)` | `max(a,b)` | Maximum |
| `stp(e,x)` | `step(e,x)` | Step function |
| `md(x,y)` | `mod(x,y)` | Modulo |
| `eq(a,b)` | `abs((a)-(b))<0.001` | Float equality check |

### Specialized Functions

| Macro | Expands To | Description |
|-------|------------|-------------|
| `smin(a,b,k)` | `-(log(exp(-k*a)+exp(-k*b))/k)` | Smooth minimum |

## Rotation Matrices

| Macro | Expands To | Description |
|-------|------------|-------------|
| `rot(a)` or `ROT(a)` | `mat2(cos(a),sin(a),-sin(a),cos(a))` | 2D rotation matrix |
| `r(v,a)` | `{ float _c=cos(a),_s=sin(a); v=vec2(_c*v.x-_s*v.y,_s*v.x+_c*v.y); }` | Inline 2D rotation (modifies v) |

**Note:** The `r(v,a)` macro modifies the vector `v` in place. It's designed for code-golf style shaders.

## Type Shortcuts

| Macro | Expands To | Description |
|-------|------------|-------------|
| `v2(x)` | `vec2(x)` | Create vec2 |
| `v3(x)` | `vec3(x)` | Create vec3 |
| `v4(x)` | `vec4(x)` | Create vec4 |
| `m2(...)` | `mat2(...)` | Create mat2 |
| `m3(...)` | `mat3(...)` | Create mat3 |

## Texture Shortcuts

| Macro | Expands To | Description |
|-------|------------|-------------|
| `T0(u)` | `texture(iChannel0,u)` | Sample iChannel0 |
| `T1(u)` | `texture(iChannel1,u)` | Sample iChannel1 |
| `T2(u)` | `texture(iChannel2,u)` | Sample iChannel2 |
| `T3(u)` | `texture(iChannel3,u)` | Sample iChannel3 |

## Hash Function Constants

These are commonly used with hash functions for pseudo-random number generation:

| Macro | Value | Description |
|-------|-------|-------------|
| `MOD3` | `vec3(.1031,.1030,.0973)` | 3D modulo constants |
| `MOD4` | `vec4(.1031,.1030,.0973,.1099)` | 4D modulo constants |

## Miscellaneous

| Macro | Expands To | Description |
|-------|------------|-------------|
| `mul(a,b)` | `(b*a)` | Matrix multiplication (HLSL-style) |

## Usage Examples

### Before (Verbose)
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = vec3(smoothstep(0.4, 0.6, length(uv - 0.5)));
    fragColor = vec4(col, 1.0);
}
```

### After (Compact, using shortcuts)
```glsl
void mainImage(out vec4 o, in vec2 U) {
    vec2 uv = U / R.xy;
    vec3 col = v3(S(0.4, 0.6, l(uv - 0.5)));
    o = v4(col, 1.0);
}
```

### Rotation Example
```glsl
// Using rot() - returns a rotation matrix
vec2 p = vec2(1.0, 0.0);
p *= rot(iTime);  // Rotate by iTime radians

// Using r() - inline rotation (modifies in place)
vec2 q = vec2(1.0, 0.0);
r(q, T);  // Rotate q by iTime radians
```

### Quadtree LOD Example (from Shadertoy)
```glsl
#define lodM  8 - int(H<200.)
#define T iTime

vec3 p, d, right, up;
float scal, fov, Znear, H;

float lod(vec2 P, float r) {
    vec3 Vs = vec3(-P/r, p.z);
    Vs *= mat3(right, up, d);
    vec2 S = abs(Vs.xy / Vs.z);
    return max(S.x, S.y) > 1. || Vs.z < Znear
        ? 0.
        : float(lodM) - log2(Vs.z);
}
```

## Automatic Injection

All these macros are automatically injected into your shader during preprocessing. You don't need to define them manually. The preprocessor is smart enough to:

1. **Avoid conflicts**: If your shader already defines a macro, Staticwall won't override it
2. **Conditional definitions**: Constants like `PI` are only defined if not already present
3. **Context-aware**: Macros are injected before your shader code, after any `#version` or `#extension` directives

## Performance Notes

These macros are compile-time substitutions and have **zero runtime overhead**. They're expanded by the GLSL compiler before execution, just as if you had written the expanded form directly.

## Compatibility

These macros are compatible with:
- OpenGL ES 2.0, 3.0, 3.1, and 3.2
- GLSL ES 100, 300 es, 310 es, and 320 es
- All Shadertoy shaders (with appropriate texture fallbacks)

## See Also

- [Shadertoy Compatibility Guide](SHADERTOY_COMPAT.md) - Full guide to running Shadertoy shaders
- [Shader Adaptation](SHADER_ADAPTATION.md) - How shaders are automatically adapted between GLSL versions
- [Implementation Status](IMPLEMENTATION_STATUS.md) - Current feature support matrix

---

**Note:** This feature is part of Staticwall's intelligent shader preprocessing system. For more information on how shaders are processed, see `src/shadertoy_compat.c`.