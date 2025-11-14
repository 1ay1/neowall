/* NeoWall Tray - Menu Items Implementation
 * Centralized menu item definitions
 */

#include "menu_items.h"
#include "menu_callbacks.h"
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * WALLPAPER SUBMENU ITEMS
 * ============================================================================ */

static const MenuItemDef wallpaper_items[] = {
    {
        .label = MENU_LABEL_NEXT,
        .id = "next",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_next_wallpaper),
        .submenu_id = MENU_ID_WALLPAPER,
        .order = 1
    },
    {
        .label = MENU_LABEL_PREV,
        .id = "prev",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_prev_wallpaper),
        .submenu_id = MENU_ID_WALLPAPER,
        .order = 2
    }
};

/* ============================================================================
 * CYCLING SUBMENU ITEMS
 * ============================================================================ */

static const MenuItemDef cycling_items[] = {
    {
        .label = MENU_LABEL_PAUSE_CYCLE,
        .id = "pause_cycle",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_pause_cycling),
        .submenu_id = MENU_ID_CYCLING,
        .order = 1
    },
    {
        .label = MENU_LABEL_RESUME_CYCLE,
        .id = "resume_cycle",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_resume_cycling),
        .submenu_id = MENU_ID_CYCLING,
        .order = 2
    }
};

/* ============================================================================
 * SHADER SUBMENU ITEMS
 * ============================================================================ */

static const MenuItemDef shader_items[] = {
    {
        .label = MENU_LABEL_LIVE_PAUSE,
        .id = "live_pause",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_live_pause),
        .submenu_id = MENU_ID_SHADER,
        .order = 1
    },
    {
        .label = MENU_LABEL_LIVE_RESUME,
        .id = "live_resume",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_live_resume),
        .submenu_id = MENU_ID_SHADER,
        .order = 2
    },

};

/* ============================================================================
 * DAEMON RUNNING ITEMS
 * ============================================================================ */

static const MenuItemDef daemon_running_items[] = {
    {
        .label = NULL,
        .id = "sep_before_status",
        .type = MENU_ITEM_TYPE_SEPARATOR,
        .callback = NULL,
        .submenu_id = NULL,
        .order = 1
    },
    {
        .label = MENU_LABEL_STATUS_FULL,
        .id = "status_full",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_show_status),
        .submenu_id = NULL,
        .order = 2
    }
};

/* ============================================================================
 * DAEMON STOPPED ITEMS
 * ============================================================================ */

static const MenuItemDef daemon_stopped_items[] = {
    {
        .label = MENU_LABEL_START,
        .id = "start",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_start_daemon),
        .submenu_id = NULL,
        .order = 1
    }
};

/* ============================================================================
 * SYSTEM SECTION ITEMS (dynamic based on daemon state)
 * ============================================================================ */

static const MenuItemDef system_items_running[] = {
    {
        .label = NULL,
        .id = "sep_system",
        .type = MENU_ITEM_TYPE_SEPARATOR,
        .callback = NULL,
        .submenu_id = NULL,
        .order = 1
    },
    {
        .label = MENU_LABEL_RESTART,
        .id = "restart",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_restart_daemon),
        .submenu_id = NULL,
        .order = 2
    },
    {
        .label = MENU_LABEL_STOP,
        .id = "stop",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_stop_daemon),
        .submenu_id = NULL,
        .order = 3
    }
};

static const MenuItemDef system_items_stopped[] = {
    {
        .label = NULL,
        .id = "sep_system",
        .type = MENU_ITEM_TYPE_SEPARATOR,
        .callback = NULL,
        .submenu_id = NULL,
        .order = 1
    }
};

/* ============================================================================
 * INFO SECTION ITEMS
 * ============================================================================ */

static const MenuItemDef info_items[] = {
    {
        .label = NULL,
        .id = "sep_info",
        .type = MENU_ITEM_TYPE_SEPARATOR,
        .callback = NULL,
        .submenu_id = NULL,
        .order = 1
    },
    {
        .label = MENU_LABEL_EDIT_SHADER,
        .id = "edit_shader",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_edit_shader),
        .submenu_id = NULL,
        .order = 2
    },
    {
        .label = MENU_LABEL_SETTINGS,
        .id = "settings",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_show_settings),
        .submenu_id = NULL,
        .order = 3
    },
    {
        .label = MENU_LABEL_ABOUT,
        .id = "about",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_about),
        .submenu_id = NULL,
        .order = 4
    },
    {
        .label = MENU_LABEL_QUIT,
        .id = "quit",
        .type = MENU_ITEM_TYPE_NORMAL,
        .callback = G_CALLBACK(menu_callback_quit),
        .submenu_id = NULL,
        .order = 5
    }
};

/* ============================================================================
 * SUBMENU DEFINITIONS
 * ============================================================================ */

static const SubmenuDef submenus[] = {
    {
        .id = MENU_ID_WALLPAPER,
        .label = MENU_LABEL_WALLPAPER,
        .parent_id = NULL,
        .order = 1
    },
    {
        .id = MENU_ID_CYCLING,
        .label = MENU_LABEL_CYCLING,
        .parent_id = NULL,
        .order = 2
    },
    {
        .id = MENU_ID_SHADER,
        .label = MENU_LABEL_SHADER,
        .parent_id = NULL,
        .order = 3
    }
};

/* ============================================================================
 * PUBLIC API FUNCTIONS
 * ============================================================================ */

const SubmenuDef *menu_items_get_submenus(int *count) {
    if (count) {
        *count = sizeof(submenus) / sizeof(submenus[0]);
    }
    return submenus;
}

const MenuItemDef *menu_items_get_daemon_running(int *count) {
    if (count) {
        *count = sizeof(daemon_running_items) / sizeof(daemon_running_items[0]);
    }
    return daemon_running_items;
}

const MenuItemDef *menu_items_get_daemon_stopped(int *count) {
    if (count) {
        *count = sizeof(daemon_stopped_items) / sizeof(daemon_stopped_items[0]);
    }
    return daemon_stopped_items;
}

const MenuItemDef *menu_items_get_system(bool daemon_running, int *count) {
    if (daemon_running) {
        if (count) {
            *count = sizeof(system_items_running) / sizeof(system_items_running[0]);
        }
        return system_items_running;
    } else {
        if (count) {
            *count = sizeof(system_items_stopped) / sizeof(system_items_stopped[0]);
        }
        return system_items_stopped;
    }
}

const MenuItemDef *menu_items_get_info(int *count) {
    if (count) {
        *count = sizeof(info_items) / sizeof(info_items[0]);
    }
    return info_items;
}

const MenuItemDef *menu_items_get_wallpaper_submenu(int *count) {
    if (count) {
        *count = sizeof(wallpaper_items) / sizeof(wallpaper_items[0]);
    }
    return wallpaper_items;
}

const MenuItemDef *menu_items_get_cycling_submenu(int *count) {
    if (count) {
        *count = sizeof(cycling_items) / sizeof(cycling_items[0]);
    }
    return cycling_items;
}

const MenuItemDef *menu_items_get_shader_submenu(int *count) {
    if (count) {
        *count = sizeof(shader_items) / sizeof(shader_items[0]);
    }
    return shader_items;
}
