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
static GtkWidget *fps_label = NULL;
static GtkWidget *source_view = NULL;
static GtkWidget *compile_btn = NULL;
static GtkWidget *cursor_label = NULL;
static char *current_file_path = NULL;
static char *saved_theme_preference = NULL;

/* Editor settings */
static int editor_font_size = 11;
static int editor_tab_width = 4;
static bool editor_line_wrap = false;
static bool editor_auto_compile = true;
static int preview_fps = 60;
static float preview_zoom = 1.0f;
static char editor_font_family[64] = "Monospace";

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

/* FPS tracking */
static double last_fps_update_time = 0.0;
static int frame_count = 0;
static double current_fps = 0.0;
static double last_compile_time = 0.0;

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

/* Example shader templates */
static const char *EXAMPLE_PLASMA =
"// Plasma effect\n"
"void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    float t = iTime * 0.5;\n"
"    \n"
"    float c = sin(uv.x * 10.0 + t);\n"
"    c += sin(uv.y * 10.0 + t);\n"
"    c += sin((uv.x + uv.y) * 10.0 + t);\n"
"    c += sin(length(uv - 0.5) * 20.0 + t);\n"
"    \n"
"    vec3 col = vec3(0.5, 0.3, 0.8) + 0.5 * cos(c + vec3(0.0, 1.0, 2.0));\n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

static const char *EXAMPLE_TUNNEL =
"// Tunnel effect\n"
"void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
"    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
"    float a = atan(uv.y, uv.x);\n"
"    float r = length(uv);\n"
"    \n"
"    vec2 tuv = vec2(a / 6.28318, 1.0 / r + iTime * 0.3);\n"
"    \n"
"    vec3 col = vec3(0.5) + 0.5 * cos(tuv.xyx * 20.0 + vec3(0, 2, 4));\n"
"    col *= 0.5 + 0.5 * sin(r * 10.0 - iTime * 2.0);\n"
"    \n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

static const char *EXAMPLE_WAVES =
"// Animated waves\n"
"void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    float t = iTime * 0.5;\n"
"    \n"
"    float y = uv.y;\n"
"    y += sin(uv.x * 10.0 + t) * 0.1;\n"
"    y += sin(uv.x * 20.0 - t * 2.0) * 0.05;\n"
"    y += sin(uv.x * 5.0 + t * 0.5) * 0.15;\n"
"    \n"
"    float d = abs(y - 0.5);\n"
"    vec3 col = mix(vec3(0.1, 0.3, 0.8), vec3(0.9, 0.5, 0.2), smoothstep(0.0, 0.1, d));\n"
"    \n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

static const char *EXAMPLE_MANDELBROT =
"// Mandelbrot set zoom\n"
"void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
"    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
"    float zoom = 0.5 + 0.5 * sin(iTime * 0.3);\n"
"    vec2 c = uv * zoom * 3.0 + vec2(-0.5, 0.0);\n"
"    \n"
"    vec2 z = vec2(0.0);\n"
"    float iter = 0.0;\n"
"    const float maxIter = 100.0;\n"
"    \n"
"    for (float i = 0.0; i < maxIter; i++) {\n"
"        if (length(z) > 2.0) break;\n"
"        z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;\n"
"        iter++;\n"
"    }\n"
"    \n"
"    float f = iter / maxIter;\n"
"    vec3 col = 0.5 + 0.5 * cos(3.0 + f * 10.0 + vec3(0.0, 1.0, 2.0));\n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

/* Forward declarations */
static gboolean animation_timer_cb(gpointer user_data);
static void on_theme_changed(GtkComboBox *combo, gpointer user_data);
static void update_shader_program(void);
static void on_save_clicked(GtkButton *button, gpointer user_data);
static void on_load_clicked(GtkButton *button, gpointer user_data);
static void on_apply_clicked(GtkButton *button, gpointer user_data);
static void on_reset_clicked(GtkButton *button, gpointer user_data);
static void on_example_selected(GtkMenuItem *item, gpointer user_data);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer user_data);
static void on_zoom_changed(GtkRange *range, gpointer user_data);

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

