# NeoWall - GPU-Accelerated Live Wallpapers for Wayland & X11
# Copyright (C) 2025
#
# This meson.build replicates the functionality of the Makefile exactly.

project('neowall', 'c',
  version: '0.4.4',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
    'optimization=2',
  ],
)

cc = meson.get_compiler('c')

# =============================================================================
# Session Detection (for helpful error messages)
# =============================================================================

session_type = run_command('sh', '-c', 'echo $XDG_SESSION_TYPE', check: false).stdout().strip()
message('Detected session type: ' + (session_type != '' ? session_type : 'unknown'))

# =============================================================================
# Required Dependencies
# =============================================================================

# EGL is always required
egl_dep = dependency('egl', required: true)
message('EGL detected: ' + egl_dep.version())

# OpenGL ES 2.0 is the minimum requirement
glesv2_dep = dependency('glesv2', required: true)
message('OpenGL ES 2.0 detected')

# Image loading libraries
libpng_dep = dependency('libpng', required: true)
libjpeg_dep = dependency('libjpeg', required: true)

# Math and threading (always needed)
m_dep = cc.find_library('m', required: true)
thread_dep = dependency('threads', required: true)

# =============================================================================
# Optional OpenGL ES Versions
# =============================================================================

# OpenGL ES 1.x (legacy, optional)
glesv1_dep = dependency('glesv1_cm', required: false)
has_gles1 = glesv1_dep.found()
if has_gles1
  message('OpenGL ES 1.x detected (legacy support)')
else
  message('OpenGL ES 1.x not found (optional)')
endif

# Check for OpenGL ES 3.x headers
has_gles30 = cc.has_header('GLES3/gl3.h')
has_gles31 = cc.has_header('GLES3/gl31.h')
has_gles32 = cc.has_header('GLES3/gl32.h')

# OpenGL ES 3.x implies lower versions
if has_gles32
  has_gles31 = true
  has_gles30 = true
elif has_gles31
  has_gles30 = true
endif

if has_gles30
  message('OpenGL ES 3.0 detected (enhanced Shadertoy support)')
else
  message('OpenGL ES 3.0 not found (Shadertoy compatibility limited)')
endif

if has_gles31
  message('OpenGL ES 3.1 detected (compute shader support)')
else
  message('OpenGL ES 3.1 not found (optional)')
endif

if has_gles32
  message('OpenGL ES 3.2 detected (geometry/tessellation shaders)')
else
  message('OpenGL ES 3.2 not found (optional)')
endif

# Check EGL extensions
has_egl_khr_image = cc.has_header_symbol('EGL/eglext.h', 'EGL_KHR_image_base')
has_egl_khr_fence = cc.has_header_symbol('EGL/eglext.h', 'EGL_KHR_fence_sync')

if has_egl_khr_image
  message('EGL_KHR_image extension available')
endif

if has_egl_khr_fence
  message('EGL_KHR_fence_sync extension available')
endif

# =============================================================================
# Backend Dependencies
# =============================================================================

# Allow explicit backend control via options
wayland_backend_option = get_option('wayland_backend')
x11_backend_option = get_option('x11_backend')

# Wayland dependencies
wayland_client_dep = dependency('wayland-client', required: false)
wayland_egl_dep = dependency('wayland-egl', required: false)
wayland_protocols_dep = dependency('wayland-protocols', required: false)

# We use pre-generated protocol files, so only need client libs at runtime
has_wayland_deps = wayland_client_dep.found() and wayland_egl_dep.found() and wayland_protocols_dep.found()

# Determine if Wayland backend should be enabled
if wayland_backend_option.disabled()
  has_wayland = false
  message('Wayland backend disabled by user')
elif has_wayland_deps
  has_wayland = true
  message('Wayland backend available')
else
  has_wayland = false
  message('Wayland backend not available (requires wayland-client, wayland-egl, and wayland-protocols)')
endif

# X11 dependencies
x11_dep = dependency('x11', required: false)
xrandr_dep = dependency('xrandr', required: false)

has_x11_deps = x11_dep.found() and xrandr_dep.found()

# Determine if X11 backend should be enabled
if x11_backend_option.disabled()
  has_x11 = false
  message('X11 backend disabled by user')
elif has_x11_deps
  has_x11 = true
  message('X11 backend available (with XRandR multi-monitor support)')
else
  has_x11 = false
  message('X11 backend not available (requires libx11 and libxrandr)')
endif

# =============================================================================
# Backend Validation
# =============================================================================

# Error if no backends available
if not has_wayland and not has_x11
  error('No display server backend available. Install wayland-client/wayland-egl or libx11-dev')
endif

