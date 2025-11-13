#ifndef CONFIG_SCHEMA_H
#define CONFIG_SCHEMA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * CONFIGURATION SCHEMA - SINGLE SOURCE OF TRUTH
 * ============================================================================
 * 
 * This header defines ALL configuration keys, their types, defaults, and
 * validation rules in ONE PLACE. To add a new config key, simply add an
 * entry to the appropriate section below.
 * 
 * Design Principles:
 * 1. Single Point of Truth - All config metadata in one place
 * 2. Easy to Extend - Add new keys with one line
 * 3. Type Safety - Compile-time type checking
 * 4. Self-Documenting - Description and examples embedded
 * 5. Validation Built-in - Constraints defined with the key
 * 
 * ============================================================================ */

/* Forward declarations */
struct neowall_state;
struct output_state;

/* Configuration value types */
typedef enum {
    CONFIG_TYPE_INTEGER,    /* 64-bit signed integer */
    CONFIG_TYPE_FLOAT,      /* Double precision float */
    CONFIG_TYPE_BOOLEAN,    /* true/false */
    CONFIG_TYPE_STRING,     /* Null-terminated string */
    CONFIG_TYPE_ENUM,       /* String from fixed set */
    CONFIG_TYPE_PATH,       /* File/directory path */
} config_value_type_t;

/* Configuration scope */
typedef enum {
    CONFIG_SCOPE_GLOBAL,        /* Global setting */
    CONFIG_SCOPE_OUTPUT,        /* Per-output override */
    CONFIG_SCOPE_BOTH,          /* Can be both global and per-output */
} config_scope_t;

/* Validation result */
typedef struct {
    bool valid;
    char error_message[256];
} config_validation_result_t;

/* Configuration constraints (for validation) */
typedef union {
    struct {
        int64_t min;
        int64_t max;
    } int_range;
    
    struct {
        double min;
        double max;
    } float_range;
    
    struct {
        const char **values;        /* NULL-terminated array of valid values */
        size_t count;
    } enum_values;
    
    struct {
        bool must_exist;            /* File/dir must exist */
        bool is_directory;          /* Must be a directory */
    } path_constraints;
} config_constraints_t;

/* Configuration key metadata */
typedef struct config_key_info {
    /* Identity */
    const char *key;                    /* Full key path (e.g., "general.cycle_interval") */
    const char *section;                /* Section name (e.g., "general") */
    const char *name;                   /* Key name (e.g., "cycle_interval") */
    
    /* Type information */
    config_value_type_t type;
    config_scope_t scope;
    
    /* Documentation */
    const char *description;            /* Human-readable description */
    const char *example;                /* Example value */
    
    /* Default value (as string for uniformity) */
    const char *default_value;
    
    /* Validation constraints */
    config_constraints_t constraints;
    
    /* Custom validation function (optional) */
    config_validation_result_t (*validate)(const char *value);
    
    /* Apply function - called when value changes at runtime */
    bool (*apply)(struct neowall_state *state, const char *value);
    
    /* Per-output apply function */
    bool (*apply_output)(struct output_state *output, const char *value);
    
} config_key_info_t;

/* ============================================================================
 * CONFIGURATION KEY REGISTRY MACROS
 * ============================================================================
 * The CONFIG_ENTRY macro is defined in config_schema.c for use with
 * CONFIG_KEYS_LIST. Additional convenience macros can be added here if needed.
 * ============================================================================ */

/* ============================================================================
 * CONFIGURATION REGISTRY - SINGLE SOURCE OF TRUTH
 * ============================================================================
 * ALL configuration keys are defined here. To add a new key, add an entry.
 * The system automatically handles parsing, validation, and application.
 * ============================================================================ */

/* Get the global config registry */
const config_key_info_t *config_get_schema(void);

/* Get number of config keys */
size_t config_get_schema_count(void);

/* Lookup config key by name */
const config_key_info_t *config_lookup_key(const char *key);

/* Lookup config keys by section */
const config_key_info_t **config_lookup_section(const char *section, size_t *count);

/* ============================================================================
 * VALIDATION API
 * ============================================================================ */

/**
 * Validate a config value against schema constraints
 * 
 * @param key Config key (e.g., "general.cycle_interval")
 * @param value Value to validate (as string)
 * @return Validation result with error message if invalid
 */
config_validation_result_t config_validate_value(const char *key, const char *value);

/**
 * Validate entire configuration object
 * 
 * @param config_root VIBE object representing configuration
 * @param errors Array to store error messages (NULL-terminated)
 * @param max_errors Maximum number of errors to collect
 * @return Number of validation errors found
 */
size_t config_validate_full(const void *config_root, char **errors, size_t max_errors);

/* ============================================================================
 * TYPE CONVERSION API
 * ============================================================================ */

/**
 * Parse string value to integer
 */
bool config_parse_int(const char *str, int64_t *out);

/**
 * Parse string value to float
 */
bool config_parse_float(const char *str, double *out);

/**
 * Parse string value to boolean
 * Accepts: true/false, yes/no, on/off, 1/0
 */
bool config_parse_bool(const char *str, bool *out);

/**
 * Format value for display
 */
void config_format_value(const config_key_info_t *key, const char *value, 
                        char *buf, size_t buf_size);

/* ============================================================================
 * DEFAULT VALUES API
 * ============================================================================ */

/**
 * Get default value for a config key
 * 
 * @param key Config key
 * @return Default value as string (never NULL)
 */
const char *config_get_default(const char *key);

/**
 * Reset config key to default value
 * 
 * @param state NeoWall state
 * @param key Config key (NULL = reset all)
 * @return true on success
 */
bool config_reset_to_default(struct neowall_state *state, const char *key);

/* ============================================================================
 * INTROSPECTION API
 * ============================================================================ */

/**
 * Get list of all config keys
 * 
 * @param keys Output array of key names
 * @param max_keys Maximum keys to return
 * @return Number of keys returned
 */
size_t config_list_all_keys(const char **keys, size_t max_keys);

/**
 * Get list of sections
 * 
 * @param sections Output array of section names
 * @param max_sections Maximum sections to return
 * @return Number of sections returned
 */
size_t config_list_sections(const char **sections, size_t max_sections);

/**
 * Get JSON schema for all config keys
 * 
 * @return JSON string (caller must free)
 */
char *config_export_schema_json(void);

/* ============================================================================
 * CONFIG KEY DEFINITIONS
 * ============================================================================
 * The actual config key definitions are in config_schema.c
 * This keeps the header clean while maintaining single source of truth
 * ============================================================================ */

#endif /* CONFIG_SCHEMA_H */