/* Keyboard shortcuts handler */
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    guint modifiers = gtk_accelerator_get_default_mod_mask();

    /* Ctrl+S - Save */
    if ((event->state & modifiers) == GDK_CONTROL_MASK && event->keyval == GDK_KEY_s) {
        on_save_clicked(NULL, NULL);
        return TRUE;
    }

    /* Ctrl+O - Load */
    if ((event->state & modifiers) == GDK_CONTROL_MASK && event->keyval == GDK_KEY_o) {
        on_load_clicked(NULL, NULL);
        return TRUE;
    }

    /* Ctrl+R - Reset */
    if ((event->state & modifiers) == GDK_CONTROL_MASK && event->keyval == GDK_KEY_r) {
        on_reset_clicked(NULL, NULL);
        return TRUE;
    }

    /* Ctrl+Return - Apply */
    if ((event->state & modifiers) == GDK_CONTROL_MASK &&
        (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
        on_apply_clicked(NULL, NULL);
        return TRUE;
    }

    return FALSE;
}

/* Update FPS display */
static void update_fps_display(void) {
    if (!fps_label) return;

    double now = get_time();
    frame_count++;

    /* Update FPS every 0.5 seconds */
    if (now - last_fps_update_time >= 0.5) {
        current_fps = frame_count / (now - last_fps_update_time);
        frame_count = 0;
        last_fps_update_time = now;

        char fps_text[128];
        if (last_compile_time > 0) {
            snprintf(fps_text, sizeof(fps_text),
                    "<small><span color='#4CAF50'><b>%.1f FPS</b></span> • <span color='#888'>Compile: %.0fms</span></small>",
                    current_fps, last_compile_time);
        } else {
            snprintf(fps_text, sizeof(fps_text),
                    "<small><span color='#4CAF50'><b>%.1f FPS</b></span></small>",
                    current_fps);
        }
        gtk_label_set_markup(GTK_LABEL(fps_label), fps_text);
    }
}

/* Update cursor position display */
static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;

    if (!cursor_label) return;

    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);

    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;

    char cursor_text[64];
    snprintf(cursor_text, sizeof(cursor_text),
             "<small>Ln %d, Col %d</small>", line, col);
    gtk_label_set_markup(GTK_LABEL(cursor_label), cursor_text);
}

/* Zoom control for preview */
static void on_zoom_changed(GtkRange *range, gpointer user_data) {
    (void)user_data;
    preview_zoom = gtk_range_get_value(range);

    if (gl_area) {
        gtk_widget_queue_draw(gl_area);
    }
}

