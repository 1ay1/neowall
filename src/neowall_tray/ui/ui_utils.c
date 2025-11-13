/* NeoWall Tray - UI Utilities Implementation
 * Theming, styling, and UI helper functions
 */

#include "ui_utils.h"
#include "../common/log.h"
#include <stdlib.h>
#include <string.h>

#define COMPONENT "UI_UTILS"

/* CSS provider for custom styling */
static GtkCssProvider *g_css_provider = NULL;

/* Embedded CSS styles - minimal, respects system theme */
static const char *embedded_css =
"* {"
"    outline-style: none;"
"}"
""
"window, dialog {"
"    border-radius: 8px;"
"}"
""
".settings-section {"
"    padding: 16px;"
"    margin: 10px;"
"    border-radius: 6px;"
"}"
""
".settings-row {"
"    padding: 10px 0;"
"    margin: 4px 0;"
"}"
""
".status-label {"
"    padding: 10px 14px;"
"    border-radius: 4px;"
"    margin: 6px 0;"
"}"
""
".info-box {"
"    padding: 12px;"
"    margin: 10px 0;"
"    border-radius: 4px;"
"}"
""
"label.setting-label {"
"    margin-bottom: 6px;"
"    font-weight: 600;"
"}"
""
"label.help-text {"
"    margin-top: 4px;"
"    font-size: 0.9em;"
"    opacity: 0.8;"
"}"
""
"button {"
"    padding: 8px 16px;"
"    min-height: 32px;"
"    border-radius: 4px;"
"}"
""
"button.suggested-action, button.apply-button {"
"    padding: 10px 20px;"
"    min-height: 36px;"
"    font-weight: 600;"
"}"
""
"entry {"
"    padding: 8px 12px;"
"    min-height: 36px;"
"    border-radius: 4px;"
"}"
""
"combobox button {"
"    padding: 8px 12px;"
"    min-height: 36px;"
"    border-radius: 4px;"
"}"
""
"spinbutton {"
"    padding: 6px 10px;"
"    border-radius: 4px;"
"}"
""
"notebook > header {"
"    padding: 8px 12px;"
"}"
""
"notebook > header > tabs > tab {"
"    padding: 12px 24px;"
"    margin: 0 2px;"
"    border-radius: 4px 4px 0 0;"
"}"
""
"separator {"
"    margin: 8px 0;"
"}"
""
"/* Menu styling */"
"menu {"
"    padding: 4px 0;"
"    border-radius: 8px;"
"}"
""
"menuitem {"
"    padding: 8px 16px;"
"    min-height: 32px;"
"}"
""
"menuitem label {"
"    padding: 2px 0;"
"}"
""
"menuitem arrow {"
"    min-width: 16px;"
"    min-height: 16px;"
"}"
""
"separator.horizontal {"
"    min-height: 1px;"
"    margin: 6px 0;"
"}"
""
"menuitem:disabled {"
"    padding: 10px 16px;"
"}"
""
"menuitem:disabled label {"
"    padding: 4px 0;"
"}";

/* ========================================================================
   CSS & THEMING
   ======================================================================== */

gboolean ui_utils_init_theme(void) {
    if (g_css_provider) {
        TRAY_LOG_DEBUG(COMPONENT, "CSS provider already initialized");
        return TRUE;
    }

    g_css_provider = gtk_css_provider_new();

    GError *error = NULL;

    /* Try to load external CSS file first */
    const char *css_paths[] = {
        "/usr/share/neowall/style.css",
        "/usr/local/share/neowall/style.css",
        "src/neowall_tray/ui/style.css",
        NULL
    };

    gboolean loaded = FALSE;
    for (int i = 0; css_paths[i] != NULL; i++) {
        if (g_file_test(css_paths[i], G_FILE_TEST_EXISTS)) {
            if (gtk_css_provider_load_from_path(g_css_provider, css_paths[i], &error)) {
                TRAY_LOG_INFO(COMPONENT, "Loaded CSS from: %s", css_paths[i]);
                loaded = TRUE;
                break;
            } else {
                TRAY_LOG_DEBUG(COMPONENT, "Failed to load CSS from %s: %s",
                             css_paths[i], error ? error->message : "unknown");
                if (error) {
                    g_error_free(error);
                    error = NULL;
                }
            }
        }
    }

    /* Fall back to embedded CSS */
    if (!loaded) {
        TRAY_LOG_INFO(COMPONENT, "Loading embedded CSS styles");
        if (!gtk_css_provider_load_from_data(g_css_provider, embedded_css, -1, &error)) {
            TRAY_LOG_INFO(COMPONENT, "Failed to load embedded CSS: %s",
                         error ? error->message : "unknown");
            if (error) g_error_free(error);
            g_object_unref(g_css_provider);
            g_css_provider = NULL;
            return FALSE;
        }
    }

    /* Apply to default screen */
    GdkScreen *screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(g_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    TRAY_LOG_INFO(COMPONENT, "UI theme initialized successfully");
    return TRUE;
}

void ui_utils_add_class(GtkWidget *widget, const char *class_name) {
    if (!widget || !class_name) return;

    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    gtk_style_context_add_class(context, class_name);
}

void ui_utils_remove_class(GtkWidget *widget, const char *class_name) {
    if (!widget || !class_name) return;

    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    gtk_style_context_remove_class(context, class_name);
}

gboolean ui_utils_has_class(GtkWidget *widget, const char *class_name) {
    if (!widget || !class_name) return FALSE;

    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    return gtk_style_context_has_class(context, class_name);
}

/* ========================================================================
   WIDGET CREATION HELPERS
   ======================================================================== */

GtkWidget *ui_utils_create_labeled_row(const char *label_text,
                                        GtkWidget *widget,
                                        gboolean use_markup) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *label = gtk_label_new(NULL);
    if (use_markup) {
        gtk_label_set_markup(GTK_LABEL(label), label_text);
    } else {
        gtk_label_set_text(GTK_LABEL(label), label_text);
    }
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    ui_utils_add_class(label, "setting-label");

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);

    return box;
}

