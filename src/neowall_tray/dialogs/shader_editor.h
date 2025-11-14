/* NeoWall Tray - Shader Editor Dialog Header
 * Live shader playground with code editor and real-time preview
 */

#ifndef NEOWALL_TRAY_SHADER_EDITOR_H
#define NEOWALL_TRAY_SHADER_EDITOR_H

#include <gtk/gtk.h>
#include <stdbool.h>

/**
 * Show the shader editor dialog
 * Opens a window with a split view:
 * - Left pane: Code editor with GLSL syntax highlighting
 * - Right pane: Live OpenGL preview rendering the shader
 * 
 * Features:
 * - Loads current shader if active, otherwise a default template
 * - Real-time compilation and preview as you type
 * - Save/Load/Apply/Reset functionality
 * - Error messages for compilation failures
 * - Multiple shader templates to choose from
 */
void shader_editor_show(void);

/**
 * Close the shader editor dialog if open
 */
void shader_editor_close(void);

/**
 * Check if shader editor is currently open
 * @return true if editor window is visible, false otherwise
 */
bool shader_editor_is_open(void);

#endif /* NEOWALL_TRAY_SHADER_EDITOR_H */