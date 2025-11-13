/* NeoWall Tray - Settings Dialog Implementation
 * User-friendly configuration dialog for common settings
 */

#include "settings_dialog.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "dialogs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define COMPONENT "SETTINGS"

/* Helper structure to track original values for comparison */
typedef struct {
    char folder[512];
    int duration;
    int mode_index;
    double speed;
    bool battery_pause;
} OriginalSettings;

/* Widget references for accessing values */
typedef struct {
    GtkWidget *folder_chooser;
    GtkWidget *duration_spin;
    GtkWidget *mode_combo;
    GtkWidget *speed_scale;
    GtkWidget *battery_check;
} SettingsWidgets;

/* Callback for "Open in Text Editor" button */
static void on_open_editor_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    TRAY_LOG_INFO(COMPONENT, "Opening configuration in text editor");

    if (!command_execute("config")) {
        dialog_show_error("Editor Error",
                         "Failed to open configuration file.\n"
                         "Please check your EDITOR environment variable.");
    }
}

/* Callback for "Show Configuration Path" button */
static void on_show_path_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

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
}

/* Callback for "Open Configuration Folder" button */
static void on_open_folder_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

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

/* Create the Wallpaper Settings tab */
static GtkWidget *create_wallpaper_tab(SettingsWidgets *widgets) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Wallpaper folder selection */
    GtkWidget *folder_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_label), "<b>Wallpaper Folder:</b>");
    gtk_widget_set_halign(folder_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), folder_label, FALSE, FALSE, 0);

    widgets->folder_chooser = gtk_file_chooser_button_new(
        "Select Wallpaper Folder",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    );

    /* Try to set current folder if HOME is available */
    const char *home = getenv("HOME");
    if (home) {
        char wallpaper_dir[512];
        snprintf(wallpaper_dir, sizeof(wallpaper_dir), "%s/Pictures/Wallpapers", home);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widgets->folder_chooser), wallpaper_dir);
    }

    gtk_box_pack_start(GTK_BOX(box), widgets->folder_chooser, FALSE, FALSE, 0);

    GtkWidget *folder_help = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_help),
                        "<small><i>Choose a folder containing wallpaper images or shaders</i></small>");
    gtk_widget_set_halign(folder_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), folder_help, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep1, FALSE, FALSE, 10);

    /* Cycle duration */
    GtkWidget *duration_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(duration_label), "<b>Cycle Duration (seconds):</b>");
    gtk_widget_set_halign(duration_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), duration_label, FALSE, FALSE, 0);

    GtkWidget *duration_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    widgets->duration_spin = gtk_spin_button_new_with_range(0, 3600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->duration_spin), 60);
    gtk_box_pack_start(GTK_BOX(duration_box), widgets->duration_spin, FALSE, FALSE, 0);

    GtkWidget *duration_help = gtk_label_new("(0 = disable auto-cycling)");
    gtk_widget_set_halign(duration_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(duration_box), duration_help, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), duration_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep2, FALSE, FALSE, 10);

    /* Display mode */
    GtkWidget *mode_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(mode_label), "<b>Display Mode:</b>");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), mode_label, FALSE, FALSE, 0);

    widgets->mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Fill - Cover entire screen");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Fit - Fit to screen preserving aspect");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Center - Center on screen");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Stretch - Stretch to fill");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Tile - Repeat pattern");
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->mode_combo), 0);
    gtk_box_pack_start(GTK_BOX(box), widgets->mode_combo, FALSE, FALSE, 0);

    return box;
}

