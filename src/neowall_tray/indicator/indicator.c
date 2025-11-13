/* NeoWall Tray - Indicator Component
 * Implementation of system tray indicator
 */

#include "indicator.h"
#include "../common/log.h"
#include "../daemon/daemon_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COMPONENT "INDICATOR"

#define APP_ID "com.github.neowall.tray"
#define APP_TITLE "NeoWall"
#define ICON_NAME "neowall-icon"
#define ICON_FALLBACK "preferences-desktop-wallpaper"

/* Helper to find icon path - returns absolute path */
static char *find_icon_path(void) {
    char *abs_path = NULL;

    /* Try PNG icon paths in order of preference */
    const char *icon_paths[] = {
        "/usr/share/pixmaps/neowall-icon.png",           /* Installed path */
        "/usr/share/icons/hicolor/128x128/apps/neowall-icon.png",  /* Icon theme */
        "neowall-icon.png",                               /* Current directory (builddir) */
        "data/icons/neowall-icon.png",                    /* Build tree */
        "../data/icons/neowall-icon.png",                 /* Alternative build tree */
        NULL
    };

    for (int i = 0; icon_paths[i] != NULL; i++) {
        if (access(icon_paths[i], F_OK) == 0) {
            /* Get absolute path for relative paths */
            if (icon_paths[i][0] != '/') {
                abs_path = realpath(icon_paths[i], NULL);
                if (abs_path) {
                    TRAY_LOG_DEBUG(COMPONENT, "Found icon at: %s -> %s", icon_paths[i], abs_path);
                    return abs_path;
                }
            } else {
                TRAY_LOG_DEBUG(COMPONENT, "Found icon at: %s", icon_paths[i]);
                return strdup(icon_paths[i]);
            }
        }
    }

    /* No custom icon found, use icon name (will search theme) */
    TRAY_LOG_DEBUG(COMPONENT, "No custom icon found, using fallback: %s", ICON_FALLBACK);
    return NULL;
}

/* Initialize the system tray indicator */
AppIndicator *indicator_init(void) {
    /* Try to find custom icon, otherwise use fallback */
    char *icon_path = find_icon_path();
    const char *icon = icon_path ? icon_path : ICON_FALLBACK;

    if (icon_path) {
        TRAY_LOG_INFO(COMPONENT, "Using custom icon: %s", icon_path);
    } else {
        TRAY_LOG_INFO(COMPONENT, "Using fallback icon: %s", icon);
    }

    /* Create indicator with icon path */
    AppIndicator *indicator = app_indicator_new(
        APP_ID,
        icon,
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    if (!indicator) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to create AppIndicator");
        if (icon_path) free(icon_path);
        return NULL;
    }

    /* If using custom icon path, set the icon theme path */
    if (icon_path) {
        /* Extract directory from full path */
        char *icon_dir = strdup(icon_path);
        char *last_slash = strrchr(icon_dir, '/');
        const char *filename = icon_path;

        if (last_slash) {
            *last_slash = '\0';
            filename = last_slash + 1;  /* Save pointer before freeing icon_dir */
            app_indicator_set_icon_theme_path(indicator, icon_dir);
            TRAY_LOG_DEBUG(COMPONENT, "Set icon theme path: %s", icon_dir);
        }

        /* Extract filename without extension for icon name */
        char *icon_name = strdup(filename);
        char *dot = strrchr(icon_name, '.');
        if (dot) *dot = '\0';

        /* Update indicator to use just the name */
        app_indicator_set_icon(indicator, icon_name);
        TRAY_LOG_DEBUG(COMPONENT, "Set icon name: %s", icon_name);

        free(icon_name);
        free(icon_dir);
        free(icon_path);
    }

    /* Set initial status */
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator, APP_TITLE);

    /* Update status based on daemon state */
    indicator_update_status(indicator);

    return indicator;
}

/* Update the indicator status based on daemon state */
void indicator_update_status(AppIndicator *indicator) {
    if (!indicator) {
        return;
    }

    bool running = daemon_is_running();

    if (running) {
        /* Daemon is running - normal active state */
        app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    } else {
        /* Daemon is stopped - attention state (often shows as orange/red) */
        app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ATTENTION);
    }
}

/* Set the indicator menu */
void indicator_set_menu(AppIndicator *indicator, GtkWidget *menu) {
    if (!indicator || !menu) {
        return;
    }

    app_indicator_set_menu(indicator, GTK_MENU(menu));
}

/* Cleanup and destroy the indicator */
void indicator_cleanup(AppIndicator *indicator) {
    if (indicator) {
        g_object_unref(indicator);
    }
}

/* Get the indicator title */
const char *indicator_get_title(void) {
    return APP_TITLE;
}

/* Get the indicator icon name */
const char *indicator_get_icon_name(void) {
    return ICON_NAME;
}

/* Check if the indicator is visible/embedded in the system tray */
bool indicator_is_visible(AppIndicator *indicator) {
    if (!indicator) {
        return false;
    }

    AppIndicatorStatus status = app_indicator_get_status(indicator);
    return (status != APP_INDICATOR_STATUS_PASSIVE);
}
