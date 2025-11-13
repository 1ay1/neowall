/* NeoWall Tray - Dialogs Component
 * Implementation of dialog windows
 */

#include "dialogs.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "../daemon/daemon_check.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include <version.h>

#define COMPONENT "DIALOGS"

#define APP_TITLE "NeoWall"
#define LOGO_PATH "/usr/share/pixmaps/neowall.svg"
#define ICON_PATH "/usr/share/pixmaps/neowall-icon.svg"

/* Simple JSON value extractor */
__attribute__((unused))
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start) {
        return false;
    }

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) {
        return false;
    }

    size_t len = end - start;
    if (len >= out_size) {
        len = out_size - 1;
    }

    strncpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) {
        return false;
    }

    start += strlen(search);
    while (*start && isspace(*start)) start++;

    if (*start == '"') start++;  /* Skip quote if present */

    *out = atoi(start);
    return true;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) {
        return false;
    }

    start += strlen(search);
    while (*start && isspace(*start)) start++;

    if (strncmp(start, "true", 4) == 0) {
        *out = true;
        return true;
    } else if (strncmp(start, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool json_get_float(const char *json, const char *key, float *out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) {
        return false;
    }

    start += strlen(search);
    while (*start && isspace(*start)) start++;

    *out = atof(start);
    return true;
}

/* Show the daemon status dialog with detailed information */
void dialog_show_status(void) {
    char status_text[4096];

    /* Check if daemon is running first */
    if (!daemon_is_running()) {
        snprintf(status_text, sizeof(status_text),
                 "○ Daemon Status: Stopped\n\n"
                 "The NeoWall daemon is not currently running.\n"
                 "Use the tray menu to start it.");

        GtkWidget *dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "NeoWall Status"
        );

        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", status_text);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* Get detailed status via IPC (with --json flag for machine-readable output) */
    char output[8192] = {0};
    bool ipc_success = command_execute_with_output("--json status", output, sizeof(output));

    if (ipc_success && strstr(output, "\"daemon\":\"running\"")) {
        /* JSON response is nested: {"status":"ok","message":"OK","data":{"message":"OK","data":{...}}}
         * We need to extract the innermost data object */

        /* Find the innermost "data" that contains daemon info */
        const char *daemon_data = strstr(output, "\"daemon\":\"running\"");
        if (!daemon_data) {
            /* Fallback if structure is different */
            daemon_data = output;
        } else {
            /* Move back to find the opening brace of this object */
            while (daemon_data > output && *daemon_data != '{') {
                daemon_data--;
            }
        }

        /* Parse JSON response */
        int pid = 0, output_count = 0;
        bool paused = false, shader_paused = false;
        float shader_speed = 1.0f;

        json_get_int(daemon_data, "pid", &pid);
        json_get_int(daemon_data, "outputs", &output_count);
        json_get_bool(daemon_data, "paused", &paused);
        json_get_bool(daemon_data, "shader_paused", &shader_paused);
        json_get_float(daemon_data, "shader_speed", &shader_speed);

        /* Start building status display */
        size_t offset = 0;
        offset += snprintf(status_text + offset, sizeof(status_text) - offset,
                          "● <b>Daemon Status: Running</b>\n\n"
                          "<b>Process Information:</b>\n"
                          "  • PID: %d\n"
                          "  • Outputs: %d\n\n"
                          "<b>Wallpaper State:</b>\n"
                          "  • Cycling: %s\n\n"
                          "<b>Shader State:</b>\n"
                          "  • Animation: %s\n"
                          "  • Speed: %.2fx\n",
                          pid,
                          output_count,
                          paused ? "⏸ Paused" : "▶ Active",
                          shader_paused ? "⏸ Paused" : "▶ Active",
                          shader_speed);

        /* Parse and display current wallpapers */
        const char *wallpapers = strstr(daemon_data, "\"wallpapers\":[");
        if (wallpapers && output_count > 0) {
            offset += snprintf(status_text + offset, sizeof(status_text) - offset,
                             "\n<b>Current Wallpapers:</b>\n");

            wallpapers += 14;  /* Skip "wallpapers":[ */
            const char *obj_start = wallpapers;
            int wp_num = 1;

            while ((obj_start = strchr(obj_start, '{')) != NULL && offset < sizeof(status_text) - 512) {
                const char *obj_end = strchr(obj_start, '}');
                if (!obj_end) break;

                /* Extract wallpaper info */
                const char *output_pos = strstr(obj_start, "\"output\":\"");
                const char *type_pos = strstr(obj_start, "\"type\":\"");
                const char *path_pos = strstr(obj_start, "\"path\":\"");

                if (output_pos && type_pos && path_pos && output_pos < obj_end) {
                    output_pos += 10;
                    type_pos += 8;
                    path_pos += 8;

                    /* Extract output name */
                    char output_name[64] = {0};
                    const char *q = strchr(output_pos, '"');
                    if (q && (q - output_pos) < 63) {
                        strncpy(output_name, output_pos, q - output_pos);
                    }

                    /* Extract type */
                    char type[16] = {0};
                    q = strchr(type_pos, '"');
                    if (q && (q - type_pos) < 15) {
                        strncpy(type, type_pos, q - type_pos);
                    }

                    /* Extract path (with unescaping) */
                    char path[256] = {0};
                    q = path_pos;
                    size_t pi = 0;
                    while (*q && *q != '"' && pi < sizeof(path) - 1) {
                        if (*q == '\\' && *(q+1) == '"') {
                            path[pi++] = '"';
                            q += 2;
                        } else if (*q == '\\' && *(q+1) == '\\') {
                            path[pi++] = '\\';
                            q += 2;
                        } else {
                            path[pi++] = *q++;
                        }
                    }
                    path[pi] = '\0';

                    /* Get basename for display */
                    const char *basename = strrchr(path, '/');
                    basename = basename ? basename + 1 : path;

                    offset += snprintf(status_text + offset, sizeof(status_text) - offset,
                                     "  %d. <b>%s</b> (%s)\n"
                                     "      <small>%s</small>\n",
                                     wp_num++, output_name, type, basename);
                }

                obj_start = obj_end + 1;
            }
        }

        offset += snprintf(status_text + offset, sizeof(status_text) - offset,
                          "\n<small><i>Tip: Use the tray menu to control these settings</i></small>");
    } else {
        /* Fallback to basic status */
        pid_t pid = daemon_read_pid();
        snprintf(status_text, sizeof(status_text),
                 "● <b>Daemon Status: Running</b>\n\n"
                 "<b>Process Information:</b>\n"
                 "  • PID: %d\n\n"
                 "<i>Unable to retrieve detailed status via IPC.\n"
                 "The daemon may still be starting up.</i>",
                 pid);
    }

    /* Create and show dialog */
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "NeoWall Status"
    );

    gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(dialog), "%s", status_text);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Show the current wallpaper dialog */
