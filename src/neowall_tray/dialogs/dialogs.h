/* NeoWall Tray - Dialogs Component
 * Provides dialog windows for status, info, and user interactions
 */

#ifndef NEOWALL_TRAY_DIALOGS_H
#define NEOWALL_TRAY_DIALOGS_H

#include <gtk/gtk.h>

/**
 * Show the daemon status dialog
 * Displays detailed information about the daemon state
 */
void dialog_show_status(void);

/**
 * Show the current wallpaper dialog
 * Displays the path to the currently active wallpaper
 */
void dialog_show_current_wallpaper(void);

/**
 * Show the about dialog
 * Displays version information and project details
 */
void dialog_show_about(void);

/**
 * Show a confirmation dialog for stopping the daemon
 * @return GTK_RESPONSE_YES if user confirmed, GTK_RESPONSE_NO otherwise
 */
gint dialog_confirm_stop_daemon(void);

/**
 * Show an error dialog
 * @param title The dialog title
 * @param message The error message to display
 */
void dialog_show_error(const char *title, const char *message);

/**
 * Show an info dialog
 * @param title The dialog title
 * @param message The info message to display
 */
void dialog_show_info(const char *title, const char *message);

/**
 * Show a warning dialog
 * @param title The dialog title
 * @param message The warning message to display
 */
void dialog_show_warning(const char *title, const char *message);

#endif /* NEOWALL_TRAY_DIALOGS_H */