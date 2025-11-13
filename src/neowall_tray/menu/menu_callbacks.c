/* NeoWall Tray - Menu Callbacks Component
 * Implementation of menu callback functions
 */

#include "menu_callbacks.h"
#include "../daemon/command_exec.h"
#include "../dialogs/dialogs.h"
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
    command_execute("pause");
}

void menu_callback_resume_cycling(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("resume");
}

/* Shader control callbacks */
void menu_callback_shader_pause(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("shader-pause");
}

void menu_callback_shader_resume(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("shader-resume");
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

void menu_callback_edit_config(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (!command_execute("config")) {
        dialog_show_error("Configuration Error",
                         "Failed to open configuration file.\n"
                         "Please check your editor settings.");
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
