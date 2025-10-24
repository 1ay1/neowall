# Multi-Version EGL/OpenGL ES Architecture

## Overview

This document describes Staticwall's modular architecture for supporting all versions of EGL (1.0-1.5) and OpenGL ES (1.0-3.2). The system automatically detects available capabilities at runtime and selects the best version to use.

## Architecture Goals

1. **Universal Compatibility**: Support all EGL and OpenGL ES versions
2. **Runtime Detection**: Automatically detect what the user's system supports
3. **Graceful Degradation**: Fall back to older versions when newer ones unavailable
4. **Modular Design**: Each version in separate files for maintainability
5. **Zero Configuration**: Works out of the box on any system
6. **Build-Time Optimization**: Only compile support for what's available

---

## Directory Structure

```
staticwall/
├── src/egl/
│   ├── capability.c           # Runtime capability detection
│   ├── egl_core.c             # Main EGL initialization & dispatch
│   ├── egl_v10.c              # EGL 1.0 specific code
│   ├── egl_v11.c              # EGL 1.1 specific code
│   ├── egl_v12.c              # EGL 1.2 specific code
│   ├── egl_v13.c              # EGL 1.3 specific code
│   ├── egl_v14.c              # EGL 1.4 specific code
│   ├── egl_v15.c              # EGL 1.5 specific code
│   ├── gles_v10.c             # OpenGL ES 1.0 (fixed pipeline)
│   ├── gles_v11.c             # OpenGL ES 1.1 (enhanced fixed)
│   ├── gles_v20.c             # OpenGL ES 2.0 (shaders)
│   ├── gles_v30.c             # OpenGL ES 3.0 (enhanced shaders)
│   ├── gles_v31.c             # OpenGL ES 3.1 (compute shaders)
│   └── gles_v32.c             # OpenGL ES 3.2 (geometry/tess)
├── include/egl/
│   ├── capability.h           # Capability detection API
│   ├── egl_core.h             # Core EGL API
│   ├── egl_versions.h         # Version-specific APIs
│   └── gles_versions.h        # OpenGL ES version APIs
└── docs/
    ├── MULTI_VERSION_ARCHITECTURE.md  # This file
    ├── EGL_VERSIONS.md        # EGL version details
    └── GLES_VERSIONS.md       # OpenGL ES version details
```

---

## Version Support Matrix

### EGL Versions

| Version | Year | Key Features | Status |
|---------|------|--------------|--------|
| EGL 1.0 | 2003 | Basic context creation, surfaces | ✅ Planned |
| EGL 1.1 | 2004 | Surface locking | ✅ Planned |
| EGL 1.2 | 2005 | OpenGL ES 2.0 binding | ✅ Planned |
| EGL 1.3 | 2007 | VG colorspace, alpha format | ✅ Planned |
| EGL 1.4 | 2011 | Multiple contexts, multithreading | ✅ Planned |
| EGL 1.5 | 2014 | Sync objects, platform displays | ✅ Planned |

### OpenGL ES Versions

| Version | Year | Key Features | Status |
|---------|------|--------------|--------|
| ES 1.0 | 2003 | Fixed-function pipeline | ⚠️ Limited (for compatibility) |
| ES 1.1 | 2004 | VBOs, point sprites | ⚠️ Limited (for compatibility) |
| ES 2.0 | 2007 | Programmable shaders (GLSL 100) | ✅ Full Support (Current) |
| ES 3.0 | 2012 | Enhanced shaders (GLSL 300 es) | ✅ Full Support (New) |
| ES 3.1 | 2014 | Compute shaders (GLSL 310 es) | 🚧 In Progress |
| ES 3.2 | 2015 | Geometry/tessellation shaders | 🚧 In Progress |

---

## Capability Detection System

### Runtime Detection Flow

