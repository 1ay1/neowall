/* NeoWall Tray - Menu Builder Component
 * Provides functions to create and manage the system tray menu
 */

#ifndef NEOWALL_TRAY_MENU_BUILDER_H
#define NEOWALL_TRAY_MENU_BUILDER_H

#include <gtk/gtk.h>
#include <stdbool.h>

/**
 * Create the main tray menu
 * Builds a complete menu based on current daemon status
 * @return A new GtkWidget menu (caller must handle lifecycle)
 */
GtkWidget *menu_builder_create(void);

/**
 * Schedule a menu refresh
 * Triggers a delayed menu rebuild (500ms delay)
 * Useful after daemon state changes (start/stop/restart)
 */
void menu_schedule_refresh(void);

/**
 * Set the menu refresh callback
 * This function will be called when the menu needs to be rebuilt
 * @param callback Function to call when menu should refresh
 * @param user_data User data to pass to callback
 */
void menu_builder_set_refresh_callback(void (*callback)(gpointer), gpointer user_data);

/**
 * Add a menu item with label and callback
 * @param menu The menu to add to
 * @param label The item label
 * @param callback The callback function
 * @param user_data User data for callback
 * @return The created menu item
 */
GtkWidget *menu_builder_add_item(GtkWidget *menu, const char *label,
                                   GCallback callback, gpointer user_data);

/**
 * Add a disabled menu item (for status display)
 * @param menu The menu to add to
 * @param label The item label
 * @return The created menu item
 */
GtkWidget *menu_builder_add_disabled_item(GtkWidget *menu, const char *label);

/**
 * Add a separator to the menu
 * @param menu The menu to add to
 * @return The created separator
 */
GtkWidget *menu_builder_add_separator(GtkWidget *menu);

/**
 * Add a submenu with label
 * @param menu The parent menu
 * @param label The submenu label
 * @return The created submenu widget
 */
GtkWidget *menu_builder_add_submenu(GtkWidget *menu, const char *label);

#endif /* NEOWALL_TRAY_MENU_BUILDER_H */