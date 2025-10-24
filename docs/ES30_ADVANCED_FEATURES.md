# OpenGL ES 3.0 Advanced Features in Staticwall

This document describes the advanced OpenGL ES 3.0 features supported in Staticwall, their implementation status, and usage examples.

---

## Overview

OpenGL ES 3.0 provides significant enhancements over ES 2.0, enabling advanced rendering techniques commonly used in modern graphics applications and Shadertoy shaders.

---

## Feature Status Matrix

| Feature | Status | Priority | Notes |
|---------|--------|----------|-------|
| **Multiple Render Targets (MRT)** | ⚠️ Planned | High | Essential for multipass Shadertoy shaders |
| **Uniform Buffer Objects (UBO)** | ⚠️ Planned | Medium | Performance optimization |
| **Transform Feedback** | ❌ Not Planned | Low | Not needed for fragment shaders |
| **Instanced Rendering** | ❌ Not Planned | Low | Not applicable to fullscreen quads |
| **3D Textures** | ⚠️ Planned | Medium | Some Shadertoy shaders use sampler3D |
| **Texture Arrays** | ⚠️ Planned | Low | Advanced use cases |
| **Integer Textures** | ✅ Supported | N/A | Part of ES 3.0 core |
| **sRGB Framebuffers** | ⚠️ Planned | Low | Color accuracy |
| **Depth Textures** | ❌ Not Planned | Low | Not needed for 2D wallpapers |
| **Occlusion Queries** | ❌ Not Planned | Low | Not applicable |
| **Vertex Array Objects (VAO)** | ✅ Supported | N/A | Already implemented |
| **GLSL ES 3.00** | ✅ Supported | N/A | Full support with auto-conversion |
| **Non-power-of-2 Textures** | ✅ Supported | N/A | Part of ES 3.0 core |
| **Floating Point Textures** | ✅ Supported | N/A | Part of ES 3.0 core |
| **Half Float Textures** | ✅ Supported | N/A | Part of ES 3.0 core |
| **Packed Pixel Formats** | ✅ Supported | N/A | Part of ES 3.0 core |

---

## 1. Multiple Render Targets (MRT)

### Description
Multiple Render Targets allow rendering to multiple textures simultaneously in a single draw call.

### Use Case in Shadertoy
Shadertoy's multipass rendering (Buffer A, B, C, D) requires MRT support to render intermediate results that can be sampled in subsequent passes.

### Implementation Status
⚠️ **Planned for v0.3.0**

### API Design

```c
// Multiple framebuffer outputs
typedef struct {
    GLuint fbo;
    GLuint textures[MAX_MRT_TARGETS];
    int num_targets;
    int width;
    int height;
} mrt_framebuffer_t;

// Create MRT framebuffer
mrt_framebuffer_t *mrt_create(int width, int height, int num_targets);

// Bind for rendering
void mrt_bind(mrt_framebuffer_t *mrt);

// Bind textures for reading
void mrt_bind_textures(mrt_framebuffer_t *mrt, int start_unit);

// Cleanup
void mrt_destroy(mrt_framebuffer_t *mrt);
```

### GLSL Usage

```glsl
#version 300 es
precision highp float;

// Multiple output targets
layout(location = 0) out vec4 fragColor0;  // Buffer A
layout(location = 1) out vec4 fragColor1;  // Buffer B
layout(location = 2) out vec4 fragColor2;  // Buffer C
layout(location = 3) out vec4 fragColor3;  // Buffer D

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Compute values for each buffer
    fragColor0 = vec4(1.0, 0.0, 0.0, 1.0);  // Buffer A output
    fragColor1 = vec4(0.0, 1.0, 0.0, 1.0);  // Buffer B output
    fragColor2 = vec4(0.0, 0.0, 1.0, 1.0);  // Buffer C output
    fragColor3 = vec4(1.0, 1.0, 0.0, 1.0);  // Buffer D output
}
```

### Configuration Example

