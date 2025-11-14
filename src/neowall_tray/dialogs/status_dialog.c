/* NeoWall Tray - Status Dialog Implementation
 * Beautiful status dialog showing daemon and wallpaper information
 */

#include "status_dialog.h"
#include "../common/log.h"
#include "../daemon/daemon_check.h"
#include "../daemon/command_exec.h"
#include "../ui/ui_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define COMPONENT "STATUS"

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

    if (*start == '"') start++;

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



/* Create a status section */
static GtkWidget *create_status_section(const char *title, const char *icon_name) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    /* Section header */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    if (icon_name) {
        GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start(GTK_BOX(header_box), icon, FALSE, FALSE, 0);
    }

    GtkWidget *title_label = gtk_label_new(NULL);
    char markup[256];
    snprintf(markup, sizeof(markup), "<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(title_label), markup);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header_box), title_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);

    return vbox;
}

/* Create a status item row */
static GtkWidget *create_status_item(const char *label, const char *value, bool bold_value) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_start(hbox, 20);
    gtk_widget_set_margin_end(hbox, 20);
    gtk_widget_set_margin_top(hbox, 6);
    gtk_widget_set_margin_bottom(hbox, 6);

    /* Label */
    GtkWidget *label_widget = gtk_label_new(NULL);
    char label_markup[256];
    snprintf(label_markup, sizeof(label_markup), "<span weight='500'>%s</span>", label);
    gtk_label_set_markup(GTK_LABEL(label_widget), label_markup);
    gtk_widget_set_halign(label_widget, GTK_ALIGN_START);
    gtk_widget_set_opacity(label_widget, 0.85);
    gtk_label_set_width_chars(GTK_LABEL(label_widget), 20);
    gtk_box_pack_start(GTK_BOX(hbox), label_widget, FALSE, FALSE, 0);

    /* Value */
    GtkWidget *value_widget = gtk_label_new(NULL);
    char markup[512];
    if (bold_value) {
        snprintf(markup, sizeof(markup), "<span size='large' weight='600'>%s</span>", value);
    } else {
        snprintf(markup, sizeof(markup), "<span>%s</span>", value);
    }
    gtk_label_set_markup(GTK_LABEL(value_widget), markup);
    gtk_widget_set_halign(value_widget, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(value_widget), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(value_widget), 60);
    gtk_label_set_selectable(GTK_LABEL(value_widget), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), value_widget, TRUE, TRUE, 0);

    return hbox;
}

/* Create wallpaper item */
static GtkWidget *create_wallpaper_item(int num, const char *output, const char *type, const char *path) {
    (void)num;  /* No longer displaying item numbers */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    /* Container with subtle frame */
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);

    /* Header line with output info */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *header = gtk_label_new(NULL);
    char markup[256];

    /* Choose icon and label based on type */
    const char *icon = strcmp(type, "shader") == 0 ? "✨" : "🖼️";
    const char *type_label = strcmp(type, "shader") == 0 ? "Live Shader" : "Static Image";

    snprintf(markup, sizeof(markup),
             "<span size='large'>%s</span> <b>%s</b>",
             icon, output);
    gtk_label_set_markup(GTK_LABEL(header), markup);
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header_box), header, FALSE, FALSE, 0);

    /* Spacer */
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);

    /* Type badge */
    GtkWidget *type_badge = gtk_label_new(NULL);
    char type_markup[64];
    snprintf(type_markup, sizeof(type_markup),
             "<span size='small' weight='600'>%s</span>", type_label);
    gtk_label_set_markup(GTK_LABEL(type_badge), type_markup);
    gtk_widget_set_opacity(type_badge, 0.7);
    gtk_box_pack_start(GTK_BOX(header_box), type_badge, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), header_box, FALSE, FALSE, 0);

    /* Path display */
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    /* File icon */
    GtkWidget *file_icon = gtk_label_new("📄");
    gtk_widget_set_opacity(file_icon, 0.6);
    gtk_box_pack_start(GTK_BOX(path_box), file_icon, FALSE, FALSE, 0);

    /* Path label */
    GtkWidget *path_label = gtk_label_new(NULL);
    char path_markup[512];
    snprintf(path_markup, sizeof(path_markup), "<tt>%s</tt>", basename);
    gtk_label_set_markup(GTK_LABEL(path_label), path_markup);
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(path_label, 0.65);
    gtk_label_set_selectable(GTK_LABEL(path_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(path_label), 70);
    gtk_box_pack_start(GTK_BOX(path_box), path_label, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(content), path_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), content);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    return vbox;
}

