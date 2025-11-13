/* NeoWall Tray - Menu Builder Component
 * Implementation of menu building functions using centralized definitions
 */

#include "menu_builder.h"
#include "menu_callbacks.h"
#include "menu_items.h"
#include "../daemon/daemon_check.h"
#include <stdio.h>
#include <string.h>

/* Static variables for menu callbacks */
static void (*refresh_callback)(gpointer) = NULL;
static gpointer refresh_user_data = NULL;
static void (*status_update_callback)(gpointer) = NULL;
static gpointer status_update_user_data = NULL;

/* Static variable to store the status menu item */
static GtkWidget *g_status_item = NULL;

/* Helper function for scheduled refresh */
static gboolean refresh_callback_timer(gpointer user_data) {
    (void)user_data;
    if (refresh_callback) {
        refresh_callback(refresh_user_data);
    }
    return FALSE;  /* One-shot timer */
}

/* Helper function for scheduled status update */
static gboolean status_update_callback_timer(gpointer user_data) {
    (void)user_data;
    if (status_update_callback) {
        status_update_callback(status_update_user_data);
    }
    return FALSE;  /* One-shot timer */
}

/* Schedule a full menu refresh */
void menu_schedule_refresh(void) {
    /* Wait 500ms for daemon to start/stop before refreshing */
    g_timeout_add(500, refresh_callback_timer, NULL);
}

/* Schedule a status update only */
void menu_schedule_status_update(void) {
    /* Wait 500ms for daemon to start/stop before updating status */
    g_timeout_add(500, status_update_callback_timer, NULL);
}

/* Set the menu refresh callback */
void menu_builder_set_refresh_callback(void (*callback)(gpointer), gpointer user_data) {
    refresh_callback = callback;
    refresh_user_data = user_data;
}

/* Set the status update callback */
void menu_builder_set_status_update_callback(void (*callback)(gpointer), gpointer user_data) {
    status_update_callback = callback;
    status_update_user_data = user_data;
}

/* Update only the status item in an existing menu */
bool menu_builder_update_status(GtkWidget *menu) {
    if (!menu || !g_status_item) {
        return false;
    }

    /* Check daemon status */
    bool daemon_running = daemon_is_running();

    /* Get status label */
    const char *status_text = daemon_running ?
        MENU_LABEL_STATUS_RUNNING : MENU_LABEL_STATUS_STOPPED;

    /* Update the label */
    GtkWidget *label_widget = gtk_bin_get_child(GTK_BIN(g_status_item));
    if (label_widget && GTK_IS_LABEL(label_widget)) {
        gtk_label_set_text(GTK_LABEL(label_widget), status_text);
        return true;
    }

    return false;
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

    /* Add padding to the label */
    GtkWidget *label_widget = gtk_bin_get_child(GTK_BIN(item));
    if (label_widget) {
        gtk_widget_set_margin_top(label_widget, 8);
        gtk_widget_set_margin_bottom(label_widget, 8);
        gtk_widget_set_margin_start(label_widget, 12);
        gtk_widget_set_margin_end(label_widget, 12);
    }

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

/* Build menu items from definitions */
static void build_menu_items(GtkWidget *parent, const MenuItemDef *items, int count) {
    for (int i = 0; i < count; i++) {
        const MenuItemDef *item = &items[i];

        switch (item->type) {
            case MENU_ITEM_TYPE_NORMAL:
                menu_builder_add_item(parent, item->label, item->callback, NULL);
                break;

            case MENU_ITEM_TYPE_SEPARATOR:
                menu_builder_add_separator(parent);
                break;

            case MENU_ITEM_TYPE_DISABLED:
                menu_builder_add_disabled_item(parent, item->label);
                break;

            case MENU_ITEM_TYPE_SUBMENU:
                /* Submenus are handled separately */
                break;
        }
    }
}

/* Build a specific submenu by ID */
static GtkWidget *build_submenu_by_id(GtkWidget *menu, const char *submenu_id) {
    int count = 0;
    const MenuItemDef *items = NULL;

    if (strcmp(submenu_id, MENU_ID_WALLPAPER) == 0) {
        items = menu_items_get_wallpaper_submenu(&count);
        GtkWidget *submenu = menu_builder_add_submenu(menu, MENU_LABEL_WALLPAPER);
        build_menu_items(submenu, items, count);
        return submenu;
    } else if (strcmp(submenu_id, MENU_ID_CYCLING) == 0) {
        items = menu_items_get_cycling_submenu(&count);
        GtkWidget *submenu = menu_builder_add_submenu(menu, MENU_LABEL_CYCLING);
        build_menu_items(submenu, items, count);
        return submenu;
    } else if (strcmp(submenu_id, MENU_ID_SHADER) == 0) {
        items = menu_items_get_shader_submenu(&count);
        GtkWidget *submenu = menu_builder_add_submenu(menu, MENU_LABEL_SHADER);
        build_menu_items(submenu, items, count);
        return submenu;
    }

    return NULL;
}

/* Build all submenus */
static void build_submenus(GtkWidget *menu) {
    int count = 0;
    const SubmenuDef *submenus = menu_items_get_submenus(&count);

    for (int i = 0; i < count; i++) {
        build_submenu_by_id(menu, submenus[i].id);
    }
}

/* Build daemon running menu section */
static void build_daemon_running_section(GtkWidget *menu) {
    /* Build submenus (wallpaper, cycling, shader) */
    build_submenus(menu);

    /* Add daemon running items (separator + status) */
    int count = 0;
    const MenuItemDef *items = menu_items_get_daemon_running(&count);
    build_menu_items(menu, items, count);
}

/* Build daemon stopped menu section */
static void build_daemon_stopped_section(GtkWidget *menu) {
    /* Add daemon stopped items (start button) */
    int count = 0;
    const MenuItemDef *items = menu_items_get_daemon_stopped(&count);
    build_menu_items(menu, items, count);
}

/* Build system section */
static void build_system_section(GtkWidget *menu, bool daemon_running) {
    int count = 0;
    const MenuItemDef *items = menu_items_get_system(daemon_running, &count);
    build_menu_items(menu, items, count);
}

/* Build info section */
static void build_info_section(GtkWidget *menu) {
    int count = 0;
    const MenuItemDef *items = menu_items_get_info(&count);
    build_menu_items(menu, items, count);
}

/* Create the main tray menu */
GtkWidget *menu_builder_create(void) {
    GtkWidget *menu = gtk_menu_new();

    /* Check daemon status */
    bool daemon_running = daemon_is_running();

    /* Status indicator at top */
    const char *status_text = daemon_running ?
        MENU_LABEL_STATUS_RUNNING : MENU_LABEL_STATUS_STOPPED;
    g_status_item = menu_builder_add_disabled_item(menu, status_text);

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
