/* NeoWall System Tray - Main Entry Point
 * Component-based system tray application for NeoWall wallpaper daemon
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "common/log.h"
#include "daemon/daemon_check.h"
#include "daemon/command_exec.h"
#include "menu/menu_builder.h"
#include "menu/menu_callbacks.h"
#include "indicator/indicator.h"
#include "dialogs/dialogs.h"

#define COMPONENT "MAIN"

/* Global state */
static AppIndicator *g_indicator = NULL;
static GtkWidget *g_menu = NULL;
static guint g_update_timer_id = 0;

/* Update interval in seconds */
#define UPDATE_INTERVAL_SECONDS 2

/* Menu refresh callback - rebuilds menu with current daemon state */
static void refresh_menu(gpointer user_data) {
    (void)user_data;

    TRAY_LOG_DEBUG(COMPONENT, "Refreshing menu (full rebuild)");

    /* Destroy old menu if it exists */
    if (g_menu) {
        TRAY_LOG_DEBUG(COMPONENT, "Destroying old menu");
        gtk_widget_destroy(g_menu);
        g_menu = NULL;
    }

    /* Create new menu with current state */
    TRAY_LOG_DEBUG(COMPONENT, "Creating new menu");
    g_menu = menu_builder_create();

    /* Update indicator */
    if (g_indicator) {
        TRAY_LOG_DEBUG(COMPONENT, "Updating indicator with new menu");
        indicator_set_menu(g_indicator, g_menu);
        indicator_update_status(g_indicator);
    } else {
        TRAY_LOG_INFO(COMPONENT, "Warning: Indicator not available for menu update");
    }
}

/* Status update callback - updates only the status item */
static void update_status_only(gpointer user_data) {
    (void)user_data;

    TRAY_LOG_DEBUG(COMPONENT, "Updating menu status (status item only)");

    /* Update only the status item in the existing menu */
    if (g_menu) {
        if (menu_builder_update_status(g_menu)) {
            TRAY_LOG_DEBUG(COMPONENT, "Status item updated successfully");
        } else {
            TRAY_LOG_INFO(COMPONENT, "Warning: Failed to update status item");
        }
    } else {
        TRAY_LOG_INFO(COMPONENT, "Warning: Menu not available for status update");
    }

    /* Update indicator status */
    if (g_indicator) {
        indicator_update_status(g_indicator);
    }
}

/* Periodic update timer callback */
static gboolean update_timer_callback(gpointer user_data) {
    (void)user_data;

    TRAY_LOG_DEBUG(COMPONENT, "Periodic update timer fired");

    /* Update only status to avoid closing the menu */
    update_status_only(NULL);

    /* Return TRUE to continue timer */
    return TRUE;
}

/* Signal handler for clean shutdown */
static void signal_handler(int signum) {
    TRAY_LOG_INFO(COMPONENT, "Received signal %d, shutting down", signum);
    gtk_main_quit();
}

/* Cleanup function */
static void cleanup(void) {
    TRAY_LOG_INFO(COMPONENT, "Cleaning up tray resources");

    /* Stop update timer */
    if (g_update_timer_id != 0) {
        TRAY_LOG_DEBUG(COMPONENT, "Removing update timer (ID: %u)", g_update_timer_id);
        g_source_remove(g_update_timer_id);
        g_update_timer_id = 0;
    }

    /* Cleanup menu */
    if (g_menu) {
        TRAY_LOG_DEBUG(COMPONENT, "Destroying menu");
        gtk_widget_destroy(g_menu);
        g_menu = NULL;
    }

    /* Cleanup indicator */
    if (g_indicator) {
        TRAY_LOG_DEBUG(COMPONENT, "Cleaning up indicator");
        indicator_cleanup(g_indicator);
        g_indicator = NULL;
    }

    TRAY_LOG_INFO(COMPONENT, "Cleanup complete");
}

