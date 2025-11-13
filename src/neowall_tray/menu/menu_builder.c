/* NeoWall Tray - Menu Builder Component
 * Implementation of menu building functions
 */

#include "menu_builder.h"
#include "menu_callbacks.h"
#include "../daemon/daemon_check.h"
#include <stdio.h>
#include <string.h>

/* Static variables for menu refresh callback */
static void (*refresh_callback)(gpointer) = NULL;
static gpointer refresh_user_data = NULL;

/* Helper function for scheduled refresh */
static gboolean refresh_callback_timer(gpointer user_data) {
    (void)user_data;
    if (refresh_callback) {
        refresh_callback(refresh_user_data);
    }
    return FALSE;  /* One-shot timer */
}

/* Schedule a menu refresh */
void menu_schedule_refresh(void) {
    /* Wait 500ms for daemon to start/stop before refreshing */
    g_timeout_add(500, refresh_callback_timer, NULL);
}

/* Set the menu refresh callback */
void menu_builder_set_refresh_callback(void (*callback)(gpointer), gpointer user_data) {
    refresh_callback = callback;
    refresh_user_data = user_data;
}

/* Add a menu item with label and callback */
GtkWidget *menu_builder_add_item(GtkWidget *menu, const char *label,
                                   GCallback callback, gpointer user_data) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    if (callback) {
        g_signal_connect(item, "activate", callback, user_data);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

/* Add a disabled menu item (for status display) */
GtkWidget *menu_builder_add_disabled_item(GtkWidget *menu, const char *label) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

/* Add a separator to the menu */
GtkWidget *menu_builder_add_separator(GtkWidget *menu) {
    GtkWidget *item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

/* Add a submenu with label */
GtkWidget *menu_builder_add_submenu(GtkWidget *menu, const char *label) {
    GtkWidget *submenu = gtk_menu_new();
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return submenu;
}

/* Build wallpaper submenu */
static void build_wallpaper_submenu(GtkWidget *menu) {
    GtkWidget *submenu = menu_builder_add_submenu(menu, "Wallpaper");

    menu_builder_add_item(submenu, "Next",
                          G_CALLBACK(menu_callback_next_wallpaper), NULL);
    menu_builder_add_item(submenu, "Previous",
                          G_CALLBACK(menu_callback_prev_wallpaper), NULL);
    menu_builder_add_item(submenu, "Show Current",
                          G_CALLBACK(menu_callback_show_current), NULL);
}

/* Build cycling submenu */
static void build_cycling_submenu(GtkWidget *menu) {
    GtkWidget *submenu = menu_builder_add_submenu(menu, "Cycling");

    menu_builder_add_item(submenu, "Pause",
                          G_CALLBACK(menu_callback_pause_cycling), NULL);
    menu_builder_add_item(submenu, "Resume",
                          G_CALLBACK(menu_callback_resume_cycling), NULL);
}

/* Build shader submenu */
static void build_shader_submenu(GtkWidget *menu) {
    GtkWidget *submenu = menu_builder_add_submenu(menu, "Shader Effects");

    menu_builder_add_item(submenu, "Pause Animation",
                          G_CALLBACK(menu_callback_shader_pause), NULL);
    menu_builder_add_item(submenu, "Resume Animation",
                          G_CALLBACK(menu_callback_shader_resume), NULL);

    menu_builder_add_separator(submenu);

    menu_builder_add_item(submenu, "Speed Up",
                          G_CALLBACK(menu_callback_speed_up), NULL);
    menu_builder_add_item(submenu, "Speed Down",
                          G_CALLBACK(menu_callback_speed_down), NULL);
}

/* Build daemon running menu section */
static void build_daemon_running_section(GtkWidget *menu) {
    /* Wallpaper controls */
    build_wallpaper_submenu(menu);

    /* Cycling controls */
    build_cycling_submenu(menu);

    /* Shader controls */
    build_shader_submenu(menu);

    /* Separator before status */
    menu_builder_add_separator(menu);

    /* Status */
    menu_builder_add_item(menu, "Show Status",
                          G_CALLBACK(menu_callback_show_status), NULL);
}

/* Build daemon stopped menu section */
static void build_daemon_stopped_section(GtkWidget *menu) {
    /* Start daemon option */
    menu_builder_add_item(menu, "Start Daemon",
                          G_CALLBACK(menu_callback_start_daemon), NULL);
}

/* Build configuration and system section */
static void build_system_section(GtkWidget *menu, bool daemon_running) {
    /* Separator */
    menu_builder_add_separator(menu);

    /* Configuration */
    menu_builder_add_item(menu, "Edit Configuration",
                          G_CALLBACK(menu_callback_edit_config), NULL);

    /* Daemon control */
    if (daemon_running) {
        menu_builder_add_item(menu, "Restart Daemon",
                              G_CALLBACK(menu_callback_restart_daemon), NULL);
        menu_builder_add_item(menu, "Stop Daemon",
                              G_CALLBACK(menu_callback_stop_daemon), NULL);
    }
}

/* Build info section */
static void build_info_section(GtkWidget *menu) {
    /* Separator */
    menu_builder_add_separator(menu);

    /* About */
    menu_builder_add_item(menu, "About",
                          G_CALLBACK(menu_callback_about), NULL);

    /* Quit */
    menu_builder_add_item(menu, "Quit Tray",
                          G_CALLBACK(menu_callback_quit), NULL);
}

/* Create the main tray menu */
GtkWidget *menu_builder_create(void) {
    GtkWidget *menu = gtk_menu_new();

    /* Check daemon status */
    bool daemon_running = daemon_is_running();

    /* Status indicator at top */
    char status_text[64];
    if (daemon_running) {
        snprintf(status_text, sizeof(status_text), "● Daemon Running");
    } else {
        snprintf(status_text, sizeof(status_text), "○ Daemon Stopped");
    }
    menu_builder_add_disabled_item(menu, status_text);

    /* Separator */
    menu_builder_add_separator(menu);

    /* Build appropriate section based on daemon state */
    if (daemon_running) {
        build_daemon_running_section(menu);
    } else {
        build_daemon_stopped_section(menu);
    }

    /* System section (config, restart, stop) */
    build_system_section(menu, daemon_running);

    /* Info section (about, quit) */
    build_info_section(menu);

    /* Show all items */
    gtk_widget_show_all(menu);

    return menu;
}
