# NeoWall - GPU-accelerated shader wallpapers for Wayland
# Meson build configuration

project('neowall', 'c',
  version: '0.4.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=true',
    'buildtype=release',
  ]
)

# Project information
project_name = 'neowall'
project_version = meson.project_version()

# Parse version components
version_parts = project_version.split('.')
version_major = version_parts[0]
version_minor = version_parts[1]
version_patch = version_parts.length() > 2 ? version_parts[2] : '0'

# Build information
build_date = run_command('date', '+%Y-%m-%d', check: false).stdout().strip()
build_type = get_option('buildtype')
copyright_year = run_command('date', '+%Y', check: false).stdout().strip()

# Generate version header from template
version_conf = configuration_data()
version_conf.set('VERSION', project_version)
version_conf.set('VERSION_MAJOR', version_major)
version_conf.set('VERSION_MINOR', version_minor)
version_conf.set('VERSION_PATCH', version_patch)
version_conf.set('BUILD_DATE', build_date)
version_conf.set('BUILD_TYPE', build_type)
version_conf.set('COPYRIGHT_YEAR', copyright_year)

version_h = configure_file(
  input: 'include/version/version.h.in',
  output: 'version.h',
  configuration: version_conf,
  install: true,
  install_dir: get_option('includedir') / 'neowall'
)

# Create dependency for version header
version_dep = declare_dependency(
  sources: version_h,
  include_directories: include_directories('.')
)

# Add version definition (keep for backwards compatibility)
add_project_arguments('-DNEOWALL_VERSION="@0@"'.format(project_version), language: 'c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# Compiler
cc = meson.get_compiler('c')

# Dependencies - Always required
threads = dependency('threads')
libm = cc.find_library('m', required: true)

# Backend-specific dependencies (only for neowalld)
# Check if we're building the daemon
build_daemon = get_option('build_daemon')

if build_daemon
  wayland_client = dependency('wayland-client', version: '>=1.20')
  wayland_egl = dependency('wayland-egl')
  wayland_protocols = dependency('wayland-protocols', version: '>=1.25')
  egl = dependency('egl')
  glesv2 = dependency('glesv2')
else
  # Dummy variables for when daemon is not built
  wayland_client = disabler()
  wayland_egl = disabler()
  wayland_protocols = disabler()
  egl = disabler()
  glesv2 = dependency('glesv2', required: false)
  if not glesv2.found()
    # Fallback to OpenGL for non-Wayland platforms
    glesv2 = dependency('gl', required: false)
  endif
  if not glesv2.found()
    warning('OpenGL ES or OpenGL not found - shader editor will not work')
  endif
endif

# Dependencies - Optional
libpng = dependency('libpng', required: false)
libjpeg = dependency('libjpeg', required: false)

if libpng.found()
  add_project_arguments('-DHAVE_PNG', language: 'c')
endif

if libjpeg.found()
  add_project_arguments('-DHAVE_JPEG', language: 'c')
endif

# Wayland protocols (only if building daemon)
proto_sources = []
proto_headers = []

if build_daemon
  wayland_scanner = find_program('wayland-scanner')
  wl_protocol_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')

  # Protocol generation
  wayland_scanner_code = generator(
    wayland_scanner,
    output: '@BASENAME@-client-protocol.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@']
  )

  wayland_scanner_client = generator(
    wayland_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@']
  )

  # Protocols
  protocol_files = [
    'protocols/plasma-shell.xml',
    'protocols/tearing-control-v1.xml',
  ]

  # Generate stable protocols from wayland-protocols
  xdg_shell_xml = join_paths(wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml')
  viewporter_xml = join_paths(wl_protocol_dir, 'stable/viewporter/viewporter.xml')

  # Generate unstable protocols
  xdg_output_xml = join_paths(wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml')
  wlr_layer_shell_xml = 'protocols/wlr-layer-shell-unstable-v1.xml'

  # Check if wlr-layer-shell exists locally, otherwise skip
  fs = import('fs')
  if not fs.exists(wlr_layer_shell_xml)
    warning('wlr-layer-shell-unstable-v1.xml not found in protocols/, layer-shell support may be limited')
    wlr_layer_shell_xml = ''
  endif

  # Collect all protocol XMLs
  all_protocols = protocol_files + [xdg_shell_xml, viewporter_xml, xdg_output_xml]
  if wlr_layer_shell_xml != ''
    all_protocols += [wlr_layer_shell_xml]
  endif

  # Generate protocol sources
  foreach xml : all_protocols
    proto_sources += wayland_scanner_code.process(xml)
    proto_headers += wayland_scanner_client.process(xml)
  endforeach
endif

# Include directories (add builddir for generated headers)
inc_dirs = [
  include_directories('include', 'src'),
  include_directories('.', is_system: false)
]

# Create bin directory in build root for convenience
run_command('mkdir', '-p', meson.project_build_root() / 'bin', check: false)

# Subdirectories
subdir('src/shader_lib')
subdir('src/ipc')
if build_daemon
  subdir('src/neowalld')
  subdir('data')
endif
subdir('src/neowall')
subdir('src/neowall_tray')

# Summary
summary_config = {
  'Version': project_version,
  'C Compiler': cc.get_id(),
  'PNG support': libpng.found(),
  'JPEG support': libjpeg.found(),
}

if build_daemon
  summary_config += {
    'Wayland': wayland_client.version(),
    'EGL': egl.version(),
    'OpenGL ES': glesv2.version(),
  }
else
  summary_config += {
    'OpenGL': glesv2.found() ? 'found' : 'not found',
  }
endif

summary(summary_config, section: 'Configuration')

binaries_built = {
  'Client': 'neowall',
  'System Tray': 'neowall_tray',
}

if build_daemon
  binaries_built = {'Daemon': 'neowalld'} + binaries_built
  binaries_built += {'IPC': 'Unix domain sockets (JSON)'}
endif

summary(binaries_built, section: 'Binaries')
