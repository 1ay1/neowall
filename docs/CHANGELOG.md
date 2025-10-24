# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0-dev] - 2024-10-24

### Added
- **Multi-version EGL/OpenGL ES support** with automatic runtime detection (ES 2.0 → ES 3.2)
- Comprehensive capability detection system (`include/egl/capability.h`, `src/egl/capability.c`)
- Core EGL dispatch system with automatic context fallback (`src/egl/egl_core.c`)
- Shader adaptation layer for cross-version compatibility (`src/shader_adaptation.c`)
- Dynamic Shadertoy wrapper that adapts to GL version
- Version-specific implementation files (ES 2.0, ES 3.0, ES 3.1, ES 3.2)
- Deferred configuration system for initialization race conditions
- Defensive initialization checks for surfaces, contexts, and windows
- Enhanced build system with automatic version detection
- New Makefile target: `make print-caps` to display detected capabilities
- Comprehensive documentation (MULTI_VERSION_ARCHITECTURE.md, SHADERTOY_SUPPORT.md, etc.)
- .gitignore for build artifacts and backup files

### Changed
- Migrated from monolithic `src/egl.c` to modular `src/egl/` directory structure
- Shader wrapper now dynamically selects GLSL version based on GL context
- Improved Shadertoy support with ES 3.0+ features (texture(), in/out, integers)
- Enhanced build output with color-coded capability detection
- Updated initialization order to prevent race conditions

### Fixed
- **Critical**: Runtime SIGSEGV crash during surface initialization
- **Critical**: Shader version mismatch (#version 100 vs #version 300 es)
- **Critical**: Missing VBO initialization causing crashes on first render
- Initialization race condition when config loads before surfaces are ready
- Surface creation timing issues with Wayland layer surfaces
- Shader compilation failures with ES 3.0 syntax on ES 3.0 contexts

### Removed
- `src/egl.c` (replaced by modular `src/egl/` directory)

### Testing
- ✅ Tested on NVIDIA RTX 4060 with OpenGL ES 3.2
- ✅ Verified automatic context creation and fallback
- ✅ Validated Shadertoy shader compilation and rendering
- ✅ Confirmed stable operation without crashes

### Performance
- Runtime capability detection: <1ms overhead at startup
- Zero performance impact after initialization
- Native shader execution without conversion overhead

### Security
- Added defensive null pointer checks throughout initialization
- Validated all GL state before operations
- Proper resource cleanup on errors

## [0.1.0] - 2024-XX-XX

### Initial Release
- Basic Wayland wallpaper daemon functionality
- OpenGL ES 2.0 support
- Static shader support
- Basic Shadertoy compatibility
