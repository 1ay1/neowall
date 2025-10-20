#ifndef VIBE_H
#define VIBE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * ============================================================================
 * VIBE Parser - Dead Simple Config Format Parser
 * ============================================================================
 *
 * This is the main header you'll need. Just include this and you're good to go.
 * No complicated setup, no weird dependencies, just straightforward C.
 *
 * VIBE is a minimalist configuration format designed to be human-readable and
 * easy to parse. It supports nested objects, arrays, and basic data types
 * without the verbosity of JSON or the complexity of YAML.
 *
 * Quick Start:
 *   1. Create a parser: VibeParser *parser = vibe_parser_new();
 *   2. Parse your config: VibeValue *root = vibe_parse_file(parser, "config.vibe");
 *   3. Access values: const char *host = vibe_get_string(root, "server.host");
 *   4. Clean up: vibe_value_free(root); vibe_parser_free(parser);
 *
 * That's it! No initialization, no context setup, just parse and go.
 */

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

/**
 * VibeType - Type identifier for values
 *
 * Every value in VIBE has a type. This enum tells you what kind of data
 * you're looking at. Use this to safely cast values or determine how to
 * process them.
 *
 * Type mapping:
 *   - NULL:    Absence of value (like JSON null)
 *   - INTEGER: 64-bit signed integer (-9,223,372,036,854,775,808 to 9,223,372,036,854,775,807)
 *   - FLOAT:   IEEE 754 double precision (about 15-17 significant digits)
 *   - BOOLEAN: true or false (stored as a byte but only uses 1 bit conceptually)
 *   - STRING:  UTF-8 text, null-terminated C string
 *   - ARRAY:   Ordered list of values (can be mixed types)
 *   - OBJECT:  Key-value map (like a hash table or dictionary)
 */
typedef enum {
    VIBE_TYPE_NULL = 0,     // Nothing, nada, zilch - absence of value
    VIBE_TYPE_INTEGER,      // Whole numbers: 42, -7, 1000
    VIBE_TYPE_FLOAT,        // Decimals: 3.14, -0.5, 2.71828
    VIBE_TYPE_BOOLEAN,      // true or false (that's it)
    VIBE_TYPE_STRING,       // Text: "hello", "foo bar", etc.
    VIBE_TYPE_ARRAY,        // Lists: [item1 item2 item3]
    VIBE_TYPE_OBJECT        // Nested groups: key { ... }
} VibeType;

// Forward declarations - these types reference each other
// (objects contain values, values can be objects... circular deps, you know)
typedef struct VibeValue VibeValue;
typedef struct VibeObject VibeObject;
typedef struct VibeArray VibeArray;

/**
 * VibeObjectEntry - A single key-value pair in an object
 *
 * Objects are collections of these entries. Each entry is just a name
 * (the key) pointing to any value (which can be any type).
 *
 * Memory ownership: The object owns both the key string and the value.
 * When you free the object, these get freed too.
 */
typedef struct {
    char* key;              // The name (e.g., "port", "database", etc.)
    VibeValue* value;       // What it points to (could be anything)
} VibeObjectEntry;

/**
 * VibeObject - A collection of key-value pairs
 *
 * Objects are basically hash maps with an array backend. We grow the array
 * as needed using the classic dynamic array pattern. Keys are unique within
 * an object (setting a key twice overwrites the first value).
 *
 * Implementation note: We use linear search for key lookups. For typical
 * config files with < 100 keys per object, this is faster than hashing
 * due to better cache locality. If you need thousands of keys, consider
 * a different data structure.
 */
struct VibeObject {
    VibeObjectEntry* entries;   // Our key-value pairs live here
    size_t count;                // How many entries we actually have
    size_t capacity;             // How many we *could* have before realloc
};

/**
 * VibeArray - An ordered list of values
 *
 * Arrays are just lists of values - dead simple. They preserve insertion
 * order and allow duplicates. Again using the dynamic array pattern because
 * it works and it's fast for typical use cases.
 *
 * Arrays can hold mixed types. For example: [42 "hello" true 3.14] is valid.
 */
struct VibeArray {
    VibeValue** values;         // Pointers to our values
    size_t count;               // How many items in the list
    size_t capacity;            // Space we've allocated
};

/**
 * VibeValue - The main value container
 *
 * This is the universal value type. Everything in VIBE becomes one of these.
 * We use a tagged union so we don't waste memory storing all possible types
 * at once - only the active type takes up space.
 *
 * Memory model: VibeValue owns its data. When you free a value, it
 * recursively frees any nested objects, arrays, and strings.
 *
 * Thread safety: Not thread-safe. Don't share values across threads without
 * external synchronization.
 */
