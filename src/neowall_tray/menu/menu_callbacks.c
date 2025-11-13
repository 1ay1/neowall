/* NeoWall Tray - Menu Callbacks Component
 * Implementation of menu callback functions
 */

#include "menu_callbacks.h"
#include "../daemon/command_exec.h"
#include "../dialogs/dialogs.h"
#include "../dialogs/settings_dialog.h"
#include <stdio.h>

/* Forward declaration for menu update functions */
extern void menu_schedule_refresh(void);

/* Wallpaper control callbacks */
void menu_callback_next_wallpaper(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("next");
}

void menu_callback_prev_wallpaper(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("prev");
}

void menu_callback_show_current(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    dialog_show_current_wallpaper();
}

/* Cycling control callbacks */
void menu_callback_pause_cycling(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("cycle-pause");
}

void menu_callback_resume_cycling(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("cycle-resume");
}

/* Live/Shader animation control callbacks */
void menu_callback_live_pause(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("live-pause");
}

void menu_callback_live_resume(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("live-resume");
}

void menu_callback_speed_up(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("speed-up");
}

void menu_callback_speed_down(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("speed-down");
}

/* System control callbacks */
void menu_callback_show_status(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    dialog_show_status();
}

void menu_callback_show_settings(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    settings_dialog_show();
}

void menu_callback_reload_config(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (command_execute("reload")) {
        dialog_show_info("Configuration Reloaded",
                        "Configuration has been reloaded successfully.",
                        2000);  /* Auto-close after 2 seconds */
    } else {
        dialog_show_error("Reload Failed",
                         "Failed to reload configuration.\n"
                         "Please check the configuration file for errors.");
    }
}

void menu_callback_edit_config(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    /* Create dialog with configuration options */
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "Configuration Options"
    );

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Choose how you would like to configure NeoWall:\n\n"
        "• Open Editor - Edit configuration file in your default editor\n"
        "• Show Path - Display the configuration file location\n"
        "• Open Folder - Open the configuration directory in file manager"
    );

    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                          "Open Editor", 1,
                          "Show Path", 2,
                          "Open Folder", 3,
                          "Cancel", GTK_RESPONSE_CANCEL,
                          NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == 1) {
        /* Open in editor */
        if (!command_execute("config")) {
            dialog_show_error("Editor Error",
                             "Failed to open configuration file.\n"
                             "Please check your EDITOR environment variable.");
        }
    } else if (response == 2) {
        /* Show config path */
        char config_path[512];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(config_path, sizeof(config_path),
                    "%s/.config/neowall/config.vibe", home);
        } else {
            snprintf(config_path, sizeof(config_path),
                    "~/.config/neowall/config.vibe");
        }

        GtkWidget *path_dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Configuration File Path"
        );
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(path_dialog),
            "%s", config_path
        );
        gtk_dialog_run(GTK_DIALOG(path_dialog));
        gtk_widget_destroy(path_dialog);
    } else if (response == 3) {
        /* Open config folder in file manager */
        char cmd[1024];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(cmd, sizeof(cmd), "xdg-open %s/.config/neowall", home);
            if (system(cmd) != 0) {
                dialog_show_error("File Manager Error",
                                 "Failed to open configuration directory.\n"
                                 "Please navigate to: ~/.config/neowall");
            }
        } else {
            dialog_show_error("Path Error",
                             "Could not determine HOME directory.\n"
                             "Please navigate to: ~/.config/neowall");
        }
    }
}

void menu_callback_start_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (command_execute("start")) {
        dialog_show_info("Starting Daemon",
                        "NeoWall daemon is starting...\n"
                        "Please wait a moment.",
                        2000);  /* Auto-close after 2 seconds */

        /* Schedule full menu refresh after daemon starts (menu structure changes) */
        menu_schedule_refresh();
    } else {
        dialog_show_error("Start Failed",
                         "Failed to start NeoWall daemon.\n"
                         "Please check the logs for details.");
    }
}

void menu_callback_restart_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (command_execute("restart")) {
        dialog_show_info("Restarting Daemon",
                        "NeoWall daemon is restarting...\n"
                        "Please wait a moment.",
                        2000);  /* Auto-close after 2 seconds */

        /* Schedule full menu refresh after daemon restarts (menu structure changes) */
        menu_schedule_refresh();
    } else {
        dialog_show_error("Restart Failed",
                         "Failed to restart NeoWall daemon.\n"
                         "Please check the logs for details.");
    }
}

void menu_callback_stop_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    /* Ask for confirmation */
    gint result = dialog_confirm_stop_daemon();

    if (result == GTK_RESPONSE_YES) {
        if (command_execute("stop")) {
            dialog_show_info("Stopping Daemon",
                            "NeoWall daemon is stopping...",
                            2000);  /* Auto-close after 2 seconds */

            /* Schedule full menu refresh after daemon stops (menu structure changes) */
            menu_schedule_refresh();
        } else {
            dialog_show_error("Stop Failed",
                             "Failed to stop NeoWall daemon.\n"
                             "The daemon may not be running.");
        }
    }
}

/* Info callbacks */
void menu_callback_about(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    dialog_show_about();
}

void menu_callback_quit(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    gtk_main_quit();
}