GtkWidget *ui_utils_create_section(const char *title, const char *subtitle) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    ui_utils_add_class(box, "settings-section");

    /* Title */
    GtkWidget *title_label = gtk_label_new(NULL);
    char markup[512];
    snprintf(markup, sizeof(markup), "<big><b>%s</b></big>", title);
    gtk_label_set_markup(GTK_LABEL(title_label), markup);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);

    /* Subtitle */
    if (subtitle) {
        GtkWidget *subtitle_label = gtk_label_new(NULL);
        snprintf(markup, sizeof(markup), "<small>%s</small>", subtitle);
        gtk_label_set_markup(GTK_LABEL(subtitle_label), markup);
        gtk_widget_set_halign(subtitle_label, GTK_ALIGN_START);
        ui_utils_add_class(subtitle_label, "help-text");
        gtk_box_pack_start(GTK_BOX(box), subtitle_label, FALSE, FALSE, 0);
    }

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 5);

    return box;
}

GtkWidget *ui_utils_create_info_box(const char *message, const char *icon_name) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui_utils_add_class(box, "info-box");

    if (icon_name) {
        GtkWidget *icon = ui_utils_create_icon(icon_name, 24);
        gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    }

    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    return box;
}

GtkWidget *ui_utils_create_settings_row(const char *label_text,
                                         GtkWidget *widget,
                                         const char *help_text) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    ui_utils_add_class(box, "settings-row");

    /* Main label */
    GtkWidget *label = gtk_label_new(NULL);
    char markup[256];
    snprintf(markup, sizeof(markup), "<b>%s</b>", label_text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    /* Control widget */
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);

    /* Help text */
    if (help_text) {
        GtkWidget *help = gtk_label_new(NULL);
        snprintf(markup, sizeof(markup), "<small><i>%s</i></small>", help_text);
        gtk_label_set_markup(GTK_LABEL(help), markup);
        gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
        gtk_widget_set_halign(help, GTK_ALIGN_START);
        ui_utils_add_class(help, "help-text");
        gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 0);
    }

    return box;
}

/* ========================================================================
   ICONS & IMAGES
   ======================================================================== */

GtkWidget *ui_utils_create_icon(const char *icon_name, int size) {
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    return image;
}

GtkWidget *ui_utils_create_icon_label(const char *icon_name,
                                       const char *text,
                                       int icon_size) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *icon = ui_utils_create_icon(icon_name, icon_size);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new(text);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    return box;
}

void ui_utils_set_window_icon(GtkWindow *window) {
    const char *icon_paths[] = {
        "/usr/share/pixmaps/neowall-icon.svg",
        "/usr/share/pixmaps/neowall.svg",
        "/usr/local/share/pixmaps/neowall-icon.svg",
        NULL
    };

    for (int i = 0; icon_paths[i] != NULL; i++) {
        if (g_file_test(icon_paths[i], G_FILE_TEST_EXISTS)) {
            GError *error = NULL;
            GdkPixbuf *icon = gdk_pixbuf_new_from_file_at_size(icon_paths[i], 48, 48, &error);
            if (icon) {
                gtk_window_set_icon(window, icon);
                g_object_unref(icon);
                return;
            }
            if (error) g_error_free(error);
        }
    }

    /* Fallback to theme icon */
    gtk_window_set_icon_name(window, "preferences-desktop-wallpaper");
}

