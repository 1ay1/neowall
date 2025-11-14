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

# Dependencies - Required
wayland_client = dependency('wayland-client', version: '>=1.20')
wayland_egl = dependency('wayland-egl')
wayland_protocols = dependency('wayland-protocols', version: '>=1.25')
egl = dependency('egl')
glesv2 = dependency('glesv2')
threads = dependency('threads')
libm = cc.find_library('m', required: true)

# Dependencies - Optional
libpng = dependency('libpng', required: false)
libjpeg = dependency('libjpeg', required: false)

if libpng.found()
  add_project_arguments('-DHAVE_PNG', language: 'c')
endif

if libjpeg.found()
  add_project_arguments('-DHAVE_JPEG', language: 'c')
endif

# Wayland scanner
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
proto_sources = []
proto_headers = []

foreach xml : all_protocols
  proto_sources += wayland_scanner_code.process(xml)
  proto_headers += wayland_scanner_client.process(xml)
endforeach

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
subdir('src/neowalld')
subdir('src/neowall')
subdir('src/neowall_tray')
subdir('data')

# Summary
summary({
  'Version': project_version,
  'C Compiler': cc.get_id(),
  'Wayland': wayland_client.version(),
  'EGL': egl.version(),
  'OpenGL ES': glesv2.version(),
  'PNG support': libpng.found(),
  'JPEG support': libjpeg.found(),
}, section: 'Configuration')

summary({
  'Daemon': 'neowalld',
  'Client': 'neowall',
  'System Tray': 'neowall_tray',
  'IPC': 'Unix domain sockets (JSON)',
}, section: 'Binaries')
