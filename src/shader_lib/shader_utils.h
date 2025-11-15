/* Shader Library - Utility Functions
 * Helper functions for common shader operations in the editor
 */

#ifndef SHADER_UTILS_H
#define SHADER_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <GLES2/gl2.h>

/**
 * Shader error information
 */
typedef struct {
    int line_number;          /* Line where error occurred (-1 if unknown) */
    char *message;            /* Error message (must be freed) */
    char *code_snippet;       /* Code snippet around error (must be freed) */
} shader_error_info_t;

/**
 * Shader statistics
 */
typedef struct {
    size_t line_count;        /* Number of lines in shader */
    size_t uniform_count;     /* Number of uniforms used */
    size_t texture_count;     /* Number of texture samples */
    size_t function_count;    /* Number of functions defined */
    bool uses_loops;          /* Contains for/while loops */
    bool uses_conditionals;   /* Contains if/else statements */
    bool is_shadertoy_format; /* Detected Shadertoy mainImage */
    int complexity_score;     /* Estimated complexity (0-100) */
} shader_stats_t;

/**
 * Shader validation result
 */
typedef struct {
    bool is_valid;            /* Overall validity */
    bool has_main;            /* Has main() or mainImage() */
    bool has_version;         /* Has #version directive */
    int detected_version;     /* Detected GLSL version (100, 300, etc.) */
    char **warnings;          /* Array of warning messages */
    size_t warning_count;     /* Number of warnings */
    char **errors;            /* Array of error messages */
    size_t error_count;       /* Number of errors */
} shader_validation_t;

/* ============================================
 * Shader Analysis
 * ============================================ */

/**
 * Extract error information from OpenGL shader log
 * Parses error messages to extract line numbers and context
 * 
 * @param shader_log OpenGL shader info log
 * @param shader_source Original shader source code
 * @return Error info structure (caller must free with shader_free_error_info)
 */
shader_error_info_t *shader_parse_error_log(const char *shader_log, 
                                             const char *shader_source);

/**
 * Free shader error info
 * 
 * @param error_info Error info to free
 */
void shader_free_error_info(shader_error_info_t *error_info);

/**
 * Get shader statistics
 * Analyzes shader source and returns statistics
 * 
 * @param shader_source Shader source code
 * @return Statistics structure (caller must free with shader_free_stats)
 */
shader_stats_t *shader_get_statistics(const char *shader_source);

/**
 * Free shader statistics
 * 
 * @param stats Statistics to free
 */
void shader_free_stats(shader_stats_t *stats);

/**
 * Validate shader syntax without compiling
 * Performs basic syntax validation
 * 
 * @param shader_source Shader source code
 * @param is_fragment true for fragment shader, false for vertex
 * @return Validation result (caller must free with shader_free_validation)
 */
shader_validation_t *shader_validate_syntax(const char *shader_source, 
                                             bool is_fragment);

/**
 * Free shader validation result
 * 
 * @param validation Validation result to free
 */
void shader_free_validation(shader_validation_t *validation);

/* ============================================
 * Shader Formatting
 * ============================================ */

/**
 * Format shader source with indentation
 * 
 * @param shader_source Shader source code
 * @return Formatted shader source (caller must free)
 */
char *shader_format_source(const char *shader_source);

/**
 * Add line numbers to shader source (for display)
 * 
 * @param shader_source Shader source code
 * @param start_line Starting line number (usually 1)
 * @return Source with line numbers (caller must free)
 */
char *shader_add_line_numbers(const char *shader_source, int start_line);

/**
 * Strip comments from shader source
 * 
 * @param shader_source Shader source code
 * @param keep_newlines Preserve line breaks for debugging
 * @return Source without comments (caller must free)
 */
char *shader_strip_comments(const char *shader_source, bool keep_newlines);

/* ============================================
 * Shader Templates
 * ============================================ */

/**
 * Get default shader template by name
 * 
 * Available templates:
 * - "basic"        : Simple gradient
 * - "animated"     : Time-based animation
 * - "plasma"       : Plasma effect
 * - "raymarch"     : Basic raymarching
 * - "noise"        : Noise-based pattern
 * - "shadertoy"    : Shadertoy format template
 * 
 * @param template_name Name of template
 * @return Template source code (static, do not free)
 */
const char *shader_get_template(const char *template_name);

/**
 * List available template names
 * 
 * @param count Output parameter for number of templates
 * @return Array of template names (static, do not free)
 */
const char **shader_list_templates(size_t *count);

/* ============================================
 * Shader Information Extraction
 * ============================================ */

/**
 * Extract uniform declarations from shader
 * 
 * @param shader_source Shader source code
 * @param uniform_names Output array of uniform names (caller must free)
 * @param uniform_types Output array of uniform types (caller must free)
 * @return Number of uniforms found
 */
size_t shader_extract_uniforms(const char *shader_source,
                                char ***uniform_names,
                                char ***uniform_types);

