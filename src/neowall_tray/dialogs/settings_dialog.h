/* NeoWall Tray - Settings Dialog
 * User-friendly configuration dialog for common settings
 */

#ifndef NEOWALL_TRAY_SETTINGS_DIALOG_H
#define NEOWALL_TRAY_SETTINGS_DIALOG_H

#include <gtk/gtk.h>

/**
 * Show the settings/preferences dialog
 * Displays common configuration options in a user-friendly GUI with tabs:
 * - Wallpaper settings (folder, duration, display mode)
 * - Animation settings (speed, auto-pause)
 * - Advanced (direct config file access)
 */
void settings_dialog_show(void);

#endif /* NEOWALL_TRAY_SETTINGS_DIALOG_H */