/* Print usage information */
static void print_usage(const char *program_name) {
    printf("NeoWall System Tray\n");
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -d, --debug    Enable debug logging\n");
    printf("\n");
    printf("The system tray provides a GUI interface to control the NeoWall daemon.\n");
    printf("Right-click the tray icon to access wallpaper controls.\n");
    printf("\n");
    printf("Environment Variables:\n");
    printf("  NEOWALL_TRAY_LOG_LEVEL    Set log level (ERROR, INFO, DEBUG)\n");
    printf("  NO_COLOR                  Disable colored output\n");
}

/* Print version information */
static void print_version(void) {
    char version[256] = "Unknown";

    /* Try to get version from neowall binary */
    if (command_execute_with_output("version", version, sizeof(version))) {
        /* Remove trailing newline */
        size_t len = strlen(version);
        if (len > 0 && version[len - 1] == '\n') {
            version[len - 1] = '\0';
        }
        printf("%s (tray)\n", version);
    } else {
        printf("NeoWall Tray (version unknown)\n");
    }
}

/* Main entry point */
int main(int argc, char *argv[]) {
    /* Initialize logging system first */
    tray_log_init();

    TRAY_LOG_INFO(COMPONENT, "=== NeoWall System Tray Starting ===");
    TRAY_LOG_DEBUG(COMPONENT, "argc=%d", argc);
    for (int i = 0; i < argc; i++) {
        TRAY_LOG_DEBUG(COMPONENT, "argv[%d]=%s", i, argv[i]);
    }

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            tray_log_set_level(TRAY_LOG_DEBUG);
            TRAY_LOG_INFO(COMPONENT, "Debug logging enabled");
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Unknown option: %s", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Initialize GTK */
    TRAY_LOG_INFO(COMPONENT, "Initializing GTK...");
    gtk_init(&argc, &argv);
    TRAY_LOG_DEBUG(COMPONENT, "GTK initialized successfully");

    /* Set up signal handlers for clean shutdown */
    TRAY_LOG_DEBUG(COMPONENT, "Setting up signal handlers");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize indicator */
    TRAY_LOG_INFO(COMPONENT, "Initializing system tray indicator...");
    g_indicator = indicator_init();
    if (!g_indicator) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to initialize system tray indicator");
        TRAY_LOG_ERROR(COMPONENT, "Make sure your desktop environment supports system tray");
        return 1;
    }
    TRAY_LOG_DEBUG(COMPONENT, "Indicator initialized successfully");

    /* Check if indicator is visible */
    if (!indicator_is_visible(g_indicator)) {
        TRAY_LOG_INFO(COMPONENT, "Warning: System tray may not be available");
        TRAY_LOG_INFO(COMPONENT, "The tray icon might not appear in your system tray");
    }

    /* Set up menu refresh callback (for full rebuilds) */
    TRAY_LOG_DEBUG(COMPONENT, "Setting up menu refresh callback");
    menu_builder_set_refresh_callback(refresh_menu, NULL);

    /* Set up status update callback (for status-only updates) */
    TRAY_LOG_DEBUG(COMPONENT, "Setting up status update callback");
    menu_builder_set_status_update_callback(update_status_only, NULL);

    /* Create initial menu */
    TRAY_LOG_INFO(COMPONENT, "Creating initial menu");
    refresh_menu(NULL);

    /* Set up periodic update timer (every 2 seconds) */
    TRAY_LOG_INFO(COMPONENT, "Setting up periodic update timer (%d second interval)", UPDATE_INTERVAL_SECONDS);
    g_update_timer_id = g_timeout_add_seconds(UPDATE_INTERVAL_SECONDS,
                                               update_timer_callback,
                                               NULL);

    /* Log startup complete */
    TRAY_LOG_INFO(COMPONENT, "Tray startup complete - Daemon status: %s", daemon_get_status_string());

    /* Register cleanup function */
    atexit(cleanup);

    /* Run GTK main loop */
    TRAY_LOG_DEBUG(COMPONENT, "Starting GTK main loop");
    gtk_main();

    TRAY_LOG_INFO(COMPONENT, "GTK main loop exited, shutting down");
    return 0;
}