struct VibeValue {
    VibeType type;              // What flavor of value is this?
    union {
        int64_t as_integer;     // For whole numbers (64-bit, plenty of room)
        double as_float;        // For decimals (IEEE 754 double precision)
        bool as_boolean;        // true/false (just a byte really)
        char* as_string;        // Heap-allocated text (we own this memory)
        VibeArray* as_array;    // Pointer to array (also heap-allocated)
        VibeObject* as_object;  // Pointer to nested object
    };
};

/**
 * VibeError - Error information from parsing
 *
 * When something goes wrong during parsing, we store the details here.
 * Line and column numbers help you find the problem in your config file.
 *
 * Check has_error first before reading the other fields. If has_error is
 * false, the other fields are undefined.
 */
typedef struct {
    bool has_error;             // Did anything go wrong?
    char* message;              // Human-readable explanation
    int line;                   // Which line in the file (1-indexed)
    int column;                 // Which column on that line (1-indexed)
} VibeError;

/**
 * VibeParser - The parser state machine
 *
 * This structure keeps track of where we are in the input and any errors
 * we've encountered. Think of it as our reading position + error buffer.
 *
 * You don't typically manipulate this directly - just create it with
 * vibe_parser_new(), use it for parsing, then free it with vibe_parser_free().
 *
 * The parser doesn't own the input string (we just keep a pointer to it),
 * so make sure the input stays valid for the lifetime of the parser.
 */
typedef struct {
    const char* input;          // The text we're parsing (we don't own this)
    size_t pos;                 // Current byte offset
    size_t length;              // Total input length
    int line;                   // Current line number (for error messages)
    int column;                 // Current column (also for errors)
    VibeError error;            // If we messed up, details are here
} VibeParser;

/* ============================================================================
 * PARSER LIFECYCLE
 * ============================================================================ */

/**
 * vibe_parser_new - Create a new parser instance
 *
 * Allocates and initializes a fresh parser. Always pair this with
 * vibe_parser_free() when you're done.
 *
 * Returns:
 *   A new parser instance, or NULL if allocation failed.
 *
 * Example:
 *   VibeParser *parser = vibe_parser_new();
 *   if (!parser) {
 *       fprintf(stderr, "Out of memory!\n");
 *       return 1;
 *   }
 *   // ... use parser ...
 *   vibe_parser_free(parser);
 *
 * Note: The parser is stateful. Don't reuse the same parser for multiple
 * parse operations without resetting it.
 */
VibeParser* vibe_parser_new(void);

/**
 * vibe_parser_free - Free a parser instance
 *
 * Releases all memory associated with the parser, including any error
 * messages. After calling this, the parser pointer is invalid.
 *
 * Parameters:
 *   parser - The parser to free (can be NULL, in which case this is a no-op)
 *
 * Example:
 *   VibeParser *parser = vibe_parser_new();
 *   // ... use parser ...
 *   vibe_parser_free(parser);
 *   parser = NULL;  // Good practice to avoid use-after-free
 */
void vibe_parser_free(VibeParser* parser);

/* ============================================================================
 * PARSING FUNCTIONS
 * ============================================================================ */

/**
 * vibe_parse_string - Parse a VIBE config from a string
 *
 * Takes a string containing VIBE configuration and parses it into a tree
 * of VibeValue structures. This is the main parsing entry point.
 *
 * Parameters:
 *   parser - The parser instance to use
 *   input  - The VIBE config string (must be null-terminated)
 *
 * Returns:
 *   A VibeValue representing the root of the parsed config, or NULL if
 *   parsing failed. Check parser->error for details.
 *
 * Example:
 *   VibeParser *parser = vibe_parser_new();
 *   const char *config = "server { port 8080 host localhost }";
 *   VibeValue *root = vibe_parse_string(parser, config);
 *
 *   if (!root) {
 *       fprintf(stderr, "Parse error at %d:%d: %s\n",
 *               parser->error.line, parser->error.column,
 *               parser->error.message);
 *       vibe_parser_free(parser);
 *       return 1;
 *   }
 *
 *   // Use root...
 *   vibe_value_free(root);
 *   vibe_parser_free(parser);
 *
 * Memory: The returned VibeValue is heap-allocated. You must free it with
 * vibe_value_free() when done.
 *
 * Note: The parser keeps a pointer to the input string, so the string must
 * remain valid for the duration of parsing.
 */
