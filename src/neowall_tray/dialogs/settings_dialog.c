/* NeoWall Tray - Settings Dialog Implementation
 * Modular settings UI with per-output configuration
 */

#include "settings_dialog.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "../daemon/daemon_check.h"
#include "../ui/ui_utils.h"
#include "dialogs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define COMPONENT "SETTINGS"
#define MAX_OUTPUTS 16

/* Per-output/scope settings widgets */
typedef struct {
    char scope[64];  /* "default" or output name like "DP-1" */

    /* Widgets */
    GtkWidget *type_combo;
    GtkWidget *folder_chooser;
    GtkWidget *duration_spin;
    GtkWidget *mode_combo;
    GtkWidget *speed_scale;
    GtkWidget *fps_spin;
    GtkWidget *vsync_check;
    GtkWidget *show_fps_check;
    GtkWidget *transition_combo;
    GtkWidget *transition_duration_spin;

    /* Widget containers for show/hide */
    GtkWidget *mode_container;
    GtkWidget *transition_container;
    GtkWidget *speed_container;
    GtkWidget *fps_container;
    GtkWidget *vsync_container;
    GtkWidget *show_fps_container;

    /* VSync state tracking */
    gboolean vsync_enabled;

    /* Original values for revert */
    int original_type_index;
    int current_type_index;  /* Current active type (for folder switching) */
    char original_folder[512];
    char last_image_folder[512];  /* Remember last image folder */
    char last_shader_folder[512]; /* Remember last shader folder */
    double original_duration;
    int original_mode_index;
    double original_speed;
    int original_fps;
    gboolean original_vsync;
    gboolean original_show_fps;
    int original_transition_index;
    double original_transition_duration;

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

    gboolean has_any_changes;
    gboolean loading_config;  /* Flag to prevent signal handling during reload */
} SettingsDialog;

/* Forward declarations */
static void on_scope_setting_changed(GtkWidget *widget, gpointer user_data);
static void on_folder_selected(GtkFileChooserButton *widget, gpointer user_data);

/* Helper to show status message */
static void show_status(SettingsDialog *dlg, const char *message, gboolean is_error) {
    if (!dlg->status_label) return;

    /* Use UI utils for consistent status display */
    ui_utils_update_status_label(dlg->status_label, message,
                                  is_error ? UI_STATUS_ERROR : UI_STATUS_SUCCESS);
}

