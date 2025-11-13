/* NeoWall Tray - Settings Dialog Implementation
 * Modular settings UI with per-output configuration
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
#define MAX_OUTPUTS 16

/* Per-output/scope settings widgets */
typedef struct {
    char scope[64];  /* "default" or output name like "DP-1" */

    /* Widgets */
    GtkWidget *folder_chooser;
    GtkWidget *duration_spin;
    GtkWidget *mode_combo;
    GtkWidget *speed_scale;
    GtkWidget *fps_spin;
    GtkWidget *vsync_check;

    /* Original values for revert */
    char original_folder[512];
    double original_duration;
    int original_mode_index;
    double original_speed;
    int original_fps;
    gboolean original_vsync;

    /* Track if this scope has changes */
    gboolean has_changes;
} ScopeSettings;

/* Main settings dialog state */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *notebook;
    GtkWidget *apply_button;
    GtkWidget *revert_button;
    GtkWidget *status_label;

    /* Array of per-scope settings */
    ScopeSettings scopes[MAX_OUTPUTS];
    int scope_count;

    /* Global change tracking */
    gboolean has_any_changes;
} SettingsDialog;

/* Helper to show status message */
static void show_status(SettingsDialog *dlg, const char *message, gboolean is_error) {
    if (!dlg->status_label) return;

    char markup[512];
    if (is_error) {
        snprintf(markup, sizeof(markup), "<span foreground='red'>✗ %s</span>", message);
    } else {
        snprintf(markup, sizeof(markup), "<span foreground='green'>✓ %s</span>", message);
    }

    gtk_label_set_markup(GTK_LABEL(dlg->status_label), markup);
}

/* Parse config value from daemon JSON response */
static gboolean parse_config_value(const char *output, char *value, size_t value_size) {
    /* Response format: {"data":{"message":"...","data":{"key":"...","value":"..."}}} */
    /* We need to find the LAST occurrence of "value": which is the actual config value */
    const char *value_start = NULL;
    const char *pos = output;

    /* Find all occurrences of "value": and use the last one */
    while ((pos = strstr(pos, "\"value\"")) != NULL) {
        value_start = pos;
        pos += 7;  /* Move past "value" */
    }

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
        /* Number value */
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
        strncpy(value, "true", value_size - 1);
        value[value_size - 1] = '\0';
        return TRUE;
    } else if (strncmp(value_start, "false", 5) == 0) {
        strncpy(value, "false", value_size - 1);
        value[value_size - 1] = '\0';
        return TRUE;
    }

    return FALSE;
}

/* Load configuration for a specific scope */
static void load_scope_config(ScopeSettings *scope) {
    char output[4096];
    char cmd[256];
    int values_loaded = 0;

    TRAY_LOG_INFO(COMPONENT, "Loading config for scope: %s", scope->scope);

    /* Load path */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.path\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char path[512] = {0};
        if (parse_config_value(output, path, sizeof(path)) && path[0] != '\0') {
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(scope->folder_chooser), path);
            size_t len = strlen(path);
            if (len >= sizeof(scope->original_folder)) len = sizeof(scope->original_folder) - 1;
            memcpy(scope->original_folder, path, len);
            scope->original_folder[len] = '\0';
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded path: %s", scope->scope, path);
            values_loaded++;
        }
    }

    /* Load duration */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.duration\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char duration_str[32] = {0};
        if (parse_config_value(output, duration_str, sizeof(duration_str))) {
            double duration = atof(duration_str);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin), duration);
            scope->original_duration = duration;
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded duration: %.0f", scope->scope, duration);
            values_loaded++;
        }
    } else {
        scope->original_duration = 60.0;
    }

    /* Load mode */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.mode\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char mode[32] = {0};
        if (parse_config_value(output, mode, sizeof(mode))) {
            const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
            for (int i = 0; i < 5; i++) {
                if (strcmp(mode, mode_names[i]) == 0) {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo), i);
                    scope->original_mode_index = i;
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded mode: %s", scope->scope, mode);
                    values_loaded++;
                    break;
                }
            }
        }
    } else {
        scope->original_mode_index = 0;
    }

    /* Load shader speed */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.shader_speed\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char speed_str[32] = {0};
        if (parse_config_value(output, speed_str, sizeof(speed_str))) {
            double speed = atof(speed_str);
            if (speed >= 0.1 && speed <= 5.0) {
                gtk_range_set_value(GTK_RANGE(scope->speed_scale), speed);
                scope->original_speed = speed;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded speed: %.2f", scope->scope, speed);
                values_loaded++;
            }
        }
    } else {
        scope->original_speed = 1.0;
    }

    /* Load FPS */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.shader_fps\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char fps_str[32] = {0};
        if (parse_config_value(output, fps_str, sizeof(fps_str))) {
            int fps = atoi(fps_str);
            if (fps >= 1 && fps <= 144) {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), fps);
                scope->original_fps = fps;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded FPS: %d", scope->scope, fps);
                values_loaded++;
            }
        }
    } else {
        scope->original_fps = 60;
    }

    /* Load vsync */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.vsync\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char vsync_str[32] = {0};
        if (parse_config_value(output, vsync_str, sizeof(vsync_str))) {
            gboolean vsync = (strcmp(vsync_str, "true") == 0 || strcmp(vsync_str, "1") == 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check), vsync);
            scope->original_vsync = vsync;
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded vsync: %s", scope->scope, vsync ? "true" : "false");
            values_loaded++;
        }
    } else {
        scope->original_vsync = TRUE;
    }

    scope->has_changes = FALSE;

    TRAY_LOG_INFO(COMPONENT, "[%s] Loaded %d config values", scope->scope, values_loaded);
}