```
┌─────────────────────────────────────┐
│  Application Startup                │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  egl_detect_capabilities()          │
│  - Query EGL version                │
│  - Query available extensions       │
│  - Store in egl_capabilities_t      │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Try ES 3.2 Context Creation        │
│  ├─ Success → Use ES 3.2            │
│  └─ Fail → Try ES 3.1               │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Try ES 3.1 Context Creation        │
│  ├─ Success → Use ES 3.1            │
│  └─ Fail → Try ES 3.0               │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Try ES 3.0 Context Creation        │
│  ├─ Success → Use ES 3.0            │
│  └─ Fail → Try ES 2.0               │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Try ES 2.0 Context Creation        │
│  ├─ Success → Use ES 2.0            │
│  └─ Fail → Error (ES 2.0 minimum)   │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  gles_detect_capabilities()         │
│  - Query GL version                 │
│  - Query extensions                 │
│  - Query limits (max textures, etc) │
│  - Update egl_capabilities_t        │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Select Rendering Path              │
│  - ES 3.2: Use geometry shaders     │
│  - ES 3.1: Use compute shaders      │
│  - ES 3.0: Use MRT, instancing      │
│  - ES 2.0: Basic shader rendering   │
└─────────────────────────────────────┘
```

### Capability Structure

```c
typedef struct {
    /* Detected versions */
    egl_version_t egl_version;    // EGL_VERSION_1_5, etc.
    gles_version_t gles_version;  // GLES_VERSION_3_0, etc.
    
    /* Version-specific capabilities */
    egl_v10_caps_t egl_v10;
    egl_v11_caps_t egl_v11;
    egl_v12_caps_t egl_v12;
    egl_v13_caps_t egl_v13;
    egl_v14_caps_t egl_v14;
    egl_v15_caps_t egl_v15;
    
    gles_v10_caps_t gles_v10;
    gles_v11_caps_t gles_v11;
    gles_v20_caps_t gles_v20;
    gles_v30_caps_t gles_v30;
    gles_v31_caps_t gles_v31;
    gles_v32_caps_t gles_v32;
    
    /* Extension support */
    bool has_egl_khr_image_base;
    bool has_egl_khr_fence_sync;
    bool has_oes_texture_3d;
    bool has_oes_packed_depth_stencil;
    // ... many more
    
    /* Runtime strings */
    char egl_vendor[256];
    char gl_renderer[256];
    char gl_version[256];
    // ... etc
} egl_capabilities_t;
```

---

## Context Creation Strategy

### Priority Order (Highest to Lowest)

1. **OpenGL ES 3.2** - If available, use for maximum Shadertoy compatibility
2. **OpenGL ES 3.1** - Compute shaders for advanced effects
3. **OpenGL ES 3.0** - Modern GLSL, great Shadertoy support
4. **OpenGL ES 2.0** - Minimum requirement, basic shader support

### Context Attributes by Version

#### ES 3.2 Context
```c
EGLint attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 2,
    EGL_NONE
};
```

#### ES 3.1 Context
```c
EGLint attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 1,
    EGL_NONE
};
```

#### ES 3.0 Context
```c
EGLint attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 0,
    EGL_NONE
};
```

#### ES 2.0 Context (Fallback)
```c
EGLint attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};
```

---

## Shader Version Adaptation

### GLSL Version Mapping

| OpenGL ES | GLSL Version | Shader Features |
|-----------|--------------|-----------------|
| ES 1.0/1.1 | N/A | Fixed-function (no shaders) |
| ES 2.0 | #version 100 | Basic shaders, texture2D() |
| ES 3.0 | #version 300 es | texture(), in/out, integers |
| ES 3.1 | #version 310 es | Compute shaders, SSBOs |
| ES 3.2 | #version 320 es | Geometry, tessellation |

### Automatic Adaptation

The shader adaptation layer automatically converts between versions:

```c
// Shadertoy shader (ES 3.0)
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture(iChannel0, uv).rgb;
    fragColor = vec4(col, 1.0);
}

// ↓ Automatically converted for ES 2.0 ↓

#version 100
precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D iChannel0;
uniform vec3 iResolution;

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture2D(iChannel0, uv).rgb;
    fragColor = vec4(col, 1.0);
}

void main() {
    mainImage(gl_FragColor, gl_FragCoord.xy);
}
```