/* Create the Animation Settings tab */
static GtkWidget *create_animation_tab(SettingsWidgets *widgets) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Shader speed */
    GtkWidget *speed_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(speed_label), "<b>Animation Speed:</b>");
    gtk_widget_set_halign(speed_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), speed_label, FALSE, FALSE, 0);

    widgets->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->speed_scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(widgets->speed_scale), 1);
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 0.5, GTK_POS_BOTTOM, "0.5x");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 1.0, GTK_POS_BOTTOM, "1.0x (Normal)");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 2.0, GTK_POS_BOTTOM, "2.0x");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 3.0, GTK_POS_BOTTOM, "3.0x");
    gtk_range_set_value(GTK_RANGE(widgets->speed_scale), 1.0);
    gtk_box_pack_start(GTK_BOX(box), widgets->speed_scale, FALSE, FALSE, 0);

    GtkWidget *speed_help = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(speed_help),
                        "<small><i>Controls shader animation speed\n"
                        "Higher values = faster animation, more GPU usage</i></small>");
    gtk_widget_set_halign(speed_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), speed_help, FALSE, FALSE, 5);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep1, FALSE, FALSE, 10);

    /* Auto-pause options */
    GtkWidget *auto_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(auto_label), "<b>Power Saving:</b>");
    gtk_widget_set_halign(auto_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), auto_label, FALSE, FALSE, 0);

    widgets->battery_check = gtk_check_button_new_with_label(
        "Pause animations on battery power (saves energy)"
    );
    gtk_box_pack_start(GTK_BOX(box), widgets->battery_check, FALSE, FALSE, 5);

    GtkWidget *battery_help = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(battery_help),
                        "<small><i>Automatically pause shader animations when laptop\n"
                        "is running on battery to extend battery life</i></small>");
    gtk_widget_set_halign(battery_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), battery_help, FALSE, FALSE, 0);

    return box;
}

/* Create the Advanced tab */
static GtkWidget *create_advanced_tab(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Header */
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label),
                        "<big><b>Advanced Configuration</b></big>");
    gtk_box_pack_start(GTK_BOX(box), header_label, FALSE, FALSE, 0);

    GtkWidget *desc_label = gtk_label_new(
        "For advanced configuration options not available in this dialog,\n"
        "you can edit the configuration file directly using VIBE format."
    );
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), desc_label, FALSE, FALSE, 10);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep1, FALSE, FALSE, 10);

    /* Buttons for advanced config access */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *open_editor_btn = gtk_button_new_with_label("Open in Text Editor");
    g_signal_connect(open_editor_btn, "clicked", G_CALLBACK(on_open_editor_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), open_editor_btn, FALSE, FALSE, 0);

    GtkWidget *show_path_btn = gtk_button_new_with_label("Show Configuration Path");
    g_signal_connect(show_path_btn, "clicked", G_CALLBACK(on_show_path_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), show_path_btn, FALSE, FALSE, 0);

    GtkWidget *open_folder_btn = gtk_button_new_with_label("Open Configuration Folder");
    g_signal_connect(open_folder_btn, "clicked", G_CALLBACK(on_open_folder_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), open_folder_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), button_box, FALSE, FALSE, 10);

    /* Config info */
    GtkWidget *config_info = gtk_label_new(NULL);
    const char *home = getenv("HOME");
    char config_path[512];
    if (home) {
        snprintf(config_path, sizeof(config_path),
                "<small><b>Configuration File:</b>\n"
                "<tt>%s/.config/neowall/config.vibe</tt></small>", home);
    } else {
        snprintf(config_path, sizeof(config_path),
                "<small><b>Configuration File:</b>\n"
                "<tt>~/.config/neowall/config.vibe</tt></small>");
    }
    gtk_label_set_markup(GTK_LABEL(config_info), config_path);
    gtk_label_set_line_wrap(GTK_LABEL(config_info), TRUE);
    gtk_box_pack_start(GTK_BOX(box), config_info, FALSE, FALSE, 5);

    /* Documentation link */
    GtkWidget *doc_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(doc_label),
                        "<small><i>See documentation for VIBE format details:\n"
                        "https://1ay1.github.io/vibe/</i></small>");
    gtk_box_pack_start(GTK_BOX(box), doc_label, FALSE, FALSE, 5);

    return box;
}

