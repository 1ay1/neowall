/* NeoWall Tray - Menu Callbacks Component
 * Implementation of menu callback functions
 */

#include "menu_callbacks.h"
#include "../daemon/command_exec.h"
#include "../daemon/daemon_check.h"
#include "../dialogs/dialogs.h"
#include "../dialogs/settings_dialog.h"
#include "../dialogs/shader_editor.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* Timing Constants (milliseconds) */
#define DAEMON_OPERATION_DELAY 800
#define CONFIG_RELOAD_DELAY 1500

/* Path Constants */
#define CONFIG_SUBDIR ".config/neowall"
#define CONFIG_FILENAME "config.vibe"
#define SHADERS_SUBDIR "shaders"

/* Forward declaration for menu update functions */
extern void menu_schedule_refresh(void);

/* Helper: Get configuration directory path */
static bool get_config_dir(char *buffer, size_t size) {
    const char *home = getenv("HOME");
    if (!home) {
        return false;
    }
    int result = snprintf(buffer, size, "%s/%s", home, CONFIG_SUBDIR);
    return (result >= 0 && (size_t)result < size);
}

/* Helper: Get configuration file path */
static bool get_config_path(char *buffer, size_t size) {
    char config_dir[PATH_MAX];
    if (!get_config_dir(config_dir, sizeof(config_dir))) {
        return false;
    }
    int result = snprintf(buffer, size, "%s/%s", config_dir, CONFIG_FILENAME);
    return (result >= 0 && (size_t)result < size);
}

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
void menu_callback_edit_shader(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    shader_editor_show();
}

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
        dialog_show_info("✅ Configuration Reloaded",
                        "The daemon has successfully reloaded your configuration.",
                        CONFIG_RELOAD_DELAY);
    } else {
        dialog_show_error("❌ Configuration Reload Failed",
                         "Failed to reload configuration.\n\n"
                         "Please check your configuration file for syntax errors.");
    }
}

void menu_callback_edit_config(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    /* Create dialog with configuration options */
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "📝 Configuration Options"
    );

    gtk_message_dialog_format_secondary_markup(
        GTK_MESSAGE_DIALOG(dialog),
        "Choose how you would like to configure NeoWall:\n\n"
        "• <b>Open Editor</b> - Edit configuration file in your default editor\n"
        "• <b>Show Path</b> - Display the configuration file location\n"
        "• <b>Open Folder</b> - Open the configuration directory in file manager"
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
            dialog_show_error("❌ Editor Error",
                             "Failed to open configuration file.\n\n"
                             "Please check your EDITOR environment variable.");
        }
    } else if (response == 2) {
        /* Show config path */
        char config_path[PATH_MAX];
        if (!get_config_path(config_path, sizeof(config_path))) {
            snprintf(config_path, sizeof(config_path), "~/%s/%s", CONFIG_SUBDIR, CONFIG_FILENAME);
        }

        GtkWidget *path_dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "📁 Configuration File Path"
        );

        /* Make path selectable */
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(path_dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);

        GtkWidget *path_label = gtk_label_new(config_path);
        gtk_label_set_selectable(GTK_LABEL(path_label), TRUE);
        gtk_widget_set_margin_top(path_label, 8);
        gtk_widget_set_margin_bottom(path_label, 8);
        gtk_box_pack_start(GTK_BOX(content), path_label, FALSE, FALSE, 0);
        gtk_widget_show(path_label);
        gtk_dialog_run(GTK_DIALOG(path_dialog));
        gtk_widget_destroy(path_dialog);
    } else if (response == 3) {
        /* Open config folder in file manager */
        char config_dir[PATH_MAX];
        if (get_config_dir(config_dir, sizeof(config_dir))) {
            char cmd[PATH_MAX + 32];
            snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", config_dir);
            if (system(cmd) != 0) {
                dialog_show_error("❌ File Manager Error",
                                 "Failed to open configuration directory.\n\n"
                                 "Please navigate to: ~/.config/neowall");
            }
        } else {
            dialog_show_error("❌ Path Error",
                             "Could not determine HOME directory.\n\n"
                             "Please navigate to: ~/.config/neowall");
        }
    }
}

/* Check callback for daemon start */
static gboolean daemon_started_check(void) {
    return daemon_is_running();
}

/* Check callback for daemon stop */
static gboolean daemon_stopped_check(void) {
    return !daemon_is_running();
}

void menu_callback_start_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (command_execute("start")) {
        dialog_show_progress_auto_close("🚀 Starting Daemon",
                                       "Please wait while NeoWall daemon starts up...",
                                       daemon_started_check,
                                       DAEMON_OPERATION_DELAY);

        /* Schedule full menu refresh after daemon starts (menu structure changes) */
        menu_schedule_refresh();
    } else {
        dialog_show_error("❌ Start Failed", "Failed to start NeoWall daemon.");
    }
}

void menu_callback_restart_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (command_execute("restart")) {
        dialog_show_progress_auto_close("🔁 Restarting Daemon",
                                       "Please wait while NeoWall daemon restarts...",
                                       daemon_started_check,
                                       DAEMON_OPERATION_DELAY);

        /* Schedule full menu refresh after daemon restarts (menu structure changes) */
        menu_schedule_refresh();
    } else {
        dialog_show_error("❌ Restart Failed", "Failed to restart NeoWall daemon.");
    }
}

void menu_callback_stop_daemon(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    /* Ask for confirmation */
    gint result = dialog_confirm_stop_daemon();

    if (result == GTK_RESPONSE_YES) {
        if (command_execute("stop")) {
            dialog_show_progress_auto_close("🛑 Stopping Daemon",
                                           "Please wait while NeoWall daemon shuts down...",
                                           daemon_stopped_check,
                                           DAEMON_OPERATION_DELAY);

            /* Schedule full menu refresh after daemon stops (menu structure changes) */
            menu_schedule_refresh();
        } else {
            dialog_show_error("❌ Stop Failed", "Failed to stop NeoWall daemon.");
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