---

## Build System Integration

### Makefile Feature Detection

The Makefile automatically detects available headers and libraries:

```makefile
# Detect OpenGL ES versions
HAS_GLES1 := $(shell pkg-config --exists glesv1_cm && echo yes)
HAS_GLES2 := $(shell pkg-config --exists glesv2 && echo yes)
HAS_GLES3 := $(shell test -f /usr/include/GLES3/gl3.h && echo yes)

# Detect EGL version
EGL_VERSION := $(shell pkg-config --modversion egl 2>/dev/null)

# Conditional compilation flags
ifeq ($(HAS_GLES3),yes)
    CFLAGS += -DHAVE_GLES3
    LDFLAGS += -lGLESv3
endif

ifeq ($(HAS_GLES2),yes)
    CFLAGS += -DHAVE_GLES2
    LDFLAGS += -lGLESv2
endif

ifeq ($(HAS_GLES1),yes)
    CFLAGS += -DHAVE_GLES1
    LDFLAGS += -lGLESv1_CM
endif

# Conditional source files
GLES_SOURCES :=
ifeq ($(HAS_GLES3),yes)
    GLES_SOURCES += src/egl/gles_v30.c src/egl/gles_v31.c src/egl/gles_v32.c
endif
ifeq ($(HAS_GLES2),yes)
    GLES_SOURCES += src/egl/gles_v20.c
endif
ifeq ($(HAS_GLES1),yes)
    GLES_SOURCES += src/egl/gles_v10.c src/egl/gles_v11.c
endif
```

### Conditional Compilation

Use preprocessor macros for version-specific code:

```c
#ifdef HAVE_GLES3
    // ES 3.0+ specific code
    if (caps->gles_version >= GLES_VERSION_3_0) {
        use_transform_feedback();
    }
#endif

#ifdef HAVE_GLES2
    // ES 2.0+ specific code
    if (caps->gles_version >= GLES_VERSION_2_0) {
        use_programmable_shaders();
    }
#endif

#ifdef HAVE_GLES1
    // ES 1.x specific code (legacy support)
    if (caps->gles_version <= GLES_VERSION_1_1) {
        use_fixed_pipeline();
    }
#endif
```

---

## Rendering Path Selection

### Per-Version Rendering Strategies

#### OpenGL ES 3.2 Path
- **Use**: Geometry shaders for procedural geometry
- **Use**: Tessellation for detailed surfaces
- **Use**: Compute shaders for particle systems
- **Best for**: Complex effects, advanced Shadertoy shaders

#### OpenGL ES 3.1 Path
- **Use**: Compute shaders for GPU computation
- **Use**: Shader storage buffers for large data
- **Use**: Atomic counters for synchronization
- **Best for**: GPU-accelerated effects, complex simulations

#### OpenGL ES 3.0 Path
- **Use**: Multiple render targets for post-processing
- **Use**: Transform feedback for particle systems
- **Use**: Integer textures for precise data
- **Best for**: Most Shadertoy shaders (80-90% compatible)

#### OpenGL ES 2.0 Path (Fallback)
- **Use**: Basic fragment shaders
- **Use**: Single render target
- **Use**: Float-only operations
- **Best for**: Simple effects, basic Shadertoy shaders

### Decision Tree

```c
void select_rendering_path(egl_capabilities_t *caps) {
    if (caps->gles_version >= GLES_VERSION_3_2) {
        if (shader_needs_geometry) {
            use_geometry_shader_path();
        } else if (shader_needs_tessellation) {
            use_tessellation_path();
        }
    }
    
    if (caps->gles_version >= GLES_VERSION_3_1) {
        if (shader_needs_compute) {
            use_compute_shader_path();
        }
    }
    
    if (caps->gles_version >= GLES_VERSION_3_0) {
        // Default modern path
        use_es3_shader_path();
    } else {
        // Fallback to ES 2.0
        use_es2_shader_path();
    }
}
```

