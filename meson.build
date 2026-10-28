# NeoWall - GPU-Accelerated Live Wallpapers for Wayland & X11

project('neowall', 'c',
  version: '0.8.0',
  license: 'MIT',
  meson_version: '>= 0.64.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'optimization=2',
  ],
)

# Warnings are errors in CI (-Dwerror=true), not by default: a new compiler
# release shouldn't break a distro build.

cc = meson.get_compiler('c')

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

# Wayland dependencies. Protocol code is generated at build time by
# wayland-scanner from wayland-protocols plus the XML vendored in protocols/.
wayland_client_dep = dependency('wayland-client', required: wayland_backend_option)
wayland_egl_dep = dependency('wayland-egl', required: wayland_backend_option)
wayland_cursor_dep = dependency('wayland-cursor', required: wayland_backend_option)
wayland_protocols_dep = dependency('wayland-protocols', version: '>= 1.31',
  required: wayland_backend_option)
wayland_scanner_dep = dependency('wayland-scanner', native: true,
  required: wayland_backend_option)

has_wayland_deps = wayland_client_dep.found() and wayland_egl_dep.found() and \
  wayland_cursor_dep.found() and wayland_protocols_dep.found() and \
  wayland_scanner_dep.found()

# xkbcommon: keymap + keysym translation for terminal-wallpaper keyboard input.
# Optional — without it the terminal is display+mouse only (no typing).
xkbcommon_dep = dependency('xkbcommon', required: false)
if xkbcommon_dep.found()
  add_project_arguments('-DNEOWALL_HAVE_XKB=1', language: 'c')
endif

# Determine if Wayland backend should be enabled
if wayland_backend_option.disabled()
  has_wayland = false
  message('Wayland backend disabled by user')
elif has_wayland_deps
  has_wayland = true
  message('Wayland backend available')
else
  has_wayland = false
  message('Wayland backend not available (needs wayland-client, wayland-egl, wayland-cursor, wayland-protocols, wayland-scanner)')
endif

# X11 dependencies
x11_dep = dependency('x11', required: x11_backend_option)
xrandr_dep = dependency('xrandr', required: x11_backend_option)

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

# =============================================================================
# Generate version.h from version.h.in
# =============================================================================

version_parts = meson.project_version().split('.')

