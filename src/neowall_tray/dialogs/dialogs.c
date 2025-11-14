/* NeoWall Tray - Dialogs Component
 * Implementation of dialog windows
 */

#include "dialogs.h"
#include "settings_dialog.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "../daemon/daemon_check.h"
#include "../ui/ui_utils.h"
#include "about_dialog.h"
#include "status_dialog.h"
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

/* Dialog timing constants (milliseconds) */
#define DIALOG_AUTO_CLOSE_SHORT 1000
#define DIALOG_AUTO_CLOSE_MEDIUM 1500
#define DIALOG_AUTO_CLOSE_LONG 2000
#define DIALOG_CHECK_INTERVAL 500
#define DIALOG_BORDER_WIDTH 12
#define DIALOG_CONTENT_MARGIN 16



/* Show the daemon status dialog with detailed information */
void dialog_show_status(void) {
    /* Use new beautiful status dialog */
    status_dialog_show();
}



/* Show the about dialog */
void dialog_show_about(void) {
    /* Use new modern about dialog */
    about_dialog_show();
}

/* Helper: Configure basic dialog properties */
static void configure_dialog_base(GtkWidget *dialog) {
    ui_utils_set_window_icon(GTK_WINDOW(dialog));
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
}

/* Helper: Add padding to dialog content area */
static void add_dialog_padding(GtkWidget *dialog) {
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), DIALOG_BORDER_WIDTH);
}

/* Show a confirmation dialog for stopping the daemon */
gint dialog_confirm_stop_daemon(void) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "🛑 Stop NeoWall Daemon?"
    );

    configure_dialog_base(dialog);

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "This will stop the wallpaper daemon. The tray will remain open.");

    /* Add custom buttons with better styling */
    GtkWidget *cancel_btn = gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_NO);
    GtkWidget *stop_btn = gtk_dialog_add_button(GTK_DIALOG(dialog), "Stop Daemon", GTK_RESPONSE_YES);

    /* Style the stop button as destructive */
    GtkStyleContext *context = gtk_widget_get_style_context(stop_btn);
    gtk_style_context_add_class(context, "destructive-action");
    (void)cancel_btn;

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result;
}

/* Show an error dialog */
void dialog_show_error(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s",
        title ? title : "❌ Error"
    );

    configure_dialog_base(dialog);

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    add_dialog_padding(dialog);

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
        title ? title : "ℹ️ Information"
    );

    configure_dialog_base(dialog);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    add_dialog_padding(dialog);

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
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s",
        title ? title : "⚠️ Warning"
    );

    configure_dialog_base(dialog);

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    add_dialog_padding(dialog);

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
        /* Clear timer ID to prevent double-removal */
        data->check_timer_id = 0;

        /* Operation complete - schedule dialog close after delay */
        if (data->auto_close_delay_ms > 0) {
            g_timeout_add(data->auto_close_delay_ms, destroy_widget_callback, data->dialog);
        } else {
            gtk_widget_destroy(data->dialog);
        }

        /* Don't free data here - let the destroy signal handler do it */
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
        title ? title : "⏳ Progress"
    );

    configure_dialog_base(dialog);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

    if (message) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    }

    /* Add padding and better styling */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), DIALOG_CONTENT_MARGIN);

    /* Add a spinner to show progress */
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_margin_top(spinner, 12);
    gtk_widget_set_margin_bottom(spinner, 12);
    gtk_box_pack_start(GTK_BOX(content_area), spinner, FALSE, FALSE, 0);

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
        data->check_timer_id = g_timeout_add(DIALOG_CHECK_INTERVAL, progress_check_timer, data);
    }

    return dialog;
}

/* Show the settings/preferences dialog - delegates to settings_dialog.c */
void dialog_show_settings(void) {
    settings_dialog_show();
}
