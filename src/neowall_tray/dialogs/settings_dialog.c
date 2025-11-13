/* NeoWall Tray - Settings Dialog Implementation
 * Comprehensive settings UI with excellent UX
 */

#include "settings_dialog.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "../daemon/daemon_check.h"
#include "dialogs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define COMPONENT "SETTINGS"

/* Widget references for accessing values */
typedef struct {
    /* Main dialog */
    GtkWidget *dialog;
    GtkWidget *apply_button;
    GtkWidget *revert_button;

    /* Wallpaper tab */
    GtkWidget *folder_chooser;
    GtkWidget *duration_spin;
    GtkWidget *mode_combo;

    /* Animation tab */
    GtkWidget *speed_scale;
    GtkWidget *fps_spin;
    GtkWidget *vsync_check;

    /* Status/info */
    GtkWidget *status_label;
    GtkWidget *info_bar;

    /* Track changes */
    gboolean has_changes;
} SettingsWidgets;

/* Original values for revert functionality */
typedef struct {
    char folder[512];
    double duration;
    int mode_index;
    double speed;
    int fps;
    gboolean vsync;
} OriginalSettings;

static OriginalSettings original_settings = {0};

/* Helper to show status message with icon */
static void show_status(SettingsWidgets *widgets, const char *message, gboolean is_error) {
    if (!widgets->status_label) return;

    char markup[512];
    if (is_error) {
        snprintf(markup, sizeof(markup), "<span foreground='red'>✗ %s</span>", message);
    } else {
        snprintf(markup, sizeof(markup), "<span foreground='green'>✓ %s</span>", message);
    }

    gtk_label_set_markup(GTK_LABEL(widgets->status_label), markup);
}

/* Parse config value from daemon output */
static gboolean parse_config_value(const char *output, char *value, size_t value_size) {
    /* Expected format: {"key":"...", "value":"something"} or {"key":"...", "value":123} */
    const char *value_start = strstr(output, "\"value\"");
    if (!value_start) return FALSE;

    value_start = strchr(value_start, ':');
    if (!value_start) return FALSE;
    value_start++;

    /* Skip whitespace */
    while (*value_start && isspace(*value_start)) value_start++;

    /* Check if string or number */
    if (*value_start == '"') {
        /* String value */
        value_start++;
        const char *value_end = strchr(value_start, '"');
        if (!value_end) return FALSE;

        size_t len = value_end - value_start;
        if (len >= value_size) len = value_size - 1;

        strncpy(value, value_start, len);
        value[len] = '\0';
        return TRUE;
    } else if (isdigit(*value_start) || *value_start == '-' || *value_start == '.') {
        /* Number value (integer or float) */
        const char *value_end = value_start;
        while (*value_end && (isdigit(*value_end) || *value_end == '.' || *value_end == '-')) {
            value_end++;
        }

        size_t len = value_end - value_start;
        if (len >= value_size) len = value_size - 1;

        strncpy(value, value_start, len);
        value[len] = '\0';
        return TRUE;
    } else if (strncmp(value_start, "true", 4) == 0) {
        /* Boolean true */
        strncpy(value, "true", value_size - 1);
        value[value_size - 1] = '\0';
        return TRUE;
    } else if (strncmp(value_start, "false", 5) == 0) {
        /* Boolean false */
        strncpy(value, "false", value_size - 1);
        value[value_size - 1] = '\0';
        return TRUE;
    }

    return FALSE;
}

