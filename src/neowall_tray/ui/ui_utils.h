/* NeoWall Tray - UI Utilities Header
 * Theming, styling, and UI helper functions
 */

#ifndef UI_UTILS_H
#define UI_UTILS_H

#include <gtk/gtk.h>
#include <stdbool.h>

/* ========================================================================
   CSS & THEMING
   ======================================================================== */

/**
 * Initialize and apply UI theming
 * Loads CSS styles and sets up the visual theme
 * 
 * @return TRUE on success, FALSE on failure
 */
gboolean ui_utils_init_theme(void);

/**
 * Apply CSS class to a widget
 * 
 * @param widget Widget to apply class to
 * @param class_name CSS class name
 */
void ui_utils_add_class(GtkWidget *widget, const char *class_name);

/**
 * Remove CSS class from a widget
 * 
 * @param widget Widget to remove class from
 * @param class_name CSS class name
 */
void ui_utils_remove_class(GtkWidget *widget, const char *class_name);

/**
 * Check if widget has a CSS class
 * 
 * @param widget Widget to check
 * @param class_name CSS class name
 * @return TRUE if widget has the class
 */
gboolean ui_utils_has_class(GtkWidget *widget, const char *class_name);

/* ========================================================================
   WIDGET CREATION HELPERS
   ======================================================================== */

/**
 * Create a labeled setting row with label and control widget
 * 
 * @param label_text Label text (can include Pango markup if use_markup is TRUE)
 * @param widget Control widget (entry, combo, etc.)
 * @param use_markup Whether to use Pango markup in label
 * @return Container box with label and widget
 */
GtkWidget *ui_utils_create_labeled_row(const char *label_text, 
                                        GtkWidget *widget, 
                                        gboolean use_markup);

/**
 * Create a section container with header
 * 
 * @param title Section title
 * @param subtitle Optional subtitle (can be NULL)
 * @return Container box for the section
 */
GtkWidget *ui_utils_create_section(const char *title, const char *subtitle);

/**
 * Create an info box with icon and message
 * 
 * @param message Info message
 * @param icon_name GTK icon name (can be NULL for no icon)
 * @return Info box widget
 */
GtkWidget *ui_utils_create_info_box(const char *message, const char *icon_name);

/**
 * Create a settings row with label, widget, and optional help text
 * 
 * @param label_text Main label
 * @param widget Control widget
 * @param help_text Optional help text (can be NULL)
 * @return Container with label, widget, and help text
 */
GtkWidget *ui_utils_create_settings_row(const char *label_text,
                                         GtkWidget *widget,
                                         const char *help_text);

/* ========================================================================
   ICONS & IMAGES
   ======================================================================== */

/**
 * Create an icon image widget
 * 
 * @param icon_name Icon name from theme
 * @param size Icon size in pixels
 * @return Image widget
 */
GtkWidget *ui_utils_create_icon(const char *icon_name, int size);

/**
 * Create an icon with text label
 * 
 * @param icon_name Icon name
 * @param text Label text
 * @param icon_size Icon size in pixels
 * @return Box containing icon and label
 */
GtkWidget *ui_utils_create_icon_label(const char *icon_name, 
                                       const char *text, 
                                       int icon_size);

/**
 * Set window icon from app resources
 * 
 * @param window Window to set icon for
 */
void ui_utils_set_window_icon(GtkWindow *window);

/* ========================================================================
   STATUS & FEEDBACK
   ======================================================================== */

/**
 * Status message type
 */
typedef enum {
    UI_STATUS_INFO,
    UI_STATUS_SUCCESS,
    UI_STATUS_WARNING,
    UI_STATUS_ERROR
} UIStatusType;

/**
 * Create a status label with styling
 * 
 * @param message Status message
 * @param type Status type
 * @return Styled label widget
 */
GtkWidget *ui_utils_create_status_label(const char *message, UIStatusType type);

/**
 * Update status label with new message and type
 * 
 * @param label Label widget to update
 * @param message New message
 * @param type New status type
 */
void ui_utils_update_status_label(GtkWidget *label, 
                                   const char *message, 
                                   UIStatusType type);

/**
 * Show a temporary toast-style notification
 * 
 * @param parent Parent window (can be NULL)
 * @param message Message to display
 * @param type Status type
 * @param duration_ms Duration in milliseconds (0 for manual dismiss)
 */
void ui_utils_show_toast(GtkWindow *parent, 
                         const char *message, 
                         UIStatusType type,
                         int duration_ms);

/* ========================================================================
   DIALOGS
   ======================================================================== */

/**
 * Create a modern styled dialog
 * 
 * @param parent Parent window
 * @param title Dialog title
 * @param width Default width
 * @param height Default height
 * @return Dialog widget
 */