---

## Shadertoy Compatibility

### Compatibility by Version

| OpenGL ES Version | Shadertoy Compatibility | Notes |
|-------------------|------------------------|-------|
| ES 2.0 | ~40% | Basic shaders only, needs adaptation |
| ES 3.0 | ~85% | Most shaders work, texture() support |
| ES 3.1 | ~90% | Compute shader effects possible |
| ES 3.2 | ~95% | Nearly complete, geometry shaders |

### Missing Features (All Versions)

- **iMouse**: Mouse input (planned)
- **iChannel**: Real texture loading (uses procedural fallback)
- **Multipass**: Buffer A/B/C/D (planned for ES 3.0+)
- **Audio**: Audio input/visualization (future)

---

## Performance Considerations

### Version-Specific Optimizations

#### ES 3.2
- Use geometry shaders to reduce draw calls
- Use tessellation for LOD management
- Minimize CPU-GPU synchronization

#### ES 3.1
- Use compute shaders for heavy computation
- Use shader storage buffers to avoid texture lookups
- Use atomic counters for lock-free algorithms

#### ES 3.0
- Use uniform buffer objects to reduce API calls
- Use instanced rendering for repeated geometry
- Use multiple render targets for deferred shading

#### ES 2.0
- Minimize varying variables
- Use mediump/lowp precision where possible
- Avoid complex branching in shaders

### Memory Footprint

- **ES 1.x**: ~512 KB (fixed pipeline state)
- **ES 2.0**: ~2 MB (shader compilation, VBOs)
- **ES 3.0**: ~4 MB (additional features, larger shaders)
- **ES 3.1+**: ~6 MB (compute shaders, SSBOs)

---

## API Examples

### Initializing Multi-Version Support

```c
#include "egl/capability.h"
#include "egl/egl_core.h"

int main() {
    egl_capabilities_t caps;
    
    // Get EGL display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    
    // Detect all capabilities
    if (!egl_detect_capabilities(display, &caps)) {
        fprintf(stderr, "Failed to detect capabilities\n");
        return 1;
    }
    
    // Print summary
    egl_print_capabilities(&caps);
    
    // Create best available context
    EGLContext context = egl_create_best_context(display, &caps);
    
    // Make current and detect GL capabilities
    eglMakeCurrent(display, surface, surface, context);
    gles_detect_capabilities_for_context(display, context, &caps);
    
    // Select rendering path
    switch (caps.gles_version) {
        case GLES_VERSION_3_2:
            printf("Using ES 3.2 rendering path\n");
            init_es32_renderer(&caps);
            break;
        case GLES_VERSION_3_1:
            printf("Using ES 3.1 rendering path\n");
            init_es31_renderer(&caps);
            break;
        case GLES_VERSION_3_0:
            printf("Using ES 3.0 rendering path\n");
            init_es30_renderer(&caps);
            break;
        case GLES_VERSION_2_0:
            printf("Using ES 2.0 rendering path (fallback)\n");
            init_es20_renderer(&caps);
            break;
        default:
            fprintf(stderr, "No compatible OpenGL ES version\n");
            return 1;
    }
    
    return 0;
}
```

### Checking for Features

```c
// Check if compute shaders are available
if (gles_has_min_version(&caps, GLES_VERSION_3_1)) {
    if (caps.gles_v31.has_compute_shaders) {
        use_compute_shader_for_particle_system();
    }
}

// Check for specific extension
if (caps.has_oes_texture_3d) {
    use_3d_texture_effects();
}

// Get capability limits
printf("Max texture units: %d\n", caps.gles_v20.max_texture_image_units);
printf("Max texture size: %d\n", caps.gles_v20.max_texture_size);
```

---

## Testing Strategy

### Unit Tests