/* Load current configuration from daemon */
static gboolean load_current_config(SettingsWidgets *widgets) {
    if (!daemon_is_running()) {
        show_status(widgets, "Daemon not running - using defaults", FALSE);
        TRAY_LOG_DEBUG(COMPONENT, "Daemon not running, using default values");

        /* Set defaults */
        original_settings.duration = 60.0;
        original_settings.mode_index = 0;
        original_settings.speed = 1.0;
        original_settings.fps = 60;
        original_settings.vsync = TRUE;

        widgets->has_changes = FALSE;
        gtk_widget_set_sensitive(widgets->apply_button, FALSE);
        gtk_widget_set_sensitive(widgets->revert_button, FALSE);
        return FALSE;
    }

    TRAY_LOG_INFO(COMPONENT, "Loading current configuration...");

    char output[4096];
    int values_loaded = 0;

    /* Load wallpaper path - try both 'path' (images) and 'shader' (shaders) */
    char path[512] = {0};
    gboolean path_loaded = FALSE;

    /* Try default.path first (for image folders) */
    if (command_execute_with_output("get-config \"default.path\"", output, sizeof(output))) {
        if (parse_config_value(output, path, sizeof(path)) && path[0] != '\0') {
            path_loaded = TRUE;
        }
    }

    /* If not found, try default.shader (for shader folders) */
    if (!path_loaded && command_execute_with_output("get-config \"default.shader\"", output, sizeof(output))) {
        if (parse_config_value(output, path, sizeof(path)) && path[0] != '\0') {
            path_loaded = TRUE;
        }
    }

    /* If we got a path, set it */
    if (path_loaded && path[0] != '\0') {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widgets->folder_chooser), path);
        strncpy(original_settings.folder, path, sizeof(original_settings.folder) - 1);
        original_settings.folder[sizeof(original_settings.folder) - 1] = '\0';
        TRAY_LOG_DEBUG(COMPONENT, "Loaded path: %s", path);
        values_loaded++;
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No wallpaper path set, using default");
    }

    /* Load cycle duration - use default 60 if not set */
    if (command_execute_with_output("get-config \"default.duration\"", output, sizeof(output))) {
        char duration_str[32] = {0};
        if (parse_config_value(output, duration_str, sizeof(duration_str))) {
            double duration = atof(duration_str);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->duration_spin), duration);
            original_settings.duration = duration;
            TRAY_LOG_DEBUG(COMPONENT, "Loaded duration: %.0f", duration);
            values_loaded++;
        }
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No duration set, using default 60s");
        original_settings.duration = 60.0;
    }

    /* Load display mode - use default 'fill' if not set */
    if (command_execute_with_output("get-config \"default.mode\"", output, sizeof(output))) {
        char mode[32] = {0};
        if (parse_config_value(output, mode, sizeof(mode))) {
            const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
            for (int i = 0; i < 5; i++) {
                if (strcmp(mode, mode_names[i]) == 0) {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->mode_combo), i);
                    original_settings.mode_index = i;
                    TRAY_LOG_DEBUG(COMPONENT, "Loaded mode: %s (index %d)", mode, i);
                    values_loaded++;
                    break;
                }
            }
        }
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No mode set, using default 'fill'");
        original_settings.mode_index = 0;
    }

    /* Load shader speed - use default 1.0 if not set */
    if (command_execute_with_output("get-config \"default.shader_speed\"", output, sizeof(output))) {
        char speed_str[32] = {0};
        if (parse_config_value(output, speed_str, sizeof(speed_str))) {
            double speed = atof(speed_str);
            if (speed >= 0.1 && speed <= 5.0) {
                gtk_range_set_value(GTK_RANGE(widgets->speed_scale), speed);
                original_settings.speed = speed;
                TRAY_LOG_DEBUG(COMPONENT, "Loaded shader speed: %.2f", speed);
                values_loaded++;
            }
        }
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No shader speed set, using default 1.0");
        original_settings.speed = 1.0;
    }

    /* Load shader FPS - use default 60 if not set */
    if (command_execute_with_output("get-config \"default.shader_fps\"", output, sizeof(output))) {
        char fps_str[32] = {0};
        if (parse_config_value(output, fps_str, sizeof(fps_str))) {
            int fps = atoi(fps_str);
            if (fps >= 1 && fps <= 144) {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->fps_spin), fps);
                original_settings.fps = fps;
                TRAY_LOG_DEBUG(COMPONENT, "Loaded shader FPS: %d", fps);
                values_loaded++;
            }
        }
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No shader FPS set, using default 60");
        original_settings.fps = 60;
    }

    /* Load vsync - use default true if not set */
    if (command_execute_with_output("get-config \"default.vsync\"", output, sizeof(output))) {
        char vsync_str[32] = {0};
        if (parse_config_value(output, vsync_str, sizeof(vsync_str))) {
            gboolean vsync = (strcmp(vsync_str, "true") == 0 || strcmp(vsync_str, "1") == 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->vsync_check), vsync);
            original_settings.vsync = vsync;
            TRAY_LOG_DEBUG(COMPONENT, "Loaded vsync: %s", vsync ? "true" : "false");
            values_loaded++;
        }
    } else {
        TRAY_LOG_DEBUG(COMPONENT, "No vsync set, using default true");
        original_settings.vsync = TRUE;
    }

    if (values_loaded > 0) {
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "Loaded %d configuration values", values_loaded);
        show_status(widgets, status_msg, FALSE);
        TRAY_LOG_INFO(COMPONENT, "Configuration loaded: %d values", values_loaded);
    } else {
        show_status(widgets, "Using default values (no config found)", FALSE);
        TRAY_LOG_INFO(COMPONENT, "No configuration values found, using defaults");
    }

    widgets->has_changes = FALSE;
    gtk_widget_set_sensitive(widgets->apply_button, FALSE);
    gtk_widget_set_sensitive(widgets->revert_button, FALSE);

    return (values_loaded > 0);
}

