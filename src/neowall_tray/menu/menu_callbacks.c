/* NeoWall Tray - Menu Callbacks Component
 * Implementation of menu callback functions
 */

#include "menu_callbacks.h"
#include "../daemon/command_exec.h"
#include "../dialogs/dialogs.h"
#include <stdio.h>

/* Forward declaration for menu refresh function */
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
    command_execute("config");
}

void menu_callback_start_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("start");

    /* Schedule menu refresh after daemon starts */
    menu_schedule_refresh();
}

void menu_callback_restart_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    command_execute("restart");

    /* Schedule menu refresh after daemon restarts */
    menu_schedule_refresh();
}

void menu_callback_stop_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    /* Ask for confirmation */
    gint result = dialog_confirm_stop_daemon();

    if (result == GTK_RESPONSE_YES) {
        command_execute("stop");

        /* Schedule menu refresh after daemon stops */
        menu_schedule_refresh();
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
