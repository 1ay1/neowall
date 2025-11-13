/* NeoWall Tray - Indicator Component
 * Provides functions to manage the system tray indicator
 */

#ifndef NEOWALL_TRAY_INDICATOR_H
#define NEOWALL_TRAY_INDICATOR_H

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#include <stdbool.h>

/**
 * Initialize the system tray indicator
 * @return Pointer to the created AppIndicator, NULL on failure
 */
AppIndicator *indicator_init(void);

/**
 * Update the indicator status based on daemon state
 * @param indicator The AppIndicator to update
 * Changes the indicator status (active/attention) based on daemon running state
 */
void indicator_update_status(AppIndicator *indicator);

/**
 * Set the indicator menu
 * @param indicator The AppIndicator
 * @param menu The GtkMenu to attach
 */
void indicator_set_menu(AppIndicator *indicator, GtkWidget *menu);

/**
 * Cleanup and destroy the indicator
 * @param indicator The AppIndicator to cleanup
 */
void indicator_cleanup(AppIndicator *indicator);

/**
 * Get the indicator title
 * @return Static string with the application title
 */
const char *indicator_get_title(void);

/**
 * Get the indicator icon name
 * @return Static string with the icon name
 */
const char *indicator_get_icon_name(void);

/**
 * Check if the indicator is visible/embedded in the system tray
 * @param indicator The AppIndicator to check
 * @return true if visible, false otherwise
 */
bool indicator_is_visible(AppIndicator *indicator);

#endif /* NEOWALL_TRAY_INDICATOR_H */