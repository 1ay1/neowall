#ifndef VIBE_PATH_H
#define VIBE_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include "vibe.h"

/*
 * ============================================================================
 * VIBE Path Operations - Navigate and Modify VIBE Trees Using Dot Notation
 * ============================================================================
 * 
 * This module provides utilities for accessing and modifying VIBE objects
 * using hierarchical path notation (e.g., "default.shader", "output.DP-1.mode")
 * 
 * Key Features:
 * - Get/set values using dot-separated paths
 * - Create nested objects automatically
 * - Delete keys at any level
 * - Type-safe access with validation
 * - Atomic file writes for persistence
 * 
 * Example Usage:
 * 
 *   VibeValue *root = vibe_parse_file(parser, "config.vibe");
 *   
 *   // Get value
 *   const char *shader = vibe_path_get_string(root, "default.shader");
 *   
 *   // Set value (creates nested objects if needed)
 *   vibe_path_set_string(root, "output.DP-1.mode", "fill");
 *   
 *   // Delete key
 *   vibe_path_delete(root, "default.shader_speed");
 *   
 *   // Save back to file (atomic)
 *   vibe_path_write_file(root, "config.vibe");
 * 
 * ============================================================================
 */

/* ============================================================================
 * Path Get Operations
 * ============================================================================ */

/**
 * Get a value at the given path
 * 
 * @param root Root VIBE value (must be object)
 * @param path Dot-separated path (e.g., "default.shader", "output.DP-1.mode")
 * @return Value at path, or NULL if not found
 * 
 * Example:
 *   VibeValue *val = vibe_path_get(root, "default.shader");
 *   if (val && val->type == VIBE_TYPE_STRING) {
 *       printf("Shader: %s\n", val->as_string);
 *   }
 */
VibeValue *vibe_path_get(VibeValue *root, const char *path);

/**
 * Get a string value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @return String value, or NULL if not found or wrong type
 */
const char *vibe_path_get_string(VibeValue *root, const char *path);

/**
 * Get an integer value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param out Output integer (only set if successful)
 * @return true if found and is integer, false otherwise
 */
bool vibe_path_get_int(VibeValue *root, const char *path, int64_t *out);

/**
 * Get a float value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param out Output float (only set if successful)
 * @return true if found and is float, false otherwise
 */
bool vibe_path_get_float(VibeValue *root, const char *path, double *out);

/**
 * Get a boolean value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param out Output boolean (only set if successful)
 * @return true if found and is boolean, false otherwise
 */
bool vibe_path_get_bool(VibeValue *root, const char *path, bool *out);

/**
 * Check if a path exists
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @return true if path exists, false otherwise
 */
bool vibe_path_exists(VibeValue *root, const char *path);

/* ============================================================================
 * Path Set Operations
 * ============================================================================ */

/**
 * Set a value at the given path (creates nested objects if needed)
 * 
 * @param root Root VIBE value (must be object)
 * @param path Dot-separated path
 * @param value Value to set (takes ownership)
 * @return true on success, false on error
 * 
 * Example:
 *   VibeValue *shader = vibe_value_new_string("matrix.glsl");
 *   vibe_path_set(root, "default.shader", shader);
 * 
 * If "default" object doesn't exist, it will be created automatically.
 */
bool vibe_path_set(VibeValue *root, const char *path, VibeValue *value);

/**
 * Set a string value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param value String value to set
 * @return true on success, false on error
 * 
 * Example:
 *   vibe_path_set_string(root, "default.shader", "plasma.glsl");
 *   vibe_path_set_string(root, "output.DP-1.mode", "fill");
 */
bool vibe_path_set_string(VibeValue *root, const char *path, const char *value);

/**
 * Set an integer value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param value Integer value to set
 * @return true on success, false on error
 */
bool vibe_path_set_int(VibeValue *root, const char *path, int64_t value);

/**
 * Set a float value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param value Float value to set
 * @return true on success, false on error
 */
bool vibe_path_set_float(VibeValue *root, const char *path, double value);