```vibe
default {
    shader multipass_shader.glsl
    
    # Define render passes
    passes {
        buffer_a {
            output buffer_a
            inputs [buffer_b, buffer_c]
        }
        buffer_b {
            output buffer_b
            inputs [buffer_a]
        }
        image {
            output screen
            inputs [buffer_a, buffer_b]
        }
    }
}
```

---

## 2. Uniform Buffer Objects (UBO)

### Description
UBOs provide an efficient way to share uniform data between shaders and update multiple uniforms with a single API call.

### Use Case in Shadertoy
Group common uniforms (iTime, iResolution, iMouse, etc.) into a single buffer for efficient updates.

### Implementation Status
⚠️ **Planned for v0.3.0**

### API Design

```c
// Standard Shadertoy uniform block
typedef struct {
    vec3 iResolution;      // Viewport resolution
    float iTime;           // Shader playback time
    float iTimeDelta;      // Frame time
    int iFrame;            // Frame number
    vec4 iMouse;           // Mouse input
    vec4 iDate;            // Current date
    float iSampleRate;     // Audio sample rate
    float _padding[3];     // Align to 16 bytes
} shadertoy_uniforms_t;

// Create UBO
GLuint ubo_create_shadertoy_uniforms(void);

// Update UBO
void ubo_update_shadertoy_uniforms(GLuint ubo, const shadertoy_uniforms_t *uniforms);

// Bind UBO to shader
void ubo_bind_to_program(GLuint ubo, GLuint program, const char *block_name);
```

### GLSL Usage

```glsl
#version 300 es
precision highp float;

// Uniform block (std140 layout)
layout(std140) uniform ShadertoyInputs {
    vec3 iResolution;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec4 iMouse;
    vec4 iDate;
    float iSampleRate;
};

out vec4 fragColor;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Use uniforms from UBO
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}

void main() {
    mainImage(fragColor, gl_FragCoord.xy);
}
```

### Benefits

1. **Performance**: Single update for all uniforms (one glBufferSubData call)
2. **Sharing**: Same UBO can be bound to multiple shaders
3. **Size**: Can store up to 16KB of uniform data
4. **Cache-friendly**: GPU can cache UBO data

---

## 3. 3D Textures

### Description
3D textures are volumetric textures that can be sampled in three dimensions.

### Use Case in Shadertoy
Volumetric effects, 3D noise, raymarching through volumes, cloud rendering.

### Implementation Status
⚠️ **Planned for v0.3.0**

### API Design

```c
// Create 3D texture
GLuint texture_create_3d(int width, int height, int depth, 
                         GLenum internal_format,
                         GLenum format, GLenum type,
                         const void *data);

// Update 3D texture
void texture_update_3d(GLuint texture, int level,
                       int xoffset, int yoffset, int zoffset,
                       int width, int height, int depth,
                       GLenum format, GLenum type,
                       const void *data);
```

### GLSL Usage

```glsl
#version 300 es
precision highp float;

uniform sampler3D volumeTexture;

out vec4 fragColor;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    
    // Sample 3D texture
    vec3 uvw = vec3(uv, 0.5 + 0.5 * sin(iTime));
    vec4 sample = texture(volumeTexture, uvw);
    
    fragColor = sample;
}

void main() {
    mainImage(fragColor, gl_FragCoord.xy);
}
```

### Configuration Example

```vibe
default {
    shader volume_render.glsl
    
    # Define 3D texture input
    ichannels {
        0 {
            type volume
            file noise_volume_256x256x256.raw
            width 256
            height 256
            depth 256
            format rgb
        }
    }
}
```

---

## 4. Texture Arrays

### Description
Texture arrays are collections of 2D textures with the same size and format, accessible via a single sampler.

### Use Case in Shadertoy
Multiple texture layers, animation sequences, terrain splatting.

### Implementation Status
⚠️ **Planned for future release**

### GLSL Usage

