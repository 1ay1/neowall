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

#ifndef NO_APPINDICATOR
#include <libappindicator/app-indicator.h>
#endif

#define COMPONENT "INDICATOR"

#define APP_ID "com.github.neowall.tray"
#define APP_TITLE "NeoWall"
#define ICON_NAME "neowall-icon"
#define ICON_FALLBACK "preferences-desktop-wallpaper"

/* Indicator state */
#ifndef NO_APPINDICATOR
static AppIndicator *app_indicator = NULL;
#else
static GtkStatusIcon *status_icon = NULL;
#endif

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
#ifndef NO_APPINDICATOR
AppIndicator *indicator_init(void) {
#else
void *indicator_init(void) {
#endif
    /* Try to find custom icon, otherwise use fallback */
    char *icon_path = find_icon_path();
    const char *icon = icon_path ? icon_path : ICON_FALLBACK;

    if (icon_path) {
        TRAY_LOG_INFO(COMPONENT, "Using custom icon: %s", icon_path);
    } else {
        TRAY_LOG_INFO(COMPONENT, "Using fallback icon: %s", icon);
    }

#ifndef NO_APPINDICATOR
    /* Create AppIndicator */
    app_indicator = app_indicator_new(
        APP_ID,
        icon,
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    if (!app_indicator) {
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
            app_indicator_set_icon_theme_path(app_indicator, icon_dir);
            TRAY_LOG_DEBUG(COMPONENT, "Set icon theme path: %s", icon_dir);
        }

        /* Extract filename without extension for icon name */
        char *icon_name = strdup(filename);
        char *dot = strrchr(icon_name, '.');
        if (dot) *dot = '\0';

        /* Update indicator to use just the name */
        app_indicator_set_icon(app_indicator, icon_name);
        TRAY_LOG_DEBUG(COMPONENT, "Set icon name: %s", icon_name);

        free(icon_name);
        free(icon_dir);
        free(icon_path);
    }

    /* Set initial status */
    app_indicator_set_status(app_indicator, APP_INDICATOR_STATUS_ACTIVE);
#else
    /* Create GtkStatusIcon fallback */
    TRAY_LOG_INFO(COMPONENT, "Using GtkStatusIcon fallback (AppIndicator not available)");

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    status_icon = gtk_status_icon_new();

    if (!status_icon) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to create GtkStatusIcon");
        if (icon_path) free(icon_path);
        return NULL;
    }

    /* Set icon */
    if (icon_path) {
        gtk_status_icon_set_from_file(status_icon, icon_path);
        free(icon_path);
    } else {
        gtk_status_icon_set_from_icon_name(status_icon, ICON_FALLBACK);
    }

    gtk_status_icon_set_tooltip_text(status_icon, APP_TITLE);
    gtk_status_icon_set_visible(status_icon, TRUE);
    #pragma GCC diagnostic pop
#endif

#ifndef NO_APPINDICATOR
    app_indicator_set_title(app_indicator, APP_TITLE);

    /* Update status based on daemon state */
    indicator_update_status(app_indicator);

    return app_indicator;
#else
    /* Update status for GtkStatusIcon */
    indicator_update_status(NULL);

    return status_icon;
#endif
}

/* Update the indicator status based on daemon state */
#ifndef NO_APPINDICATOR
void indicator_update_status(AppIndicator *indicator) {
#else
void indicator_update_status(void *indicator) {
#endif
    (void)indicator; /* May be unused in fallback mode */

    bool running = daemon_is_running();

#ifndef NO_APPINDICATOR
    if (!app_indicator) {
        return;
    }

    if (running) {
        /* Daemon is running - normal active state */
        app_indicator_set_status(app_indicator, APP_INDICATOR_STATUS_ACTIVE);
#else
    if (!status_icon) {
        return;
    }

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (running) {
        /* Update tooltip to show daemon is running */
        gtk_status_icon_set_tooltip_text(status_icon, APP_TITLE " (Running)");
    } else {
        /* Update tooltip to show daemon is not running */
        gtk_status_icon_set_tooltip_text(status_icon, APP_TITLE " (Not Running)");
    }
    #pragma GCC diagnostic pop
#endif
    }

#ifndef NO_APPINDICATOR
    if (!running) {
        /* Daemon is stopped - attention state (often shows as orange/red) */
        app_indicator_set_status(app_indicator, APP_INDICATOR_STATUS_ATTENTION);
    }
#endif
}

/* Set the indicator menu */
#ifndef NO_APPINDICATOR
void indicator_set_menu(AppIndicator *indicator, GtkWidget *menu) {
    (void)indicator; /* Use global instead */
    if (!app_indicator || !menu) {
        return;
    }

    app_indicator_set_menu(app_indicator, GTK_MENU(menu));
}
#else
void indicator_set_menu(void *indicator, GtkWidget *menu) {
    (void)indicator; /* Use global instead */
    if (!status_icon || !menu) {
        return;
    }

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    /* Connect popup menu to status icon */
    g_signal_connect(status_icon, "popup-menu",
                     G_CALLBACK(gtk_menu_popup_at_pointer), menu);
    g_signal_connect(status_icon, "activate",
                     G_CALLBACK(gtk_menu_popup_at_pointer), menu);
    #pragma GCC diagnostic pop
}
#endif

/* Cleanup and destroy the indicator */
void indicator_cleanup(AppIndicator *indicator) {
    (void)indicator; /* Use globals instead */
#ifndef NO_APPINDICATOR
    if (app_indicator) {
        g_object_unref(app_indicator);
        app_indicator = NULL;
    }
#else
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (status_icon) {
        g_object_unref(status_icon);
        status_icon = NULL;
    }
    #pragma GCC diagnostic pop
#endif
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
#ifndef NO_APPINDICATOR
bool indicator_is_visible(AppIndicator *indicator) {
    (void)indicator; /* Use global instead */
    if (!app_indicator) {
        return false;
    }

    AppIndicatorStatus status = app_indicator_get_status(app_indicator);
    return (status != APP_INDICATOR_STATUS_PASSIVE);
}
#else
bool indicator_is_visible(void *indicator) {
    (void)indicator; /* Use global instead */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (!status_icon) {
        return false;
    }

    gboolean visible = gtk_status_icon_get_visible(status_icon);
    #pragma GCC diagnostic pop
    return visible ? true : false;
}
#endif