/* Show the status dialog */
void status_dialog_show(void) {
    /* Check if daemon is running */
    if (!daemon_is_running()) {
        GtkWidget *dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "⭕ NeoWall Daemon Stopped"
        );

        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
            "The NeoWall daemon is not currently running.\n"
            "Use the tray menu to start it.");

        ui_utils_set_window_icon(GTK_WINDOW(dialog));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* Create custom dialog - modal to parent */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "📊 NeoWall Status",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL
    );

    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 700);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
    ui_utils_set_window_icon(GTK_WINDOW(dialog));

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 0);

    /* Scrolled window */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(main_box, 24);
    gtk_widget_set_margin_end(main_box, 24);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 24);
    gtk_container_add(GTK_CONTAINER(scrolled), main_box);

    /* Get status via IPC */
    char output[8192] = {0};
    bool ipc_success = command_execute_with_output("--json status", output, sizeof(output));

    if (ipc_success && strstr(output, "\"daemon\":\"running\"")) {
        /* Find daemon data */
        const char *daemon_data = strstr(output, "\"daemon\":\"running\"");
        if (daemon_data) {
            while (daemon_data > output && *daemon_data != '{') {
                daemon_data--;
            }
        } else {
            daemon_data = output;
        }

        /* Parse data */
        int pid = 0, output_count = 0;
        bool paused = false, shader_paused = false;

        json_get_int(daemon_data, "pid", &pid);
        json_get_int(daemon_data, "outputs", &output_count);
        json_get_bool(daemon_data, "paused", &paused);
        json_get_bool(daemon_data, "shader_paused", &shader_paused);

        /* === DAEMON SECTION === */
        GtkWidget *daemon_section = create_status_section("⚙️  Daemon Information", "system-run");

        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", pid);
        gtk_box_pack_start(GTK_BOX(daemon_section),
            create_status_item("🆔 Process ID:", pid_str, true), FALSE, FALSE, 0);

        char outputs_str[32];
        snprintf(outputs_str, sizeof(outputs_str), "%d", output_count);
        gtk_box_pack_start(GTK_BOX(daemon_section),
            create_status_item("🖥️  Active Outputs:", outputs_str, true), FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(main_box), daemon_section, FALSE, FALSE, 0);

        /* Separator */
        GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(main_box), sep1, FALSE, FALSE, 0);

        /* === CYCLING SECTION === */
        GtkWidget *cycling_section = create_status_section("🔄 Wallpaper Cycling", "media-playlist-repeat");

        const char *cycle_status = paused ? "⏸️  Paused" : "▶️  Active";
        gtk_box_pack_start(GTK_BOX(cycling_section),
            create_status_item("📊 Status:", cycle_status, true), FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(main_box), cycling_section, FALSE, FALSE, 0);

        /* Separator */
        GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(main_box), sep2, FALSE, FALSE, 0);

        /* === SHADER SECTION === */
        GtkWidget *shader_section = create_status_section("✨ Live Animation", "video-display");

        const char *shader_status = shader_paused ? "⏸️  Paused" : "▶️  Active";
        gtk_box_pack_start(GTK_BOX(shader_section),
            create_status_item("📊 Status:", shader_status, true), FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(main_box), shader_section, FALSE, FALSE, 0);

        /* === CURRENT WALLPAPERS === */
        const char *wallpapers = strstr(daemon_data, "\"wallpapers\":[");
        if (wallpapers && output_count > 0) {
            /* Separator */
            GtkWidget *sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_box_pack_start(GTK_BOX(main_box), sep3, FALSE, FALSE, 0);

            GtkWidget *wallpaper_section = create_status_section("🖼️  Current Wallpapers", "image-x-generic");

            wallpapers += 14;  /* Skip "wallpapers":[ */
            const char *obj_start = wallpapers;
            int wp_num = 1;

            while ((obj_start = strchr(obj_start, '{')) != NULL) {
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

                    /* Add wallpaper item */
                    GtkWidget *wp_item = create_wallpaper_item(wp_num++, output_name, type, path);
                    gtk_box_pack_start(GTK_BOX(wallpaper_section), wp_item, FALSE, FALSE, 0);
                }

                obj_start = obj_end + 1;
            }

            gtk_box_pack_start(GTK_BOX(main_box), wallpaper_section, FALSE, FALSE, 0);
        }

    } else {
        /* Fallback */
        GtkWidget *info_box = ui_utils_create_info_box(
            "Unable to retrieve detailed status. The daemon may be starting up.",
            "dialog-warning"
        );
        gtk_box_pack_start(GTK_BOX(main_box), info_box, FALSE, FALSE, 0);

        pid_t pid = daemon_read_pid();
        if (pid > 0) {
            GtkWidget *daemon_section = create_status_section("Basic Information", "system-run");
            char pid_str[32];
            snprintf(pid_str, sizeof(pid_str), "%d", pid);
            gtk_box_pack_start(GTK_BOX(daemon_section),
                create_status_item("Process ID:", pid_str, true), FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(main_box), daemon_section, FALSE, FALSE, 0);
        }
    }

    /* Show everything */
    gtk_widget_show_all(dialog);

    /* Run dialog (blocking) */
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    TRAY_LOG_DEBUG(COMPONENT, "Status dialog closed");
}