void dialog_show_current_wallpaper(void) {
    char output[2048] = {0};

    if (!command_execute_with_output("current", output, sizeof(output))) {
        dialog_show_error("Current Wallpaper", "Failed to get current wallpaper");
        return;
    }

    /* Remove trailing newline */
    size_t len = strlen(output);
    if (len > 0 && output[len - 1] == '\n') {
        output[len - 1] = '\0';
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Current Wallpaper"
    );

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", output);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Show the about dialog */
void dialog_show_about(void) {
    GtkWidget *dialog = gtk_about_dialog_new();

    /* Basic info - use central metadata */
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), NEOWALL_PROJECT_NAME);
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), NEOWALL_VERSION);

    /* Description - from central metadata */
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), NEOWALL_PROJECT_DESCRIPTION);

    /* Website - from central metadata */
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), NEOWALL_PROJECT_URL);
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), NEOWALL_PROJECT_URL_SHORT);

    /* License - from central metadata */
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_MIT_X11);
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dialog), NEOWALL_LICENSE_TEXT);
    gtk_about_dialog_set_wrap_license(GTK_ABOUT_DIALOG(dialog), TRUE);

    /* Copyright - from central metadata */
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), NEOWALL_PROJECT_COPYRIGHT);

    /* Authors - from central metadata */
    const gchar *authors[] = {
        "Created by: " NEOWALL_PROJECT_AUTHOR,
        "",
        "Contributors:",
        "• NeoWall Community",
        "",
        "Special Thanks:",
        "• Wayland Community",
        "• GLSL Shader Artists",
        "• Open Source Contributors",
        "",
        "Features:",
        "• Real-time GLSL shader effects",
        "• Image cycling with smooth transitions",
        "• Multi-monitor support",
        "• GPU-accelerated rendering",
        "• Wayland native",
        "• Low resource usage",
        "• System tray integration",
        "• IPC control via JSON",
        NULL
    };
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

    /* Try to load SVG logo from multiple paths */
    GdkPixbuf *logo = NULL;
    GError *error = NULL;

    const char *logo_paths[] = {
        "/usr/share/pixmaps/neowall.svg",           /* Installed path */
        "data/icons/neowall.svg",                   /* Build directory */
        "../data/icons/neowall.svg",                /* From bin */
        "neowall.svg",                              /* Current directory */
        "../packaging/neowall.svg",                 /* Source tree */
        "packaging/neowall.svg",                    /* Project root */
        NULL
    };

    for (int i = 0; logo_paths[i] != NULL && !logo; i++) {
        if (access(logo_paths[i], F_OK) == 0) {
            /* Load SVG at a reasonable size (560px wide) */
            logo = gdk_pixbuf_new_from_file_at_scale(logo_paths[i], 560, 160, TRUE, &error);
            if (logo) {
                TRAY_LOG_DEBUG(COMPONENT, "Loaded about logo from: %s", logo_paths[i]);
                break;
            }
            if (error) {
                g_error_free(error);
                error = NULL;
            }
        }
    }

    if (logo) {
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), logo);
        g_object_unref(logo);
    } else {
        /* Fallback to icon name */
        gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), "preferences-desktop-wallpaper");
        TRAY_LOG_DEBUG(COMPONENT, "Using fallback icon for about dialog");
    }

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Show a confirmation dialog for stopping the daemon */
gint dialog_confirm_stop_daemon(void) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Stop NeoWall Daemon?"
    );

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "This will stop the wallpaper daemon. The tray will remain open.");

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result;
}