- Test capability detection on various systems
- Test context creation for each version
- Test shader adaptation between versions
- Test graceful fallback when versions unavailable

### Integration Tests

- Test full rendering pipeline with each ES version
- Test Shadertoy shader compatibility at each level
- Test performance with different version paths
- Test multi-monitor with mixed versions

### Compatibility Tests

Systems to test:
- **Mesa drivers** (Intel, AMD, open source)
- **Proprietary drivers** (NVIDIA, AMD)
- **ARM Mali** (mobile/embedded)
- **PowerVR** (mobile)
- **Older systems** (ES 2.0 only)
- **Cutting-edge** (ES 3.2 with all extensions)

---

## Migration Guide

### From Current Code

**Old (single version)**:
```c
// Hardcoded ES 2.0
const EGLint attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};
context = eglCreateContext(display, config, EGL_NO_CONTEXT, attribs);
```

**New (multi-version)**:
```c
// Automatic best version selection
egl_capabilities_t caps;
egl_detect_capabilities(display, &caps);
EGLContext context = egl_create_best_context(display, &caps);
```

### Adding Version-Specific Features

1. Check capability:
   ```c
   if (caps.gles_version >= GLES_VERSION_3_1) {
       // ES 3.1+ feature
   }
   ```

2. Add version-specific implementation:
   ```c
   // src/egl/gles_v31.c
   bool gles31_setup_compute_shader(...) {
       // ES 3.1 specific code
   }
   ```

3. Add fallback for older versions:
   ```c
   if (caps.gles_version >= GLES_VERSION_3_1) {
       gles31_setup_compute_shader(...);
   } else {
       gles20_fragment_shader_fallback(...);
   }
   ```

---

## Future Enhancements

### Planned Features

1. **Vulkan Backend** (alternative to EGL/OpenGL ES)
2. **WebGL Support** (run shaders in browser)
3. **OpenGL Desktop** (for non-embedded systems)
4. **Direct3D** (Windows support)
5. **Metal** (macOS/iOS support)

### Extension Support

High priority extensions to add:
- `EGL_KHR_image_base` - Zero-copy texture sharing
- `EGL_KHR_fence_sync` - Synchronization primitives
- `GL_OES_EGL_image` - External image sources
- `GL_EXT_multisampled_render_to_texture` - MSAA
- `GL_OES_texture_float_linear` - Float texture filtering

---

## Troubleshooting

### Common Issues

**Issue**: ES 3.0 context fails to create
**Solution**: Check driver version, may need driver update

**Issue**: Shader fails to compile on ES 2.0
**Solution**: Check shader uses only ES 2.0 features, or enable adaptation

**Issue**: Performance degraded after adding multi-version support
**Solution**: Ensure using best available version, check capability detection

**Issue**: Extensions not detected
**Solution**: Make context current before querying extensions

### Debug Logging

Enable verbose capability logging:
```bash
staticwall -f -v --debug-capabilities
```

Output includes:
- Detected EGL version
- Detected OpenGL ES version
- All available extensions
- Capability limits
- Selected rendering path

---

## References

- **EGL 1.5 Specification**: https://www.khronos.org/registry/EGL/
- **OpenGL ES 3.2 Specification**: https://www.khronos.org/registry/OpenGL/
- **Shadertoy**: https://www.shadertoy.com/
- **Mesa3D**: https://www.mesa3d.org/ (reference implementation)

---

## Contributing

### Adding New Version Support

1. Create `src/egl/gles_vXY.c` for new version
2. Add capability detection in `capability.c`
3. Update `egl_core.c` context creation
4. Add rendering path selection
5. Update documentation
6. Add tests

### Coding Standards

- Use version-specific prefixes (`gles30_`, `egl15_`)
- Always check capabilities before using features
- Provide fallbacks for older versions
- Document minimum version requirements
- Add compile-time guards (`#ifdef HAVE_GLES3`)

---

**Last Updated**: 2024
**Version**: 1.0.0
**Status**: In Development