/* Parse config value from daemon JSON response */
static gboolean parse_config_value(const char *output, char *value, size_t value_size) {
    /* Response format with --json flag:
     * {"status":"ok","message":"Config value retrieved","data":{"key":"...","value":"...","type":"..."}}
     * We need to extract the "value" field from the "data" object */

    /* First find the "data" object */
    const char *data_start = strstr(output, "\"data\":");
    if (!data_start) {
        TRAY_LOG_DEBUG(COMPONENT, "No 'data' field in response");
        return FALSE;
    }

    /* Move past "data": and find the opening brace */
    data_start = strchr(data_start, '{');
    if (!data_start) return FALSE;

    /* Now find "value": within the data object */
    const char *value_start = strstr(data_start, "\"value\":");
    if (!value_start) {
        TRAY_LOG_DEBUG(COMPONENT, "No 'value' field in data object");
        return FALSE;
    }

    /* Move past "value": */
    value_start += 8;

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

/* Helper: Check if wallpaper type is live (shader) */
static gboolean is_live_type(int type_index) {
    /* 0=image, 1=shader */
    return type_index == 1;
}

/* Helper: Check if wallpaper type is static (image) */
static gboolean is_static_type(int type_index) {
    /* 0=image */
    return type_index == 0;
}

/* Update widget visibility based on wallpaper type */
static void update_widgets_for_type(ScopeSettings *scope, int type_index) {
    gboolean is_static = is_static_type(type_index);
    gboolean is_live = is_live_type(type_index);

    /* Static-only widgets: mode, transition, transition_duration */
    if (scope->mode_container) {
        gtk_widget_set_visible(scope->mode_container, is_static);
    }
    if (scope->transition_container) {
        gtk_widget_set_visible(scope->transition_container, is_static);
    }

    /* Live-only widgets: shader_speed, vsync, show_fps */
    if (scope->speed_container) {
        gtk_widget_set_visible(scope->speed_container, is_live);
    }
    if (scope->vsync_container) {
        gtk_widget_set_visible(scope->vsync_container, is_live);
    }
    if (scope->show_fps_container) {
        gtk_widget_set_visible(scope->show_fps_container, is_live);
    }

    /* FPS widget: visible for live types, but hidden if vsync is enabled */
    if (scope->fps_container) {
        gboolean show_fps_widget = is_live && !scope->vsync_enabled;
        gtk_widget_set_visible(scope->fps_container, show_fps_widget);
    }

    TRAY_LOG_DEBUG(COMPONENT, "[%s] Updated widgets for type %d (static=%d, live=%d, vsync=%d)",
                  scope->scope, type_index, is_static, is_live, scope->vsync_enabled);
}

/* Load configuration for a specific scope */
static void load_scope_config(ScopeSettings *scope) {
    char output[4096];
    char cmd[256];
    int values_loaded = 0;

    TRAY_LOG_INFO(COMPONENT, "Loading config for scope: %s", scope->scope);

    /* Note: Signal blocking removed - was causing Apply button to never activate.
     * We'll just set has_changes = FALSE at the end instead. */

    /* Initialize original value defaults (will be overwritten if config exists) */
    scope->original_type_index = 0;
    scope->current_type_index = 0;
    scope->original_duration = 60.0;
    scope->last_image_folder[0] = '\0';
    scope->last_shader_folder[0] = '\0';
    scope->original_mode_index = 0;
    scope->original_transition_index = 1; // fade
    scope->original_transition_duration = 0.3;
    scope->original_speed = 1.0;
    scope->original_fps = 60;
    scope->original_vsync = FALSE;
    scope->original_show_fps = FALSE;
    scope->vsync_enabled = FALSE;

    /* Load type */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.type\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char type[32] = {0};
        if (parse_config_value(output, type, sizeof(type))) {
            int type_idx = 0;
            if (strcmp(type, "image") == 0) type_idx = 0;
            else if (strcmp(type, "shader") == 0) type_idx = 1;
            // GIF, Video, SVG hidden for future use

            gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo), type_idx);
            scope->original_type_index = type_idx;
            scope->current_type_index = type_idx;
            gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo), type_idx);

            /* Update widget visibility based on type */
            update_widgets_for_type(scope, type_idx);

            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded type: %s", scope->scope, type);
            values_loaded++;
        } else {
            /* No type in config, use default */
            gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo), 0);
            scope->current_type_index = 0;
            update_widgets_for_type(scope, 0);
        }
    } else {
        /* Failed to load, use default */
        gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo), 0);
        scope->current_type_index = 0;
        update_widgets_for_type(scope, 0);
    }

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
        } else {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin), scope->original_duration);
        }
    } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin), scope->original_duration);
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
        } else {
            gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo), scope->original_mode_index);
        }
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo), scope->original_mode_index);
    }

    /* Load transition */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.transition\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char transition[32] = {0};
        if (parse_config_value(output, transition, sizeof(transition))) {
            const char *transition_names[] = {"none", "fade", "slide-left", "slide-right", "glitch", "pixelate"};
            for (int i = 0; i < 6; i++) {
                if (strcmp(transition, transition_names[i]) == 0) {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->transition_combo), i);
                    scope->original_transition_index = i;
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded transition: %s", scope->scope, transition);
                    values_loaded++;
                    break;
                }
            }
        } else {
            gtk_combo_box_set_active(GTK_COMBO_BOX(scope->transition_combo), scope->original_transition_index);
        }
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(scope->transition_combo), scope->original_transition_index);
    }

    /* Load transition_duration */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.transition_duration\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char duration_str[32] = {0};
        if (parse_config_value(output, duration_str, sizeof(duration_str))) {
            double trans_duration = atof(duration_str);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->transition_duration_spin), trans_duration);
            scope->original_transition_duration = trans_duration;
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded transition_duration: %.2f", scope->scope, trans_duration);
            values_loaded++;
        } else {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->transition_duration_spin), scope->original_transition_duration);
        }
    } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->transition_duration_spin), scope->original_transition_duration);
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
            } else {
                gtk_range_set_value(GTK_RANGE(scope->speed_scale), scope->original_speed);
            }
        } else {
            gtk_range_set_value(GTK_RANGE(scope->speed_scale), scope->original_speed);
        }
    } else {
        gtk_range_set_value(GTK_RANGE(scope->speed_scale), scope->original_speed);
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
            } else {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), scope->original_fps);
            }
        } else {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), scope->original_fps);
        }
    } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), scope->original_fps);
    }

    /* Load vsync */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.vsync\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char vsync_str[32] = {0};
        if (parse_config_value(output, vsync_str, sizeof(vsync_str))) {
            gboolean vsync = (strcmp(vsync_str, "true") == 0 || strcmp(vsync_str, "1") == 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check), vsync);
            scope->original_vsync = vsync;
            scope->vsync_enabled = vsync;
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded vsync: %s", scope->scope, vsync ? "true" : "false");
            values_loaded++;
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check), scope->original_vsync);
            scope->vsync_enabled = scope->original_vsync;
        }
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check), scope->original_vsync);
        scope->vsync_enabled = scope->original_vsync;
    }

    /* Update FPS widget visibility based on vsync state */
    if (scope->fps_container) {
        int type_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->type_combo));
        gboolean show_fps_widget = is_live_type(type_idx) && !scope->vsync_enabled;
        gtk_widget_set_visible(scope->fps_container, show_fps_widget);
    }

    /* Load show_fps */
    snprintf(cmd, sizeof(cmd), "get-config \"%s.show_fps\"", scope->scope);
    if (command_execute_with_output(cmd, output, sizeof(output))) {
        char show_fps_str[32] = {0};
        if (parse_config_value(output, show_fps_str, sizeof(show_fps_str))) {
            gboolean show_fps = (strcmp(show_fps_str, "true") == 0 || strcmp(show_fps_str, "1") == 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->show_fps_check), show_fps);
            scope->original_show_fps = show_fps;
            TRAY_LOG_DEBUG(COMPONENT, "[%s] Loaded show_fps: %s", scope->scope, show_fps ? "true" : "false");
            values_loaded++;
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->show_fps_check), scope->original_show_fps);
        }
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->show_fps_check), scope->original_show_fps);
    }

    /* Reset has_changes after loading - any subsequent changes will trigger it */
    scope->has_changes = FALSE;

    TRAY_LOG_INFO(COMPONENT, "[%s] Loaded %d config values", scope->scope, values_loaded);
}

