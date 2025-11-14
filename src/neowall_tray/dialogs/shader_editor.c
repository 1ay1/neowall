/* NeoWall Tray - Shader Editor Dialog Implementation
 * Live shader playground using the modular NeoWall shader library
 */

#include "shader_editor.h"
#include "../common/log.h"
#include "../daemon/command_exec.h"
#include "../../shader_lib/shader.h"
#include <gtk/gtk.h>

/* Suppress GTimeVal deprecation warnings from GtkSourceView headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop

#include <GLES3/gl3.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Window state */
static GtkWidget *editor_window = NULL;
static GtkSourceBuffer *source_buffer = NULL;
static GtkWidget *gl_area = NULL;
static GtkWidget *error_label = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *theme_combo = NULL;
static char *current_file_path = NULL;
static char *saved_theme_preference = NULL;

/* OpenGL state */
static GLuint shader_program = 0;
static GLuint vao = 0;
static GLuint vbo = 0;
static bool gl_initialized = false;
static bool shader_valid = false;
static double start_time = 0.0;

/* Animation timer for 60 FPS updates */
static guint animation_timer_id = 0;

/* Shader compilation debounce */
static guint compile_timeout_id = 0;

/* Default simple shader template - exactly as NeoWall expects it */
static const char *DEFAULT_SHADER =
"// Simple animated gradient shader\n"
"// This shader uses NeoWall's standard Shadertoy-compatible uniforms\n"
"\n"
"void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
"    // Normalized pixel coordinates (from 0 to 1)\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    \n"
"    // Time-based color animation\n"
"    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0.0, 2.0, 4.0));\n"
"    \n"
"    // Output to screen\n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

/* Forward declarations */
static gboolean animation_timer_cb(gpointer user_data);
static void on_theme_changed(GtkComboBox *combo, gpointer user_data);

/* Helper: Get current time in seconds */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Helper: Update status label */
static void update_status(const char *message, bool is_error) {
    if (!status_label) return;

    if (is_error) {
        char *markup = g_markup_printf_escaped("<span foreground='red'>%s</span>", message);
        gtk_label_set_markup(GTK_LABEL(status_label), markup);
        g_free(markup);
    } else {
        char *markup = g_markup_printf_escaped("<span foreground='green'>%s</span>", message);
        gtk_label_set_markup(GTK_LABEL(status_label), markup);
        g_free(markup);
    }
}

/* Compile shader using NeoWall's modular shader library */
static void update_shader_program(void) {
    if (!gl_initialized) {
        return;
    }

    gtk_gl_area_make_current(GTK_GL_AREA(gl_area));

    /* Clear previous error */
    if (error_label) {
        gtk_label_set_text(GTK_LABEL(error_label), "");
    }

    /* Get user shader source */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(source_buffer), &start, &end);
    char *user_shader = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(source_buffer),
                                                   &start, &end, FALSE);

    /* Destroy old program if exists */
    if (shader_program) {
        shader_destroy_program(shader_program);
        shader_program = 0;
    }

    /* Debug: Check if shader has mainImage */
    bool has_main_image = strstr(user_shader, "mainImage") != NULL;
    tray_log_info("[ShaderEditor] Shader has mainImage: %s", has_main_image ? "YES" : "NO");
    tray_log_info("[ShaderEditor] Shader length: %zu bytes", strlen(user_shader));

    /* Save to temp file for shader_create_live_program (same as daemon) */
    const char *temp_path = "/tmp/neowall_tray_shader_temp.glsl";
    FILE *f = fopen(temp_path, "w");
    if (!f) {
        tray_log_error("[ShaderEditor] Failed to create temporary shader file");
        g_free(user_shader);

        if (error_label) {
            gtk_label_set_markup(GTK_LABEL(error_label),
                "<span foreground='red'>Error: Failed to create temporary file</span>");
        }

        shader_valid = false;
        if (animation_timer_id) {
            g_source_remove(animation_timer_id);
            animation_timer_id = 0;
        }
        return;
    }

    fprintf(f, "%s", user_shader);
    fclose(f);
    g_free(user_shader);

    /* Use shader_create_live_program exactly like neowalld does */
    GLuint new_program = 0;
    bool success = shader_create_live_program(temp_path, &new_program, 4);

    /* Clean up temp file */
    unlink(temp_path);

    if (!success) {
        tray_log_error("[ShaderEditor] Shader compilation failed - check console for details");

        if (error_label) {
            gtk_label_set_markup(GTK_LABEL(error_label),
                "<span foreground='red'>Compilation Error - check console for details</span>");
        }

        shader_valid = false;

        /* Stop animation timer on error */
        if (animation_timer_id) {
            g_source_remove(animation_timer_id);
            animation_timer_id = 0;
        }
        return;
    }

    shader_program = new_program;
    shader_valid = true;

    /* Update status */
    if (status_label) {
        gtk_label_set_markup(GTK_LABEL(status_label),
            "<span foreground='green'>✓ Shader compiled successfully (using NeoWall shader library)</span>");
    }

    /* Start animation timer at 60 FPS (16.67ms per frame) */
    if (!animation_timer_id) {
        animation_timer_id = g_timeout_add(1000 / 60, animation_timer_cb, gl_area);
    }

    /* Trigger initial redraw */
    gtk_widget_queue_draw(gl_area);
}