VibeValue* vibe_parse_string(VibeParser* parser, const char* input);

/**
 * vibe_parse_file - Parse a VIBE config from a file
 *
 * Reads a file and parses its contents as VIBE configuration. This is a
 * convenience wrapper around vibe_parse_string() that handles file I/O.
 *
 * Parameters:
 *   parser   - The parser instance to use
 *   filename - Path to the file to parse
 *
 * Returns:
 *   A VibeValue representing the root of the parsed config, or NULL if
 *   the file couldn't be read or parsing failed.
 *
 * Example:
 *   VibeParser *parser = vibe_parser_new();
 *   VibeValue *config = vibe_parse_file(parser, "/etc/myapp/config.vibe");
 *
 *   if (!config) {
 *       if (parser->error.has_error) {
 *           fprintf(stderr, "Parse error: %s\n", parser->error.message);
 *       } else {
 *           fprintf(stderr, "Could not read file\n");
 *       }
 *       vibe_parser_free(parser);
 *       return 1;
 *   }
 *
 *   // Use config...
 *   vibe_value_free(config);
 *   vibe_parser_free(parser);
 *
 * Memory: The returned VibeValue is heap-allocated. You must free it with
 * vibe_value_free() when done.
 *
 * Errors: File I/O errors (file not found, permissions, etc.) will result
 * in a NULL return but won't set parser->error. Parse errors will set
 * parser->error appropriately.
 */
VibeValue* vibe_parse_file(VibeParser* parser, const char* filename);

/* ============================================================================
 * VALUE CONSTRUCTION
 * ============================================================================ */

/**
 * vibe_value_new_integer - Create an integer value
 *
 * Creates a new VibeValue containing a 64-bit signed integer.
 *
 * Parameters:
 *   value - The integer to store
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_INTEGER, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *port = vibe_value_new_integer(8080);
 *   // Use port...
 *   vibe_value_free(port);
 *
 * Range: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
 */
VibeValue* vibe_value_new_integer(int64_t value);

/**
 * vibe_value_new_float - Create a floating-point value
 *
 * Creates a new VibeValue containing an IEEE 754 double-precision float.
 *
 * Parameters:
 *   value - The float to store
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_FLOAT, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *timeout = vibe_value_new_float(3.5);
 *   // Use timeout...
 *   vibe_value_free(timeout);
 *
 * Precision: About 15-17 significant decimal digits. Be aware of floating-point
 * rounding issues if you need exact decimal representation.
 */
VibeValue* vibe_value_new_float(double value);

/**
 * vibe_value_new_boolean - Create a boolean value
 *
 * Creates a new VibeValue containing a boolean (true or false).
 *
 * Parameters:
 *   value - The boolean to store (non-zero is true, zero is false)
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_BOOLEAN, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *debug = vibe_value_new_boolean(true);
 *   VibeValue *production = vibe_value_new_boolean(false);
 *   // Use values...
 *   vibe_value_free(debug);
 *   vibe_value_free(production);
 */
VibeValue* vibe_value_new_boolean(bool value);

/**
 * vibe_value_new_string - Create a string value
 *
 * Creates a new VibeValue containing a copy of the provided string.
 * The string is duplicated, so you can free the original after calling this.
 *
 * Parameters:
 *   value - The string to store (must be null-terminated)
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_STRING, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *host = vibe_value_new_string("localhost");
 *   // Original string can be freed or go out of scope
 *   // Use host...
 *   vibe_value_free(host);
 *
 * Memory: The string is copied internally, so modifications to the original
 * won't affect the VibeValue.
 *
 * Note: The string should be valid UTF-8 for proper display, but this isn't
 * enforced. Binary data will work but may cause issues with printing.
 */
VibeValue* vibe_value_new_string(const char* value);

/**
 * vibe_value_new_array - Create an empty array
 *
 * Creates a new VibeValue containing an empty array. Use vibe_array_push()
 * to add elements to it.
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_ARRAY, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *arr = vibe_value_new_array();
 *   vibe_array_push(arr->as_array, vibe_value_new_integer(1));
 *   vibe_array_push(arr->as_array, vibe_value_new_integer(2));
 *   vibe_array_push(arr->as_array, vibe_value_new_integer(3));
 *   // arr now contains [1, 2, 3]
 *   vibe_value_free(arr);  // This frees all elements too
 *
 * Initial capacity: The array starts with a small capacity and grows
 * automatically as needed. Growth is typically 2x on reallocation.
 */
