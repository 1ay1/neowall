# NeoWall Shader Library - Modular shader compilation
# Single source of truth used by both daemon and tray

shader_lib_sources = files(
  'shader_core.c',
  'shadertoy_compat.c',
  'shader_adaptation.c',
  'neowall_shader_api.c',
)

# Determine which OpenGL library to use
# Prefer OpenGL ES, fallback to desktop OpenGL
shader_gl_dep = glesv2
if not shader_gl_dep.found() or shader_gl_dep.type_name() == 'disabler'
  shader_gl_dep = dependency('gl', required: false)
  if shader_gl_dep.found()
    message('Using desktop OpenGL for shader library')
  else
    warning('No OpenGL library found - shader library will not work')
  endif
else
  message('Using OpenGL ES for shader library')
endif

# Shader library as a static library
shader_lib = static_library(
  'neowall_shader',
  shader_lib_sources,
  include_directories: inc_dirs,
  dependencies: [shader_gl_dep],
)

# Dependency for linking
shader_lib_dep = declare_dependency(
  link_with: shader_lib,
  include_directories: include_directories('.'),
)