/* Debounced shader compilation callback */
static gboolean compile_timeout_cb(gpointer user_data) {
    (void)user_data;
    compile_timeout_id = 0;
    update_shader_program();
    return FALSE;
}

/* Text buffer changed callback - debounce compilation */
static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
    (void)buffer;
    (void)user_data;

    /* Cancel previous timeout */
    if (compile_timeout_id) {
        g_source_remove(compile_timeout_id);
    }

    /* Schedule compilation after 500ms of no typing */
    compile_timeout_id = g_timeout_add(500, compile_timeout_cb, NULL);
}

/* GL Area: Realize callback - initialize OpenGL */
static void on_gl_realize(GtkGLArea *area, gpointer user_data) {
    (void)user_data;

    gtk_gl_area_make_current(area);

    if (gtk_gl_area_get_error(area) != NULL) {
        tray_log_error("[ShaderEditor] Failed to initialize GL area");
        return;
    }

    tray_log_info("[ShaderEditor] OpenGL Version: %s", glGetString(GL_VERSION));
    tray_log_info("[ShaderEditor] GLSL Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    /* Create fullscreen quad */
    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    gl_initialized = true;
    start_time = get_time();

    /* Compile initial shader */
    update_shader_program();
}

/* Animation timer callback - drives preview at 60 FPS */
static gboolean animation_timer_cb(gpointer user_data) {
    GtkWidget *area = GTK_WIDGET(user_data);
    gtk_widget_queue_draw(area);
    return G_SOURCE_CONTINUE; /* Keep running */
}

/* GL Area: Render callback */
static gboolean on_gl_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data) {
    (void)context;
    (void)user_data;

    if (!gl_initialized || !shader_valid) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return TRUE;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    /* Calculate shader time */
    double current_time = get_time();
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    float shader_time = (float)(current_time - start_time);

    /* Set uniforms directly like daemon does */
    glUseProgram(shader_program);

    GLint loc_time = glGetUniformLocation(shader_program, "_neowall_time");
    if (loc_time >= 0) {
        glUniform1f(loc_time, shader_time);
    }

    GLint loc_resolution = glGetUniformLocation(shader_program, "_neowall_resolution");
    if (loc_resolution >= 0) {
        glUniform2f(loc_resolution, (float)width, (float)height);
    }

    GLint loc_iresolution = glGetUniformLocation(shader_program, "iResolution");
    if (loc_iresolution >= 0) {
        float aspect = (width > 0 && height > 0) ? (float)width / (float)height : 1.0f;
        glUniform3f(loc_iresolution, (float)width, (float)height, aspect);
    }

    /* Draw fullscreen quad */
    GLint loc_position = glGetAttribLocation(shader_program, "position");
    if (loc_position >= 0) {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    return TRUE;
}

/* GL Area: Unrealize callback - cleanup */
static void on_gl_unrealize(GtkGLArea *area, gpointer user_data) {
    (void)user_data;

    gtk_gl_area_make_current(area);

    /* Stop animation timer */
    if (animation_timer_id) {
        g_source_remove(animation_timer_id);
        animation_timer_id = 0;
    }

    if (shader_program) {
        glDeleteProgram(shader_program);
        shader_program = 0;
    }

    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }

    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }

    gl_initialized = false;
}

