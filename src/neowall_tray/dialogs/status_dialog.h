/* NeoWall Tray - Status Dialog Header
 * Beautiful status dialog showing daemon and wallpaper information
 */

#ifndef NEOWALL_TRAY_STATUS_DIALOG_H
#define NEOWALL_TRAY_STATUS_DIALOG_H

#include <gtk/gtk.h>

/**
 * Show the status dialog
 * Displays a beautiful, detailed status dialog with:
 * - Daemon state and process info
 * - Current wallpapers per output
 * - Cycling and shader state
 * - All in a clean, formatted layout
 */
void status_dialog_show(void);

#endif /* NEOWALL_TRAY_STATUS_DIALOG_H */