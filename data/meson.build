# NeoWall Data Files Installation

# Install SVG icons to pixmaps (for about dialog logo)
install_data(
  'icons/neowall.svg',
  install_dir: get_option('datadir') / 'pixmaps'
)

# Install PNG icons to pixmaps (for tray icon compatibility)
install_data(
  'icons/neowall-icon.png',
  install_dir: get_option('datadir') / 'pixmaps'
)

# Install PNG icons to hicolor icon theme (multiple sizes)
icon_sizes = ['16', '22', '24', '32', '48', '64', '128', '256']
foreach size : icon_sizes
  install_data(
    'icons/neowall-icon-' + size + '.png',
    rename: 'neowall-icon.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size + 'x' + size / 'apps'
  )
endforeach

# Install desktop entry
install_data(
  'neowall_tray.desktop',
  install_dir: get_option('datadir') / 'applications'
)

# Install to XDG autostart directory (optional, user can enable manually)
# Uncomment if you want tray to autostart by default
# install_data(
#   'neowall_tray.desktop',
#   install_dir: get_option('sysconfdir') / 'xdg' / 'autostart'
# )