/* Helper: Save theme preference to file */
static void save_theme_preference(const char *theme_id) {
    if (!theme_id) return;

    const char *config_dir = g_get_user_config_dir();
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/neowall", config_dir);

    /* Create config directory if it doesn't exist */
    g_mkdir_with_parents(config_path, 0755);

    snprintf(config_path, sizeof(config_path), "%s/neowall/shader_editor_theme.conf", config_dir);

    FILE *f = fopen(config_path, "w");
    if (f) {
        fprintf(f, "%s\n", theme_id);
        fclose(f);
        tray_log_info("[ShaderEditor] Saved theme preference: %s", theme_id);
    }
}

/* Helper: Load theme preference from file */
static char *load_theme_preference(void) {
    const char *config_dir = g_get_user_config_dir();
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/neowall/shader_editor_theme.conf", config_dir);

    FILE *f = fopen(config_path, "r");
    if (!f) {
        return g_strdup("cobalt"); /* Default: cobalt theme */
    }

    char theme_id[128];
    if (fgets(theme_id, sizeof(theme_id), f)) {
        /* Remove trailing newline */
        size_t len = strlen(theme_id);
        if (len > 0 && theme_id[len - 1] == '\n') {
            theme_id[len - 1] = '\0';
        }
        fclose(f);
        tray_log_info("[ShaderEditor] Loaded theme preference: %s", theme_id);
        return g_strdup(theme_id);
    }

    fclose(f);
    return g_strdup("cobalt"); /* Default: cobalt theme */
}

/* Theme selector callback */
static void on_theme_changed(GtkComboBox *combo, gpointer user_data) {
    (void)user_data;

    if (!source_buffer) {
        tray_log_info("[ShaderEditor] Theme change requested but source_buffer not ready yet");
        return;
    }

    const gchar *scheme_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
    if (!scheme_id) return;

    GtkSourceStyleSchemeManager *scheme_manager = gtk_source_style_scheme_manager_get_default();

    /* Handle "none" as no theme (system default) */
    if (g_strcmp0(scheme_id, "none") == 0) {
        gtk_source_buffer_set_style_scheme(source_buffer, NULL);
        save_theme_preference("none");
        tray_log_info("[ShaderEditor] Changed theme to: System Default (none)");
    } else {
        GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(scheme_manager, scheme_id);
        if (scheme) {
            gtk_source_buffer_set_style_scheme(source_buffer, scheme);
            save_theme_preference(scheme_id);
            tray_log_info("[ShaderEditor] Changed theme to: %s", scheme_id);
        }
    }
}