/* Mark that a scope has changes */
static void on_scope_setting_changed(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SettingsDialog *dlg = (SettingsDialog *)user_data;

    /* Find which scope changed by checking the widget */
    for (int i = 0; i < dlg->scope_count; i++) {
        ScopeSettings *scope = &dlg->scopes[i];
        if (widget == scope->folder_chooser ||
            widget == scope->duration_spin ||
            widget == scope->mode_combo ||
            widget == scope->speed_scale ||
            widget == scope->fps_spin ||
            widget == scope->vsync_check) {

            if (!scope->has_changes) {
                scope->has_changes = TRUE;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Settings modified", scope->scope);
            }
            break;
        }
    }

    /* Check if any scope has changes */
    gboolean any_changes = FALSE;
    for (int i = 0; i < dlg->scope_count; i++) {
        if (dlg->scopes[i].has_changes) {
            any_changes = TRUE;
            break;
        }
    }

    if (any_changes != dlg->has_any_changes) {
        dlg->has_any_changes = any_changes;
        gtk_widget_set_sensitive(dlg->apply_button, any_changes);
        gtk_widget_set_sensitive(dlg->revert_button, any_changes);

        if (any_changes) {
            show_status(dlg, "Settings modified (not saved)", FALSE);
        }
    }
}

/* Revert a scope to original values */
static void revert_scope(ScopeSettings *scope) {
    if (scope->original_folder[0] != '\0') {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(scope->folder_chooser),
                                     scope->original_folder);
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin),
                             scope->original_duration);
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo),
                            scope->original_mode_index);
    gtk_range_set_value(GTK_RANGE(scope->speed_scale),
                       scope->original_speed);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin),
                             scope->original_fps);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check),
                                scope->original_vsync);

    scope->has_changes = FALSE;
}

/* Revert all scopes */
static void on_revert_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    SettingsDialog *dlg = (SettingsDialog *)user_data;

    TRAY_LOG_INFO(COMPONENT, "Reverting all settings");

    for (int i = 0; i < dlg->scope_count; i++) {
        revert_scope(&dlg->scopes[i]);
    }

    dlg->has_any_changes = FALSE;
    gtk_widget_set_sensitive(dlg->apply_button, FALSE);
    gtk_widget_set_sensitive(dlg->revert_button, FALSE);

    show_status(dlg, "All settings reverted", FALSE);
}

