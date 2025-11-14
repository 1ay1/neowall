/* NeoWall Tray - Menu Items Definitions
 * Centralized menu item definitions for easy management
 */

#ifndef NEOWALL_TRAY_MENU_ITEMS_H
#define NEOWALL_TRAY_MENU_ITEMS_H

#include <gtk/gtk.h>
#include <stdbool.h>

/* Menu item types */
typedef enum {
    MENU_ITEM_TYPE_NORMAL,      /* Regular clickable item */
    MENU_ITEM_TYPE_SUBMENU,     /* Has submenu */
    MENU_ITEM_TYPE_SEPARATOR,   /* Separator line */
    MENU_ITEM_TYPE_DISABLED,    /* Disabled/info item */
} MenuItemType;

/* Menu item definition structure */
typedef struct {
    const char *label;          /* Display label with icon */
    const char *id;             /* Unique identifier */
    MenuItemType type;          /* Item type */
    GCallback callback;         /* Callback function (can be NULL) */
    const char *submenu_id;     /* ID of parent submenu (NULL for root) */
    int order;                  /* Display order within parent */
} MenuItemDef;

/* Submenu definition structure */
typedef struct {
    const char *id;             /* Unique identifier */
    const char *label;          /* Display label with icon */
    const char *parent_id;      /* ID of parent submenu (NULL for root) */
    int order;                  /* Display order within parent */
} SubmenuDef;

/* Menu item IDs - for easy reference */
#define MENU_ID_STATUS              "status"
#define MENU_ID_WALLPAPER           "wallpaper"
#define MENU_ID_CYCLING             "cycling"
#define MENU_ID_SHADER              "shader"
#define MENU_ID_SYSTEM              "system"

/* Menu item labels - centralized for easy updates */
#define MENU_LABEL_STATUS_RUNNING   "● NeoWall Running"
#define MENU_LABEL_STATUS_STOPPED   "○ NeoWall Stopped"

/* Wallpaper submenu */
#define MENU_LABEL_WALLPAPER        "Wallpaper"
#define MENU_LABEL_NEXT             "Next Wallpaper"
#define MENU_LABEL_PREV             "Previous Wallpaper"
#define MENU_LABEL_CURRENT          "Show Current"

/* Cycling submenu */
#define MENU_LABEL_CYCLING          "Cycling"
#define MENU_LABEL_PAUSE_CYCLE      "Pause Cycling"
#define MENU_LABEL_RESUME_CYCLE     "Resume Cycling"

/* Live/Shader animation submenu */
#define MENU_LABEL_SHADER           "Live Animation"
#define MENU_LABEL_EDIT_SHADER      "Edit Shader..."
#define MENU_LABEL_LIVE_PAUSE       "Pause Animation"
#define MENU_LABEL_LIVE_RESUME      "Resume Animation"
#define MENU_LABEL_SPEED_UP         "Speed Up"
#define MENU_LABEL_SPEED_DOWN       "Speed Down"

/* System controls */
#define MENU_LABEL_STATUS_FULL      "Show Full Status"
#define MENU_LABEL_SETTINGS         "Settings..."
#define MENU_LABEL_RELOAD           "Reload Configuration"
#define MENU_LABEL_CONFIG           "Edit Configuration..."
#define MENU_LABEL_START            "Start Daemon"
#define MENU_LABEL_RESTART          "Restart Daemon"
#define MENU_LABEL_STOP             "Stop Daemon"

/* Info section */
#define MENU_LABEL_ABOUT            "About NeoWall"
#define MENU_LABEL_QUIT             "Quit Tray"

/* Function declarations */

/**
 * Get submenu definitions for the tray menu
 * @param count Output parameter for number of submenus
 * @return Array of submenu definitions
 */
const SubmenuDef *menu_items_get_submenus(int *count);

/**
 * Get menu items for daemon running state
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_daemon_running(int *count);

/**
 * Get menu items for daemon stopped state
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_daemon_stopped(int *count);

/**
 * Get system section menu items
 * @param daemon_running Whether daemon is running
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_system(bool daemon_running, int *count);

/**
 * Get info section menu items
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_info(int *count);

/**
 * Get wallpaper submenu items
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_wallpaper_submenu(int *count);

/**
 * Get cycling submenu items
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_cycling_submenu(int *count);

/**
 * Get live/shader animation submenu items
 * @param count Output parameter for number of items
 * @return Array of menu item definitions
 */
const MenuItemDef *menu_items_get_shader_submenu(int *count);

#endif /* NEOWALL_TRAY_MENU_ITEMS_H */