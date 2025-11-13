# NeoWall Daemon (neowalld)

# Protocol headers and sources are available from parent
proto_headers_dep = declare_dependency(
  sources: proto_headers,
)

# Collect all daemon sources
neowalld_sources = files(
  'main.c',
  'eventloop.c',
  'shader.c',
  'shader_adaptation.c',
  'shadertoy_compat.c',
  'utils.c',
)

# EGL subsystem
egl_sources = files(
  'egl/capability.c',
  'egl/egl_core.c',
  'egl/egl_v10.c',
  'egl/egl_v11.c',
  'egl/egl_v12.c',
  'egl/egl_v13.c',
  'egl/egl_v14.c',
  'egl/egl_v15.c',
  'egl/gles_v10.c',
  'egl/gles_v11.c',
  'egl/gles_v20.c',
  'egl/gles_v30.c',
  'egl/gles_v31.c',
  'egl/gles_v32.c',
)

# Compositor subsystem
compositor_sources = files(
  'compositor/compositor_registry.c',
  'compositor/compositor_surface.c',
  'compositor/backends/wayland/wayland_core.c',
  'compositor/backends/wayland/compositors/wlr_layer_shell.c',
  'compositor/backends/wayland/compositors/kde_plasma.c',
  'compositor/backends/wayland/compositors/gnome_shell.c',
  'compositor/backends/wayland/compositors/fallback.c',
)

# Config subsystem
config_sources = files(
  'config/config.c',
  'config/vibe.c',
)

# Command handlers
commands_sources = files(
  'commands/commands.c',
  'commands/registry.c',
  'commands/output_commands.c',
  'commands/config_commands.c',
)

# Output subsystem
output_sources = files(
  'output/output.c',
)

# Render subsystem
render_sources = files(
  'render/render.c',
)

# Image subsystem
image_sources = files(
  'image/image.c',
)

# Transitions
transitions_sources = files(
  'transitions/transitions.c',
  'transitions/fade.c',
  'transitions/slide.c',
  'transitions/pixelate.c',
  'transitions/glitch.c',
)

# Textures
textures_sources = files(
  'textures/abstract.c',
  'textures/blue_noise.c',
  'textures/gray_noise.c',
  'textures/rgba_noise.c',
  'textures/wood.c',
)

# Combine all daemon sources (including generated protocol sources)
all_daemon_sources = [
  neowalld_sources,
  egl_sources,
  compositor_sources,
  config_sources,
  commands_sources,
  output_sources,
  render_sources,
  image_sources,
  transitions_sources,
  textures_sources,
  proto_sources,
]

# Daemon dependencies (include protocol headers)
daemon_deps = [
  wayland_client,
  wayland_egl,
  egl,
  glesv2,
  threads,
  libm,
  ipc_dep,
  proto_headers_dep,
]

if libpng.found()
  daemon_deps += libpng
endif

if libjpeg.found()
  daemon_deps += libjpeg
endif

# Build neowalld executable
neowalld = executable('neowalld',
  all_daemon_sources,
  dependencies: daemon_deps,
  include_directories: inc_dirs,
  install: true,
  install_dir: get_option('bindir'),
  build_by_default: true,
)

# Also copy to builddir/bin for convenience
custom_target('neowalld_bin_copy',
  input: neowalld,
  output: 'neowalld_copy_dummy',
  command: ['cp', '@INPUT@', meson.project_build_root() / 'bin' / 'neowalld'],
  build_by_default: true,
  build_always_stale: true,
)
