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
 * Show a non-blocking info dialog that auto-closes
 * @param title The dialog title
 * @param message The info message to display
 * @param auto_close_ms Auto-close delay in milliseconds (0 = don't auto-close)
 */
void dialog_show_info(const char *title, const char *message, guint auto_close_ms);

/**
 * Show a warning dialog
 * @param title The dialog title
 * @param message The warning message to display
 */
void dialog_show_warning(const char *title, const char *message);

/**
 * Show a non-blocking progress dialog that auto-closes
 * @param title The dialog title
 * @param message The progress message to display
 * @param check_callback Function to check if operation is complete (returns true when done)
 * @param auto_close_delay_ms How long to wait after completion before closing (milliseconds)
 * @return Dialog widget (can be destroyed manually if needed)
 */
GtkWidget *dialog_show_progress_auto_close(const char *title, const char *message,
                                           gboolean (*check_callback)(void),
                                           guint auto_close_delay_ms);

/**
 * Show the settings/preferences dialog
 * Displays common configuration options in a user-friendly GUI
 */
void dialog_show_settings(void);

#endif /* NEOWALL_TRAY_DIALOGS_H */