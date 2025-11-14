# NeoWall System Tray Application
# Component-based build configuration

# GTK3 and AppIndicator dependencies
gtk3 = dependency('gtk+-3.0', version: '>=3.22', required: true)
gtksourceview = dependency('gtksourceview-3.0', required: true)

# AppIndicator is Linux-specific (uses D-Bus and libdbusmenu)
# On other platforms, we'll use a simple GtkStatusIcon fallback
appindicator3 = dependency('appindicator3-0.1', required: false)
if not appindicator3.found()
  warning('AppIndicator not found - system tray will use fallback implementation')
  add_project_arguments('-DNO_APPINDICATOR', language: 'c')
endif



# Source files organized by component
tray_sources = [
  # Main entry point
  'main.c',

  # Common utilities
  'common/log.c',

  # Daemon component
  'daemon/daemon_check.c',
  'daemon/command_exec.c',

  # Menu component
  'menu/menu_builder.c',
  'menu/menu_callbacks.c',
  'menu/menu_items.c',

  # Indicator component
  'indicator/indicator.c',

  # Dialogs component
  'dialogs/dialogs.c',
  'dialogs/settings_dialog.c',
  'dialogs/about_dialog.c',
  'dialogs/status_dialog.c',
  'dialogs/shader_editor.c',

  # UI component
  'ui/ui_utils.c',
]

# Include directories
tray_inc = include_directories('.')

# Collect dependencies
tray_deps = [gtk3, gtksourceview, shader_lib_dep]
if appindicator3.found()
  tray_deps += [appindicator3]
endif

# Build executable
neowall_tray = executable(
  'neowall_tray',
  tray_sources,
  dependencies: tray_deps,
  include_directories: [tray_inc, inc_dirs, include_directories('../shader_lib')],
  install: true,
  install_dir: get_option('bindir'),
)

# Create symlink in build bin directory for convenience
custom_target('neowall_tray-symlink',
  output: 'neowall_tray-link',
  command: ['ln', '-sf',
            meson.current_build_dir() / 'neowall_tray',
            meson.project_build_root() / 'bin' / 'neowall_tray'],
  build_by_default: true,
  build_always_stale: true,
)