/* Compile shader using NeoWall's modular shader library */
static void update_shader_program(void) {
    if (!gl_initialized) {
        return;
    }

    gtk_gl_area_make_current(GTK_GL_AREA(gl_area));

    /* Track compilation time */
    double compile_start = get_time();

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

    /* Calculate and store compilation time */
    last_compile_time = (get_time() - compile_start) * 1000.0; // Convert to ms

    /* Update status */
    if (status_label) {
        char status_text[256];
        snprintf(status_text, sizeof(status_text),
                 "✓ Shader compiled successfully in %.0fms", last_compile_time);
        char *markup = g_markup_printf_escaped("<span foreground='green'>%s</span>", status_text);
        gtk_label_set_markup(GTK_LABEL(status_label), markup);
        g_free(markup);
    }

    /* Start animation timer at configured FPS */
    if (!animation_timer_id) {
        int interval = 1000 / preview_fps;
        animation_timer_id = g_timeout_add(interval, animation_timer_cb, gl_area);
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

    /* Only auto-compile if enabled */
    if (!editor_auto_compile) {
        return;
    }

    /* Debounce shader compilation - wait 500ms after last edit */
    if (compile_timeout_id) {
        g_source_remove(compile_timeout_id);
    }

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
    last_fps_update_time = start_time;
    frame_count = 0;
    current_fps = 0.0;

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

    /* Update FPS counter */
    update_fps_display();

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

    /* Apply zoom to viewport */
    int zoomed_width = (int)(width * preview_zoom);
    int zoomed_height = (int)(height * preview_zoom);
    int offset_x = (width - zoomed_width) / 2;
    int offset_y = (height - zoomed_height) / 2;
    glViewport(offset_x, offset_y, zoomed_width, zoomed_height);

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
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    GLint loc_position = glGetAttribLocation(shader_program, "position");
    if (loc_position >= 0) {
        glVertexAttribPointer(loc_position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(loc_position);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (loc_position >= 0) {
        glDisableVertexAttribArray(loc_position);
    }
    glBindVertexArray(0);

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

/* Example shader selection handler */
static void on_example_selected(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    const char *example_code = (const char *)user_data;

    if (!source_buffer || !example_code) {
        return;
    }

    /* Set the example shader code */
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), example_code, -1);

    /* Update status */
    update_status("✓ Example shader loaded", FALSE);

    tray_log_info("[ShaderEditor] Loaded example shader");
}

/* Settings dialog with live auto-apply */
static void apply_font_settings(void) {
    if (!source_view) return;

    char css_font[256];
    snprintf(css_font, sizeof(css_font), "textview { font-family: %s; font-size: %dpt; }",
             editor_font_family, editor_font_size);
    GtkCssProvider *font_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(font_provider, css_font, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(source_view),
                                  GTK_STYLE_PROVIDER(font_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(font_provider);
}

static void on_font_family_changed(GtkComboBox *combo, gpointer user_data) {
    (void)user_data;
    gchar *new_font = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (new_font) {
        strncpy(editor_font_family, new_font, sizeof(editor_font_family) - 1);
        editor_font_family[sizeof(editor_font_family) - 1] = '\0';
        g_free(new_font);
        apply_font_settings();
    }
}

static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)user_data;
    editor_font_size = gtk_spin_button_get_value_as_int(spin);
    apply_font_settings();
}

static void on_tab_width_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)user_data;
    if (!source_view) return;
    editor_tab_width = gtk_spin_button_get_value_as_int(spin);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(source_view), editor_tab_width);
    gtk_source_view_set_indent_width(GTK_SOURCE_VIEW(source_view), editor_tab_width);
}

static void on_wrap_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    if (!source_view) return;
    editor_line_wrap = gtk_switch_get_active(sw);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(source_view),
        editor_line_wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}

static void on_auto_compile_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    editor_auto_compile = gtk_switch_get_active(sw);
    if (compile_btn) {
        if (editor_auto_compile) {
            gtk_widget_hide(compile_btn);
        } else {
            gtk_widget_show(compile_btn);
        }
    }
}

static void on_line_numbers_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    if (!source_view) return;
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(source_view), gtk_switch_get_active(sw));
}

