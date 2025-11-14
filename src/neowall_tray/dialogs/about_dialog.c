/* NeoWall Tray - About Dialog Implementation
 * Modern, cool about dialog with enhanced styling
 */

#include "about_dialog.h"
#include "../common/log.h"
#include "../ui/ui_utils.h"
#include <version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COMPONENT "ABOUT"

/* Create a feature item with icon */
static GtkWidget *create_feature_item(const char *icon_name, const char *title, const char *description) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);

    /* Icon */
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);
    gtk_widget_set_margin_top(icon, 4);
    gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);

    /* Text box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    /* Title */
    GtkWidget *title_label = gtk_label_new(NULL);
    char markup[256];
    snprintf(markup, sizeof(markup), "<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(title_label), markup);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);

    /* Description */
    GtkWidget *desc_label = gtk_label_new(description);
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 50);
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(desc_label, 0.8);
    gtk_box_pack_start(GTK_BOX(vbox), desc_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    return hbox;
}

/* Create a link button with icon */
static GtkWidget *create_link_button(const char *icon_name, const char *label, const char *uri) {
    GtkWidget *button = gtk_link_button_new_with_label(uri, label);

    /* Create custom content with icon */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    GtkWidget *label_widget = gtk_label_new(label);

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label_widget, FALSE, FALSE, 0);

    /* Replace button child */
    GList *children = gtk_container_get_children(GTK_CONTAINER(button));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_container_remove(GTK_CONTAINER(button), GTK_WIDGET(l->data));
    }
    g_list_free(children);

    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_widget_show_all(button);

    return button;
}