/* Apply settings for a specific scope */
static gboolean apply_scope_settings(ScopeSettings *scope, char *error_msg, size_t error_size) {
    if (!scope->has_changes) {
        return TRUE;  /* Nothing to do */
    }

    char cmd[1024];
    gboolean success = TRUE;
    int changes_applied = 0;

    TRAY_LOG_INFO(COMPONENT, "Applying settings for scope: %s", scope->scope);

    /* Get values from widgets */
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(scope->folder_chooser));
    double duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(scope->duration_spin));
    int mode_index = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->mode_combo));
    double speed = gtk_range_get_value(GTK_RANGE(scope->speed_scale));
    int fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(scope->fps_spin));
    gboolean vsync = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope->vsync_check));

    const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
    const char *mode = (mode_index >= 0 && mode_index < 5) ? mode_names[mode_index] : "fill";

    /* Set path */
    if (folder && folder[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.path\" \"%s\"", scope->scope, folder);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set path", scope->scope);
            success = FALSE;
        }
    }

    /* Set duration */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.duration\" \"%.0f\"", scope->scope, duration);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set duration", scope->scope);
            success = FALSE;
        }
    }

    /* Set mode */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.mode\" \"%s\"", scope->scope, mode);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set mode", scope->scope);
            success = FALSE;
        }
    }

    /* Set shader speed */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.shader_speed\" \"%.2f\"", scope->scope, speed);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set shader speed", scope->scope);
            success = FALSE;
        }
    }

    /* Set FPS */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.shader_fps\" \"%d\"", scope->scope, fps);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set FPS", scope->scope);
            success = FALSE;
        }
    }

    /* Set vsync */
    if (success) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.vsync\" \"%s\"", scope->scope, vsync ? "true" : "false");
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set vsync", scope->scope);
            success = FALSE;
        }
    }

    if (success) {
        TRAY_LOG_INFO(COMPONENT, "[%s] Applied %d changes", scope->scope, changes_applied);

        /* Update original values */
        if (folder) {
            size_t len = strlen(folder);
            if (len >= sizeof(scope->original_folder)) len = sizeof(scope->original_folder) - 1;
            memcpy(scope->original_folder, folder, len);
            scope->original_folder[len] = '\0';
        }
        scope->original_duration = duration;
        scope->original_mode_index = mode_index;
        scope->original_speed = speed;
        scope->original_fps = fps;
        scope->original_vsync = vsync;

        scope->has_changes = FALSE;
    }

    if (folder) g_free(folder);

    return success;
}

/* Apply all changed settings */
static gboolean apply_all_settings(SettingsDialog *dlg) {
    char error_msg[256] = {0};
    int scopes_applied = 0;

    show_status(dlg, "Applying settings...", FALSE);

    /* Apply each scope that has changes */
    for (int i = 0; i < dlg->scope_count; i++) {
        if (dlg->scopes[i].has_changes) {
            if (apply_scope_settings(&dlg->scopes[i], error_msg, sizeof(error_msg))) {
                scopes_applied++;
            } else {
                show_status(dlg, error_msg, TRUE);
                dialog_show_error("Settings Error", error_msg);
                return FALSE;
            }
        }
    }

    /* Reload configuration */
    if (scopes_applied > 0) {
        if (command_execute("reload")) {
            char success_msg[128];
            snprintf(success_msg, sizeof(success_msg),
                    "Applied settings for %d scope%s",
                    scopes_applied, scopes_applied == 1 ? "" : "s");
            show_status(dlg, success_msg, FALSE);

            dialog_show_info("Settings Applied",
                           "Your settings have been saved and applied.",
                           2000);

            dlg->has_any_changes = FALSE;
            gtk_widget_set_sensitive(dlg->apply_button, FALSE);
            gtk_widget_set_sensitive(dlg->revert_button, FALSE);

            return TRUE;
        } else {
            show_status(dlg, "Failed to reload configuration", TRUE);
            dialog_show_error("Reload Failed",
                            "Settings were saved but failed to reload.\n"
                            "Try using 'Reload Configuration' from the menu.");
            return FALSE;
        }
    } else {
        show_status(dlg, "No changes to apply", FALSE);
    }

    return TRUE;
}

