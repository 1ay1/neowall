# NeoWall Client (neowall)

# Client sources
neowall_sources = files(
  'main.c',
)

# Client dependencies
client_deps = [
  threads,
  ipc_dep,
]

# Build neowall executable
neowall = executable('neowall',
  neowall_sources,
  dependencies: client_deps,
  include_directories: inc_dirs,
  install: true,
  install_dir: get_option('bindir'),
  build_by_default: true,
)

# Also copy to builddir/bin for convenience
custom_target('neowall_bin_copy',
  input: neowall,
  output: 'neowall_copy_dummy',
  command: ['cp', '@INPUT@', meson.project_build_root() / 'bin' / 'neowall'],
  build_by_default: true,
  build_always_stale: true,
)