VibeValue* vibe_value_new_array(void);

/**
 * vibe_value_new_object - Create an empty object
 *
 * Creates a new VibeValue containing an empty object. Use vibe_object_set()
 * to add key-value pairs to it.
 *
 * Returns:
 *   A new VibeValue of type VIBE_TYPE_OBJECT, or NULL on allocation failure.
 *
 * Example:
 *   VibeValue *obj = vibe_value_new_object();
 *   vibe_object_set(obj->as_object, "port", vibe_value_new_integer(8080));
 *   vibe_object_set(obj->as_object, "host", vibe_value_new_string("localhost"));
 *   // obj now contains { port 8080 host localhost }
 *   vibe_value_free(obj);  // This frees all key-value pairs too
 *
 * Initial capacity: The object starts with a small capacity and grows
 * automatically as needed.
 */
VibeValue* vibe_value_new_object(void);

/* ============================================================================
 * VALUE ACCESS - PATH-BASED
 * ============================================================================ */

/**
 * vibe_get - Get a value by path
 *
 * Navigates through nested objects using dot notation to find a value.
 * This is the most convenient way to access deeply nested config values.
 *
 * Parameters:
 *   root - The root value to search from (typically an object)
 *   path - Dot-separated path like "server.database.port"
 *
 * Returns:
 *   The value at the specified path, or NULL if the path doesn't exist.
 *
 * Example:
 *   // Config: server { database { host localhost port 5432 } }
 *   VibeValue *db_port = vibe_get(root, "server.database.port");
 *   if (db_port && db_port->type == VIBE_TYPE_INTEGER) {
 *       printf("Port: %lld\n", (long long)db_port->as_integer);
 *   }
 *
 * Path syntax: Paths are dot-separated sequences of keys. Each segment must
 * be a valid key in an object. Array indexing is not supported in paths
 * (use vibe_array_get() for arrays).
 *
 * Note: Returns a pointer to the value, NOT a copy. Don't free the returned
 * value separately - it's owned by the parent structure.
 */
VibeValue* vibe_get(VibeValue* root, const char* path);

/**
 * vibe_get_string - Get a string value by path
 *
 * Convenience wrapper that gets a value and returns it as a string.
 * Returns NULL if the path doesn't exist or the value isn't a string.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   The string value, or NULL if not found or wrong type.
 *
 * Example:
 *   const char *host = vibe_get_string(root, "server.host");
 *   if (host) {
 *       printf("Host: %s\n", host);
 *   } else {
 *       printf("Host not configured\n");
 *   }
 *
 * Note: The returned string is owned by the VibeValue. Don't free it or
 * modify it. If you need to keep it, make a copy with strdup().
 */
const char* vibe_get_string(VibeValue* value, const char* path);

/**
 * vibe_get_int - Get an integer value by path
 *
 * Convenience wrapper that gets a value and returns it as an integer.
 * Returns 0 if the path doesn't exist or the value isn't an integer.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   The integer value, or 0 if not found or wrong type.
 *
 * Example:
 *   int64_t port = vibe_get_int(root, "server.port");
 *   printf("Port: %lld\n", (long long)port);
 *
 * Warning: Can't distinguish between "value is 0" and "value not found".
 * If you need to know whether the key exists, use vibe_get() and check
 * the type explicitly.
 */
int64_t vibe_get_int(VibeValue* value, const char* path);

/**
 * vibe_get_float - Get a float value by path
 *
 * Convenience wrapper that gets a value and returns it as a float.
 * Returns 0.0 if the path doesn't exist or the value isn't a float.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   The float value, or 0.0 if not found or wrong type.
 *
 * Example:
 *   double timeout = vibe_get_float(root, "server.timeout");
 *   printf("Timeout: %.2f seconds\n", timeout);
 *
 * Warning: Can't distinguish between "value is 0.0" and "value not found".
 */
double vibe_get_float(VibeValue* value, const char* path);

/**
 * vibe_get_bool - Get a boolean value by path
 *
 * Convenience wrapper that gets a value and returns it as a boolean.
 * Returns false if the path doesn't exist or the value isn't a boolean.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   The boolean value, or false if not found or wrong type.
 *
 * Example:
 *   bool debug = vibe_get_bool(root, "app.debug");
 *   if (debug) {
 *       enable_debug_logging();
 *   }
 *
 * Warning: Can't distinguish between "value is false" and "value not found".
 */
bool vibe_get_bool(VibeValue* value, const char* path);