/* ========================================================================
   STATUS & FEEDBACK
   ======================================================================== */

GtkWidget *ui_utils_create_status_label(const char *message, UIStatusType type) {
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);

    /* Just add padding class, let system theme handle colors */
    ui_utils_add_class(label, "status-label");

    /* Unused for now - system theme provides appropriate styling */
    (void)type;

    return label;
}

void ui_utils_update_status_label(GtkWidget *label,
                                   const char *message,
                                   UIStatusType type) {
    if (!GTK_IS_LABEL(label)) return;

    /* Set new message */
    gtk_label_set_text(GTK_LABEL(label), message);

    /* Unused for now - system theme provides appropriate styling */
    (void)type;
}

/* Toast data structure */
typedef struct {
    GtkWidget *window;
    guint timeout_id;
} ToastData;

static gboolean toast_timeout_cb(gpointer user_data) {
    ToastData *data = (ToastData *)user_data;
    if (data->window) {
        gtk_widget_destroy(data->window);
    }
    g_free(data);
    return FALSE;
}

void ui_utils_show_toast(GtkWindow *parent,
                         const char *message,
                         UIStatusType type,
                         int duration_ms) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
    } else {
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    }

    /* Simple label with padding, system theme handles colors */
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    ui_utils_add_class(label, "status-label");
    gtk_container_add(GTK_CONTAINER(window), label);

    /* Unused - system theme provides styling */
    (void)type;

    gtk_widget_show_all(window);

    if (duration_ms > 0) {
        ToastData *data = g_new0(ToastData, 1);
        data->window = window;
        data->timeout_id = g_timeout_add(duration_ms, toast_timeout_cb, data);
    }
}

/* ========================================================================
   DIALOGS
   ======================================================================== */

GtkWidget *ui_utils_create_dialog(GtkWindow *parent,
                                   const char *title,
                                   int width,
                                   int height) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL, NULL
    );

    gtk_window_set_default_size(GTK_WINDOW(dialog), width, height);
    ui_utils_set_window_icon(GTK_WINDOW(dialog));

    return dialog;
}

GtkWidget *ui_utils_dialog_add_button(GtkWidget *dialog,
                                       const char *label,
                                       gint response_id,
                                       gboolean is_primary) {
    GtkWidget *button = gtk_dialog_add_button(GTK_DIALOG(dialog), label, response_id);

    if (is_primary) {
        ui_utils_add_class(button, "suggested-action");
        ui_utils_add_class(button, "apply-button");
    }

    return button;
}

/* ========================================================================
   LAYOUT HELPERS
   ======================================================================== */

GtkWidget *ui_utils_create_hbox(int spacing, gboolean homogeneous) {
    (void)homogeneous;  /* Reserved for future use */
    return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing);
}

GtkWidget *ui_utils_create_vbox(int spacing, gboolean homogeneous) {
    (void)homogeneous;  /* Reserved for future use */
    return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
}

GtkWidget *ui_utils_create_frame(const char *title) {
    GtkWidget *frame = gtk_frame_new(title);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    return frame;
}

void ui_utils_set_padding(GtkWidget *widget,
                          int top, int right, int bottom, int left) {
    if (!GTK_IS_CONTAINER(widget)) return;
    gtk_container_set_border_width(GTK_CONTAINER(widget), top);
    /* GTK3 doesn't support asymmetric padding easily, use margins */
    ui_utils_set_margins(widget, top, right, bottom, left);
}

void ui_utils_set_margins(GtkWidget *widget,
                          int top, int right, int bottom, int left) {
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_end(widget, right);
    gtk_widget_set_margin_bottom(widget, bottom);
    gtk_widget_set_margin_start(widget, left);
}

/* ========================================================================
   ANIMATIONS & TRANSITIONS
   ======================================================================== */

typedef struct {
    GtkWidget *widget;
    gdouble current_opacity;
    gdouble target_opacity;
    gdouble step;
    int steps_remaining;
} FadeData;

static gboolean fade_tick(gpointer user_data) {
    FadeData *data = (FadeData *)user_data;

    if (data->steps_remaining <= 0) {
        gtk_widget_set_opacity(data->widget, data->target_opacity);
        g_free(data);
        return FALSE;
    }

    data->current_opacity += data->step;
    gtk_widget_set_opacity(data->widget, data->current_opacity);
    data->steps_remaining--;

    return TRUE;
}