GtkWidget *ui_utils_create_dialog(GtkWindow *parent,
                                   const char *title,
                                   int width,
                                   int height);

/**
 * Add styled button to dialog
 * 
 * @param dialog Dialog widget
 * @param label Button label
 * @param response_id Response ID
 * @param is_primary Whether this is a primary action button
 * @return Created button widget
 */
GtkWidget *ui_utils_dialog_add_button(GtkWidget *dialog,
                                       const char *label,
                                       gint response_id,
                                       gboolean is_primary);

/* ========================================================================
   LAYOUT HELPERS
   ======================================================================== */

/**
 * Create a horizontal box with spacing
 * 
 * @param spacing Spacing between children
 * @param homogeneous Whether children should be same size
 * @return Box widget
 */
GtkWidget *ui_utils_create_hbox(int spacing, gboolean homogeneous);

/**
 * Create a vertical box with spacing
 * 
 * @param spacing Spacing between children
 * @param homogeneous Whether children should be same size
 * @return Box widget
 */
GtkWidget *ui_utils_create_vbox(int spacing, gboolean homogeneous);

/**
 * Create a frame with title
 * 
 * @param title Frame title (can be NULL)
 * @return Frame widget
 */
GtkWidget *ui_utils_create_frame(const char *title);

/**
 * Add padding to a widget
 * 
 * @param widget Widget to add padding to
 * @param top Top padding
 * @param right Right padding
 * @param bottom Bottom padding
 * @param left Left padding
 */
void ui_utils_set_padding(GtkWidget *widget, 
                          int top, int right, int bottom, int left);

/**
 * Set margins on a widget
 * 
 * @param widget Widget to set margins on
 * @param top Top margin
 * @param right Right margin
 * @param bottom Bottom margin
 * @param left Left margin
 */
void ui_utils_set_margins(GtkWidget *widget,
                          int top, int right, int bottom, int left);

/* ========================================================================
   ANIMATIONS & TRANSITIONS
   ======================================================================== */

/**
 * Fade in a widget
 * 
 * @param widget Widget to fade in
 * @param duration_ms Animation duration in milliseconds
 */
void ui_utils_fade_in(GtkWidget *widget, int duration_ms);

/**
 * Fade out a widget
 * 
 * @param widget Widget to fade out
 * @param duration_ms Animation duration in milliseconds
 */
void ui_utils_fade_out(GtkWidget *widget, int duration_ms);

/**
 * Pulse a widget (for drawing attention)
 * 
 * @param widget Widget to pulse
 * @param count Number of pulses
 */
void ui_utils_pulse_widget(GtkWidget *widget, int count);

/* ========================================================================
   COLOR UTILITIES
   ======================================================================== */

/**
 * Parse color string to GdkRGBA
 * 
 * @param color_str Color string (hex or name)
 * @param color Output RGBA color
 * @return TRUE on success
 */
gboolean ui_utils_parse_color(const char *color_str, GdkRGBA *color);

/**
 * Set widget foreground color
 * 
 * @param widget Widget to colorize
 * @param color_str Color string
 */
void ui_utils_set_fg_color(GtkWidget *widget, const char *color_str);

/**
 * Set widget background color
 * 
 * @param widget Widget to colorize
 * @param color_str Color string
 */
void ui_utils_set_bg_color(GtkWidget *widget, const char *color_str);

/* ========================================================================
   UTILITY FUNCTIONS
   ======================================================================== */

/**
 * Set tooltip with optional icon
 * 
 * @param widget Widget to add tooltip to
 * @param text Tooltip text
 * @param markup Whether text contains Pango markup
 */
void ui_utils_set_tooltip(GtkWidget *widget, const char *text, gboolean markup);

/**
 * Make widget expand and fill
 * 
 * @param widget Widget to configure
 * @param horizontal Expand horizontally
 * @param vertical Expand vertically
 */
void ui_utils_expand_fill(GtkWidget *widget, gboolean horizontal, gboolean vertical);

/**
 * Enable/disable widget with fade effect
 * 
 * @param widget Widget to enable/disable
 * @param enabled New enabled state
 */
void ui_utils_set_enabled(GtkWidget *widget, gboolean enabled);

/**
 * Get system accent color if available
 * 
 * @param color Output color
 * @return TRUE if accent color was retrieved
 */
gboolean ui_utils_get_accent_color(GdkRGBA *color);

/**
 * Check if system is using dark theme
 * 
 * @return TRUE if dark theme is active
 */
gboolean ui_utils_is_dark_theme(void);

#endif /* UI_UTILS_H */