/* Callback for vsync checkbox change - update FPS widget visibility */
static void on_vsync_changed(GtkToggleButton *toggle, gpointer user_data) {
    SettingsDialog *dlg = (SettingsDialog *)user_data;
    gboolean vsync_active = gtk_toggle_button_get_active(toggle);

    /* Find which scope this toggle belongs to */
    for (int i = 0; i < dlg->scope_count; i++) {
        ScopeSettings *scope = &dlg->scopes[i];
        if (GTK_WIDGET(toggle) == scope->vsync_check) {
            scope->vsync_enabled = vsync_active;

            /* Update FPS widget visibility */
            if (scope->fps_container) {
                int type_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->type_combo));
                gboolean show_fps_widget = is_live_type(type_idx) && !vsync_active;
                gtk_widget_set_visible(scope->fps_container, show_fps_widget);

                TRAY_LOG_DEBUG(COMPONENT, "[%s] VSync %s, FPS widget %s",
                              scope->scope,
                              vsync_active ? "enabled" : "disabled",
                              show_fps_widget ? "visible" : "hidden");
            }
            break;
        }
    }

    /* Call the generic change handler */
    on_scope_setting_changed(GTK_WIDGET(toggle), user_data);
}

/* Callback for folder selection - save to appropriate last folder variable */
static void on_folder_selected(GtkFileChooserButton *widget, gpointer user_data) {
    SettingsDialog *dlg = (SettingsDialog *)user_data;

    /* Find which scope this folder chooser belongs to */
    for (int i = 0; i < dlg->scope_count; i++) {
        ScopeSettings *scope = &dlg->scopes[i];
        if (GTK_WIDGET(widget) == scope->folder_chooser) {
            gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget));
            if (folder) {
                /* Save to the appropriate last folder based on current type */
                if (scope->current_type_index == 0) {  /* Image type */
                    strncpy(scope->last_image_folder, folder, sizeof(scope->last_image_folder) - 1);
                    scope->last_image_folder[sizeof(scope->last_image_folder) - 1] = '\0';
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Saved image folder: %s", scope->scope, folder);
                } else if (scope->current_type_index == 1) {  /* Shader type */
                    strncpy(scope->last_shader_folder, folder, sizeof(scope->last_shader_folder) - 1);
                    scope->last_shader_folder[sizeof(scope->last_shader_folder) - 1] = '\0';
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Saved shader folder: %s", scope->scope, folder);
                }
                g_free(folder);
            }
            break;
        }
    }

    /* Call the generic change handler */
    on_scope_setting_changed(GTK_WIDGET(widget), user_data);
}