```glsl
#version 300 es
precision highp float;

uniform sampler2DArray textureArray;

out vec4 fragColor;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    
    // Sample from layer based on time
    float layer = mod(iTime * 10.0, 8.0);
    vec4 sample = texture(textureArray, vec3(uv, layer));
    
    fragColor = sample;
}
```

---

## 5. Integer Textures and Operations

### Description
ES 3.0 adds full support for integer textures and integer operations in shaders.

### Implementation Status
✅ **Fully Supported**

### GLSL Usage

```glsl
#version 300 es
precision highp float;
precision highp int;

uniform isampler2D intTexture;     // Integer texture
uniform usampler2D uintTexture;    // Unsigned integer texture

out vec4 fragColor;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    ivec2 coord = ivec2(fragCoord);
    
    // Fetch integer values
    ivec4 intValue = texelFetch(intTexture, coord, 0);
    uvec4 uintValue = texelFetch(uintTexture, coord, 0);
    
    // Integer arithmetic
    int sum = intValue.r + intValue.g;
    uint product = uintValue.r * uintValue.g;
    
    // Bitwise operations
    int masked = intValue.r & 0xFF;
    int shifted = intValue.g >> 2;
    
    // Convert to float for output
    fragColor = vec4(float(sum) / 255.0, float(product) / 65535.0, 0.0, 1.0);
}
```

---

## 6. sRGB Framebuffers

### Description
sRGB framebuffers automatically perform gamma correction on output.

### Use Case
Proper color management and gamma-correct rendering.

### Implementation Status
⚠️ **Planned for future release**

### API Usage

```c
// Create sRGB framebuffer
GLuint fbo;
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);

// Create sRGB texture
GLuint tex;
glGenTextures(1, &tex);
glBindTexture(GL_TEXTURE_2D, tex);
glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, NULL);

// Attach to framebuffer
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D, tex, 0);
```

---

## 7. Advanced Texture Formats

### Description
ES 3.0 adds many new texture formats for specialized use cases.

### Supported Formats

#### Floating Point Formats
- `GL_R16F`, `GL_RG16F`, `GL_RGB16F`, `GL_RGBA16F` - Half float
- `GL_R32F`, `GL_RG32F`, `GL_RGB32F`, `GL_RGBA32F` - Full float

#### Integer Formats
- `GL_R8I`, `GL_RG8I`, `GL_RGB8I`, `GL_RGBA8I` - Signed 8-bit integer
- `GL_R16I`, `GL_RG16I`, `GL_RGB16I`, `GL_RGBA16I` - Signed 16-bit integer
- `GL_R32I`, `GL_RG32I`, `GL_RGB32I`, `GL_RGBA32I` - Signed 32-bit integer

#### Unsigned Integer Formats
- `GL_R8UI`, `GL_RG8UI`, `GL_RGB8UI`, `GL_RGBA8UI` - Unsigned 8-bit
- `GL_R16UI`, `GL_RG16UI`, `GL_RGB16UI`, `GL_RGBA16UI` - Unsigned 16-bit
- `GL_R32UI`, `GL_RG32UI`, `GL_RGB32UI`, `GL_RGBA32UI` - Unsigned 32-bit

#### Packed Formats
- `GL_RGB10_A2` - 10-bit RGB, 2-bit alpha
- `GL_R11F_G11F_B10F` - Packed floating point
- `GL_RGB9_E5` - Shared exponent floating point

#### Depth/Stencil
- `GL_DEPTH_COMPONENT16`, `GL_DEPTH_COMPONENT24`, `GL_DEPTH_COMPONENT32F`
- `GL_DEPTH24_STENCIL8`, `GL_DEPTH32F_STENCIL8`

---

## 8. Enhanced GLSL ES 3.00

### New Features

#### Integer Types
```glsl
int x = 42;
uint y = 42U;
ivec2 coord = ivec2(10, 20);
uvec4 mask = uvec4(0xFFu);
```

#### Bitwise Operations
```glsl
int a = 0xF0;
int b = 0x0F;
int and = a & b;      // Bitwise AND
int or = a | b;       // Bitwise OR
int xor = a ^ b;      // Bitwise XOR
int not = ~a;         // Bitwise NOT
int lshift = a << 2;  // Left shift
int rshift = a >> 2;  // Right shift
```