/**
 * Free uniform extraction results
 * 
 * @param uniform_names Array of uniform names
 * @param uniform_types Array of uniform types
 * @param count Number of uniforms
 */
void shader_free_uniforms(char **uniform_names, 
                          char **uniform_types, 
                          size_t count);

/**
 * Extract function signatures from shader
 * 
 * @param shader_source Shader source code
 * @param function_names Output array of function names (caller must free)
 * @param function_signatures Output array of signatures (caller must free)
 * @return Number of functions found
 */
size_t shader_extract_functions(const char *shader_source,
                                 char ***function_names,
                                 char ***function_signatures);

/**
 * Free function extraction results
 * 
 * @param function_names Array of function names
 * @param function_signatures Array of function signatures
 * @param count Number of functions
 */
void shader_free_functions(char **function_names,
                           char **function_signatures,
                           size_t count);

/* ============================================
 * Shader Minification
 * ============================================ */

/**
 * Minify shader source (remove whitespace, comments)
 * Useful for reducing shader size
 * 
 * @param shader_source Shader source code
 * @return Minified source (caller must free)
 */
char *shader_minify(const char *shader_source);

/**
 * Calculate estimated shader size
 * 
 * @param shader_source Shader source code
 * @return Size in bytes after minification
 */
size_t shader_estimate_size(const char *shader_source);

/* ============================================
 * Shader Conversion Helpers
 * ============================================ */

/**
 * Convert raw fragment shader to Shadertoy format
 * 
 * @param fragment_source Fragment shader source
 * @return Shadertoy-format shader (caller must free)
 */
char *shader_to_shadertoy_format(const char *fragment_source);

/**
 * Convert Shadertoy format to raw fragment shader
 * 
 * @param shadertoy_source Shadertoy shader source
 * @return Raw fragment shader (caller must free)
 */
char *shader_from_shadertoy_format(const char *shadertoy_source);

/* ============================================
 * Shader Performance Analysis
 * ============================================ */

/**
 * Estimate shader performance
 * Returns a score from 0 (excellent) to 100 (very poor)
 * 
 * @param shader_source Shader source code
 * @return Performance score
 */
int shader_estimate_performance(const char *shader_source);

/**
 * Get performance recommendations
 * Analyzes shader and suggests optimizations
 * 
 * @param shader_source Shader source code
 * @param recommendations Output array of recommendation strings (caller must free)
 * @return Number of recommendations
 */
size_t shader_get_performance_tips(const char *shader_source,
                                    char ***recommendations);

/**
 * Free performance recommendations
 * 
 * @param recommendations Array of recommendation strings
 * @param count Number of recommendations
 */
void shader_free_recommendations(char **recommendations, size_t count);

/* ============================================
 * Code Generation Helpers
 * ============================================ */

/**
 * Generate vertex shader for fullscreen quad
 * 
 * @param use_es3 Use OpenGL ES 3.0 syntax
 * @return Vertex shader source (static, do not free)
 */
const char *shader_generate_fullscreen_vertex(bool use_es3);

/**
 * Generate fragment shader boilerplate
 * Creates basic fragment shader structure
 * 
 * @param use_es3 Use OpenGL ES 3.0 syntax
 * @param include_time Include time uniform
 * @param include_resolution Include resolution uniform
 * @return Fragment shader boilerplate (caller must free)
 */
char *shader_generate_fragment_boilerplate(bool use_es3,
                                            bool include_time,
                                            bool include_resolution);

/* ============================================
 * Shader Diffing
 * ============================================ */

/**
 * Compare two shader sources
 * Useful for showing changes before/after
 * 
 * @param old_source Original shader source
 * @param new_source Modified shader source
 * @param diff_output Output diff string (caller must free)
 * @return Number of differences found
 */
size_t shader_diff(const char *old_source,
                   const char *new_source,
                   char **diff_output);

/* ============================================
 * Miscellaneous Utilities
 * ============================================ */

/**
 * Check if string contains valid GLSL code
 * Quick sanity check before compilation
 * 
 * @param shader_source Shader source code
 * @return true if appears valid, false otherwise
 */
bool shader_is_likely_valid(const char *shader_source);

/**
 * Get GLSL version string for target
 * 
 * @param es_version ES version (100, 300, 310, 320)
 * @return Version directive string (e.g., "#version 300 es")
 */
const char *shader_get_version_string(int es_version);

/**
 * Detect GLSL version from source
 * 
 * @param shader_source Shader source code
 * @return Detected version (100, 300, etc.) or 0 if none
 */
int shader_detect_version(const char *shader_source);

/**
 * Get human-readable description of shader
 * Generates a short description based on shader content
 * 
 * @param shader_source Shader source code
 * @return Description string (caller must free)
 */
char *shader_generate_description(const char *shader_source);

#endif /* SHADER_UTILS_H */