/* Callback for type combo box change - update widget visibility */
static void on_type_changed(GtkComboBox *combo, gpointer user_data) {
    SettingsDialog *dlg = (SettingsDialog *)user_data;

    /* Find which scope this combo belongs to */
    for (int i = 0; i < dlg->scope_count; i++) {
        ScopeSettings *scope = &dlg->scopes[i];
        if (GTK_WIDGET(combo) == scope->type_combo) {
            int old_type_idx = scope->current_type_index;
            int new_type_idx = gtk_combo_box_get_active(combo);

            /* Save current folder to the appropriate last folder variable */
            gchar *current_folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(scope->folder_chooser));
            if (current_folder) {
                if (old_type_idx == 0) {  /* Was image type */
                    strncpy(scope->last_image_folder, current_folder, sizeof(scope->last_image_folder) - 1);
                    scope->last_image_folder[sizeof(scope->last_image_folder) - 1] = '\0';
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Saved image folder: %s", scope->scope, current_folder);
                } else if (old_type_idx == 1) {  /* Was shader type */
                    strncpy(scope->last_shader_folder, current_folder, sizeof(scope->last_shader_folder) - 1);
                    scope->last_shader_folder[sizeof(scope->last_shader_folder) - 1] = '\0';
                    TRAY_LOG_DEBUG(COMPONENT, "[%s] Saved shader folder: %s", scope->scope, current_folder);
                }
                g_free(current_folder);
            }

            /* Restore folder for the new type */
            const char *restore_folder = NULL;
            if (new_type_idx == 0 && scope->last_image_folder[0] != '\0') {  /* Switching to image */
                restore_folder = scope->last_image_folder;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Restoring image folder: %s", scope->scope, restore_folder);
            } else if (new_type_idx == 1 && scope->last_shader_folder[0] != '\0') {  /* Switching to shader */
                restore_folder = scope->last_shader_folder;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Restoring shader folder: %s", scope->scope, restore_folder);
            }

            if (restore_folder) {
                gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(scope->folder_chooser), restore_folder);
            }

            /* Update current type tracker */
            scope->current_type_index = new_type_idx;

            update_widgets_for_type(scope, new_type_idx);

            /* Mark as changed */
            if (!scope->has_changes) {
                scope->has_changes = TRUE;
                TRAY_LOG_DEBUG(COMPONENT, "[%s] Type changed to %d", scope->scope, new_type_idx);
            }
            break;
        }
    }

    /* Call the generic change handler */
    on_scope_setting_changed(GTK_WIDGET(combo), user_data);
}

/* Mark that a scope has changes */
static void on_scope_setting_changed(GtkWidget *widget, gpointer user_data) {
    SettingsDialog *dlg = (SettingsDialog *)user_data;

    /* Ignore signals during config reload */
    if (dlg->loading_config) {
        TRAY_LOG_DEBUG(COMPONENT, "Ignoring signal during config reload");
        return;
    }

    TRAY_LOG_DEBUG(COMPONENT, "🔔 on_scope_setting_changed called, widget=%p", (void*)widget);

    /* Find which scope changed by checking the widget */
    gboolean found = FALSE;
    for (int i = 0; i < dlg->scope_count; i++) {
        ScopeSettings *scope = &dlg->scopes[i];
        if (widget == scope->type_combo ||
            widget == scope->folder_chooser ||
            widget == scope->duration_spin ||
            widget == scope->mode_combo ||
            widget == scope->transition_combo ||
            widget == scope->transition_duration_spin ||
            widget == scope->speed_scale ||
            widget == scope->fps_spin ||
            widget == scope->vsync_check ||
            widget == scope->show_fps_check) {

            found = TRUE;
            TRAY_LOG_INFO(COMPONENT, "🔔 Widget matched for scope [%s], has_changes was %d",
                         scope->scope, scope->has_changes);

            if (!scope->has_changes) {
                scope->has_changes = TRUE;
                TRAY_LOG_INFO(COMPONENT, "✅ [%s] Settings modified - has_changes now TRUE", scope->scope);
            }
            break;
        }
    }

    if (!found) {
        TRAY_LOG_ERROR(COMPONENT, "⚠️  Widget %p not matched to any scope!", (void*)widget);
    }

    /* Check if any scope has changes */
    gboolean any_changes = FALSE;
    for (int i = 0; i < dlg->scope_count; i++) {
        if (dlg->scopes[i].has_changes) {
            any_changes = TRUE;
            TRAY_LOG_DEBUG(COMPONENT, "Scope [%s] has changes", dlg->scopes[i].scope);
            break;
        }
    }

    TRAY_LOG_INFO(COMPONENT, "any_changes=%d, has_any_changes=%d, will %s Apply button",
                 any_changes, dlg->has_any_changes,
                 (any_changes != dlg->has_any_changes) ? "UPDATE" : "NOT UPDATE");

    if (any_changes != dlg->has_any_changes) {
        dlg->has_any_changes = any_changes;
        gtk_widget_set_sensitive(dlg->apply_button, any_changes);
        gtk_widget_set_sensitive(dlg->revert_button, any_changes);

        TRAY_LOG_INFO(COMPONENT, "✅ Apply button set to: %s", any_changes ? "ENABLED" : "DISABLED");

        if (any_changes) {
            show_status(dlg, "Settings modified (not saved)", FALSE);
        }
    }
}