version_h = configure_file(
  input: 'include/version.h.in',
  output: 'version.h',
  configuration: {
    'VERSION': meson.project_version(),
    'VERSION_MAJOR': version_parts[0],
    'VERSION_MINOR': version_parts.length() > 1 ? version_parts[1] : '0',
    'VERSION_PATCH': version_parts.length() > 2 ? version_parts[2] : '0',
    'DATADIR': get_option('prefix') / get_option('datadir') / 'neowall',
    'SYSCONFDIR': get_option('prefix') / get_option('sysconfdir'),
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
  'protocols',       # vendored protocol XML; generated headers land in the build dir
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
  'src/app.c',
  'src/ipc.c',
  'src/sd_notify.c',
  'src/runtime/rt.c',
  'src/utils.c',
  'src/clock.c',
  'src/watch.c',
)

# EGL sources (desktop OpenGL 3.3)
egl_sources = files(
  'src/egl/egl_core.c',
  'src/egl/gpu_health.c',
)

# Data-plane sources (open provider registry; see
# docs/architecture/SHADER_ENGINE_V2.md). GL-free and dependency-free.
source_sources = files(
  'src/source/source.c',
  'src/source/builtin_value.c',
)

# Reactive signals as providers. Split out because it pulls in reactive.h,
# unlike the rest of the source layer which is dependency-free.
source_reactive_sources = files(
  'src/source/builtin_reactive.c',
)

# GL object layer: programs, textures, render targets. No shader semantics.
# Bottom of the v2 engine (docs/architecture/SHADER_ENGINE_V2.md).
gfx_sources = files(
  'src/gfx/gfx.c',
)

# Render graph: N passes, M bindings, topological order. Replaces the fixed
# 5-pass / 4-channel arrays of the old multipass_shader_t.
graph_sources = files(
  'src/graph/graph.c',
)

# Shader engine sources
shader_sources = files(
  'src/shader/shader_core.c',
  'src/shader/shader_state.c',
  'src/shader/shader_error_log.c',
  'src/shader/shader_diag.c',
  'src/shader/shader_docs.c',
  'src/shader/shader_multipass.c',
  'src/shader/program_cache.c',
  'src/shader/multipass_parse.c',
  'src/shader/shadertoy_compat.c',
  'src/shader/glsl_shadow.c',
  'src/shader/shader_scaffold.c',
  'src/shader/shader_clock.c',
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
  'src/output/frame_clock.c',
  'src/output/span.c',
)

# Config sources
config_sources = files(
  'src/config/config.c',
  'src/config/shuffle.c',
  'src/config/vibe_impl.c',  # compiles vendored single-header libvibe (VIBE_IMPLEMENTATION)
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

# Terminal emulator wallpaper source (in-tree PTY + VT parser; no external dep,
# uses glibc <pty.h>/forkpty). Gated behind -Dterminal.
terminal_option = get_option('terminal')
terminal_sources = []
has_terminal = false
if not terminal_option.disabled()
  fontconfig_dep = dependency('fontconfig', required: false)
  # forkpty lives in libutil on glibc; an explicitly enabled feature must fail
  # configuration when it is unavailable.
  util_dep = cc.find_library('util', required: terminal_option)
  if util_dep.found()
    has_terminal = true
    add_project_arguments('-DNEOWALL_HAVE_TERMINAL=1', language: 'c')
    terminal_sources = files(
      'src/terminal/vtparse.c',
      'src/terminal/screen.c',
      'src/terminal/pty.c',
      'src/terminal/term_render.c',
      'src/terminal/glyph_synth.c',
    )
    # glyph_atlas.c is the sole TU that includes the vendored public-domain
    # stb_truetype.h, and cbdt.c is the sole TU that includes stb_image.h —
    # neither is -Wpedantic/-Werror clean. Compile them in their own static
    # library with relaxed warnings so the strict project-wide gate
    # (warning_level=3, werror=true) still governs all our own code.
    terminal_glyph_lib = static_library('terminal_glyph',
      files('src/terminal/glyph_atlas.c',
            'src/terminal/cbdt.c'),
      include_directories: [inc_dirs, inc_dirs_build,
                            include_directories('src/terminal')],
      dependencies: fontconfig_dep,
      c_args: cc.get_supported_arguments(
        '-Wno-error', '-w') +
        (fontconfig_dep.found() ? ['-DNEOWALL_HAVE_FONTCONFIG=1'] : []),
      override_options: ['warning_level=0', 'werror=false'],
    )
  endif
endif

# Wayland protocol code, generated by wayland-scanner.
wayland_protocol_sources = []
if has_wayland
  wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')
  fs = import('fs')
  wayland_scanner = find_program(
    wayland_scanner_dep.get_variable('wayland_scanner'), native: true)
  wl_protocols = {
    'xdg-shell':          wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
    'viewporter':         wl_protocol_dir / 'stable/viewporter/viewporter.xml',
    'presentation-time':  wl_protocol_dir / 'stable/presentation-time/presentation-time.xml',
    'tablet-v2':          fs.exists(wl_protocol_dir / 'stable/tablet/tablet-v2.xml') ? \
                            wl_protocol_dir / 'stable/tablet/tablet-v2.xml' : \
                            wl_protocol_dir / 'unstable/tablet/tablet-unstable-v2.xml',
    'fractional-scale-v1': wl_protocol_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
    'tearing-control-v1': wl_protocol_dir / 'staging/tearing-control/tearing-control-v1.xml',
    'cursor-shape-v1':    'protocols/cursor-shape-v1.xml',   # vendored: 1.32+, Debian 12 has 1.31
    'xdg-output-unstable-v1': wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
    # Not in wayland-protocols; vendored unchanged from upstream.
    'wlr-layer-shell-unstable-v1': 'protocols/wlr-layer-shell-unstable-v1.xml',
    'wlr-foreign-toplevel-management-unstable-v1':
      'protocols/wlr-foreign-toplevel-management-unstable-v1.xml',
    'plasma-shell':       'protocols/plasma-shell.xml',
  }
  foreach name, xml : wl_protocols
    wayland_protocol_sources += custom_target(name + '-client-protocol.h',
      input: xml, output: name + '-client-protocol.h',
      command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    wayland_protocol_sources += custom_target(name + '-protocol.c',
      input: xml, output: name + '-client-protocol.c',
      command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  endforeach
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
    'src/compositor/backends/wayland/compositors/fallback.c',
  )
endif

# X11 backend sources (conditional)
x11_sources = []
if has_x11
  x11_sources = files(
    'src/compositor/backends/x11/x11_core.c',
    'src/compositor/backends/x11/x11_geometry.c',
    'src/compositor/backends/x11/x11_wm_detect.c',
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
  if xkbcommon_dep.found()
    deps += [xkbcommon_dep]
  endif
endif

if has_x11
  deps += [x11_dep, xrandr_dep]
endif

if has_terminal
  deps += [util_dep]
  if fontconfig_dep.found()
    deps += [fontconfig_dep]
  endif
endif

# =============================================================================
# Combine All Sources
# =============================================================================

all_sources = (
  core_sources +
  egl_sources +
  gfx_sources +
  graph_sources +
  source_sources +
  source_reactive_sources +
  shader_sources +
  transition_sources +
  texture_sources +
  output_sources +
  config_sources +
  render_sources +
  image_sources +
  compositor_sources +
  occlusion_sources +
  terminal_sources +
  wayland_protocol_sources +
  wayland_sources +
  x11_sources
)

# =============================================================================
# Build Target
# =============================================================================

neowall_exe = executable('neowall',
  all_sources,
  include_directories: has_terminal
    ? [inc_dirs, inc_dirs_build, include_directories('src/terminal')]
    : [inc_dirs, inc_dirs_build],
  dependencies: deps,
  link_with: has_terminal ? [terminal_glyph_lib] : [],
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
doc_dir = get_option('datadir') / 'doc' / 'neowall'
install_data('README.md', 'LICENSE', 'CHANGELOG.md', 'config/docs/CONFIG.md',
  install_dir: doc_dir)

# Desktop integration, named by the AppStream id
app_id = 'io.github._1ay1.neowall'
install_data('packaging' / app_id + '.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('packaging' / app_id + '.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

paths_conf = configuration_data()
paths_conf.set('VERSION', meson.project_version())
paths_conf.set('DATADIR', get_option('prefix') / get_option('datadir') / 'neowall')
paths_conf.set('SYSCONFDIR', get_option('prefix') / get_option('sysconfdir'))
paths_conf.set('DOCDIR', get_option('prefix') / doc_dir)

metainfo = configure_file(
  input: 'packaging' / app_id + '.metainfo.xml.in',
  output: app_id + '.metainfo.xml',
  configuration: paths_conf,
)
install_data(metainfo, install_dir: get_option('datadir') / 'metainfo')

# Man pages: plain roff, so no doc tool is needed at build time.
foreach page : ['neowall.1', 'neowall.vibe.5']
  install_man(configure_file(
    input: 'docs/man' / page + '.in',
    output: page,
    configuration: paths_conf,
  ))
endforeach

# systemd user unit (started by the user's graphical session). Configure the
# executable path so non-/usr prefixes work too.
service_conf = configuration_data()
service_conf.set('BINDIR', get_option('prefix') / get_option('bindir'))
configured_service = configure_file(
  input: 'packaging/systemd/neowall.service',
  output: 'neowall.service',
  configuration: service_conf,
)
# Prefer systemd's own user unit dir (correct on every distro), else the
# conventional path under our prefix.
systemd_dep = dependency('systemd', required: false)
user_unit_dir = systemd_dep.found() ? \
  systemd_dep.get_variable('systemd_user_unit_dir',
    pkgconfig_define: ['prefix', get_option('prefix')]) : \
  get_option('prefix') / 'lib' / 'systemd' / 'user'
install_data(configured_service, install_dir: user_unit_dir)

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

# The runtime loop on its own: timers keep phase and never fire early, posted
# messages all arrive in order, signals become messages, an idle loop sleeps.
test_runtime_exe = executable('test_runtime',
  files('tests/test_runtime.c', 'src/runtime/rt.c', 'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('runtime', test_runtime_exe)

# Spawned children (terminal shell, parec, nvidia-smi, exec sources) must not
# inherit the daemon's blocked signals or ignored SIGPIPE.
test_child_signals_exe = executable('test_child_signals',
  files('tests/test_child_signals.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [thread_dep],
  build_by_default: false,
)

test('child_signals', test_child_signals_exe)

test_frame_clock_exe = executable('test_frame_clock',
  files('tests/test_frame_clock.c', 'src/output/frame_clock.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('frame_clock', test_frame_clock_exe)

test_shader_diag_exe = executable('test_shader_diag',
  files('tests/test_shader_diag.c', 'src/shader/shader_diag.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('shader_diag', test_shader_diag_exe)

# The control socket end to end: request/reply, a stuck client can't wedge it,
# oversize and malformed lines are refused, a live socket isn't stolen and a
# stale one is replaced.
test_ipc_exe = executable('test_ipc',
  files('tests/test_ipc.c', 'src/ipc.c', 'src/runtime/rt.c', 'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('ipc', test_ipc_exe)

# The systemd notify protocol, path and abstract sockets, without libsystemd.
test('sd_notify', executable('test_sd_notify',
  files('tests/test_sd_notify.c', 'src/sd_notify.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
))

# GPU health: reset-status classification and the swap-failure streak.
test('gpu_health', executable('test_gpu_health',
  files('tests/test_gpu_health.c', 'src/egl/gpu_health.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [egl_dep, gl_dep],
  build_by_default: false,
))

# The adaptive resolution controller with a simulated 60 Hz clock and stubbed
# GPU timer queries: a cheap shader must stay at full resolution, a slow one
# must still be scaled down, and it must recover when the load goes away.
test_adaptive_exe = executable('test_adaptive_scale',
  files('tests/test_adaptive_scale.c', 'src/shader/adaptive_scale.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep],
  build_by_default: false,
)

test('adaptive_scale', test_adaptive_exe)

# Every controller decision, hashed: modes x quantized x eight load shapes.
# -ffp-contract=off keeps the float math (and so the hash) the same on
# targets that fuse multiply-adds by default, like aarch64.
test('adaptive_golden', executable('test_adaptive_golden',
  files('tests/test_adaptive_golden.c', 'src/shader/adaptive_scale.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  c_args: cc.get_supported_arguments('-ffp-contract=off'),
  dependencies: [m_dep],
  build_by_default: false,
))

# The open data plane: provider registry, spec grammar, interval scheduling,
# and the occlusion contract (a hidden source must not poll, spawn, or sample).
# No GL and no display server, so it runs headless in CI.
test_source_exe = executable('test_source',
  files('tests/test_source.c') + source_sources,
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('source', test_source_exe)

# The 24 hardcoded uniform_bind_t arms, ported to table-driven providers.
# Pins that every legacy bind name still resolves (a dropped one reads 0
# silently rather than crashing) and that the occlusion contract survived.
test_source_reactive_exe = executable('test_source_reactive',
  files('tests/test_source_reactive.c', 'src/shader/reactive.c', 'src/utils.c',
        'src/clock.c')
    + source_sources + source_reactive_sources,
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('source_reactive', test_source_reactive_exe)

# gfx feedback/ping-pong logic: which half of the pair is readable this frame.
# Reading the texture you are writing is undefined on most drivers rather than
# an error, so it shows up as flicker, not a crash. Linked against a GL stub so
# the invariant is checkable with no display server.
test_gfx_exe = executable('test_gfx',
  files('tests/test_gfx.c', 'tests/gl_stub.c', 'src/utils.c') + gfx_sources,
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('gfx', test_gfx_exe)

# Render graph: feedback-vs-cycle (a self-edge is legal, A<->B is not) and the
# ordering contract every downstream layer assumes. Also pins that N passes and
# M bindings work past the old 5-pass / 4-channel ceilings.
test_graph_exe = executable('test_graph',
  files('tests/test_graph.c', 'tests/gl_stub.c', 'src/utils.c')
    + gfx_sources + graph_sources + source_sources,
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('graph', test_graph_exe)

# Shadertoy source-compat transforms are GL-free and unit-tested directly.
test_compat_exe = executable('test_shadertoy_compat',
  files('tests/test_shadertoy_compat.c', 'src/shader/shadertoy_compat.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('shadertoy_compat', test_compat_exe)

# Shader-authoring plumbing: pass-marker detection, the virtual shader clock,
# and the `#pragma neowall requires` floor. All GL-free, so they run in CI.
test_authoring_exe = executable('test_shader_authoring',
  files('tests/test_shader_authoring.c',
        'src/shader/multipass_parse.c',
        'src/clock.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('shader_authoring', test_authoring_exe)

# The GLSL namespace-collision guard (issue #82): the user-definition scanner,
# plus the invariant that every unprefixed std-lib name is suppressible.
# Pure string work, no GL, so it runs headless in CI.
test_glsl_shadow_exe = executable('test_glsl_shadow',
  files('tests/test_glsl_shadow.c',
        'src/shader/glsl_shadow.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('glsl_shadow', test_glsl_shadow_exe)

# `neowall new` scaffolding: name handling, template wiring, and the refusal
# paths (no clobbering, no path traversal). Writes only under a scratch HOME.
test_scaffold_exe = executable('test_shader_scaffold',
  files('tests/test_shader_scaffold.c',
        'src/shader/shader_scaffold.c',
        'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('shader_scaffold', test_scaffold_exe)

# Shader-clock rebase policy: the clock may only jump while nobody can see it.
# Guards the "wallpaper jumps back in time" regression. No GL, no compositor.
test_shader_clock_exe = executable('test_shader_clock',
  files('tests/test_shader_clock.c',
        'src/shader/shader_clock.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('shader_clock', test_shader_clock_exe)

# Documentation drift: every shader the README advertises must actually ship.
# Guards issue #79, where the README sent users to shaders that did not exist.
test('documented_shaders',
  find_program('tests/verify-documented-shaders.sh'),
  args: [meson.project_source_root()],
)

# VIBE config parser is filesystem/GL-free in its string-parse path.
test_vibe_exe = executable('test_vibe',
  files('tests/test_vibe.c', 'src/config/vibe_impl.c'),
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

# Alias-safe config field setter — guards the self-assignment bug that blanked
# term_cmd/shader_path/path on every deferred wallpaper re-apply.
test_config_str_exe = executable('test_config_str_set',
  files('tests/test_config_str_set.c', 'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep],
  build_by_default: false,
)

test('config_str_set', test_config_str_exe)

# config.c itself, headless: parsing, validation, default/per-output precedence
# and reload keeping the last good config. output_apply_config is stubbed.
test('config', executable('test_config',
  files('tests/test_config.c', 'src/config/config.c', 'src/config/shuffle.c',
        'src/config/vibe_impl.c', 'src/utils.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [m_dep, thread_dep, egl_dep, gl_dep],
  build_by_default: false,
))

# X11 multi-monitor geometry — occlusion coverage + per-output mouse mapping.
# Pure integer math, no Xlib/display needed (see x11_geometry.c).
test_x11_geometry_exe = executable('test_x11_geometry',
  files('tests/test_x11_geometry.c',
        'src/compositor/backends/x11/x11_geometry.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('x11_geometry', test_x11_geometry_exe)

# X11 window-manager detection — the _NET_SUPPORTING_WM_CHECK decision matrix,
# the NEOWALL_X11_OVERRIDE_REDIRECT contract, and the heal-on-late-WM edge.
# Pure decision logic, no Xlib/display needed (see x11_wm_detect.c).
test_x11_wm_detect_exe = executable('test_x11_wm_detect',
  files('tests/test_x11_wm_detect.c',
        'src/compositor/backends/x11/x11_wm_detect.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  build_by_default: false,
)

test('x11_wm_detect', test_x11_wm_detect_exe)

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

# Image scaling/fitting regressions: image_center_pad heap overflow on
# mixed-axis sources, and every wallpaper mode reaching exact display size
# (the renderer draws a fullscreen quad and depends on that invariant).
# Includes src/image/image.c directly for its static helpers, so it needs the
# codec deps and utils.c/exif.c for logging + EXIF symbols.
test_image_scale_exe = executable('test_image_scale',
  files('tests/test_image_scale.c', 'src/utils.c', 'src/image/exif.c'),
  include_directories: [inc_dirs, inc_dirs_build],
  dependencies: [libpng_dep, libjpeg_dep, m_dep, thread_dep],
  build_by_default: false,
)

test('image_scale', test_image_scale_exe)

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

# In-tree terminal emulator: VT parser + screen model. Headless, no PTY/GPU —
# feeds raw escape sequences and asserts the cell grid. Only built when the
# terminal feature is enabled.
if has_terminal
  test_terminal_exe = executable('test_terminal_screen',
    files('tests/test_terminal_screen.c',
          'src/terminal/screen.c',
          'src/terminal/vtparse.c'),
    include_directories: [inc_dirs, inc_dirs_build,
                          include_directories('src/terminal')],
    build_by_default: false,
  )
  test('terminal_screen', test_terminal_exe)

  # The terminal's output hook on a real PTY: the reader thread calls it when
  # the child writes, and not while it's quiet. A settled terminal sleeps and
  # relies on this to wake.
  test_terminal_wake_exe = executable('test_terminal_wake',
    files('tests/test_terminal_wake.c',
          'src/terminal/pty.c',
          'src/terminal/screen.c',
          'src/terminal/vtparse.c'),
    include_directories: [inc_dirs, inc_dirs_build,
                          include_directories('src/terminal')],
    dependencies: [util_dep, thread_dep],
    build_by_default: false,
  )
  test('terminal_wake', test_terminal_wake_exe)

  # Glyph atlas: real font rasterization (stb_truetype). Links the relaxed-
  # warning glyph lib. SKIPs (exit 77) if no system monospace font is present.
  test_glyph_exe = executable('test_glyph_atlas',
    files('tests/test_glyph_atlas.c', 'src/terminal/glyph_synth.c'),
    include_directories: [inc_dirs, inc_dirs_build,
                          include_directories('src/terminal')],
    link_with: [terminal_glyph_lib],
    dependencies: [m_dep] + (fontconfig_dep.found() ? [fontconfig_dep] : []),
    build_by_default: false,
  )
  test('glyph_atlas', test_glyph_exe)

  # Procedural box/block/braille synth: pure CPU coverage bitmap, no font/GPU.
  test_glyph_synth_exe = executable('test_glyph_synth',
    files('tests/test_glyph_synth.c', 'src/terminal/glyph_synth.c'),
    include_directories: [inc_dirs, inc_dirs_build,
                          include_directories('src/terminal')],
    dependencies: [m_dep],
    build_by_default: false,
  )
  test('glyph_synth', test_glyph_synth_exe)
endif

# The shader engine rendering for real: a GL 3.3 context with no display
# (Mesa surfaceless + llvmpipe), pixels read back and checked, and every
# shipped shader compiled and drawn. Skips (77) without software GL.
test('multipass_gl', executable('test_multipass_gl',
  files('tests/test_multipass_gl.c', 'tests/gl_headless.c', 'src/utils.c',
        'src/clock.c', 'src/config/vibe_impl.c') +
    shader_sources + texture_sources + image_sources + source_sources +
    gfx_sources + graph_sources + terminal_sources,
  include_directories: has_terminal
    ? [inc_dirs, inc_dirs_build, include_directories('src/terminal', 'tests')]
    : [inc_dirs, inc_dirs_build, include_directories('tests')],
  dependencies: deps,
  link_with: has_terminal ? [terminal_glyph_lib] : [],
  build_by_default: false,
), env: {'NW_SHADER_DIR': meson.project_source_root() / 'examples/shaders'},
   timeout: 300)

# =============================================================================
# Fuzzers
# =============================================================================

# Each harness parses untrusted input. They always build against
# tests/fuzz_driver.c (a small mutation driver) and run briefly as part of
# `meson test --suite fuzz`, so they can't rot. With -Dfuzz=true under clang
# they also build as real libFuzzer binaries for long runs:
#   ./build/fuzz_terminal -max_total_time=600 corpus/
fuzz_harnesses = {
  'vibe':      [files('tests/fuzz_vibe.c', 'src/config/vibe_impl.c'), 'config'],
  'multipass': [files('tests/fuzz_multipass.c', 'src/shader/multipass_parse.c',
                      'src/utils.c'), 'examples/shaders'],
  'exif':      [files('tests/fuzz_exif.c', 'src/image/exif.c'), ''],
}
if has_terminal
  fuzz_harnesses += {
    'terminal': [files('tests/fuzz_terminal.c', 'src/terminal/screen.c',
                       'src/terminal/vtparse.c'), ''],
  }
endif

if get_option('fuzz') and cc.get_id() != 'clang'
  error('-Dfuzz=true needs clang (libFuzzer); the driver smoke tests build anyway')
endif

foreach name, h : fuzz_harnesses
  drv = executable('fuzz_' + name + '_driver',
    h[0] + files('tests/fuzz_driver.c'),
    include_directories: [inc_dirs, inc_dirs_build, include_directories('src/terminal')],
    dependencies: [thread_dep],
    build_by_default: false,
  )
  seed_args = ['20000']
  if h[1] != ''
    seed_args += [meson.project_source_root() / h[1]]
  endif
  test('fuzz_' + name, drv, args: seed_args, suite: 'fuzz', timeout: 120)

  if get_option('fuzz')
    executable('fuzz_' + name,
      h[0],
      include_directories: [inc_dirs, inc_dirs_build, include_directories('src/terminal')],
      dependencies: [thread_dep],
      c_args: ['-fsanitize=fuzzer'],
      link_args: ['-fsanitize=fuzzer'],
    )
  endif
endforeach

# =============================================================================
# Summary
# =============================================================================

summary({
  'Version': meson.project_version(),
  'Prefix': get_option('prefix'),
}, section: 'Project')

summary({
  'Wayland Backend': has_wayland,
  'X11 Backend': has_x11,
}, section: 'Backends')

summary({
  'Desktop OpenGL 3.3': has_gl33,
}, section: 'OpenGL Support')
