/* NeoWall System Tray - Main Entry Point
 * Component-based system tray application for NeoWall wallpaper daemon
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "daemon/daemon_check.h"
#include "daemon/command_exec.h"
#include "menu/menu_builder.h"
#include "menu/menu_callbacks.h"
#include "indicator/indicator.h"
#include "dialogs/dialogs.h"

/* Global state */
static AppIndicator *g_indicator = NULL;
static GtkWidget *g_menu = NULL;
static guint g_update_timer_id = 0;

/* Update interval in seconds */
#define UPDATE_INTERVAL_SECONDS 2

/* Menu refresh callback - rebuilds menu with current daemon state */
static void refresh_menu(gpointer user_data) {
    (void)user_data;

    /* Destroy old menu if it exists */
    if (g_menu) {
        gtk_widget_destroy(g_menu);
        g_menu = NULL;
    }

    /* Create new menu with current state */
    g_menu = menu_builder_create();

    /* Update indicator */
    if (g_indicator) {
        indicator_set_menu(g_indicator, g_menu);
        indicator_update_status(g_indicator);
    }
}

/* Periodic update timer callback */
static gboolean update_timer_callback(gpointer user_data) {
    (void)user_data;

    /* Refresh menu to reflect current daemon status */
    refresh_menu(NULL);

    /* Return TRUE to continue timer */
    return TRUE;
}

/* Signal handler for clean shutdown */
static void signal_handler(int signum) {
    (void)signum;
    gtk_main_quit();
}

/* Cleanup function */
static void cleanup(void) {
    /* Stop update timer */
    if (g_update_timer_id != 0) {
        g_source_remove(g_update_timer_id);
        g_update_timer_id = 0;
    }

    /* Cleanup menu */
    if (g_menu) {
        gtk_widget_destroy(g_menu);
        g_menu = NULL;
    }

    /* Cleanup indicator */
    if (g_indicator) {
        indicator_cleanup(g_indicator);
        g_indicator = NULL;
    }
}

/* Print usage information */
static void print_usage(const char *program_name) {
    printf("NeoWall System Tray\n");
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("\n");
    printf("The system tray provides a GUI interface to control the NeoWall daemon.\n");
    printf("Right-click the tray icon to access wallpaper controls.\n");
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
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return 1;
        }
    }

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Set up signal handlers for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize indicator */
    g_indicator = indicator_init();
    if (!g_indicator) {
        fprintf(stderr, "Error: Failed to initialize system tray indicator\n");
        fprintf(stderr, "Make sure your desktop environment supports system tray.\n");
        return 1;
    }

    /* Check if indicator is visible */
    if (!indicator_is_visible(g_indicator)) {
        fprintf(stderr, "Warning: System tray may not be available\n");
        fprintf(stderr, "The tray icon might not appear in your system tray.\n");
    }

    /* Set up menu refresh callback */
    menu_builder_set_refresh_callback(refresh_menu, NULL);

    /* Create initial menu */
    refresh_menu(NULL);

    /* Set up periodic update timer (every 2 seconds) */
    g_update_timer_id = g_timeout_add_seconds(UPDATE_INTERVAL_SECONDS,
                                               update_timer_callback,
                                               NULL);

    /* Print startup message */
    printf("NeoWall system tray started\n");
    printf("Daemon status: %s\n", daemon_get_status_string());

    /* Register cleanup function */
    atexit(cleanup);

    /* Run GTK main loop */
    gtk_main();

    /* Cleanup before exit */
    cleanup();

    printf("NeoWall system tray stopped\n");

    return 0;
}