/* Mark that settings have changed */
static void on_setting_changed(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SettingsWidgets *widgets = (SettingsWidgets *)user_data;

    if (!widgets->has_changes) {
        widgets->has_changes = TRUE;
        gtk_widget_set_sensitive(widgets->apply_button, TRUE);
        gtk_widget_set_sensitive(widgets->revert_button, TRUE);
        show_status(widgets, "Settings modified (not saved)", FALSE);
    }
}

/* Revert all settings to original values */
static void on_revert_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SettingsWidgets *widgets = (SettingsWidgets *)user_data;

    TRAY_LOG_INFO(COMPONENT, "Reverting settings to original values");

    /* Restore original values */
    if (original_settings.folder[0] != '\0') {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widgets->folder_chooser),
                                     original_settings.folder);
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->duration_spin),
                             original_settings.duration);
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->mode_combo),
                            original_settings.mode_index);
    gtk_range_set_value(GTK_RANGE(widgets->speed_scale),
                       original_settings.speed);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->fps_spin),
                             original_settings.fps);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->vsync_check),
                                original_settings.vsync);

    widgets->has_changes = FALSE;
    gtk_widget_set_sensitive(widgets->apply_button, FALSE);
    gtk_widget_set_sensitive(widgets->revert_button, FALSE);

    show_status(widgets, "Settings reverted to original values", FALSE);
}

/* Validate settings before applying */
static gboolean validate_settings(SettingsWidgets *widgets, char *error_msg, size_t error_size) {
    /* Validate duration */
    double duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widgets->duration_spin));
    if (duration < 0 || duration > 86400) {
        snprintf(error_msg, error_size, "Duration must be between 0 and 86400 seconds");
        return FALSE;
    }

    /* Validate shader speed */
    double speed = gtk_range_get_value(GTK_RANGE(widgets->speed_scale));
    if (speed < 0.1 || speed > 5.0) {
        snprintf(error_msg, error_size, "Shader speed must be between 0.1 and 5.0");
        return FALSE;
    }

    /* Validate FPS */
    int fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->fps_spin));
    if (fps < 1 || fps > 144) {
        snprintf(error_msg, error_size, "FPS must be between 1 and 144");
        return FALSE;
    }

    /* Validate folder path */
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->folder_chooser));
    if (folder) {
        if (strlen(folder) == 0) {
            g_free(folder);
            snprintf(error_msg, error_size, "Please select a wallpaper folder");
            return FALSE;
        }
        g_free(folder);
    }

    return TRUE;
}