/* Create settings widgets for a scope */
static GtkWidget *create_scope_tab(SettingsDialog *dlg, ScopeSettings *scope) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(box), 15);

    /* Header */
    GtkWidget *header = gtk_label_new(NULL);
    char header_text[256];
    if (strcmp(scope->scope, "default") == 0) {
        snprintf(header_text, sizeof(header_text),
                "<big><b>Default Settings</b></big>\n"
                "<small>These settings apply to all outputs unless overridden</small>");
    } else {
        snprintf(header_text, sizeof(header_text),
                "<big><b>Output: %s</b></big>\n"
                "<small>Settings specific to this output (overrides defaults)</small>",
                scope->scope);
    }
    gtk_label_set_markup(GTK_LABEL(header), header_text);
    gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 5);

    /* Wallpaper folder */
    GtkWidget *folder_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_label), "<b>Wallpaper Folder:</b>");
    gtk_widget_set_halign(folder_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(folder_label, "Folder containing images or shaders");
    gtk_box_pack_start(GTK_BOX(box), folder_label, FALSE, FALSE, 0);

    scope->folder_chooser = gtk_file_chooser_button_new(
        "Select Wallpaper Folder",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    );
    gtk_widget_set_tooltip_text(scope->folder_chooser, "Choose folder with images or .glsl shaders");
    g_signal_connect(scope->folder_chooser, "file-set", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->folder_chooser, FALSE, FALSE, 0);

    /* Duration */
    GtkWidget *duration_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(duration_label), "<b>Cycle Duration (seconds):</b>");
    gtk_widget_set_halign(duration_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(duration_label, "Time between wallpaper changes (0 = manual only)");
    gtk_box_pack_start(GTK_BOX(box), duration_label, FALSE, FALSE, 0);

    GtkWidget *duration_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    scope->duration_spin = gtk_spin_button_new_with_range(0, 86400, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin), 60);
    gtk_widget_set_tooltip_text(scope->duration_spin, "0 = manual control only");
    g_signal_connect(scope->duration_spin, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(duration_box), scope->duration_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), duration_box, FALSE, FALSE, 0);

    /* Display mode */
    GtkWidget *mode_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(mode_label), "<b>Display Mode:</b>");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(mode_label, "How wallpapers are displayed");
    gtk_box_pack_start(GTK_BOX(box), mode_label, FALSE, FALSE, 0);

    scope->mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->mode_combo), "Fill");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->mode_combo), "Fit");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->mode_combo), "Center");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->mode_combo), "Stretch");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->mode_combo), "Tile");
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo), 0);
    gtk_widget_set_tooltip_text(scope->mode_combo,
                               "Fill: Cover screen (recommended)\n"
                               "Fit: Preserve aspect ratio\n"
                               "Center: Original size\n"
                               "Stretch: Fill screen (may distort)\n"
                               "Tile: Repeat pattern");
    g_signal_connect(scope->mode_combo, "changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->mode_combo, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep2, FALSE, FALSE, 10);

    /* Shader speed */
    GtkWidget *speed_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(speed_label), "<b>Animation Speed:</b>");
    gtk_widget_set_halign(speed_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(speed_label, "Speed multiplier for shader animations");
    gtk_box_pack_start(GTK_BOX(box), speed_label, FALSE, FALSE, 0);

    scope->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_scale_set_value_pos(GTK_SCALE(scope->speed_scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scope->speed_scale), 1);
    gtk_scale_add_mark(GTK_SCALE(scope->speed_scale), 1.0, GTK_POS_BOTTOM, "Normal");
    gtk_range_set_value(GTK_RANGE(scope->speed_scale), 1.0);
    gtk_widget_set_tooltip_text(scope->speed_scale, "Higher = faster (more GPU usage)");
    g_signal_connect(scope->speed_scale, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->speed_scale, FALSE, FALSE, 0);

    /* FPS */
    GtkWidget *fps_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fps_label), "<b>Shader FPS:</b>");
    gtk_widget_set_halign(fps_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(fps_label, "Target frames per second for shaders");
    gtk_box_pack_start(GTK_BOX(box), fps_label, FALSE, FALSE, 0);

    scope->fps_spin = gtk_spin_button_new_with_range(1, 144, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), 60);
    gtk_widget_set_tooltip_text(scope->fps_spin, "60 recommended for most displays");
    g_signal_connect(scope->fps_spin, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->fps_spin, FALSE, FALSE, 0);

    /* VSync */
    scope->vsync_check = gtk_check_button_new_with_label("Enable VSync");
    gtk_widget_set_tooltip_text(scope->vsync_check,
                               "Sync rendering to monitor refresh rate\n"
                               "Prevents tearing, reduces GPU usage");
    g_signal_connect(scope->vsync_check, "toggled", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->vsync_check, FALSE, FALSE, 5);

    return box;
}

/* Get list of outputs from daemon */
static int get_output_list(char outputs[][64], int max_outputs) {
    char cmd_output[8192];

    if (!command_execute_with_output("status", cmd_output, sizeof(cmd_output))) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to get output list from daemon");
        return 0;
    }

    /* Parse JSON to find outputs */
    int count = 0;
    const char *outputs_start = strstr(cmd_output, "\"outputs\":[");
    if (!outputs_start) {
        TRAY_LOG_DEBUG(COMPONENT, "No outputs found in status response");
        return 0;
    }

    /* Simple parsing - look for "name":"..." patterns */
    const char *pos = outputs_start;
    while (count < max_outputs) {
        const char *name_start = strstr(pos, "\"name\":\"");
        if (!name_start) break;

        name_start += 8;  /* Skip "name":" */
        const char *name_end = strchr(name_start, '"');
        if (!name_end) break;

        size_t len = name_end - name_start;
        if (len >= 64) len = 63;

        strncpy(outputs[count], name_start, len);
        outputs[count][len] = '\0';

        TRAY_LOG_DEBUG(COMPONENT, "Found output: %s", outputs[count]);
        count++;

        pos = name_end + 1;
    }

    return count;
}