/* Button callbacks */
static void on_save_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Save Shader",
        GTK_WINDOW(editor_window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL
    );

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (current_file_path) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current_file_path);
    } else {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "my_shader.glsl");
        const char *home = g_getenv("HOME");
        if (home) {
            char shader_dir[512];
            snprintf(shader_dir, sizeof(shader_dir), "%s/.config/neowall/shaders", home);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), shader_dir);
        }
    }

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(source_buffer), &start, &end);
        char *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(source_buffer),
                                               &start, &end, FALSE);

        GError *error = NULL;
        if (!g_file_set_contents(filename, text, -1, &error)) {
            tray_log_error("[ShaderEditor] Failed to save shader: %s", error->message);
            update_status("✗ Failed to save", TRUE);
            g_error_free(error);
        } else {
            tray_log_info("[ShaderEditor] Shader saved to %s", filename);
            g_free(current_file_path);
            current_file_path = g_strdup(filename);

            char status_msg[512];
            snprintf(status_msg, sizeof(status_msg), "✓ Saved to %s", g_path_get_basename(filename));
            update_status(status_msg, FALSE);
        }

        g_free(text);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_load_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Load Shader",
        GTK_WINDOW(editor_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    /* Add file filter for GLSL files */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "GLSL Shaders");
    gtk_file_filter_add_pattern(filter, "*.glsl");
    gtk_file_filter_add_pattern(filter, "*.frag");
    gtk_file_filter_add_pattern(filter, "*.fs");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    const char *home = g_getenv("HOME");
    if (home) {
        char shader_dir[512];
        snprintf(shader_dir, sizeof(shader_dir), "%s/.config/neowall/shaders", home);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), shader_dir);
    }

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        char *contents = NULL;
        GError *error = NULL;
        if (g_file_get_contents(filename, &contents, NULL, &error)) {
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), contents, -1);
            g_free(current_file_path);
            current_file_path = g_strdup(filename);

            char status_msg[512];
            snprintf(status_msg, sizeof(status_msg), "✓ Loaded: %s", g_path_get_basename(filename));
            update_status(status_msg, FALSE);

            tray_log_info("[ShaderEditor] Shader loaded from %s", filename);
            g_free(contents);
        } else {
            tray_log_error("[ShaderEditor] Failed to load shader: %s", error->message);
            update_status("✗ Failed to load", TRUE);
            g_error_free(error);
        }

        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_apply_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    if (!shader_valid) {
        update_status("✗ Cannot apply - shader has errors", TRUE);
        return;
    }

    /* Save to temp file if no current file */
    if (!current_file_path) {
        const char *home = g_getenv("HOME");
        if (home) {
            char temp_path[512];
            snprintf(temp_path, sizeof(temp_path), "%s/.config/neowall/shaders/temp_shader.glsl", home);

            char mkdir_cmd[512];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/.config/neowall/shaders", home);
            if (system(mkdir_cmd) != 0) {
                tray_log_error("[ShaderEditor] Failed to create shader directory");
            }

            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(source_buffer), &start, &end);
            char *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(source_buffer), &start, &end, FALSE);

            GError *error = NULL;
            if (g_file_set_contents(temp_path, text, -1, &error)) {
                current_file_path = g_strdup(temp_path);
            } else {
                update_status("✗ Failed to save temp shader", TRUE);
                g_error_free(error);
                g_free(text);
                return;
            }
            g_free(text);
        }
    }

    /* Apply shader via config command */
    char command[1024];
    snprintf(command, sizeof(command), "set-config general.shader \"%s\"", current_file_path);

    if (command_execute(command)) {
        update_status("✓ Shader applied! Reloading...", FALSE);

        if (command_execute("reload")) {
            update_status("✓ Shader applied and active!", FALSE);
            tray_log_info("[ShaderEditor] Applied shader: %s", current_file_path);
        } else {
            update_status("⚠ Shader set but reload failed", TRUE);
        }
    } else {
        update_status("✗ Failed to apply shader", TRUE);
        tray_log_error("[ShaderEditor] Failed to apply shader");
    }
}

static void on_reset_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), DEFAULT_SHADER, -1);
    g_free(current_file_path);
    current_file_path = NULL;
    update_status("Reset to default shader template", FALSE);
}

/* Window destroy callback */
static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (compile_timeout_id) {
        g_source_remove(compile_timeout_id);
        compile_timeout_id = 0;
    }

    g_free(current_file_path);
    current_file_path = NULL;
    editor_window = NULL;
    source_buffer = NULL;
    gl_area = NULL;
    error_label = NULL;
    status_label = NULL;
}