/* Revert a scope to original values */
static void revert_scope(ScopeSettings *scope) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo),
                            scope->original_type_index);

    if (scope->original_folder[0] != '\0') {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(scope->folder_chooser),
                                     scope->original_folder);
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->duration_spin),
                             scope->original_duration);
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->mode_combo),
                            scope->original_mode_index);
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->transition_combo),
                            scope->original_transition_index);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->transition_duration_spin),
                             scope->original_transition_duration);
    gtk_range_set_value(GTK_RANGE(scope->speed_scale),
                       scope->original_speed);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin),
                             scope->original_fps);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->vsync_check),
                                scope->original_vsync);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope->show_fps_check),
                                scope->original_show_fps);

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
    int type_index = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->type_combo));
    const char *type_names[] = {"image", "shader"};
    const char *type = (type_index >= 0 && type_index < 2) ? type_names[type_index] : "image";
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(scope->folder_chooser));
    double duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(scope->duration_spin));
    int mode_index = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->mode_combo));
    double speed = gtk_range_get_value(GTK_RANGE(scope->speed_scale));
    int fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(scope->fps_spin));
    gboolean vsync = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope->vsync_check));
    gboolean show_fps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope->show_fps_check));
    int transition_index = gtk_combo_box_get_active(GTK_COMBO_BOX(scope->transition_combo));
    double transition_duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(scope->transition_duration_spin));

    const char *mode_names[] = {"fill", "fit", "center", "stretch", "tile"};
    const char *mode = (mode_index >= 0 && mode_index < 5) ? mode_names[mode_index] : "fill";
    const char *transition_names[] = {"none", "fade", "slide-left", "slide-right", "glitch", "pixelate"};
    const char *transition = (transition_index >= 0 && transition_index < 6) ? transition_names[transition_index] : "fade";

    /* Only set changed values */

    /* Set type if changed */
    if (type_index != scope->original_type_index) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.type\" \"%s\"", scope->scope, type);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set type", scope->scope);
            success = FALSE;
        }
    }

    /* Set path if changed */
    if (success && folder && folder[0] != '\0' && strcmp(folder, scope->original_folder) != 0) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.path\" \"%s\"", scope->scope, folder);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set path", scope->scope);
            success = FALSE;
        }
    }

    /* Set duration if changed */
    if (success && fabs(duration - scope->original_duration) > 0.01) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.duration\" \"%.0f\"", scope->scope, duration);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set duration", scope->scope);
            success = FALSE;
        }
    }

    /* Set mode if changed (STATIC TYPES ONLY) */
    if (success && is_static_type(type_index) && mode_index != scope->original_mode_index) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.mode\" \"%s\"", scope->scope, mode);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set mode", scope->scope);
            success = FALSE;
        }
    }

    /* Set transition if changed (STATIC TYPES ONLY) */
    if (success && is_static_type(type_index) && transition_index != scope->original_transition_index) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.transition\" \"%s\"", scope->scope, transition);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set transition", scope->scope);
            success = FALSE;
        }
    }

    /* Set transition_duration if changed (STATIC TYPES ONLY) */
    if (success && is_static_type(type_index) && fabs(transition_duration - scope->original_transition_duration) > 0.01) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.transition_duration\" \"%.2f\"", scope->scope, transition_duration);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set transition_duration", scope->scope);
            success = FALSE;
        }
    }

    /* Set shader speed if changed (LIVE TYPES ONLY) */
    if (success && is_live_type(type_index) && fabs(speed - scope->original_speed) > 0.01) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.shader_speed\" \"%.2f\"", scope->scope, speed);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set shader speed", scope->scope);
            success = FALSE;
        }
    }

    /* Set FPS if changed (LIVE TYPES ONLY) */
    if (success && is_live_type(type_index) && fps != scope->original_fps) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.shader_fps\" \"%d\"", scope->scope, fps);
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set FPS", scope->scope);
            success = FALSE;
        }
    }

    /* Set vsync if changed (LIVE TYPES ONLY) */
    if (success && is_live_type(type_index) && vsync != scope->original_vsync) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.vsync\" \"%s\"", scope->scope, vsync ? "true" : "false");
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set vsync", scope->scope);
            success = FALSE;
        }
    }

    /* Set show_fps if changed (LIVE TYPES ONLY) */
    if (success && is_live_type(type_index) && show_fps != scope->original_show_fps) {
        snprintf(cmd, sizeof(cmd), "set-config \"%s.show_fps\" \"%s\"", scope->scope, show_fps ? "true" : "false");
        if (command_execute(cmd)) {
            changes_applied++;
        } else {
            snprintf(error_msg, error_size, "[%s] Failed to set show_fps", scope->scope);
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
        scope->original_type_index = type_index;
        scope->current_type_index = type_index;
        scope->original_duration = duration;
        scope->original_mode_index = mode_index;
        scope->original_transition_index = transition_index;
        scope->original_transition_duration = transition_duration;
        scope->original_speed = speed;
        scope->original_fps = fps;
        scope->original_vsync = vsync;
        scope->original_show_fps = show_fps;

        scope->has_changes = FALSE;
    }

    if (folder) g_free(folder);

    return success;
}