/* Apply settings to daemon */
static gboolean apply_settings(SettingsWidgets *widgets) {
    char error_msg[256] = {0};

    /* Validate first */
    if (!validate_settings(widgets, error_msg, sizeof(error_msg))) {
        show_status(widgets, error_msg, TRUE);
        dialog_show_error("Invalid Settings", error_msg);
        return FALSE;
    }

    show_status(widgets, "Applying settings...", FALSE);

    /* Get values from widgets */
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->folder_chooser));
    double duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widgets->duration_spin));
    int mode_index = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->mode_combo));
    double speed = gtk_range_get_value(GTK_RANGE(widgets->speed_scale));
    int fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->fps_spin));
    gboolean vsync = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->vsync_check));

    /* Map mode index to mode string */
    const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
    const char *mode = (mode_index >= 0 && mode_index < 5) ? mode_names[mode_index] : "fill";

    gboolean success = TRUE;
    char cmd[1024];
    int changes_applied = 0;

    /* Set wallpaper folder/path */
    if (folder && folder[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "set-config \"default.path\" \"%s\"", folder);
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set wallpaper path: %s", folder);
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set wallpaper path");
            show_status(widgets, "Failed to set wallpaper path", TRUE);
            success = FALSE;
        }
    }

    /* Set cycle duration */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"default.duration\" \"%.0f\"", duration);
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set duration: %.0f", duration);
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set duration");
            show_status(widgets, "Failed to set cycle duration", TRUE);
            success = FALSE;
        }
    }

    /* Set display mode */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"default.mode\" \"%s\"", mode);
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set mode: %s", mode);
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set display mode");
            show_status(widgets, "Failed to set display mode", TRUE);
            success = FALSE;
        }
    }

    /* Set shader speed */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"default.shader_speed\" \"%.2f\"", speed);
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set shader speed: %.2f", speed);
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set shader speed");
            show_status(widgets, "Failed to set shader speed", TRUE);
            success = FALSE;
        }
    }

    /* Set shader FPS */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"default.shader_fps\" \"%d\"", fps);
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set shader FPS: %d", fps);
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set shader FPS");
            show_status(widgets, "Failed to set shader FPS", TRUE);
            success = FALSE;
        }
    }

    /* Set vsync */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"default.vsync\" \"%s\"", vsync ? "true" : "false");
        if (command_execute(cmd)) {
            TRAY_LOG_DEBUG(COMPONENT, "Set vsync: %s", vsync ? "true" : "false");
            changes_applied++;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to set vsync");
            show_status(widgets, "Failed to set vsync", TRUE);
            success = FALSE;
        }
    }

    /* Reload configuration to apply changes */
    if (success) {
        if (command_execute("reload")) {
            TRAY_LOG_INFO(COMPONENT, "Settings applied successfully (%d changes)", changes_applied);

            char success_msg[256];
            snprintf(success_msg, sizeof(success_msg),
                    "%d settings applied successfully", changes_applied);
            show_status(widgets, success_msg, FALSE);

            dialog_show_info("Settings Applied",
                           "Your settings have been saved and applied.\n"
                           "Changes are now active.",
                           2000);

            /* Update original settings */
            if (folder) {
                strncpy(original_settings.folder, folder, sizeof(original_settings.folder) - 1);
                original_settings.folder[sizeof(original_settings.folder) - 1] = '\0';
            }
            original_settings.duration = duration;
            original_settings.mode_index = mode_index;
            original_settings.speed = speed;
            original_settings.fps = fps;
            original_settings.vsync = vsync;

            widgets->has_changes = FALSE;
            gtk_widget_set_sensitive(widgets->apply_button, FALSE);
            gtk_widget_set_sensitive(widgets->revert_button, FALSE);
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Failed to reload configuration");
            show_status(widgets, "Failed to reload configuration", TRUE);
            dialog_show_error("Reload Failed",
                            "Settings were saved but failed to reload.\n"
                            "Try using 'Reload Configuration' from the menu.");
            success = FALSE;
        }
    } else {
        dialog_show_error("Settings Error",
                        "Failed to apply some settings.\n"
                        "Please check the log for details.");
    }

    if (folder) g_free(folder);

    return success;
}