static void on_fps_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)user_data;
    preview_fps = gtk_spin_button_get_value_as_int(spin);

    /* Always restart timer if it exists */
    if (animation_timer_id) {
        g_source_remove(animation_timer_id);
        animation_timer_id = 0;
    }

    /* Restart with new FPS if GL area is ready */
    if (gl_area && gl_initialized) {
        int interval = 1000 / preview_fps;
        animation_timer_id = g_timeout_add(interval, animation_timer_cb, gl_area);
        tray_log_info("[ShaderEditor] Preview FPS updated to %d", preview_fps);
    }
}

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Editor Settings",
        GTK_WINDOW(editor_window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_add(GTK_CONTAINER(content), grid);

    int row = 0;

    /* Font family */
    GtkWidget *font_family_label = gtk_label_new("Font Family:");
    gtk_widget_set_halign(font_family_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), font_family_label, 0, row, 1, 1);

    GtkWidget *font_family_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo), "Monospace");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo), "Courier New");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo), "Ubuntu Mono");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo), "Fira Code");
    gtk_combo_box_set_active(GTK_COMBO_BOX(font_family_combo), 0);
    g_signal_connect(font_family_combo, "changed", G_CALLBACK(on_font_family_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), font_family_combo, 1, row, 1, 1);
    row++;

    /* Font size */
    GtkWidget *font_label = gtk_label_new("Font Size:");
    gtk_widget_set_halign(font_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), font_label, 0, row, 1, 1);

    GtkWidget *font_spin = gtk_spin_button_new_with_range(8, 20, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(font_spin), editor_font_size);
    g_signal_connect(font_spin, "value-changed", G_CALLBACK(on_font_size_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), font_spin, 1, row, 1, 1);
    row++;

    /* Tab width */
    GtkWidget *tab_label = gtk_label_new("Tab Width:");
    gtk_widget_set_halign(tab_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), tab_label, 0, row, 1, 1);

    GtkWidget *tab_spin = gtk_spin_button_new_with_range(2, 8, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tab_spin), editor_tab_width);
    g_signal_connect(tab_spin, "value-changed", G_CALLBACK(on_tab_width_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), tab_spin, 1, row, 1, 1);
    row++;

    /* Word wrap */
    GtkWidget *wrap_label = gtk_label_new("Word Wrap:");
    gtk_widget_set_halign(wrap_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), wrap_label, 0, row, 1, 1);

    GtkWidget *wrap_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(wrap_switch), editor_line_wrap);
    g_signal_connect(wrap_switch, "notify::active", G_CALLBACK(on_wrap_toggled), NULL);
    gtk_grid_attach(GTK_GRID(grid), wrap_switch, 1, row, 1, 1);
    row++;

    /* Auto compile */
    GtkWidget *auto_label = gtk_label_new("Auto Compile:");
    gtk_widget_set_halign(auto_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), auto_label, 0, row, 1, 1);

    GtkWidget *auto_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(auto_switch), editor_auto_compile);
    g_signal_connect(auto_switch, "notify::active", G_CALLBACK(on_auto_compile_toggled), NULL);
    gtk_grid_attach(GTK_GRID(grid), auto_switch, 1, row, 1, 1);
    row++;

    /* Line numbers */
    GtkWidget *numbers_label = gtk_label_new("Show Line Numbers:");
    gtk_widget_set_halign(numbers_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), numbers_label, 0, row, 1, 1);

    GtkWidget *numbers_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(numbers_switch),
        gtk_source_view_get_show_line_numbers(GTK_SOURCE_VIEW(source_view)));
    g_signal_connect(numbers_switch, "notify::active", G_CALLBACK(on_line_numbers_toggled), NULL);
    gtk_grid_attach(GTK_GRID(grid), numbers_switch, 1, row, 1, 1);
    row++;

    /* Preview FPS */
    GtkWidget *fps_label = gtk_label_new("Preview FPS:");
    gtk_widget_set_halign(fps_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), fps_label, 0, row, 1, 1);

    GtkWidget *fps_spin = gtk_spin_button_new_with_range(15, 120, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(fps_spin), preview_fps);
    g_signal_connect(fps_spin, "value-changed", G_CALLBACK(on_fps_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), fps_spin, 1, row, 1, 1);
    row++;

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
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
    gtk_window_set_default_size(GTK_WINDOW(editor_window), 1400, 800);
    gtk_window_set_position(GTK_WINDOW(editor_window), GTK_WIN_POS_CENTER);
    gtk_window_set_type_hint(GTK_WINDOW(editor_window), GDK_WINDOW_TYPE_HINT_DIALOG);

    /* Connect keyboard shortcuts */
    g_signal_connect(editor_window, "key-press-event", G_CALLBACK(on_key_press), NULL);

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
        "  background: linear-gradient(to bottom, #f5f5f5, #e8e8e8);"
        "  border-bottom: 1px solid #ccc;"
        "}"
        "statusbar {"
        "  background: #2c2c2c;"
        "  color: #e8e8e8;"
        "  padding: 6px 8px;"
        "}"
        ".fps-label {"
        "  color: #4CAF50;"
        "  font-weight: bold;"
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

    /* Buttons with tooltips */
    GtkWidget *load_btn = gtk_button_new_with_label("📂 Load");
    gtk_widget_set_tooltip_text(load_btn, "Load shader from file (Ctrl+O)");

    GtkWidget *save_btn = gtk_button_new_with_label("💾 Save");
    gtk_widget_set_tooltip_text(save_btn, "Save shader to file (Ctrl+S)");

    GtkWidget *apply_btn = gtk_button_new_with_label("⚡ Apply");
    gtk_widget_set_tooltip_text(apply_btn, "Apply shader to wallpaper (Ctrl+Enter)");

    GtkWidget *reset_btn = gtk_button_new_with_label("↻ Reset");
    gtk_widget_set_tooltip_text(reset_btn, "Reset to default shader (Ctrl+R)");

    GtkWidget *settings_btn = gtk_button_new_with_label("⚙ Settings");
    gtk_widget_set_tooltip_text(settings_btn, "Configure editor settings");

    /* Manual compile button (hidden by default when auto-compile is on) */
    compile_btn = gtk_button_new_with_label("▶ Compile");
    gtk_widget_set_tooltip_text(compile_btn, "Manually compile shader");
    g_signal_connect_swapped(compile_btn, "clicked", G_CALLBACK(update_shader_program), NULL);
    if (editor_auto_compile) {
        gtk_widget_set_no_show_all(compile_btn, TRUE);
    }

    /* Examples menu button */
    GtkWidget *examples_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(examples_btn), "📚 Examples");
    gtk_widget_set_tooltip_text(examples_btn, "Load example shaders");

    GtkWidget *examples_menu = gtk_menu_new();

    GtkWidget *example1 = gtk_menu_item_new_with_label("Animated Gradient (Default)");
    g_signal_connect(example1, "activate", G_CALLBACK(on_example_selected), (gpointer)DEFAULT_SHADER);
    gtk_menu_shell_append(GTK_MENU_SHELL(examples_menu), example1);

    GtkWidget *example2 = gtk_menu_item_new_with_label("Plasma Effect");
    g_signal_connect(example2, "activate", G_CALLBACK(on_example_selected), (gpointer)EXAMPLE_PLASMA);
    gtk_menu_shell_append(GTK_MENU_SHELL(examples_menu), example2);

    GtkWidget *example3 = gtk_menu_item_new_with_label("Tunnel");
    g_signal_connect(example3, "activate", G_CALLBACK(on_example_selected), (gpointer)EXAMPLE_TUNNEL);
    gtk_menu_shell_append(GTK_MENU_SHELL(examples_menu), example3);

    GtkWidget *example4 = gtk_menu_item_new_with_label("Waves");
    g_signal_connect(example4, "activate", G_CALLBACK(on_example_selected), (gpointer)EXAMPLE_WAVES);
    gtk_menu_shell_append(GTK_MENU_SHELL(examples_menu), example4);

    GtkWidget *example5 = gtk_menu_item_new_with_label("Mandelbrot Set");
    g_signal_connect(example5, "activate", G_CALLBACK(on_example_selected), (gpointer)EXAMPLE_MANDELBROT);
    gtk_menu_shell_append(GTK_MENU_SHELL(examples_menu), example5);

    gtk_widget_show_all(examples_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(examples_btn), examples_menu);

    g_signal_connect(load_btn, "clicked", G_CALLBACK(on_load_clicked), NULL);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(toolbar), compile_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), examples_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), load_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), save_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), apply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), reset_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), settings_btn, FALSE, FALSE, 0);

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

    /* FPS Counter */
    fps_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fps_label), "<small>--.- FPS</small>");
    gtk_widget_set_tooltip_text(fps_label, "Frames per second and compilation time");
    GtkStyleContext *fps_context = gtk_widget_get_style_context(fps_label);
    gtk_style_context_add_class(fps_context, "fps-label");
    gtk_box_pack_start(GTK_BOX(toolbar), fps_label, FALSE, FALSE, 8);

    /* Info label */
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label),
        "<small><b>⚡ Live Preview</b></small>");
    gtk_widget_set_tooltip_text(info_label, "Changes compile automatically");
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

    GtkWidget *editor_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget *editor_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(editor_label), "<b>📝 Shader Code (GLSL)</b>");
    gtk_widget_set_halign(editor_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(editor_header), editor_label, FALSE, FALSE, 0);

    GtkWidget *editor_spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(editor_header), editor_spacer, TRUE, TRUE, 0);

    GtkWidget *shortcuts_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(shortcuts_label),
        "<small><i>Ctrl+S: Save | Ctrl+Enter: Apply</i></small>");
    gtk_widget_set_halign(shortcuts_label, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(editor_header), shortcuts_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(editor_box), editor_header, FALSE, FALSE, 0);

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

    source_view = gtk_source_view_new_with_buffer(source_buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_indent_width(GTK_SOURCE_VIEW(source_view), editor_tab_width);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(source_view), editor_tab_width);
    gtk_source_view_set_insert_spaces_instead_of_tabs(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(source_view),
        editor_line_wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_show_right_margin(GTK_SOURCE_VIEW(source_view), TRUE);
    gtk_source_view_set_right_margin_position(GTK_SOURCE_VIEW(source_view), 100);
    gtk_source_view_set_background_pattern(GTK_SOURCE_VIEW(source_view), GTK_SOURCE_BACKGROUND_PATTERN_TYPE_GRID);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source_view), TRUE);

    /* Enable bracket matching */
    gtk_source_buffer_set_highlight_matching_brackets(source_buffer, TRUE);

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

    /* Connect cursor position signal */
    g_signal_connect(source_buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), NULL);

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

    GtkWidget *preview_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget *preview_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(preview_label), "<b>👁 Live Preview</b>");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(preview_header), preview_label, FALSE, FALSE, 0);

    GtkWidget *preview_spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(preview_header), preview_spacer, TRUE, TRUE, 0);

    /* Zoom control */
    GtkWidget *zoom_label = gtk_label_new("Zoom:");
    gtk_box_pack_start(GTK_BOX(preview_header), zoom_label, FALSE, FALSE, 0);

    GtkWidget *zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.5, 3.0, 0.1);
    gtk_scale_set_value_pos(GTK_SCALE(zoom_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(zoom_scale), preview_zoom);
    gtk_widget_set_size_request(zoom_scale, 100, -1);
    gtk_widget_set_tooltip_text(zoom_scale, "Zoom preview (0.5x - 3.0x)");
    g_signal_connect(zoom_scale, "value-changed", G_CALLBACK(on_zoom_changed), NULL);
    gtk_box_pack_start(GTK_BOX(preview_header), zoom_scale, FALSE, FALSE, 6);

    GtkWidget *res_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(res_label), "<small><i>Auto-resize</i></small>");
    gtk_widget_set_halign(res_label, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(res_label, "Preview updates in real-time as you type");
    gtk_box_pack_start(GTK_BOX(preview_header), res_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(preview_box), preview_header, FALSE, FALSE, 0);

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
    gtk_label_set_markup(GTK_LABEL(status_label), "<small>✓ Ready - Edit shader and see live preview!</small>");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE, TRUE, 0);

    /* Cursor position indicator */
    cursor_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(cursor_label), "<small>Ln 1, Col 1</small>");
    gtk_widget_set_tooltip_text(cursor_label, "Cursor position");
    gtk_box_pack_start(GTK_BOX(status_box), cursor_label, FALSE, FALSE, 8);

    /* Add a help icon */
    GtkWidget *help_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(help_label),
        "<small>💡 <i>Tip: Use Shadertoy-compatible GLSL code</i></small>");
    gtk_widget_set_halign(help_label, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(status_box), help_label, FALSE, FALSE, 0);

    /* Style the status bar */
    GtkStyleContext *status_context = gtk_widget_get_style_context(status_box);
    gtk_style_context_add_class(status_context, "statusbar");

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