# Error if on Wayland session but Wayland backend not available
if session_type == 'wayland' and not has_wayland
  error('''
================================================================================
FATAL: You are running a Wayland session but Wayland dependencies are missing!

NeoWall requires native Wayland support to work correctly on Wayland sessions.
The X11/XWayland fallback will NOT work properly (causes black window issues).

Please install the following packages:

  Arch/Garuda:   sudo pacman -S wayland wayland-protocols

  Debian/Ubuntu: sudo apt install libwayland-dev wayland-protocols

  Fedora:        sudo dnf install wayland-devel wayland-protocols-devel

Then run: meson setup build --wipe
================================================================================
''')
endif

# Warn if on X11 session but X11 backend not available
if session_type == 'x11' and not has_x11
  warning('You are on an X11 session but X11 backend is not available. Install libx11-dev and libxrandr-dev.')
endif

# =============================================================================
# Generate version.h from version.h.in
# =============================================================================

version_h = configure_file(
  input: 'include/version.h.in',
  output: 'version.h',
  configuration: {
    'VERSION': meson.project_version(),
  },
)

# =============================================================================
# Compiler Flags
# =============================================================================

# Common definitions
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-DNEOWALL_VERSION="' + meson.project_version() + '"', language: 'c')

# EGL/OpenGL ES flags (always set)
add_project_arguments('-DHAVE_EGL', language: 'c')
add_project_arguments('-DHAVE_GLES2', language: 'c')

# Optional OpenGL ES versions
if has_gles1
  add_project_arguments('-DHAVE_GLES1', language: 'c')
endif

if has_gles30
  add_project_arguments('-DHAVE_GLES3', '-DHAVE_GLES30', language: 'c')
endif

if has_gles31
  add_project_arguments('-DHAVE_GLES31', language: 'c')
endif

if has_gles32
  add_project_arguments('-DHAVE_GLES32', language: 'c')
endif

# EGL extensions
if has_egl_khr_image
  add_project_arguments('-DHAVE_EGL_KHR_IMAGE', language: 'c')
endif

if has_egl_khr_fence
  add_project_arguments('-DHAVE_EGL_KHR_FENCE_SYNC', language: 'c')
endif

# Backend flags
if has_wayland
  add_project_arguments('-DHAVE_WAYLAND_BACKEND', language: 'c')
endif

if has_x11
  add_project_arguments('-DHAVE_X11_BACKEND', '-DHAVE_XRANDR', language: 'c')
endif

# =============================================================================
# Include Directories
# =============================================================================

inc_dirs = include_directories(
  'include',
  'protocols',
  'src/shader_lib',
)

# Also include the build directory for generated version.h
inc_dirs_build = include_directories('.')

# =============================================================================
# Source Files
# =============================================================================

# Core sources (always compiled)
core_sources = files(
  'src/main.c',
  'src/eventloop.c',
  'src/utils.c',
)

# EGL core sources (always compiled)
egl_core_sources = files(
  'src/egl/capability.c',
  'src/egl/egl_core.c',
)

# EGL version sources (always include for runtime detection)
egl_version_sources = files(
  'src/egl/egl_v10.c',
  'src/egl/egl_v11.c',
  'src/egl/egl_v12.c',
  'src/egl/egl_v13.c',
  'src/egl/egl_v14.c',
  'src/egl/egl_v15.c',
)

# OpenGL ES version-specific sources (conditional)
gles_sources = files(
  'src/egl/gles_v20.c',  # ES 2.0 always included (minimum requirement)
)

if has_gles1
  gles_sources += files(
    'src/egl/gles_v10.c',
    'src/egl/gles_v11.c',
  )
endif

if has_gles30
  gles_sources += files('src/egl/gles_v30.c')
endif

if has_gles31
  gles_sources += files('src/egl/gles_v31.c')
endif

if has_gles32
  gles_sources += files('src/egl/gles_v32.c')
endif

# All EGL sources
egl_sources = egl_core_sources + egl_version_sources + gles_sources

# Shader library sources
shader_lib_sources = files(
  'src/shader_lib/shader_core.c',
  'src/shader_lib/shader_adaptation.c',
  'src/shader_lib/shadertoy_compat.c',
  'src/shader_lib/shader_utils.c',
  'src/shader_lib/neowall_shader_api.c',
)

# Transition sources
transition_sources = files(
  'src/transitions/fade.c',
  'src/transitions/glitch.c',
  'src/transitions/pixelate.c',
  'src/transitions/slide.c',
  'src/transitions/transitions.c',
)

# Texture sources
texture_sources = files(
  'src/textures/abstract.c',
  'src/textures/blue_noise.c',
  'src/textures/gray_noise.c',
  'src/textures/rgba_noise.c',
  'src/textures/wood.c',
)

# Output sources
output_sources = files(
  'src/output/output.c',
)

# Config sources
config_sources = files(
  'src/config/config.c',
  'src/config/vibe.c',
)

# Render sources
render_sources = files(
  'src/render/render.c',
)

