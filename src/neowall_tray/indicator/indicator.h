/* NeoWall Tray - Indicator Component
 * Provides functions to manage the system tray indicator
 */

#ifndef NEOWALL_TRAY_INDICATOR_H
#define NEOWALL_TRAY_INDICATOR_H

#include <gtk/gtk.h>
#include <stdbool.h>

#ifndef NO_APPINDICATOR
#include <libappindicator/app-indicator.h>
#endif

/**
 * Initialize the system tray indicator
 * @return Pointer to the created indicator (AppIndicator or GtkStatusIcon), NULL on failure
 */
#ifndef NO_APPINDICATOR
AppIndicator *indicator_init(void);
#else
void *indicator_init(void);
#endif

/**
 * Update the indicator status based on daemon state
 * @param indicator The indicator to update
 * Changes the indicator status (active/attention) based on daemon running state
 */
#ifndef NO_APPINDICATOR
void indicator_update_status(AppIndicator *indicator);
#else
void indicator_update_status(void *indicator);
#endif

/**
 * Set the indicator menu
 * @param indicator The indicator
 * @param menu The GtkMenu to attach
 */
#ifndef NO_APPINDICATOR
void indicator_set_menu(AppIndicator *indicator, GtkWidget *menu);
#else
void indicator_set_menu(void *indicator, GtkWidget *menu);
#endif

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
 * @param indicator The indicator to check
 * @return true if visible, false otherwise
 */
#ifndef NO_APPINDICATOR
bool indicator_is_visible(AppIndicator *indicator);
#else
bool indicator_is_visible(void *indicator);
#endif

#endif /* NEOWALL_TRAY_INDICATOR_H */