/* Show an error dialog */
void dialog_show_error(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s",
        title ? title : "Error"
    );

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Timer callback to auto-close dialog */
static gboolean dialog_auto_close_callback(gpointer user_data) {
    GtkWidget *dialog = GTK_WIDGET(user_data);
    if (dialog && GTK_IS_WIDGET(dialog)) {
        gtk_widget_destroy(dialog);
    }
    return FALSE;  /* One-shot timer */
}

/* Wrapper to properly destroy widget in timeout callback */
static gboolean destroy_widget_callback(gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);
    if (widget && GTK_IS_WIDGET(widget)) {
        gtk_widget_destroy(widget);
    }
    return FALSE;  /* One-shot timer */
}

/* Show a non-blocking info dialog that auto-closes */
void dialog_show_info(const char *title, const char *message, guint auto_close_ms) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,  /* No buttons for auto-close dialogs */
        "%s",
        title ? title : "Information"
    );

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    /* Show dialog non-blocking */
    gtk_widget_show_all(dialog);

    /* Schedule auto-close if requested */
    if (auto_close_ms > 0) {
        g_timeout_add(auto_close_ms, dialog_auto_close_callback, dialog);
    }
}

/* Show a warning dialog */
void dialog_show_warning(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s",
        title ? title : "Warning"
    );

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Structure to hold progress dialog data */
typedef struct {
    GtkWidget *dialog;
    gboolean (*check_callback)(void);
    guint auto_close_delay_ms;
    guint check_timer_id;
} ProgressDialogData;

/* Timer callback to check if operation is complete */
__attribute__((unused))
static gboolean progress_check_timer(gpointer user_data) {
    ProgressDialogData *data = (ProgressDialogData *)user_data;

    if (!data || !data->dialog) {
        return FALSE;
    }

    /* Check if operation is complete */
    if (data->check_callback && data->check_callback()) {
        /* Operation complete - schedule dialog close after delay */
        if (data->auto_close_delay_ms > 0) {
            g_timeout_add(data->auto_close_delay_ms, destroy_widget_callback, data->dialog);
        } else {
            gtk_widget_destroy(data->dialog);
        }

        /* Clean up and stop timer */
        g_free(data);
        return FALSE;
    }

    /* Continue checking */
    return TRUE;
}

/* Cleanup when dialog is destroyed */
__attribute__((unused))
static void progress_dialog_destroyed(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ProgressDialogData *data = (ProgressDialogData *)user_data;

    if (data) {
        if (data->check_timer_id > 0) {
            g_source_remove(data->check_timer_id);
            data->check_timer_id = 0;
        }
        g_free(data);
    }
}

/* Show a non-blocking progress dialog that auto-closes */
GtkWidget *dialog_show_progress_auto_close(const char *title, const char *message,
                                           gboolean (*check_callback)(void),
                                           guint auto_close_delay_ms) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "%s",
        title ? title : "Progress"
    );

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    /* Create data structure for timer callback */
    ProgressDialogData *data = g_malloc0(sizeof(ProgressDialogData));
    data->dialog = dialog;
    data->check_callback = check_callback;
    data->auto_close_delay_ms = auto_close_delay_ms;

    /* Connect destroy signal for cleanup */
    g_signal_connect(dialog, "destroy", G_CALLBACK(progress_dialog_destroyed), data);

    /* Show dialog non-blocking */
    gtk_widget_show_all(dialog);

    /* Start timer to check for completion */
    if (check_callback) {
        data->check_timer_id = g_timeout_add(500, progress_check_timer, data);  /* Check every 500ms */
    }

    return dialog;
}
