/* NeoWall Tray - Dialogs Component
 * Implementation of dialog windows
 */

#include "dialogs.h"
#include "../daemon/command_exec.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define APP_TITLE "NeoWall"
#define LOGO_PATH "/usr/share/pixmaps/neowall.svg"
#define ICON_PATH "/usr/share/pixmaps/neowall-icon.svg"

/* Show the daemon status dialog */
void dialog_show_status(void) {
    char output[4096] = {0};

    if (!command_execute_with_output("status", output, sizeof(output))) {
        dialog_show_error("Status Error", "Failed to retrieve daemon status");
        return;
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "NeoWall Status"
    );

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", output);
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
    char version[256] = "Unknown";

    /* Get version from neowall */
    command_execute_with_output("version", version, sizeof(version));

    /* Remove trailing newline */
    size_t len = strlen(version);
    if (len > 0 && version[len - 1] == '\n') {
        version[len - 1] = '\0';
    }

    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "NeoWall");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), version);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "GPU-accelerated shader wallpapers for Wayland\n\n"
        "System tray control application");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
        "https://github.com/1ay1/neowall");
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "GitHub Repository");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_MIT_X11);

    /* Try to load custom logo from multiple paths */
    GdkPixbuf *logo = NULL;
    GError *error = NULL;

    const char *logo_paths[] = {
        LOGO_PATH,                          /* Installed path */
        "neowall.svg",                      /* Current directory (builddir) */
        "../packaging/neowall.svg",         /* Source tree from builddir */
        "packaging/neowall.svg",            /* Source tree from project root */
        NULL
    };

    for (int i = 0; logo_paths[i] != NULL && !logo; i++) {
        if (access(logo_paths[i], F_OK) == 0) {
            logo = gdk_pixbuf_new_from_file_at_scale(logo_paths[i], 400, -1, TRUE, &error);
            if (logo) {
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
    }

    const gchar *authors[] = {"NeoWall Contributors", NULL};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

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

/* Show an info dialog */
void dialog_show_info(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s",
        title ? title : "Information"
    );

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
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