/* Show the shader editor dialog */
void shader_editor_show(void) {
    /* Don't create multiple instances */
    if (editor_window) {
        gtk_window_present(GTK_WINDOW(editor_window));
        return;
    }

    /* Create main window */
    editor_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(editor_window), "NeoWall Shader Playground");
    gtk_window_set_default_size(GTK_WINDOW(editor_window), 1200, 700);
    gtk_window_set_position(GTK_WINDOW(editor_window), GTK_WIN_POS_CENTER);
    gtk_window_set_type_hint(GTK_WINDOW(editor_window), GDK_WINDOW_TYPE_HINT_DIALOG);

    /* Apply compact styling */
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *css_data =
        "button {"
        "  padding: 4px 12px;"
        "  min-height: 28px;"
        "}"
        "combobox button {"
        "  padding: 2px 8px;"
        "  min-height: 28px;"
        "}"
        "label {"
        "  font-size: 13px;"
        "}"
        "toolbar {"
        "  padding: 4px;"
        "}";

    gtk_css_provider_load_from_data(css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                               GTK_STYLE_PROVIDER(css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    g_signal_connect(editor_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    /* Main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(editor_window), vbox);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 6);
    gtk_widget_set_margin_bottom(toolbar, 6);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    /* Buttons */
    GtkWidget *load_btn = gtk_button_new_with_label("📂 Load");
    GtkWidget *save_btn = gtk_button_new_with_label("💾 Save");
    GtkWidget *apply_btn = gtk_button_new_with_label("✓ Apply");
    GtkWidget *reset_btn = gtk_button_new_with_label("↻ Reset");

    g_signal_connect(load_btn, "clicked", G_CALLBACK(on_load_clicked), NULL);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(toolbar), load_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), save_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), apply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), reset_btn, FALSE, FALSE, 0);

    /* Spacer */
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(toolbar), spacer, TRUE, TRUE, 0);

    /* Theme selector */
    GtkWidget *theme_label = gtk_label_new("Theme:");
    gtk_box_pack_start(GTK_BOX(toolbar), theme_label, FALSE, FALSE, 0);

    theme_combo = gtk_combo_box_text_new();

    /* Add "System Default" option first */
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), "none", "System Default");

    /* Populate with available color schemes */
    GtkSourceStyleSchemeManager *scheme_manager = gtk_source_style_scheme_manager_get_default();
    const gchar * const *scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids(scheme_manager);

    /* Load saved theme preference */
    saved_theme_preference = load_theme_preference();

    for (int i = 0; scheme_ids[i] != NULL; i++) {
        GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(scheme_manager, scheme_ids[i]);
        const gchar *name = gtk_source_style_scheme_get_name(scheme);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), scheme_ids[i], name);
    }

    /* Set saved preference as active */
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo), saved_theme_preference);

    /* Connect signal AFTER buffer is created to avoid NULL buffer */
    gtk_box_pack_start(GTK_BOX(toolbar), theme_combo, FALSE, FALSE, 0);

    /* Info label */
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label),
        "<small><b>100% NeoWall Compatible</b></small>");
    gtk_box_pack_start(GTK_BOX(toolbar), info_label, FALSE, FALSE, 8);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* Horizontal paned for editor and preview */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    /* Left pane: Source editor */
    /* Left pane: Editor */
    GtkWidget *editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(editor_box, 6);
    gtk_widget_set_margin_end(editor_box, 3);
    gtk_widget_set_margin_top(editor_box, 4);
    gtk_widget_set_margin_bottom(editor_box, 4);

    GtkWidget *editor_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(editor_label), "<b>Shader Code (GLSL)</b>");
    gtk_widget_set_halign(editor_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(editor_box), editor_label, FALSE, FALSE, 0);

    /* Source view with syntax highlighting */
    GtkSourceLanguageManager *lang_manager = gtk_source_language_manager_get_default();
    GtkSourceLanguage *glsl_lang = gtk_source_language_manager_get_language(lang_manager, "glsl");

    source_buffer = gtk_source_buffer_new(NULL);
    if (glsl_lang) {
        gtk_source_buffer_set_language(source_buffer, glsl_lang);
        gtk_source_buffer_set_highlight_syntax(source_buffer, TRUE);
    }

    /* Apply the selected theme from combo box */
    if (theme_combo) {
        const gchar *scheme_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(theme_combo));
        if (scheme_id && g_strcmp0(scheme_id, "none") != 0) {
            GtkSourceStyleSchemeManager *scheme_manager = gtk_source_style_scheme_manager_get_default();
            GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(scheme_manager, scheme_id);
            if (scheme) {
                gtk_source_buffer_set_style_scheme(source_buffer, scheme);
                tray_log_info("[ShaderEditor] Applied initial theme: %s", scheme_id);
            }
        } else {
            tray_log_info("[ShaderEditor] Using system default theme");
        }
    }

    /* Now connect the theme combo signal after buffer is ready */
    g_signal_connect(theme_combo, "changed", G_CALLBACK(on_theme_changed), NULL);

    GtkWidget *source_view = gtk_source_view_new_with_buffer(source_buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_indent_width(GTK_SOURCE_VIEW(source_view), 4);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(source_view), 4);
    gtk_source_view_set_insert_spaces_instead_of_tabs(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_show_right_margin(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_right_margin_position(GTK_SOURCE_VIEW(source_view), 100);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source_view), TRUE);

    /* Enable auto-completion */
    GtkSourceCompletion *completion = gtk_source_view_get_completion(GTK_SOURCE_VIEW(source_view));
    GtkSourceCompletionWords *words_provider = gtk_source_completion_words_new("GLSL", NULL);
    gtk_source_completion_words_register(words_provider, GTK_TEXT_BUFFER(source_buffer));
    gtk_source_completion_add_provider(completion,
                                       GTK_SOURCE_COMPLETION_PROVIDER(words_provider),
                                       NULL);

    /* Set default shader */
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), DEFAULT_SHADER, -1);

    /* Connect buffer change signal */
    g_signal_connect(source_buffer, "changed", G_CALLBACK(on_buffer_changed), NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), source_view);
    gtk_box_pack_start(GTK_BOX(editor_box), scroll, TRUE, TRUE, 0);

    /* Error display */
    error_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(error_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(error_label), TRUE);
    gtk_widget_set_halign(error_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(editor_box), error_label, FALSE, FALSE, 0);

    gtk_paned_pack1(GTK_PANED(paned), editor_box, TRUE, FALSE);

    /* Right pane: GL preview */
    GtkWidget *preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(preview_box, 3);
    gtk_widget_set_margin_end(preview_box, 6);
    gtk_widget_set_margin_top(preview_box, 4);
    gtk_widget_set_margin_bottom(preview_box, 4);

    GtkWidget *preview_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(preview_label), "<b>Live Preview</b>");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(preview_box), preview_label, FALSE, FALSE, 0);

    /* GL Area for rendering */
    gl_area = gtk_gl_area_new();
    gtk_widget_set_size_request(gl_area, 400, 400);
    gtk_gl_area_set_required_version(GTK_GL_AREA(gl_area), 3, 0);

    g_signal_connect(gl_area, "realize", G_CALLBACK(on_gl_realize), NULL);
    g_signal_connect(gl_area, "render", G_CALLBACK(on_gl_render), NULL);
    g_signal_connect(gl_area, "unrealize", G_CALLBACK(on_gl_unrealize), NULL);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame), gl_area);
    gtk_box_pack_start(GTK_BOX(preview_box), frame, TRUE, TRUE, 0);

    gtk_paned_pack2(GTK_PANED(paned), preview_box, TRUE, FALSE);

    /* Set paned position (50% editor, 50% preview) */
    gtk_paned_set_position(GTK_PANED(paned), 600);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* Status bar */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(status_box, 8);
    gtk_widget_set_margin_end(status_box, 8);
    gtk_widget_set_margin_top(status_box, 4);
    gtk_widget_set_margin_bottom(status_box, 4);

    status_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(status_label), "<small>Ready - Edit shader and see live preview!</small>");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);

    /* Show window */
    gtk_widget_show_all(editor_window);
}

/* Close the shader editor dialog */
void shader_editor_close(void) {
    if (editor_window) {
        gtk_widget_destroy(editor_window);
    }
}

/* Check if shader editor is open */
bool shader_editor_is_open(void) {
    return editor_window != NULL;
}