/**
 * vibe_get_array - Get an array by path
 *
 * Convenience wrapper that gets a value and returns it as an array.
 * Returns NULL if the path doesn't exist or the value isn't an array.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   Pointer to the VibeArray, or NULL if not found or wrong type.
 *
 * Example:
 *   VibeArray *servers = vibe_get_array(root, "cluster.servers");
 *   if (servers) {
 *       for (size_t i = 0; i < servers->count; i++) {
 *           VibeValue *server = servers->values[i];
 *           // Process server...
 *       }
 *   }
 *
 * Note: The returned array is owned by the parent VibeValue. Don't free it.
 */
VibeArray* vibe_get_array(VibeValue* value, const char* path);

/**
 * vibe_get_object - Get an object by path
 *
 * Convenience wrapper that gets a value and returns it as an object.
 * Returns NULL if the path doesn't exist or the value isn't an object.
 *
 * Parameters:
 *   value - The root value to search from
 *   path  - Dot-separated path
 *
 * Returns:
 *   Pointer to the VibeObject, or NULL if not found or wrong type.
 *
 * Example:
 *   VibeObject *db = vibe_get_object(root, "server.database");
 *   if (db) {
 *       for (size_t i = 0; i < db->count; i++) {
 *           printf("%s = ...\n", db->entries[i].key);
 *       }
 *   }
 *
 * Note: The returned object is owned by the parent VibeValue. Don't free it.
 */
VibeObject* vibe_get_object(VibeValue* value, const char* path);

/* ============================================================================
 * OBJECT MANIPULATION
 * ============================================================================ */

/**
 * vibe_object_set - Set a key-value pair in an object
 *
 * Adds or updates a key-value pair in an object. If the key already exists,
 * the old value is freed and replaced with the new one.
 *
 * Parameters:
 *   obj   - The object to modify
 *   key   - The key (will be copied internally)
 *   value - The value to store (ownership transferred to the object)
 *
 * Example:
 *   VibeObject *obj = vibe_value_new_object()->as_object;
 *   vibe_object_set(obj, "port", vibe_value_new_integer(8080));
 *   vibe_object_set(obj, "host", vibe_value_new_string("localhost"));
 *
 *   // Updating an existing key frees the old value
 *   vibe_object_set(obj, "port", vibe_value_new_integer(9000));
 *
 * Memory: The object takes ownership of the value. Don't free it yourself.
 * The key string is copied, so you can free your copy after calling this.
 *
 * Performance: O(n) for key lookup (linear search). For most config files
 * with < 100 keys per object, this is fine and often faster than hashing.
 */
void vibe_object_set(VibeObject* obj, const char* key, VibeValue* value);

/**
 * vibe_object_get - Get a value from an object by key
 *
 * Looks up a key in an object and returns the associated value.
 *
 * Parameters:
 *   obj - The object to search
 *   key - The key to look up
 *
 * Returns:
 *   The value associated with the key, or NULL if not found.
 *
 * Example:
 *   VibeValue *port = vibe_object_get(obj, "port");
 *   if (port && port->type == VIBE_TYPE_INTEGER) {
 *       printf("Port: %lld\n", (long long)port->as_integer);
 *   }
 *
 * Note: Returns a pointer to the value, NOT a copy. The object still owns
 * the value, so don't free it.
 *
 * Performance: O(n) linear search through keys.
 */
VibeValue* vibe_object_get(VibeObject* obj, const char* key);

/* ============================================================================
 * ARRAY MANIPULATION
 * ============================================================================ */

/**
 * vibe_array_push - Append a value to an array
 *
 * Adds a value to the end of an array. The array grows automatically if
 * needed.
 *
 * Parameters:
 *   arr   - The array to modify
 *   value - The value to append (ownership transferred to the array)
 *
 * Example:
 *   VibeArray *arr = vibe_value_new_array()->as_array;
 *   vibe_array_push(arr, vibe_value_new_string("first"));
 *   vibe_array_push(arr, vibe_value_new_string("second"));
 *   vibe_array_push(arr, vibe_value_new_string("third"));
 *   // arr now contains ["first", "second", "third"]
 *
 * Memory: The array takes ownership of the value. Don't free it yourself.
 *
 * Performance: Amortized O(1). When the array needs to grow, it typically
 * doubles in capacity, so most pushes don't require reallocation.
 */
void vibe_array_push(VibeArray* arr, VibeValue* value);