#### Switch Statements
```glsl
switch (mode) {
    case 0:
        color = vec3(1.0, 0.0, 0.0);
        break;
    case 1:
        color = vec3(0.0, 1.0, 0.0);
        break;
    default:
        color = vec3(0.0, 0.0, 1.0);
}
```

#### Array Operations
```glsl
float values[4] = float[](1.0, 2.0, 3.0, 4.0);
int indices[3] = int[](0, 1, 2);

// Non-constant array indexing (not allowed in ES 2.0)
int idx = int(mod(iTime, 4.0));
float value = values[idx];
```

#### Matrix Construction
```glsl
// Any size matrix
mat2x3 m = mat2x3(1.0, 2.0, 3.0,
                  4.0, 5.0, 6.0);

// Transpose
mat3x2 t = transpose(m);
```

---

## Implementation Roadmap

### Phase 1: Core MRT Support (v0.3.0)
- [ ] Implement MRT framebuffer creation
- [ ] Add multipass rendering pipeline
- [ ] Update shader wrapper to support multiple outputs
- [ ] Implement Buffer A/B/C/D for Shadertoy compatibility
- [ ] Add configuration syntax for render passes

### Phase 2: UBO Implementation (v0.3.0)
- [ ] Create standard Shadertoy UBO layout
- [ ] Implement UBO creation and binding
- [ ] Update render loop to use UBOs
- [ ] Add per-shader UBO support
- [ ] Performance benchmarking

### Phase 3: 3D Textures (v0.3.1)
- [ ] Implement 3D texture loading
- [ ] Add sampler3D support to shader wrapper
- [ ] Support procedural 3D noise generation
- [ ] Add configuration for 3D texture inputs

### Phase 4: Advanced Features (v0.4.0)
- [ ] Texture arrays
- [ ] sRGB framebuffers
- [ ] Additional texture formats
- [ ] Compute shader support (ES 3.1)

---

## Performance Considerations

### UBO vs Individual Uniforms

**Individual Uniforms:**
- Simple implementation
- Multiple API calls per update
- ~10-15 function calls per frame

**UBO:**
- Single buffer update
- One API call per frame
- Better GPU cache utilization
- ~2-3x faster uniform updates

### MRT Performance

**Benefits:**
- Single geometry pass for multiple outputs
- Reduced draw calls
- Better cache utilization

**Costs:**
- Higher memory bandwidth
- Fillrate sensitive
- May not be faster for simple shaders

**Recommendation:** Use MRT only when multiple passes are needed (e.g., deferred shading, multipass effects).

---

## Testing

### Test Shaders

#### MRT Test
```glsl
#version 300 es
precision highp float;

layout(location = 0) out vec4 fragColor0;
layout(location = 1) out vec4 fragColor1;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    fragColor0 = vec4(uv, 0.0, 1.0);
    fragColor1 = vec4(1.0 - uv, 0.0, 1.0);
}
```

#### UBO Test
```glsl
#version 300 es
precision highp float;

layout(std140) uniform ShadertoyInputs {
    vec3 iResolution;
    float iTime;
};

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

#### 3D Texture Test
```glsl
#version 300 es
precision highp float;

uniform sampler3D volume;

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    vec3 uvw = vec3(uv, 0.5);
    fragColor = texture(volume, uvw);
}
```

---

## References

- [OpenGL ES 3.0 Specification](https://www.khronos.org/registry/OpenGL/specs/es/3.0/es_spec_3.0.pdf)
- [GLSL ES 3.00 Specification](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)
- [Shadertoy Buffer Documentation](https://www.shadertoy.com/howto)
- [OpenGL ES Programming Guide](https://www.khronos.org/opengles/)

---

**Last Updated:** December 2024  
**Staticwall Version:** 0.2.0  
**Target Version for MRT/UBO:** 0.3.0