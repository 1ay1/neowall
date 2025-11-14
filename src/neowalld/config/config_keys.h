#ifndef CONFIG_KEYS_H
#define CONFIG_KEYS_H

/*
 * ============================================================================
 * NEOWALL CONFIGURATION KEYS - SINGLE SOURCE OF TRUTH
 * ============================================================================
 * 
 * All configuration keys are defined here using simple macros.
 * This ensures consistency across parsing, validation, and documentation.
 * 
 * To add a new config key:
 * 1. Add a #define CONFIG_KEY_* entry below
 * 2. Add corresponding entry to the CONFIG_KEYS_LIST macro
 * 3. That's it! Everything else is automatic.
 * 
 * ============================================================================
 */

/* ============================================================================
 * Key Name Definitions
 * ============================================================================ */

/* Default section keys */
#define CONFIG_KEY_DEFAULT_PATH                "default.path"
#define CONFIG_KEY_DEFAULT_SHADER              "default.shader"
#define CONFIG_KEY_DEFAULT_MODE                "default.mode"
#define CONFIG_KEY_DEFAULT_DURATION            "default.duration"
#define CONFIG_KEY_DEFAULT_TRANSITION          "default.transition"
#define CONFIG_KEY_DEFAULT_TRANSITION_DURATION "default.transition_duration"
#define CONFIG_KEY_DEFAULT_SHADER_SPEED        "default.shader_speed"
#define CONFIG_KEY_DEFAULT_SHADER_FPS          "default.shader_fps"
#define CONFIG_KEY_DEFAULT_VSYNC               "default.vsync"
#define CONFIG_KEY_DEFAULT_SHOW_FPS            "default.show_fps"
#define CONFIG_KEY_DEFAULT_CHANNELS            "default.channels"

/* Output section keys (per-monitor) */
#define CONFIG_KEY_OUTPUT_PATH                 "output.path"
#define CONFIG_KEY_OUTPUT_SHADER               "output.shader"
#define CONFIG_KEY_OUTPUT_MODE                 "output.mode"
#define CONFIG_KEY_OUTPUT_DURATION             "output.duration"
#define CONFIG_KEY_OUTPUT_TRANSITION           "output.transition"
#define CONFIG_KEY_OUTPUT_TRANSITION_DURATION  "output.transition_duration"
#define CONFIG_KEY_OUTPUT_SHADER_SPEED         "output.shader_speed"
#define CONFIG_KEY_OUTPUT_SHADER_FPS           "output.shader_fps"
#define CONFIG_KEY_OUTPUT_VSYNC                "output.vsync"
#define CONFIG_KEY_OUTPUT_SHOW_FPS             "output.show_fps"
#define CONFIG_KEY_OUTPUT_CHANNELS             "output.channels"

/* ============================================================================
 * Configuration List Macro
 * ============================================================================
 * This macro is used to generate code for all config keys at once.
 * Each entry contains: (key_constant, section, name, type, scope)
 * ============================================================================ */

#define CONFIG_KEYS_LIST(X) \
    /* Default section - Static wallpaper keys */ \
    X(CONFIG_KEY_DEFAULT_PATH,                "default", "path",                CONFIG_TYPE_PATH,    CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_MODE,                "default", "mode",                CONFIG_TYPE_ENUM,    CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_DURATION,            "default", "duration",            CONFIG_TYPE_INTEGER, CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_TRANSITION,          "default", "transition",          CONFIG_TYPE_ENUM,    CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_TRANSITION_DURATION, "default", "transition_duration", CONFIG_TYPE_INTEGER, CONFIG_SCOPE_GLOBAL) \
    /* Default section - Shader wallpaper keys */ \
    X(CONFIG_KEY_DEFAULT_SHADER,              "default", "shader",              CONFIG_TYPE_PATH,    CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_SHADER_FPS,          "default", "shader_fps",          CONFIG_TYPE_INTEGER, CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_VSYNC,               "default", "vsync",               CONFIG_TYPE_BOOLEAN, CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_DEFAULT_CHANNELS,            "default", "channels",            CONFIG_TYPE_STRING,  CONFIG_SCOPE_GLOBAL) \
    /* Default section - Universal keys */ \
    X(CONFIG_KEY_DEFAULT_SHOW_FPS,            "default", "show_fps",            CONFIG_TYPE_BOOLEAN, CONFIG_SCOPE_GLOBAL) \
    /* Output section - Static wallpaper keys */ \
    X(CONFIG_KEY_OUTPUT_PATH,                 "output",  "path",                CONFIG_TYPE_PATH,    CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_MODE,                 "output",  "mode",                CONFIG_TYPE_ENUM,    CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_DURATION,             "output",  "duration",            CONFIG_TYPE_INTEGER, CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_TRANSITION,           "output",  "transition",          CONFIG_TYPE_ENUM,    CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_TRANSITION_DURATION,  "output",  "transition_duration", CONFIG_TYPE_INTEGER, CONFIG_SCOPE_OUTPUT) \
    /* Output section - Shader wallpaper keys */ \
    X(CONFIG_KEY_OUTPUT_SHADER,               "output",  "shader",              CONFIG_TYPE_PATH,    CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_SHADER_FPS,           "output",  "shader_fps",          CONFIG_TYPE_INTEGER, CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_VSYNC,                "output",  "vsync",               CONFIG_TYPE_BOOLEAN, CONFIG_SCOPE_OUTPUT) \
    X(CONFIG_KEY_OUTPUT_CHANNELS,             "output",  "channels",            CONFIG_TYPE_STRING,  CONFIG_SCOPE_OUTPUT) \
    /* Output section - Universal keys */ \
    X(CONFIG_KEY_OUTPUT_SHOW_FPS,             "output",  "show_fps",            CONFIG_TYPE_BOOLEAN, CONFIG_SCOPE_OUTPUT)

/* ============================================================================
 * Usage Examples
 * ============================================================================
 * 
 * Count keys:
 *   #define COUNT_KEY(k, s, n, t, sc) +1
 *   enum { NUM_KEYS = 0 CONFIG_KEYS_LIST(COUNT_KEY) };
 * 
 * Generate switch cases:
 *   #define CASE_KEY(k, s, n, t, sc) case k: return handle_##k(value);
 *   switch(key) { CONFIG_KEYS_LIST(CASE_KEY) }
 * 
 * Generate lookup table:
 *   #define TABLE_ENTRY(k, s, n, t, sc) { k, s, n, t, sc },
 *   static const struct { ... } keys[] = { CONFIG_KEYS_LIST(TABLE_ENTRY) };
 * 
 * ============================================================================ */

#endif /* CONFIG_KEYS_H */