/**
 * vibe_array_get - Get a value from an array by index
 *
 * Retrieves a value at a specific index in an array. Indices are zero-based.
 *
 * Parameters:
 *   arr   - The array to access
 *   index - The zero-based index
 *
 * Returns:
 *   The value at the specified index, or NULL if index is out of bounds.
 *
 * Example:
 *   VibeArray *servers = vibe_get_array(root, "cluster.servers");
 *   for (size_t i = 0; i < servers->count; i++) {
 *       VibeValue *server = vibe_array_get(servers, i);
 *       if (server && server->type == VIBE_TYPE_STRING) {
 *           printf("Server %zu: %s\n", i, server->as_string);
 *       }
 *   }
 *
 * Bounds checking: Returns NULL for out-of-bounds indices. Always check
 * against arr->count before accessing.
 *
 * Note: Returns a pointer to the value, NOT a copy. The array still owns
 * the value, so don't free it.
 */
VibeValue* vibe_array_get(VibeArray* arr, size_t index);

/* ============================================================================
 * MEMORY MANAGEMENT
 * ============================================================================ */

/**
 * vibe_value_free - Free a value and all nested data
 *
 * Recursively frees a VibeValue and all data it contains. This includes:
 *   - Strings (the actual string data)
 *   - Arrays (all elements in the array)
 *   - Objects (all key-value pairs)
 *   - Nested values (recursively)
 *
 * Parameters:
 *   value - The value to free (can be NULL, in which case this is a no-op)
 *
 * Example:
 *   VibeValue *config = vibe_parse_file(parser, "config.vibe");
 *   // ... use config ...
 *   vibe_value_free(config);  // Frees everything
 *
 * Warning: After calling this, all pointers to nested values are invalid.
 * Don't keep references to values inside the tree after freeing the root.
 *
 * Example of what NOT to do:
 *   VibeValue *host = vibe_get(root, "server.host");
 *   vibe_value_free(root);
 *   printf("%s\n", host->as_string);  // CRASH! host is now invalid
 */
void vibe_value_free(VibeValue* value);

/* ============================================================================
 * ERROR HANDLING
 * ============================================================================ */

/**
 * vibe_get_last_error - Get the last error from a parser
 *
 * Retrieves error information from the most recent parse operation.
 * Check error.has_error to see if an error occurred.
 *
 * Parameters:
 *   parser - The parser to query
 *
 * Returns:
 *   A VibeError structure with error details. If no error occurred,
 *   has_error will be false.
 *
 * Example:
 *   VibeValue *root = vibe_parse_file(parser, "config.vibe");
 *   if (!root) {
 *       VibeError err = vibe_get_last_error(parser);
 *       if (err.has_error) {
 *           fprintf(stderr, "Parse error at line %d, column %d:\n",
 *                   err.line, err.column);
 *           fprintf(stderr, "  %s\n", err.message);
 *       } else {
 *           fprintf(stderr, "Could not read file\n");
 *       }
 *   }
 *
 * Note: The error message pointer is owned by the parser. Don't free it.
 * The error persists until the next parse operation or parser is freed.
 */
VibeError vibe_get_last_error(VibeParser* parser);

/**
 * vibe_error_free - Free an error structure
 *
 * Frees the message string in a VibeError. You typically don't need to call
 * this yourself - the parser handles error cleanup internally.
 *
 * Parameters:
 *   error - The error to free
 *
 * Note: Only the message is freed. The VibeError structure itself is not
 * freed (it's typically stack-allocated).
 */
void vibe_error_free(VibeError* error);

/* ============================================================================
 * DEBUGGING AND DISPLAY
 * ============================================================================ */

/**
 * vibe_value_print - Print a value tree to stdout
 *
 * Recursively prints a VibeValue and all its nested contents in a
 * human-readable format. Useful for debugging and understanding your
 * config structure.
 *
 * Parameters:
 *   value  - The value to print
 *   indent - Starting indentation level (use 0 for root)
 *
 * Example:
 *   VibeValue *config = vibe_parse_file(parser, "config.vibe");
 *   vibe_value_print(config, 0);
 *
 * Output format:
 *   INTEGER: 42
 *   STRING: "hello"
 *   OBJECT: {
 *     key1: STRING: "value1"
 *     key2: INTEGER: 123
 *   }
 *   ARRAY: [
 *     [0]: STRING: "first"
 *     [1]: STRING: "second"
 *   ]
 *
 * Note: This prints to stdout. If you need to print to a file or string,
 * you'll need to implement your own traversal function.
 */
void vibe_value_print(VibeValue* value, int indent);

#endif /* VIBE_H */