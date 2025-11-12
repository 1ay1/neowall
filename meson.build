# NeoWall - GPU-accelerated shader wallpapers for Wayland
# Meson build configuration

project('neowall', 'c',
  version: '0.3.0',
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

# Add version definition
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

# Include directories
inc_dirs = include_directories('include', 'src')

# Create bin directory in build root for convenience
run_command('mkdir', '-p', meson.project_build_root() / 'bin', check: false)

# Subdirectories
subdir('src/ipc')
subdir('src/neowalld')
subdir('src/neowall')

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
  'IPC': 'Unix domain sockets (JSON)',
}, section: 'Binaries')