/**
 * Set a boolean value at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @param value Boolean value to set
 * @return true on success, false on error
 */
bool vibe_path_set_bool(VibeValue *root, const char *path, bool value);

/* ============================================================================
 * Path Delete Operations
 * ============================================================================ */

/**
 * Delete a key at the given path
 * 
 * @param root Root VIBE value
 * @param path Dot-separated path
 * @return true if deleted, false if path doesn't exist
 * 
 * Examples:
 *   vibe_path_delete(root, "default.shader_speed");  // Delete single key
 *   vibe_path_delete(root, "output.DP-1");           // Delete entire output section
 */
bool vibe_path_delete(VibeValue *root, const char *path);

/* ============================================================================
 * File I/O Operations
 * ============================================================================ */

/**
 * Write VIBE tree to file atomically
 * 
 * Uses temp file + rename for crash safety:
 * 1. Write to config.vibe.tmp
 * 2. fsync()
 * 3. rename(config.vibe.tmp, config.vibe)
 * 
 * @param root Root VIBE value
 * @param path File path
 * @return true on success, false on error
 * 
 * Example:
 *   vibe_path_set_string(root, "default.shader", "plasma.glsl");
 *   vibe_path_write_file(root, "~/.config/neowall/config.vibe");
 */
bool vibe_path_write_file(VibeValue *root, const char *path);

/**
 * Write VIBE tree to file atomically with custom formatting
 * 
 * @param root Root VIBE value
 * @param path File path
 * @param indent_size Number of spaces per indent level
 * @return true on success, false on error
 */
bool vibe_path_write_file_formatted(VibeValue *root, const char *path, int indent_size);

/* ============================================================================
 * Utility Operations
 * ============================================================================ */

/**
 * List all keys at a given path level
 * 
 * @param root Root VIBE value
 * @param path Path to section (e.g., "default", "output", "output.DP-1")
 * @param keys Output array of key names (caller must free)
 * @param max_keys Maximum number of keys to return
 * @return Number of keys found
 * 
 * Example:
 *   const char *keys[100];
 *   size_t count = vibe_path_list_keys(root, "default", keys, 100);
 *   for (size_t i = 0; i < count; i++) {
 *       printf("default.%s\n", keys[i]);
 *   }
 */
size_t vibe_path_list_keys(VibeValue *root, const char *path, 
                           const char **keys, size_t max_keys);

/**
 * List all leaf keys recursively (full paths)
 * 
 * @param root Root VIBE value
 * @param keys Output array of full key paths (caller must free)
 * @param max_keys Maximum number of keys to return
 * @return Number of keys found
 * 
 * Example:
 *   const char *keys[1000];
 *   size_t count = vibe_path_list_all_keys(root, keys, 1000);
 *   for (size_t i = 0; i < count; i++) {
 *       printf("%s\n", keys[i]);
 *   }
 *   // Output:
 *   //   default.shader
 *   //   default.shader_speed
 *   //   output.DP-1.mode
 *   //   output.DP-1.duration
 */
size_t vibe_path_list_all_keys(VibeValue *root, const char **keys, size_t max_keys);

/**
 * Validate a path string
 * 
 * Checks if path is well-formed:
 * - Not empty
 * - No leading/trailing dots
 * - No double dots (..)
 * - Valid characters only
 * 
 * @param path Path to validate
 * @return true if valid, false otherwise
 */
bool vibe_path_validate(const char *path);

/**
 * Get the parent path and key name from a full path
 * 
 * @param path Full path (e.g., "output.DP-1.mode")
 * @param parent_buf Buffer to store parent path (e.g., "output.DP-1")
 * @param parent_size Size of parent_buf
 * @param key_buf Buffer to store key name (e.g., "mode")
 * @param key_size Size of key_buf
 * @return true on success, false if path is invalid
 */
bool vibe_path_split(const char *path, 
                     char *parent_buf, size_t parent_size,
                     char *key_buf, size_t key_size);

#endif /* VIBE_PATH_H */