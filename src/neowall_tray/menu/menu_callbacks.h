/* NeoWall Tray - Menu Callbacks Component
 * Provides callback functions for menu item actions
 */

#ifndef NEOWALL_TRAY_MENU_CALLBACKS_H
#define NEOWALL_TRAY_MENU_CALLBACKS_H

#include <gtk/gtk.h>

/* Wallpaper control callbacks */
void menu_callback_next_wallpaper(GtkMenuItem *item, gpointer user_data);
void menu_callback_prev_wallpaper(GtkMenuItem *item, gpointer user_data);
void menu_callback_show_current(GtkMenuItem *item, gpointer user_data);

/* Cycling control callbacks */
void menu_callback_pause_cycling(GtkMenuItem *item, gpointer user_data);
void menu_callback_resume_cycling(GtkMenuItem *item, gpointer user_data);

/* Live/Shader animation control callbacks */
void menu_callback_live_pause(GtkMenuItem *item, gpointer user_data);
void menu_callback_live_resume(GtkMenuItem *item, gpointer user_data);
void menu_callback_speed_up(GtkMenuItem *item, gpointer user_data);
void menu_callback_speed_down(GtkMenuItem *item, gpointer user_data);

/* System control callbacks */
void menu_callback_show_status(GtkMenuItem *item, gpointer user_data);
void menu_callback_show_settings(GtkMenuItem *item, gpointer user_data);
void menu_callback_reload_config(GtkMenuItem *item, gpointer user_data);
void menu_callback_edit_config(GtkMenuItem *item, gpointer user_data);
void menu_callback_start_daemon(GtkMenuItem *item, gpointer user_data);
void menu_callback_restart_daemon(GtkMenuItem *item, gpointer user_data);
void menu_callback_stop_daemon(GtkMenuItem *item, gpointer user_data);

/* Info callbacks */
void menu_callback_about(GtkMenuItem *item, gpointer user_data);
void menu_callback_quit(GtkMenuItem *item, gpointer user_data);

#endif /* NEOWALL_TRAY_MENU_CALLBACKS_H */