/* Apply all changed settings */
static gboolean apply_all_settings(SettingsDialog *dlg) {
    char error_msg[256] = {0};
    int scopes_applied = 0;

    /* Disable UI during apply to show we're working */
    gtk_widget_set_sensitive(dlg->notebook, FALSE);
    gtk_widget_set_sensitive(dlg->apply_button, FALSE);
    gtk_widget_set_sensitive(dlg->revert_button, FALSE);

    show_status(dlg, "⏳ Applying settings...", FALSE);

    /* Process pending GTK events to show status immediately */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    /* Apply each scope that has changes */
    for (int i = 0; i < dlg->scope_count; i++) {
        if (dlg->scopes[i].has_changes) {
            if (apply_scope_settings(&dlg->scopes[i], error_msg, sizeof(error_msg))) {
                scopes_applied++;
            } else {
                show_status(dlg, error_msg, TRUE);
                dialog_show_error("❌ Settings Error", error_msg);
                return FALSE;
            }
        }
    }

    /* Reload configuration */
    if (scopes_applied > 0) {
        show_status(dlg, "⏳ Reloading configuration...", FALSE);

        /* Process events to show reload status */
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }

        /* Block signal handling during config reload */
        dlg->loading_config = TRUE;

        if (command_execute("reload")) {
            char success_msg[128];
            snprintf(success_msg, sizeof(success_msg),
                    "✓ Applied %d change%s successfully",
                    scopes_applied, scopes_applied == 1 ? "" : "s");
            show_status(dlg, success_msg, FALSE);

            dlg->has_any_changes = FALSE;

            /* Reload config for all scopes to sync with daemon */
            for (int i = 0; i < dlg->scope_count; i++) {
                load_scope_config(&dlg->scopes[i]);
            }

            /* Unblock signal handling */
            dlg->loading_config = FALSE;

            /* Re-enable UI */
            gtk_widget_set_sensitive(dlg->notebook, TRUE);
            gtk_widget_set_sensitive(dlg->apply_button, FALSE);
            gtk_widget_set_sensitive(dlg->revert_button, FALSE);

            return TRUE;
        } else {
            show_status(dlg, "✗ Failed to reload configuration", TRUE);

            /* Unblock signal handling even on failure */
            dlg->loading_config = FALSE;

            /* Re-enable UI */
            gtk_widget_set_sensitive(dlg->notebook, TRUE);
            gtk_widget_set_sensitive(dlg->apply_button, TRUE);
            gtk_widget_set_sensitive(dlg->revert_button, TRUE);

            dialog_show_error("❌ Reload Failed",
                            "Settings were saved but failed to reload.\n"
                            "Try using 'Reload Configuration' from the menu.");
            return FALSE;
        }
    } else {
        show_status(dlg, "No changes to apply", FALSE);

        /* Re-enable UI */
        gtk_widget_set_sensitive(dlg->notebook, TRUE);
        gtk_widget_set_sensitive(dlg->apply_button, FALSE);
        gtk_widget_set_sensitive(dlg->revert_button, FALSE);
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
                "<big><b>⚙️  Default Settings</b></big>\n"
                "<small>These settings apply to all outputs unless overridden</small>");
    } else {
        snprintf(header_text, sizeof(header_text),
                "<big><b>🖥️  Output: %s</b></big>\n"
                "<small>Settings specific to this output (overrides defaults)</small>",
                scope->scope);
    }
    gtk_label_set_markup(GTK_LABEL(header), header_text);
    gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 5);

    /* Wallpaper type */
    GtkWidget *type_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(type_label), "<b>🎨 Wallpaper Type:</b>");
    gtk_widget_set_halign(type_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(type_label, "Select image or shader type");
    gtk_box_pack_start(GTK_BOX(box), type_label, FALSE, FALSE, 0);

    scope->type_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->type_combo), "Image");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->type_combo), "Shader");
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->type_combo), 0);
    gtk_widget_set_tooltip_text(scope->type_combo,
                               "Image: Static wallpapers (PNG, JPG, etc.)\n"
                               "Shader: Animated GLSL shaders (.glsl files)");
    g_signal_connect(scope->type_combo, "changed", G_CALLBACK(on_type_changed), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->type_combo, FALSE, FALSE, 5);

    /* Wallpaper folder */
    GtkWidget *folder_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(folder_label), "<b>📁 Wallpaper Folder:</b>");
    gtk_widget_set_halign(folder_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(folder_label, "Folder containing images or shaders");
    gtk_box_pack_start(GTK_BOX(box), folder_label, FALSE, FALSE, 0);

    scope->folder_chooser = gtk_file_chooser_button_new(
        "Select Wallpaper Folder",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    );
    gtk_widget_set_tooltip_text(scope->folder_chooser, "Choose folder with images or .glsl shaders");
    g_signal_connect(scope->folder_chooser, "file-set", G_CALLBACK(on_folder_selected), dlg);
    gtk_box_pack_start(GTK_BOX(box), scope->folder_chooser, FALSE, FALSE, 0);

    /* Duration */
    GtkWidget *duration_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(duration_label), "<b>⏱️  Cycle Duration (seconds):</b>");
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

    /* Display mode - STATIC ONLY (images) */
    scope->mode_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *mode_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(mode_label), "<b>🖼️  Display Mode:</b> <small>(Static images only)</small>");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(mode_label, "How wallpapers are displayed (not applicable to shaders/animations)");
    gtk_box_pack_start(GTK_BOX(scope->mode_container), mode_label, FALSE, FALSE, 0);

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
    gtk_box_pack_start(GTK_BOX(scope->mode_container), scope->mode_combo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->mode_container, FALSE, FALSE, 0);

    /* Transition settings - STATIC ONLY (images) */
    scope->transition_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *transition_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(transition_label), "<b>✨ Transition Effect:</b> <small>(Static images only)</small>");
    gtk_widget_set_halign(transition_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(transition_label, "Transition effect when changing wallpapers");
    gtk_box_pack_start(GTK_BOX(scope->transition_container), transition_label, FALSE, FALSE, 0);

    scope->transition_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "None");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "Fade");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "Slide Left");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "Slide Right");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "Glitch");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scope->transition_combo), "Pixelate");
    gtk_combo_box_set_active(GTK_COMBO_BOX(scope->transition_combo), 1); // default to fade
    gtk_widget_set_tooltip_text(scope->transition_combo,
                               "None: Instant change\n"
                               "Fade: Smooth cross-fade (recommended)\n"
                               "Slide Left: Slide from right to left\n"
                               "Slide Right: Slide from left to right\n"
                               "Glitch: Digital glitch effect\n"
                               "Pixelate: Pixelation transition");
    g_signal_connect(scope->transition_combo, "changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->transition_container), scope->transition_combo, FALSE, FALSE, 0);

    GtkWidget *transition_duration_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(transition_duration_label), "<b>⏲️  Transition Duration (seconds):</b>");
    gtk_widget_set_halign(transition_duration_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(transition_duration_label, "How long the transition takes");
    gtk_box_pack_start(GTK_BOX(scope->transition_container), transition_duration_label, FALSE, FALSE, 0);

    scope->transition_duration_spin = gtk_spin_button_new_with_range(0.1, 5.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->transition_duration_spin), 0.3);
    gtk_widget_set_tooltip_text(scope->transition_duration_spin, "0.3 seconds recommended");
    g_signal_connect(scope->transition_duration_spin, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->transition_container), scope->transition_duration_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->transition_container, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep2, FALSE, FALSE, 10);

    /* Animation speed - LIVE ONLY (shaders/gifs/videos/svg) */
    scope->speed_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *speed_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(speed_label), "<b>⚡ Animation Speed:</b> <small>(Live wallpapers only)</small>");
    gtk_widget_set_halign(speed_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(speed_label, "Speed multiplier for shader/animation playback");
    gtk_box_pack_start(GTK_BOX(scope->speed_container), speed_label, FALSE, FALSE, 0);

    scope->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_scale_set_value_pos(GTK_SCALE(scope->speed_scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scope->speed_scale), 1);
    gtk_scale_add_mark(GTK_SCALE(scope->speed_scale), 1.0, GTK_POS_BOTTOM, "Normal");
    gtk_range_set_value(GTK_RANGE(scope->speed_scale), 1.0);
    gtk_widget_set_tooltip_text(scope->speed_scale, "Higher = faster (more GPU usage)");
    g_signal_connect(scope->speed_scale, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->speed_container), scope->speed_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->speed_container, FALSE, FALSE, 0);

    /* FPS - LIVE ONLY */
    scope->fps_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *fps_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fps_label), "<b>🎯 Target FPS:</b> <small>(Live wallpapers only)</small>");
    gtk_widget_set_halign(fps_label, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(fps_label, "Target frames per second for live wallpapers");
    gtk_box_pack_start(GTK_BOX(scope->fps_container), fps_label, FALSE, FALSE, 0);

    scope->fps_spin = gtk_spin_button_new_with_range(1, 144, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scope->fps_spin), 60);
    gtk_widget_set_tooltip_text(scope->fps_spin, "60 recommended for most displays");
    g_signal_connect(scope->fps_spin, "value-changed", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->fps_container), scope->fps_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->fps_container, FALSE, FALSE, 0);

    /* VSync - LIVE ONLY */
    scope->vsync_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    scope->vsync_check = gtk_check_button_new_with_label("Enable VSync (Live wallpapers only)");
    gtk_widget_set_tooltip_text(scope->vsync_check,
                               "Sync rendering to monitor refresh rate\n"
                               "Prevents tearing, reduces GPU usage\n"
                               "When enabled, FPS is controlled by monitor refresh rate\n"
                               "Only applies to shaders and animations");
    g_signal_connect(scope->vsync_check, "toggled", G_CALLBACK(on_vsync_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->vsync_container), scope->vsync_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->vsync_container, FALSE, FALSE, 5);

    /* Show FPS - LIVE ONLY */
    scope->show_fps_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    scope->show_fps_check = gtk_check_button_new_with_label("Show FPS Counter (Live wallpapers only)");
    gtk_widget_set_tooltip_text(scope->show_fps_check,
                               "Display FPS (frames per second) counter on screen\n"
                               "Useful for performance monitoring\n"
                               "Only applies to shaders and animations");
    g_signal_connect(scope->show_fps_check, "toggled", G_CALLBACK(on_scope_setting_changed), dlg);
    gtk_box_pack_start(GTK_BOX(scope->show_fps_container), scope->show_fps_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scope->show_fps_container, FALSE, FALSE, 5);

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
        dialog_show_error("⭕ Daemon Not Running",
                         "The NeoWall daemon is not running.\n"
                         "Please start the daemon before changing settings.");
        return;
    }

    /* Create dialog state */
    SettingsDialog *dlg = g_new0(SettingsDialog, 1);
    dlg->has_any_changes = FALSE;

    /* Initialize UI theme */
    ui_utils_init_theme();

    /* Create dialog - modal to parent */
    dlg->dialog = gtk_dialog_new_with_buttons(
        "⚙️  NeoWall Settings",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL, NULL
    );

    /* Set dialog icon */
    ui_utils_set_window_icon(GTK_WINDOW(dlg->dialog));

    /* Add buttons with styling */
    dlg->revert_button = gtk_dialog_add_button(GTK_DIALOG(dlg->dialog),
                                               "Revert", GTK_RESPONSE_NONE);
    gtk_widget_set_sensitive(dlg->revert_button, FALSE);
    ui_utils_add_class(dlg->revert_button, "revert-button");

    gtk_dialog_add_button(GTK_DIALOG(dlg->dialog), "Close", GTK_RESPONSE_CLOSE);

    dlg->apply_button = gtk_dialog_add_button(GTK_DIALOG(dlg->dialog),
                                              "Apply", GTK_RESPONSE_APPLY);
    gtk_widget_set_sensitive(dlg->apply_button, FALSE);
    ui_utils_add_class(dlg->apply_button, "suggested-action");
    ui_utils_add_class(dlg->apply_button, "apply-button");

    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 700, 600);
    gtk_window_set_resizable(GTK_WINDOW(dlg->dialog), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dlg->dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

    /* Content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Status label - styled with UI utils */
    dlg->status_label = ui_utils_create_status_label("Ready", UI_STATUS_INFO);
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

    ui_utils_update_status_label(dlg->status_label, "⏳ Loading configuration...", UI_STATUS_INFO);

    /* Block signals during initial load */
    dlg->loading_config = TRUE;

    /* Load config for all scopes */
    for (int i = 0; i < dlg->scope_count; i++) {
        load_scope_config(&dlg->scopes[i]);
    }

    /* Unblock signals after initial load */
    dlg->loading_config = FALSE;

    ui_utils_update_status_label(dlg->status_label, "✓ Configuration loaded", UI_STATUS_SUCCESS);

    /* Run dialog (modal to parent) */
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