/* Main settings dialog entry point */
void settings_dialog_show(void) {
    TRAY_LOG_INFO(COMPONENT, "Opening modular settings dialog");

    /* Check daemon */
    if (!daemon_is_running()) {
        dialog_show_error("Daemon Not Running",
                         "The NeoWall daemon is not running.\n"
                         "Please start the daemon before changing settings.");
        return;
    }

    /* Create dialog state */
    SettingsDialog *dlg = g_new0(SettingsDialog, 1);
    dlg->has_any_changes = FALSE;

    /* Create dialog */
    dlg->dialog = gtk_dialog_new_with_buttons(
        "NeoWall Settings",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL, NULL
    );

    /* Add buttons */
    dlg->revert_button = gtk_dialog_add_button(GTK_DIALOG(dlg->dialog),
                                               "Revert", GTK_RESPONSE_NONE);
    gtk_widget_set_sensitive(dlg->revert_button, FALSE);

    gtk_dialog_add_button(GTK_DIALOG(dlg->dialog), "Close", GTK_RESPONSE_CLOSE);

    dlg->apply_button = gtk_dialog_add_button(GTK_DIALOG(dlg->dialog),
                                              "Apply", GTK_RESPONSE_APPLY);
    gtk_widget_set_sensitive(dlg->apply_button, FALSE);

    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 650, 550);

    /* Content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Status label */
    dlg->status_label = gtk_label_new("");
    gtk_widget_set_halign(dlg->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content_area), dlg->status_label, FALSE, FALSE, 5);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(content_area), sep, FALSE, FALSE, 5);

    /* Create notebook */
    dlg->notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(content_area), dlg->notebook, TRUE, TRUE, 0);

    /* Get list of outputs */
    char output_names[MAX_OUTPUTS][64];
    int output_count = get_output_list(output_names, MAX_OUTPUTS);

    /* Add Default tab */
    strncpy(dlg->scopes[0].scope, "default", sizeof(dlg->scopes[0].scope) - 1);
    GtkWidget *default_tab = create_scope_tab(dlg, &dlg->scopes[0]);
    gtk_notebook_append_page(GTK_NOTEBOOK(dlg->notebook), default_tab,
                            gtk_label_new("Default"));
    dlg->scope_count = 1;

    /* Add per-output tabs */
    for (int i = 0; i < output_count && dlg->scope_count < MAX_OUTPUTS; i++) {
        size_t len = strlen(output_names[i]);
        if (len >= sizeof(dlg->scopes[dlg->scope_count].scope)) {
            len = sizeof(dlg->scopes[dlg->scope_count].scope) - 1;
        }
        memcpy(dlg->scopes[dlg->scope_count].scope, output_names[i], len);
        dlg->scopes[dlg->scope_count].scope[len] = '\0';

        GtkWidget *output_tab = create_scope_tab(dlg, &dlg->scopes[dlg->scope_count]);
        gtk_notebook_append_page(GTK_NOTEBOOK(dlg->notebook), output_tab,
                                gtk_label_new(output_names[i]));
        dlg->scope_count++;
    }

    TRAY_LOG_INFO(COMPONENT, "Created %d tabs (1 default + %d outputs)",
                 dlg->scope_count, output_count);

    /* Connect revert button */
    g_signal_connect(dlg->revert_button, "clicked",
                    G_CALLBACK(on_revert_clicked), dlg);

    /* Show and load config */
    gtk_widget_show_all(dlg->dialog);

    show_status(dlg, "Loading configuration...", FALSE);

    /* Load config for all scopes */
    for (int i = 0; i < dlg->scope_count; i++) {
        load_scope_config(&dlg->scopes[i]);
    }

    show_status(dlg, "Configuration loaded", FALSE);

    /* Run dialog */
    while (TRUE) {
        gint response = gtk_dialog_run(GTK_DIALOG(dlg->dialog));

        if (response == GTK_RESPONSE_APPLY) {
            if (apply_all_settings(dlg)) {
                continue;  /* Keep dialog open */
            }
            continue;
        } else if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) {
            if (dlg->has_any_changes) {
                GtkWidget *confirm = gtk_message_dialog_new(
                    GTK_WINDOW(dlg->dialog),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_QUESTION,
                    GTK_BUTTONS_YES_NO,
                    "Discard unsaved changes?"
                );

                gint confirm_response = gtk_dialog_run(GTK_DIALOG(confirm));
                gtk_widget_destroy(confirm);

                if (confirm_response != GTK_RESPONSE_YES) {
                    continue;
                }
            }
            break;
        } else {
            break;
        }
    }

    gtk_widget_destroy(dlg->dialog);
    g_free(dlg);

    TRAY_LOG_INFO(COMPONENT, "Settings dialog closed");
}
