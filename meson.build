# NeoWall - GPU-Accelerated Live Wallpapers for Wayland & X11
# Copyright (C) 2025
#
# This meson.build replicates the functionality of the Makefile exactly.

project('neowall', 'c',
  version: '0.5.1',
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

# Desktop OpenGL for Shadertoy compatibility (uses #version 330 core)
gl_dep = dependency('gl', required: true)
message('Desktop OpenGL detected')

# Image loading libraries
libpng_dep = dependency('libpng', required: false)
if not libpng_dep.found()
  # openSUSE Leap and a few others only ship the versioned .pc file.
  libpng_dep = dependency('libpng16', required: true)
endif
libjpeg_dep = dependency('libjpeg', required: true)

# Math and threading (always needed)
m_dep = cc.find_library('m', required: true)
thread_dep = dependency('threads', required: true)

# Check for OpenGL 3.3 support (required for Shadertoy shaders)
has_gl33 = cc.has_header('GL/gl.h')
if has_gl33
  message('Desktop OpenGL 3.3+ available (full Shadertoy support)')
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
wayland_cursor_dep = dependency('wayland-cursor', required: false)
wayland_protocols_dep = dependency('wayland-protocols', required: false)

# We use pre-generated protocol files, so only need client libs at runtime
has_wayland_deps = wayland_client_dep.found() and wayland_egl_dep.found() and wayland_cursor_dep.found() and wayland_protocols_dep.found()

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

# EGL and Desktop OpenGL flags
add_project_arguments('-DHAVE_EGL', language: 'c')
add_project_arguments('-DHAVE_GL33', language: 'c')
add_project_arguments('-DGL_GLEXT_PROTOTYPES', language: 'c')

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
  'include',         # ALL public headers live under include/neowall/...
  'protocols',       # generated *-client-protocol.h
  # Module-private headers (fan-in 1) stay next to their .c and are reached
  # via these dirs. They are intentionally NOT part of the public include tree.
  'src/compositor/backends/wayland',
  'src/compositor/backends/x11',
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

# EGL sources (desktop OpenGL 3.3)
egl_sources = files(
  'src/egl/egl_core.c',
)

# Shader engine sources
shader_sources = files(
  'src/shader/shader_core.c',
  'src/shader/shader_error_log.c',
  'src/shader/shader_multipass.c',
  'src/shader/program_cache.c',
  'src/shader/multipass_parse.c',
  'src/shader/shadertoy_compat.c',
  'src/shader/adaptive_scale.c',
  'src/shader/render_optimizer.c',
  'src/shader/multipass_optimizer.c',
  'src/shader/reactive.c',
  'src/shader/manifest.c',
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
  'src/textures/font_atlas.c',
  'src/textures/gray_noise.c',
  'src/textures/rgba_noise.c',
  'src/textures/wood.c',
)

# Output sources
output_sources = files(
  'src/output/output.c',
  'src/output/span.c',
)

# Config sources
config_sources = files(
  'src/config/config.c',
  'src/config/shuffle.c',
  'src/config/vibe.c',
)

# Render sources
render_sources = files(
  'src/render/render.c',
)

# Image sources
image_sources = files(
  'src/image/image.c',
  'src/image/exif.c',
)

# Compositor abstraction layer sources
compositor_sources = files(
  'src/compositor/compositor_registry.c',
  'src/compositor/compositor_surface.c',
)

# Occlusion detection sources (always include dispatcher)
occlusion_sources = files(
  'src/occlusion/occlusion.c',
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
    'protocols/cursor-shape-v1-client-protocol.c',
    'protocols/tablet-v2-client-protocol.c',
    'protocols/wlr-foreign-toplevel-management-unstable-v1-client-protocol.c',
  )
  occlusion_sources += files(
    'src/compositor/backends/wayland/occlusion.c',
    'src/compositor/backends/wayland/occlusion_set.c',
    'src/compositor/backends/wayland/frame_watchdog.c',
    'src/compositor/backends/wayland/hyprland_coverage.c',
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
    'src/compositor/backends/x11/x11_geometry.c',
  )
  occlusion_sources += files('src/compositor/backends/x11/x11_occlusion.c')
endif

# =============================================================================
# Dependencies List
# =============================================================================

deps = [
  egl_dep,
  gl_dep,
  libpng_dep,
  libjpeg_dep,
  m_dep,
  thread_dep,
]

if has_wayland
  deps += [wayland_client_dep, wayland_egl_dep, wayland_cursor_dep]
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
  shader_sources +
  transition_sources +
  texture_sources +
  output_sources +
  config_sources +
  render_sources +
  image_sources +
  compositor_sources +
  occlusion_sources +
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

# Desktop integration
install_data('packaging/neowall.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('packaging/neowall.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

# systemd user unit (started by the user's graphical session)
install_data('packaging/systemd/neowall.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')

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
# Tests
# =============================================================================

# Headless unit tests for the pure helper functions. These link a small subset
# of the source (utils.c) and need no display server, so they run in CI via
# `meson test`. utils.c provides its own log_*() implementations, so no stubs
# are required; version.h is the generated header.
test_utils_exe = executable('test_utils',
  files('tests/test_utils.c', 'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('utils', test_utils_exe)

# Shadertoy source-compat transforms are GL-free and unit-tested directly.
test_compat_exe = executable('test_shadertoy_compat',
  files('tests/test_shadertoy_compat.c', 'src/shader/shadertoy_compat.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('shadertoy_compat', test_compat_exe)

# VIBE config parser is filesystem/GL-free in its string-parse path.
test_vibe_exe = executable('test_vibe',
  files('tests/test_vibe.c', 'src/config/vibe.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('vibe', test_vibe_exe)

# Foundation primitives (result / vec / defer) are header-only and dep-free.
test_foundation_exe = executable('test_foundation',
  files('tests/test_foundation.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('foundation', test_foundation_exe)

# Output refcount protocol — concurrency test, run under TSan in CI.
test_refcount_exe = executable('test_output_refcount',
  files('tests/test_output_refcount.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [thread_dep],
  build_by_default: false,
)

test('output_refcount', test_refcount_exe)

# X11 multi-monitor geometry — occlusion coverage + per-output mouse mapping.
# Pure integer math, no Xlib/display needed (see x11_geometry.c).
test_x11_geometry_exe = executable('test_x11_geometry',
  files('tests/test_x11_geometry.c',
        'src/compositor/backends/x11/x11_geometry.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('x11_geometry', test_x11_geometry_exe)

# Wayland toplevel output set — a toplevel can occupy several outputs at once.
# Pure pointer-set logic, no Wayland headers or display needed.
test_occlusion_set_exe = executable('test_occlusion_set',
  files('tests/test_occlusion_set.c',
        'src/compositor/backends/wayland/occlusion_set.c'),
  include_directories: [inc_dirs, inc_dirs_build,
                        include_directories('src/compositor/backends/wayland')],
  build_by_default: false,
)

test('occlusion_set', test_occlusion_set_exe)

# Reactive audio FFT — pure math, no audio thread / parec needed.
test_fft_exe = executable('test_reactive_fft',
  files('tests/test_reactive_fft.c', 'tests/reactive_fft_wrapper.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep],
  build_by_default: false,
)

test('reactive_fft', test_fft_exe)

# EXIF Orientation parser + RGBA transform (issue #48). Header-light: links
# only src/image/exif.c; no libjpeg/libpng or display server needed.
test_exif_exe = executable('test_image_exif',
  files('tests/test_image_exif.c', 'src/image/exif.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('image_exif', test_exif_exe)

# Fisher-Yates shuffle for cycle paths (issue #47). Pure data; links
# src/config/shuffle.c only.
test_shuffle_exe = executable('test_shuffle',
  files('tests/test_shuffle.c', 'src/config/shuffle.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('shuffle', test_shuffle_exe)

# Virtual-screen geometry: the bounding box a spanned shader draws into and the
# per-output gl_FragCoord offset (whose Y half is flipped). Pure integer math.
test_span_exe = executable('test_span',
  files('tests/test_span.c', 'src/output/span.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  # The span_rect literals initialise through .scale and leave device_w/device_h
  # zero on purpose: zero is the documented "device size unknown" fallback
  # (logical*scale), which is exactly what the scale-1 and integer-scale cases
  # must exercise. C guarantees the omitted fields are zeroed, so the
  # missing-field-initializers diagnostic is a false positive here.
  c_args: cc.get_supported_arguments('-Wno-missing-field-initializers'),
  build_by_default: false,
)

test('span', test_span_exe)

# =============================================================================
# Fuzzers (opt-in: -Dfuzz=true, clang only)
# =============================================================================

# libFuzzer harness over the VIBE config parser — untrusted user input, so it
# must not crash/leak on garbage. Not part of the default test set; build and
# run it manually under ASan/UBSan. See tests/fuzz_vibe.c for invocation.
if get_option('fuzz')
  if cc.get_id() != 'clang'
    error('Fuzzing requires clang (set CC=clang or use a native file)')
  endif
  fuzz_args = ['-fsanitize=fuzzer']
  executable('fuzz_vibe',
    files('tests/fuzz_vibe.c', 'src/config/vibe.c'),
    include_directories: [inc_dirs, inc_dirs_build],
    c_args: fuzz_args,
    link_args: fuzz_args,
    build_by_default: true,
  )

  # Fuzz the multipass GLSL parser — also untrusted user input.
  executable('fuzz_multipass',
    files(
      'tests/fuzz_multipass.c',
      'src/shader/multipass_parse.c',
    ),
    include_directories: [inc_dirs, inc_dirs_build],
    c_args: fuzz_args,
    link_args: fuzz_args,
    build_by_default: true,
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
  'Desktop OpenGL 3.3': has_gl33,
}, section: 'OpenGL Support')