/* Show the about dialog */
void about_dialog_show(void) {
    /* Create custom dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "ℹ️  About NeoWall",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL
    );

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 700);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
    ui_utils_set_window_icon(GTK_WINDOW(dialog));

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 0);

    /* Main container with scrolling */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scrolled), main_box);

    /* ===== HEADER SECTION ===== */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(header_box, 24);
    gtk_widget_set_margin_bottom(header_box, 24);
    gtk_widget_set_margin_start(header_box, 24);
    gtk_widget_set_margin_end(header_box, 24);

    /* Try to load logo */
    const char *logo_paths[] = {
        "/usr/share/pixmaps/neowall.svg",
        "data/icons/neowall.svg",
        "../data/icons/neowall.svg",
        NULL
    };

    GdkPixbuf *logo = NULL;
    for (int i = 0; logo_paths[i] != NULL && !logo; i++) {
        if (access(logo_paths[i], F_OK) == 0) {
            GError *error = NULL;
            /* Make logo full width - 550px (allowing margin) */
            logo = gdk_pixbuf_new_from_file_at_scale(logo_paths[i], 550, -1, TRUE, &error);
            if (error) {
                g_error_free(error);
            }
        }
    }

    if (logo) {
        GtkWidget *logo_image = gtk_image_new_from_pixbuf(logo);
        gtk_widget_set_halign(logo_image, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(header_box), logo_image, FALSE, FALSE, 0);
        g_object_unref(logo);
    } else {
        /* Fallback icon */
        GtkWidget *icon = gtk_image_new_from_icon_name("preferences-desktop-wallpaper", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 200);
        gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(header_box), icon, FALSE, FALSE, 0);
    }

    /* App name */
    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(name_label),
        "<span size='xx-large' weight='bold'>NeoWall</span>");
    gtk_box_pack_start(GTK_BOX(header_box), name_label, FALSE, FALSE, 0);

    /* Version */
    GtkWidget *version_label = gtk_label_new(NULL);
    char version_markup[128];
    snprintf(version_markup, sizeof(version_markup),
        "<span size='large'>Version %s</span>", NEOWALL_VERSION);
    gtk_label_set_markup(GTK_LABEL(version_label), version_markup);
    gtk_widget_set_opacity(version_label, 0.7);
    gtk_box_pack_start(GTK_BOX(header_box), version_label, FALSE, FALSE, 0);

    /* Tagline */
    GtkWidget *tagline_label = gtk_label_new(NEOWALL_PROJECT_TAGLINE);
    gtk_label_set_line_wrap(GTK_LABEL(tagline_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(tagline_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_opacity(tagline_label, 0.8);
    gtk_box_pack_start(GTK_BOX(header_box), tagline_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), header_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep1, FALSE, FALSE, 0);

    /* ===== DESCRIPTION SECTION ===== */
    GtkWidget *desc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(desc_box, 20);
    gtk_widget_set_margin_bottom(desc_box, 20);
    gtk_widget_set_margin_start(desc_box, 24);
    gtk_widget_set_margin_end(desc_box, 24);

    GtkWidget *desc_label = gtk_label_new(
        "Transform your desktop with stunning GPU-accelerated GLSL shaders "
        "and dynamic wallpapers. NeoWall brings cinematic effects to your "
        "background with hardware acceleration and Wayland native support."
    );
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(desc_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 60);
    gtk_box_pack_start(GTK_BOX(desc_box), desc_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), desc_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep2, FALSE, FALSE, 0);

    /* ===== FEATURES SECTION ===== */
    GtkWidget *features_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(features_box, 20);
    gtk_widget_set_margin_bottom(features_box, 20);
    gtk_widget_set_margin_start(features_box, 24);
    gtk_widget_set_margin_end(features_box, 24);

    GtkWidget *features_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(features_title), "<span size='large' weight='bold'>Features</span>");
    gtk_widget_set_halign(features_title, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(features_title, 8);
    gtk_box_pack_start(GTK_BOX(features_box), features_title, FALSE, FALSE, 0);

    /* Feature items */
    gtk_box_pack_start(GTK_BOX(features_box),
        create_feature_item("video-display", "Real-time GLSL Shaders",
            "Hardware-accelerated fragment shaders with smooth animations"),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(features_box),
        create_feature_item("image-x-generic", "Image Cycling",
            "Dynamic wallpaper rotation with customizable transitions"),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(features_box),
        create_feature_item("video-display", "Multi-Monitor",
            "Independent wallpapers for each display output"),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(features_box),
        create_feature_item("applications-system", "Low Resource",
            "Efficient GPU rendering with minimal CPU usage"),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(features_box),
        create_feature_item("preferences-system", "Full Control",
            "System tray, IPC, and command-line interfaces"),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), features_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep3, FALSE, FALSE, 0);

    /* ===== LINKS SECTION ===== */
    GtkWidget *links_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(links_box, 20);
    gtk_widget_set_margin_bottom(links_box, 20);
    gtk_widget_set_margin_start(links_box, 24);
    gtk_widget_set_margin_end(links_box, 24);

    GtkWidget *links_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(links_title), "<span size='large' weight='bold'>Links</span>");
    gtk_widget_set_halign(links_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(links_box), links_title, FALSE, FALSE, 0);

    /* Link buttons */
    GtkWidget *links_grid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    gtk_box_pack_start(GTK_BOX(links_grid),
        create_link_button("applications-internet", "Project Homepage", NEOWALL_PROJECT_URL),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(links_grid),
        create_link_button("help-browser", "Documentation", NEOWALL_PROJECT_DOCS_URL),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(links_grid),
        create_link_button("dialog-warning", "Report Issues", NEOWALL_PROJECT_ISSUES_URL),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(links_box), links_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), links_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep4 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep4, FALSE, FALSE, 0);

    /* ===== CREDITS SECTION ===== */
    GtkWidget *credits_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(credits_box, 20);
    gtk_widget_set_margin_bottom(credits_box, 24);
    gtk_widget_set_margin_start(credits_box, 24);
    gtk_widget_set_margin_end(credits_box, 24);

    GtkWidget *credits_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(credits_title), "<span size='large' weight='bold'>Credits</span>");
    gtk_widget_set_halign(credits_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(credits_box), credits_title, FALSE, FALSE, 0);

    /* Author */
    GtkWidget *author_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(author_box, GTK_ALIGN_START);

    GtkWidget *author_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(author_label), "<b>Created by:</b>");
    gtk_box_pack_start(GTK_BOX(author_box), author_label, FALSE, FALSE, 0);

    /* Make author a clickable link to project */
    GtkWidget *author_link = gtk_link_button_new_with_label(NEOWALL_PROJECT_URL, NEOWALL_PROJECT_URL_SHORT);
    gtk_button_set_relief(GTK_BUTTON(author_link), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(author_box), author_link, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(credits_box), author_box, FALSE, FALSE, 0);

    /* Contributors */
    GtkWidget *contrib_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(contrib_label),
        "<b>Contributors:</b>\n"
        "• NeoWall Community\n"
        "• Wayland Developers\n"
        "• GLSL Shader Artists\n"
        "• Open Source Community");
    gtk_widget_set_halign(contrib_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(credits_box), contrib_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), credits_box, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep5 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep5, FALSE, FALSE, 0);

    /* ===== LICENSE SECTION ===== */
    GtkWidget *license_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(license_box, 20);
    gtk_widget_set_margin_bottom(license_box, 24);
    gtk_widget_set_margin_start(license_box, 24);
    gtk_widget_set_margin_end(license_box, 24);

    GtkWidget *license_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(license_title), "<span size='large' weight='bold'>License</span>");
    gtk_widget_set_halign(license_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(license_box), license_title, FALSE, FALSE, 0);

    GtkWidget *license_label = gtk_label_new(NEOWALL_PROJECT_COPYRIGHT);
    gtk_widget_set_halign(license_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(license_box), license_label, FALSE, FALSE, 0);

    GtkWidget *mit_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(mit_label),
        "Licensed under the <b>MIT License</b>\n"
        "See LICENSE file for details");
    gtk_widget_set_halign(mit_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(mit_label, 0.7);
    gtk_box_pack_start(GTK_BOX(license_box), mit_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), license_box, FALSE, FALSE, 0);

    /* Show everything */
    gtk_widget_show_all(dialog);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    TRAY_LOG_DEBUG(COMPONENT, "About dialog closed");
}