/* Show the settings dialog */
void settings_dialog_show(void) {
    TRAY_LOG_INFO(COMPONENT, "Opening settings dialog");

    /* Create widgets struct */
    SettingsWidgets *widgets = g_new0(SettingsWidgets, 1);

    /* Create dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "NeoWall Settings",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        "Apply", GTK_RESPONSE_APPLY,
        NULL
    );

    gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 450);

    /* Get content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create notebook for tabs */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(content_area), notebook, TRUE, TRUE, 0);

    /* Add tabs */
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                            create_wallpaper_tab(widgets),
                            gtk_label_new("Wallpaper"));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                            create_animation_tab(widgets),
                            gtk_label_new("Animation"));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                            create_advanced_tab(),
                            gtk_label_new("Advanced"));

    /* Show dialog */
    gtk_widget_show_all(dialog);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_APPLY) {
        TRAY_LOG_INFO(COMPONENT, "Applying settings...");

        /* Get values from widgets */
        gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->folder_chooser));
        gdouble duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widgets->duration_spin));
        gint mode_index = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->mode_combo));
        gdouble speed = gtk_range_get_value(GTK_RANGE(widgets->speed_scale));
        gboolean battery_pause = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->battery_check));

        /* Map mode index to mode string */
        const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
        const char *mode = (mode_index >= 0 && mode_index < 5) ? mode_names[mode_index] : "fill";

        /* Apply settings via set-config commands */
        bool success = true;
        char cmd[1024];

        /* Set wallpaper folder/path */
        if (folder && folder[0] != '\0') {
            snprintf(cmd, sizeof(cmd), "set-config \"default.path\" \"%s\"", folder);
            if (!command_execute(cmd)) {
                TRAY_LOG_ERROR(COMPONENT, "Failed to set wallpaper path");
                success = false;
            }
        }

        /* Set cycle duration */
        if (success) {
            snprintf(cmd, sizeof(cmd), "set-config \"default.duration\" \"%.0f\"", duration);
            if (!command_execute(cmd)) {
                TRAY_LOG_ERROR(COMPONENT, "Failed to set duration");
                success = false;
            }
        }

        /* Set display mode */
        if (success) {
            snprintf(cmd, sizeof(cmd), "set-config \"default.mode\" \"%s\"", mode);
            if (!command_execute(cmd)) {
                TRAY_LOG_ERROR(COMPONENT, "Failed to set display mode");
                success = false;
            }
        }

        /* Set shader speed */
        if (success) {
            snprintf(cmd, sizeof(cmd), "set-config \"default.shader_speed\" \"%.2f\"", speed);
            if (!command_execute(cmd)) {
                TRAY_LOG_ERROR(COMPONENT, "Failed to set shader speed");
                success = false;
            }
        }

        /* Set battery pause option (if we add this to config schema in future) */
        if (success && battery_pause) {
            TRAY_LOG_DEBUG(COMPONENT, "Battery pause option: %d (not yet implemented in config)", battery_pause);
        }

        /* Reload configuration to apply changes */
        if (success) {
            if (command_execute("reload")) {
                TRAY_LOG_INFO(COMPONENT, "Settings applied and configuration reloaded successfully");
                dialog_show_info("Settings Applied",
                                "Your settings have been saved and applied.\n"
                                "Changes will take effect immediately.",
                                3000);
            } else {
                TRAY_LOG_ERROR(COMPONENT, "Failed to reload configuration");
                dialog_show_error("Reload Failed",
                                "Settings were saved but failed to reload.\n"
                                "Try using 'Reload Configuration' from the menu.");
                success = false;
            }
        } else {
            dialog_show_error("Settings Error",
                            "Failed to apply some settings.\n"
                            "Please check the log for details.");
        }

        g_free(folder);
    }

    gtk_widget_destroy(dialog);
    g_free(widgets);

    TRAY_LOG_DEBUG(COMPONENT, "Settings dialog closed");
}