/* Create the Wallpaper Settings tab */
static GtkWidget *create_wallpaper_tab(SettingsWidgets *widgets) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Wallpaper folder selection */
    GtkWidget *folder_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_label), "<b>Wallpaper Folder:</b>");
    gtk_widget_set_halign(folder_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(folder_label, "Select a folder containing wallpaper images or shaders");
    gtk_box_pack_start(GTK_BOX(box), folder_label, FALSE, FALSE, 0);

    widgets->folder_chooser = gtk_file_chooser_button_new(
        "Select Wallpaper Folder",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    );
    gtk_widget_set_tooltip_text(widgets->folder_chooser,
                               "Choose a folder with images (JPG, PNG) or shaders (.glsl)");
    g_signal_connect(widgets->folder_chooser, "file-set", G_CALLBACK(on_setting_changed), widgets);
    gtk_box_pack_start(GTK_BOX(box), widgets->folder_chooser, FALSE, FALSE, 0);

    GtkWidget *folder_help = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_help),
                        "<small><i>Supported: Images (JPG, PNG, WebP) and Shaders (.glsl)</i></small>");
    gtk_widget_set_halign(folder_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), folder_help, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep1, FALSE, FALSE, 10);

    /* Cycle duration */
    GtkWidget *duration_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(duration_label), "<b>Cycle Duration (seconds):</b>");
    gtk_widget_set_halign(duration_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(duration_label, "Time between wallpaper changes (0 = disable auto-cycling)");
    gtk_box_pack_start(GTK_BOX(box), duration_label, FALSE, FALSE, 0);

    GtkWidget *duration_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    widgets->duration_spin = gtk_spin_button_new_with_range(0, 86400, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->duration_spin), 60);
    gtk_widget_set_tooltip_text(widgets->duration_spin,
                               "Seconds between wallpaper changes\n0 = manual control only");
    g_signal_connect(widgets->duration_spin, "value-changed", G_CALLBACK(on_setting_changed), widgets);
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
    gtk_widget_set_tooltip_text(mode_label, "How wallpapers are displayed on screen");
    gtk_box_pack_start(GTK_BOX(box), mode_label, FALSE, FALSE, 0);

    widgets->mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Fill - Cover entire screen (may crop)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Fit - Fit to screen (preserves aspect ratio)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Center - Center on screen (original size)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Stretch - Stretch to fill (may distort)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->mode_combo), "Tile - Repeat pattern");
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->mode_combo), 0);
    gtk_widget_set_tooltip_text(widgets->mode_combo,
                               "Fill: Cover screen (recommended)\n"
                               "Fit: Preserve aspect ratio with black bars\n"
                               "Center: Original size, centered\n"
                               "Stretch: Fill screen, may distort\n"
                               "Tile: Repeat image as pattern");
    g_signal_connect(widgets->mode_combo, "changed", G_CALLBACK(on_setting_changed), widgets);
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
    gtk_widget_set_tooltip_text(speed_label, "Speed multiplier for shader animations");
    gtk_box_pack_start(GTK_BOX(box), speed_label, FALSE, FALSE, 0);

    widgets->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->speed_scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(widgets->speed_scale), 1);
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 0.5, GTK_POS_BOTTOM, "Slow");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 1.0, GTK_POS_BOTTOM, "Normal");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 2.0, GTK_POS_BOTTOM, "Fast");
    gtk_scale_add_mark(GTK_SCALE(widgets->speed_scale), 5.0, GTK_POS_BOTTOM, "Very Fast");
    gtk_range_set_value(GTK_RANGE(widgets->speed_scale), 1.0);
    gtk_widget_set_tooltip_text(widgets->speed_scale,
                               "Higher values = faster animation (more GPU usage)\n"
                               "Lower values = slower animation (less GPU usage)");
    g_signal_connect(widgets->speed_scale, "value-changed", G_CALLBACK(on_setting_changed), widgets);
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

    /* Frame rate control */
    GtkWidget *fps_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fps_label), "<b>Shader Frame Rate (FPS):</b>");
    gtk_widget_set_halign(fps_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(fps_label, "Target frames per second for shader rendering");
    gtk_box_pack_start(GTK_BOX(box), fps_label, FALSE, FALSE, 0);

    GtkWidget *fps_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    widgets->fps_spin = gtk_spin_button_new_with_range(1, 144, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->fps_spin), 60);
    gtk_widget_set_tooltip_text(widgets->fps_spin,
                               "Target FPS for shader rendering\n"
                               "Higher = smoother but more GPU usage\n"
                               "60 FPS is recommended for most users");
    g_signal_connect(widgets->fps_spin, "value-changed", G_CALLBACK(on_setting_changed), widgets);
    gtk_box_pack_start(GTK_BOX(fps_box), widgets->fps_spin, FALSE, FALSE, 0);

    GtkWidget *fps_help = gtk_label_new("(60 recommended for most displays)");
    gtk_widget_set_halign(fps_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(fps_box), fps_help, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), fps_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep2, FALSE, FALSE, 10);

    /* VSync option */
    widgets->vsync_check = gtk_check_button_new_with_label(
        "Enable VSync (sync to monitor refresh rate)"
    );
    gtk_widget_set_tooltip_text(widgets->vsync_check,
                               "Synchronize rendering with monitor refresh rate\n"
                               "Prevents screen tearing but may cap FPS\n"
                               "Recommended for most users");
    g_signal_connect(widgets->vsync_check, "toggled", G_CALLBACK(on_setting_changed), widgets);
    gtk_box_pack_start(GTK_BOX(box), widgets->vsync_check, FALSE, FALSE, 5);

    GtkWidget *vsync_help = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(vsync_help),
                        "<small><i>When enabled, rendering syncs to your monitor's refresh rate\n"
                        "Prevents screen tearing and reduces GPU usage</i></small>");
    gtk_widget_set_halign(vsync_help, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), vsync_help, FALSE, FALSE, 0);

    return box;
}