void ui_utils_fade_in(GtkWidget *widget, int duration_ms) {
    FadeData *data = g_new0(FadeData, 1);
    data->widget = widget;
    data->current_opacity = 0.0;
    data->target_opacity = 1.0;
    data->steps_remaining = duration_ms / 16;  /* ~60 FPS */
    data->step = 1.0 / data->steps_remaining;

    gtk_widget_set_opacity(widget, 0.0);
    g_timeout_add(16, fade_tick, data);
}

void ui_utils_fade_out(GtkWidget *widget, int duration_ms) {
    FadeData *data = g_new0(FadeData, 1);
    data->widget = widget;
    data->current_opacity = 1.0;
    data->target_opacity = 0.0;
    data->steps_remaining = duration_ms / 16;
    data->step = -1.0 / data->steps_remaining;

    gtk_widget_set_opacity(widget, 1.0);
    g_timeout_add(16, fade_tick, data);
}

typedef struct {
    GtkWidget *widget;
    int count;
    gboolean growing;
    gdouble scale;
} PulseData;

static gboolean pulse_tick(gpointer user_data) {
    PulseData *data = (PulseData *)user_data;

    if (data->count <= 0) {
        gtk_widget_set_opacity(data->widget, 1.0);
        g_free(data);
        return FALSE;
    }

    if (data->growing) {
        data->scale += 0.05;
        if (data->scale >= 1.1) {
            data->growing = FALSE;
            data->count--;
        }
    } else {
        data->scale -= 0.05;
        if (data->scale <= 1.0) {
            data->growing = TRUE;
        }
    }

    gtk_widget_set_opacity(data->widget, data->scale);
    return TRUE;
}

void ui_utils_pulse_widget(GtkWidget *widget, int count) {
    PulseData *data = g_new0(PulseData, 1);
    data->widget = widget;
    data->count = count;
    data->growing = TRUE;
    data->scale = 1.0;

    g_timeout_add(50, pulse_tick, data);
}

/* ========================================================================
   COLOR UTILITIES
   ======================================================================== */

gboolean ui_utils_parse_color(const char *color_str, GdkRGBA *color) {
    return gdk_rgba_parse(color, color_str);
}

void ui_utils_set_fg_color(GtkWidget *widget, const char *color_str) {
    GdkRGBA color;
    if (ui_utils_parse_color(color_str, &color)) {
        /* Use CSS provider instead of deprecated override functions */
        char css[256];
        snprintf(css, sizeof(css), "* { color: %s; }", color_str);

        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css, -1, NULL);

        GtkStyleContext *context = gtk_widget_get_style_context(widget);
        gtk_style_context_add_provider(context,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        g_object_unref(provider);
    }
}

void ui_utils_set_bg_color(GtkWidget *widget, const char *color_str) {
    GdkRGBA color;
    if (ui_utils_parse_color(color_str, &color)) {
        /* Use CSS provider instead of deprecated override functions */
        char css[256];
        snprintf(css, sizeof(css), "* { background-color: %s; }", color_str);

        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css, -1, NULL);

        GtkStyleContext *context = gtk_widget_get_style_context(widget);
        gtk_style_context_add_provider(context,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        g_object_unref(provider);
    }
}

/* ========================================================================
   UTILITY FUNCTIONS
   ======================================================================== */

void ui_utils_set_tooltip(GtkWidget *widget, const char *text, gboolean markup) {
    if (markup) {
        gtk_widget_set_tooltip_markup(widget, text);
    } else {
        gtk_widget_set_tooltip_text(widget, text);
    }
}

void ui_utils_expand_fill(GtkWidget *widget, gboolean horizontal, gboolean vertical) {
    gtk_widget_set_hexpand(widget, horizontal);
    gtk_widget_set_vexpand(widget, vertical);
    if (horizontal) gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    if (vertical) gtk_widget_set_valign(widget, GTK_ALIGN_FILL);
}

void ui_utils_set_enabled(GtkWidget *widget, gboolean enabled) {
    gtk_widget_set_sensitive(widget, enabled);
    if (enabled) {
        gtk_widget_set_opacity(widget, 1.0);
    } else {
        gtk_widget_set_opacity(widget, 0.5);
    }
}

gboolean ui_utils_get_accent_color(GdkRGBA *color) {
    /* Try to get system accent color (varies by desktop environment) */
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return FALSE;

    /* Default accent color */
    return ui_utils_parse_color("#667eea", color);
}

gboolean ui_utils_is_dark_theme(void) {
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return FALSE;

    gboolean dark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &dark, NULL);
    return dark;
}
