# NeoWall Shader Library - Modular shader compilation
# Single source of truth used by both daemon and tray

shader_lib_sources = files(
  'shader_core.c',
  'shadertoy_compat.c',
  'shader_adaptation.c',
  'neowall_shader_api.c',
)

# Shader library as a static library
shader_lib = static_library(
  'neowall_shader',
  shader_lib_sources,
  include_directories: inc_dirs,
  dependencies: [glesv2],
)

# Dependency for linking
shader_lib_dep = declare_dependency(
  link_with: shader_lib,
  include_directories: include_directories('.'),
)