/* Create the Advanced tab */
static GtkWidget *create_advanced_tab(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Header */
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label),
                        "<big><b>Configuration Files</b></big>");
    gtk_box_pack_start(GTK_BOX(box), header_label, FALSE, FALSE, 0);

    GtkWidget *desc_label = gtk_label_new(
        "NeoWall stores configuration in VIBE format.\n"
        "Most settings can be changed using this dialog."
    );
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), desc_label, FALSE, FALSE, 10);

    /* Config info */
    GtkWidget *config_info = gtk_label_new(NULL);
    const char *home = getenv("HOME");
    char config_text[512];
    if (home) {
        snprintf(config_text, sizeof(config_text),
                "<b>Configuration:</b> %s/.config/neowall/config.vibe\n"
                "<b>Runtime State:</b> %s/.local/state/neowall/daemon.state",
                home, home);
    } else {
        snprintf(config_text, sizeof(config_text),
                "<b>Configuration:</b> ~/.config/neowall/config.vibe\n"
                "<b>Runtime State:</b> ~/.local/state/neowall/daemon.state");
    }
    gtk_label_set_markup(GTK_LABEL(config_info), config_text);
    gtk_label_set_line_wrap(GTK_LABEL(config_info), TRUE);
    gtk_widget_set_halign(config_info, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), config_info, FALSE, FALSE, 10);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep1, FALSE, FALSE, 10);

    /* Info about manual editing */
    GtkWidget *manual_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(manual_label),
                        "<b>Advanced Configuration:</b>");
    gtk_widget_set_halign(manual_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), manual_label, FALSE, FALSE, 0);

    GtkWidget *manual_info = gtk_label_new(
        "For advanced options not available in this dialog,\n"
        "you can edit the config.vibe file manually.\n\n"
        "After manual changes, use 'Reload Configuration'\n"
        "from the tray menu to apply them."
    );
    gtk_label_set_line_wrap(GTK_LABEL(manual_info), TRUE);
    gtk_widget_set_halign(manual_info, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), manual_info, FALSE, FALSE, 5);

    /* Documentation link */
    GtkWidget *doc_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(doc_label),
                        "<small><i>VIBE format documentation:\n"
                        "https://1ay1.github.io/vibe/</i></small>");
    gtk_widget_set_halign(doc_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), doc_label, FALSE, FALSE, 5);

    return box;
}