# Image sources
image_sources = files(
  'src/image/image.c',
)

# Compositor abstraction layer sources
compositor_sources = files(
  'src/compositor/compositor_registry.c',
  'src/compositor/compositor_surface.c',
)

# Wayland protocol sources (pre-generated, only when Wayland backend enabled)
wayland_protocol_sources = []
if has_wayland
  wayland_protocol_sources = files(
    'protocols/wlr-layer-shell-unstable-v1-client-protocol.c',
    'protocols/xdg-shell-client-protocol.c',
    'protocols/viewporter-client-protocol.c',
    'protocols/xdg-output-unstable-v1-client-protocol.c',
    'protocols/plasma-shell-client-protocol.c',
    'protocols/tearing-control-v1-client-protocol.c',
  )
endif

# Wayland backend sources (conditional)
wayland_sources = []
if has_wayland
  wayland_sources = files(
    'src/compositor/backends/wayland/wayland_core.c',
    'src/compositor/backends/wayland/compositors/wlr_layer_shell.c',
    'src/compositor/backends/wayland/compositors/kde_plasma.c',
    'src/compositor/backends/wayland/compositors/gnome_shell.c',
    'src/compositor/backends/wayland/compositors/fallback.c',
  )
endif

# X11 backend sources (conditional)
x11_sources = []
if has_x11
  x11_sources = files(
    'src/compositor/backends/x11/x11_core.c',
  )
endif

# =============================================================================
# Dependencies List
# =============================================================================

deps = [
  egl_dep,
  glesv2_dep,
  libpng_dep,
  libjpeg_dep,
  m_dep,
  thread_dep,
]

if has_gles1
  deps += glesv1_dep
endif

if has_wayland
  deps += [wayland_client_dep, wayland_egl_dep]
endif

if has_x11
  deps += [x11_dep, xrandr_dep]
endif

# =============================================================================
# Combine All Sources
# =============================================================================

all_sources = (
  core_sources +
  egl_sources +
  shader_lib_sources +
  transition_sources +
  texture_sources +
  output_sources +
  config_sources +
  render_sources +
  image_sources +
  compositor_sources +
  wayland_protocol_sources +
  wayland_sources +
  x11_sources
)

# =============================================================================
# Build Target
# =============================================================================

neowall_exe = executable('neowall',
  all_sources,
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: deps,
  install: true,
)

# =============================================================================
# Installation
# =============================================================================

# Install config files to share/neowall/
install_data(
  'config/config.vibe',
  'config/neowall.vibe',
  install_dir: get_option('datadir') / 'neowall',
)

# Install example shaders to share/neowall/shaders/
install_subdir('examples/shaders',
  install_dir: get_option('datadir') / 'neowall',
  strip_directory: false,
)

# Install documentation
install_data('README.md', install_dir: get_option('datadir') / 'doc' / 'neowall')
install_data('LICENSE', install_dir: get_option('datadir') / 'doc' / 'neowall')

# =============================================================================
# Custom Targets (matching Makefile's development targets)
# =============================================================================

# Run target
run_target('run',
  command: [neowall_exe],
)

# Run with verbose logging
run_target('run-verbose',
  command: [neowall_exe, '-f', '-v'],
)

# Run with capability debugging
run_target('run-capabilities',
  command: [neowall_exe, '-f', '-v', '--debug-capabilities'],
)

# Format target (if clang-format is available)
clang_format = find_program('clang-format', required: false)
if clang_format.found()
  run_target('format',
    command: [
      'sh', '-c',
      'find @0@/src @0@/include -name "*.c" -o -name "*.h" | xargs clang-format -i'.format(meson.current_source_dir())
    ],
  )
endif

# Analyze target (if cppcheck is available)
cppcheck = find_program('cppcheck', required: false)
if cppcheck.found()
  run_target('analyze',
    command: [
      cppcheck,
      '--enable=all',
      '--suppress=missingIncludeSystem',
      meson.current_source_dir() / 'src',
    ],
  )
endif

# =============================================================================
# Summary
# =============================================================================

summary({
  'Version': meson.project_version(),
  'Prefix': get_option('prefix'),
}, section: 'Project')

summary({
  'Session Type': session_type != '' ? session_type : 'unknown',
  'Wayland Backend': has_wayland,
  'X11 Backend': has_x11,
}, section: 'Backends')

summary({
  'OpenGL ES 1.x': has_gles1,
  'OpenGL ES 2.0': true,
  'OpenGL ES 3.0': has_gles30,
  'OpenGL ES 3.1': has_gles31,
  'OpenGL ES 3.2': has_gles32,
}, section: 'OpenGL ES Support')

summary({
  'EGL_KHR_image': has_egl_khr_image,
  'EGL_KHR_fence_sync': has_egl_khr_fence,
}, section: 'EGL Extensions')