/* Show the settings dialog */
void settings_dialog_show(void) {
    TRAY_LOG_INFO(COMPONENT, "Opening settings dialog");

    /* Check if daemon is running */
    if (!daemon_is_running()) {
        dialog_show_error("Daemon Not Running",
                         "The NeoWall daemon is not running.\n"
                         "Please start the daemon before changing settings.");
        return;
    }

    /* Create widgets struct */
    SettingsWidgets *widgets = g_new0(SettingsWidgets, 1);
    widgets->has_changes = FALSE;

    /* Create dialog */
    widgets->dialog = gtk_dialog_new_with_buttons(
        "NeoWall Settings",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL, NULL
    );

    /* Add custom buttons */
    widgets->revert_button = gtk_dialog_add_button(GTK_DIALOG(widgets->dialog),
                                                   "Revert", GTK_RESPONSE_NONE);
    gtk_widget_set_tooltip_text(widgets->revert_button, "Revert all changes to original values");
    gtk_widget_set_sensitive(widgets->revert_button, FALSE);

    gtk_dialog_add_button(GTK_DIALOG(widgets->dialog), "Close", GTK_RESPONSE_CLOSE);

    widgets->apply_button = gtk_dialog_add_button(GTK_DIALOG(widgets->dialog),
                                                  "Apply", GTK_RESPONSE_APPLY);
    gtk_widget_set_tooltip_text(widgets->apply_button, "Save and apply settings");
    gtk_widget_set_sensitive(widgets->apply_button, FALSE);

    gtk_window_set_default_size(GTK_WINDOW(widgets->dialog), 600, 500);

    /* Get content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Create status bar at top */
    widgets->status_label = gtk_label_new("");
    gtk_widget_set_halign(widgets->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content_area), widgets->status_label, FALSE, FALSE, 5);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(content_area), sep, FALSE, FALSE, 5);

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

    /* Connect revert button */
    g_signal_connect(widgets->revert_button, "clicked",
                    G_CALLBACK(on_revert_clicked), widgets);

    /* Show dialog */
    gtk_widget_show_all(widgets->dialog);

    /* Load current configuration */
    load_current_config(widgets);

    /* Run dialog */
    while (TRUE) {
        gint response = gtk_dialog_run(GTK_DIALOG(widgets->dialog));

        if (response == GTK_RESPONSE_APPLY) {
            /* Apply button clicked */
            if (apply_settings(widgets)) {
                /* Settings applied successfully, but keep dialog open */
                continue;
            }
            /* If apply failed, stay in dialog */
            continue;
        } else if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) {
            /* Close or X button */
            if (widgets->has_changes) {
                /* Warn about unsaved changes */
                GtkWidget *confirm = gtk_message_dialog_new(
                    GTK_WINDOW(widgets->dialog),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_QUESTION,
                    GTK_BUTTONS_YES_NO,
                    "Discard unsaved changes?"
                );
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(confirm),
                    "You have unsaved changes. Are you sure you want to close?"
                );

                gint confirm_response = gtk_dialog_run(GTK_DIALOG(confirm));
                gtk_widget_destroy(confirm);

                if (confirm_response != GTK_RESPONSE_YES) {
                    /* User chose to stay */
                    continue;
                }
            }
            break;
        } else {
            /* Other response (shouldn't happen) */
            break;
        }
    }

    gtk_widget_destroy(widgets->dialog);
    g_free(widgets);

    TRAY_LOG_DEBUG(COMPONENT, "Settings dialog closed");
}
