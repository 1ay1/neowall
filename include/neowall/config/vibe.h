/*
 * VENDORED — single-header libvibe from https://github.com/1ay1/vibe
 * Do NOT edit here; update by re-vendoring the upstream vibe.h. The
 * implementation is compiled exactly once via src/config/vibe_impl.c
 * (which #defines VIBE_IMPLEMENTATION before including this header).
 * ============================================================================
 * libvibe — the reference parser and emitter for the VIBE config format
 * ============================================================================
 *
 * VIBE is a config format for humans: nested objects, flat scalar arrays, and
 * exactly four scalar types (integer, float, boolean, string). It refuses the
 * features that turn config into a programming language — no anchors, no
 * includes, no templates, no variable substitution, no implicit coercion.
 *
 * This single header is the entire library. It works two ways:
 *
 *   1. Header-only (stb-style). In EXACTLY ONE .c/.cc file, define the
 *      implementation macro before including it:
 *
 *          #define VIBE_IMPLEMENTATION
 *          #include "vibe.h"
 *
 *      Every other file just does #include "vibe.h" for the declarations.
 *      Nothing else to build — no vibe.c, no library to link.
 *
 *   2. Traditional. Compile the bundled vibe.c (a three-line shim that sets
 *      VIBE_IMPLEMENTATION and includes this header) into libvibe.a/.so and
 *      link it. Consumers include "vibe.h" as usual.
 *
 * Optional knobs (define before the implementation include):
 *   VIBE_STATIC    give every function internal linkage (great for header-only
 *                  use — no symbols leak, the compiler can inline across calls).
 *   VIBE_MALLOC / VIBE_REALLOC / VIBE_FREE   compile-time allocator override.
 *
 *   VibeParser *p   = vibe_parser_new();
 *   VibeValue  *cfg = vibe_parse_file(p, "config.vibe");
 *   if (!cfg) { VibeError e = vibe_get_last_error(p); ... }
 *   const char *host = vibe_get_string(cfg, "server.host");
 *   vibe_value_free(cfg);
 *   vibe_parser_free(p);
 *
 * Design guarantees:
 *   - No global mutable state beyond the optional allocator hooks; every parse
 *     is independent and re-entrant across threads (one VibeParser per thread).
 *   - Fails closed: malformed or oversized input is rejected with a stable
 *     error code, never silently truncated or coerced.
 *   - Round-trips: vibe_emit() serialises a value tree back to canonical VIBE
 *     text that re-parses to an equal tree.
 *
 * License: see LICENSE. SPDX-License-Identifier: MIT
 */

#ifndef VIBE_H
#define VIBE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION
 * ============================================================================
 * VIBE_VERSION_* is the version of THIS LIBRARY (libvibe). VIBE_FORMAT_VERSION
 * is the version of the language it implements. They advance independently.
 */
#define VIBE_VERSION_MAJOR 1
#define VIBE_VERSION_MINOR 2
#define VIBE_VERSION_PATCH 0
#define VIBE_VERSION_STRING "1.2.0"
#define VIBE_VERSION_NUMBER \
    (VIBE_VERSION_MAJOR * 10000 + VIBE_VERSION_MINOR * 100 + VIBE_VERSION_PATCH)

/* The VIBE language level this build parses and emits. */
#define VIBE_FORMAT_VERSION "1.0"

/* ============================================================================
 * SYMBOL VISIBILITY
 * ============================================================================
 * Define VIBE_BUILD_SHARED when compiling the shared library, and
 * VIBE_USE_SHARED when consuming it as a DLL on Windows. Static use needs
 * neither. Define VIBE_STATIC for header-only builds where every function
 * should have internal linkage (no exported symbols, best inlining).
 */
#if defined(VIBE_STATIC)
#  define VIBE_API static
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(VIBE_BUILD_SHARED)
#    define VIBE_API __declspec(dllexport)
#  elif defined(VIBE_USE_SHARED)
#    define VIBE_API __declspec(dllimport)
#  else
#    define VIBE_API
#  endif
#elif defined(VIBE_BUILD_SHARED) && (defined(__GNUC__) || defined(__clang__))
#  define VIBE_API __attribute__((visibility("default")))
#else
#  define VIBE_API
#endif

/* ============================================================================
 * VALUE TYPES
 * ============================================================================ */

/** The type tag carried by every VibeValue. */
typedef enum {
    VIBE_TYPE_NULL = 0,   /* absence of value (never produced by the parser) */
    VIBE_TYPE_INTEGER,    /* 64-bit signed integer */
    VIBE_TYPE_FLOAT,      /* IEEE-754 double */
    VIBE_TYPE_BOOLEAN,    /* true / false */
    VIBE_TYPE_STRING,     /* UTF-8 text (heap owned) */
    VIBE_TYPE_ARRAY,      /* ordered list of scalars (never objects/arrays) */
    VIBE_TYPE_OBJECT      /* ordered set of key -> value entries */
} VibeType;

typedef struct VibeValue VibeValue;
typedef struct VibeObject VibeObject;
typedef struct VibeArray VibeArray;

/** One key/value pair inside an object. Keys are heap-owned UTF-8. */
typedef struct {
    char* key;
    VibeValue* value;
} VibeObjectEntry;

/** An object: an insertion-ordered collection of key/value pairs.
 *
 * Lookups are O(1) amortised: once an object grows past a small threshold the
 * parser/builder lazily maintains an open-addressed hash index (`index`) that
 * maps a key hash to its slot in `entries`. The `entries`/`count`/`capacity`
 * triple remains the canonical, insertion-ordered store — the index is a pure
 * accelerator and may be NULL (small objects skip it entirely). These extra
 * fields are appended so the layout of the first three members is unchanged. */
struct VibeObject {
    VibeObjectEntry* entries;
    size_t count;
    size_t capacity;
    /* --- lazy hash index (accelerator; NULL until it pays off) --- */
    uint32_t* index;        /* index_cap slots; 0 = empty, else entry+1  */
    size_t index_cap;       /* power-of-two bucket count (0 = no index)  */
};

/** An array: an insertion-ordered list of scalar values. */
struct VibeArray {
    VibeValue** values;
    size_t count;
    size_t capacity;
};

/**
 * The universal tagged-union value. Inspect .type, then read the matching
 * union member. A value owns everything it points to; vibe_value_free()
 * releases the whole subtree.
 */
struct VibeValue {
    VibeType type;
    union {
        int64_t as_integer;
        double as_float;
        bool as_boolean;
        char* as_string;
        VibeArray* as_array;
        VibeObject* as_object;
    };
};

/* ============================================================================
 * ERRORS
 * ============================================================================ */

/**
 * Stable, machine-comparable error categories. The first eleven map 1:1 to the
 * error codes defined normatively in SPECIFICATION.md; the last two cover
 * non-parse failures (I/O, allocation). Use vibe_error_code_string() to get the
 * canonical spelling ("unclosed-object", ...).
 */
typedef enum {
    VIBE_OK = 0,
    VIBE_ERROR_ENCODING,            /* "encoding-error"       */
    VIBE_ERROR_ILLEGAL_CHARACTER,   /* "illegal-character"    */
    VIBE_ERROR_UNEXPECTED_TOKEN,    /* "unexpected-token"     */
    VIBE_ERROR_UNCLOSED_OBJECT,     /* "unclosed-object"      */
    VIBE_ERROR_UNCLOSED_ARRAY,      /* "unclosed-array"       */
    VIBE_ERROR_UNTERMINATED_STRING, /* "unterminated-string"  */
    VIBE_ERROR_NESTED_CONTAINER,    /* "nested-container"     */
    VIBE_ERROR_INVALID_ESCAPE,      /* "invalid-escape"       */
    VIBE_ERROR_INVALID_NUMBER,      /* "invalid-number"       */
    VIBE_ERROR_LIMIT_EXCEEDED,      /* "limit-exceeded"       */
    VIBE_ERROR_IO,                  /* "io-error"    (non-parse) */
    VIBE_ERROR_OUT_OF_MEMORY        /* "out-of-memory" (non-parse) */
} VibeErrorCode;

/**
 * Details of the most recent failure. Check has_error first; when true, code is
 * the stable category, message is a human-readable explanation, and line/column
 * are 1-based positions into the source. The message is owned by the parser and
 * remains valid until the next parse or vibe_parser_free().
 */
typedef struct {
    bool has_error;
    VibeErrorCode code;
    char* message;
    int line;
    int column;
} VibeError;

/* Canonical hyphenated spelling of an error code, e.g. "unclosed-object". */
VIBE_API const char* vibe_error_code_string(VibeErrorCode code);

/* ============================================================================
 * RESOURCE LIMITS
 * ============================================================================
 * Bounds enforced while parsing so that untrusted input cannot exhaust memory
 * or the stack. Defaults are the REQUIRED minimums from SPECIFICATION.md; every
 * conforming parser accepts at least this much, so a document within these
 * bounds is portable. Raise them via vibe_parser_set_limits() when you trust
 * the source. Exceeding any bound yields VIBE_ERROR_LIMIT_EXCEEDED.
 */
typedef struct {
    size_t max_document_size;   /* total input bytes            (default 16 MiB) */
    size_t max_depth;           /* object/array nesting levels  (default 64)     */
    size_t max_string_length;   /* decoded quoted-string bytes  (default 1 MiB)  */
    size_t max_key_length;      /* key bytes                    (default 1 KiB)  */
    size_t max_array_elements;  /* elements in one array        (default 65536)  */
    size_t max_object_keys;     /* keys in one object           (default 65536)  */
    size_t max_number_digits;   /* chars in one numeric token   (default 1024)   */
} VibeLimits;

/** The default (spec-minimum) limits. */
VIBE_API VibeLimits vibe_default_limits(void);

/* ============================================================================
 * ALLOCATOR HOOKS
 * ============================================================================
 * Optional. Install a custom allocator ONCE at startup, before any other libvibe
 * call, and do not change it while values are live (it is used to both allocate
 * and free them). Passing NULL for any function resets that slot to the C
 * standard library. Not thread-safe to call concurrently with other libvibe
 * work; treat it as one-time configuration.
 */
VIBE_API void vibe_set_allocators(void* (*malloc_fn)(size_t),
                                  void* (*realloc_fn)(void*, size_t),
                                  void  (*free_fn)(void*));

/** Allocate/free through the installed hooks. Use vibe_free() on any buffer
 *  libvibe hands back to you (e.g. from vibe_emit()). */
VIBE_API void* vibe_malloc(size_t size);
VIBE_API void  vibe_free(void* ptr);

/* ============================================================================
 * VERSION QUERIES
 * ============================================================================ */

/** Runtime library version string, e.g. "1.2.0". */
VIBE_API const char* vibe_version(void);
/** Runtime library version as MAJOR*10000 + MINOR*100 + PATCH. */
VIBE_API int vibe_version_number(void);
/** The VIBE language level this build implements, e.g. "1.0". */
VIBE_API const char* vibe_format_version(void);

/* ============================================================================
 * PARSER STATE
 * ============================================================================
 * A reusable parse context: current position plus the last error. Create with
 * vibe_parser_new(), reuse across many parses, free with vibe_parser_free().
 * The position fields are exposed for tooling/visualisation; treat them as
 * read-only during a parse.
 */
typedef struct {
    const char* input;   /* current source (not owned)     */
    size_t pos;          /* byte offset into input         */
    size_t length;       /* length of input in bytes       */
    long line;           /* current line   (1-based, saturating) */
    long column;         /* current column (1-based, saturating) */
    VibeError error;     /* details of the last failure    */
    VibeLimits limits;   /* bounds enforced while parsing  */
} VibeParser;

/* ============================================================================
 * PARSER LIFECYCLE
 * ============================================================================ */

/** Create a parser with default limits. Returns NULL on allocation failure. */
VIBE_API VibeParser* vibe_parser_new(void);

/** Free a parser and its error buffer. NULL-safe. */
VIBE_API void vibe_parser_free(VibeParser* parser);

/** Replace the parser's resource limits. NULL restores the defaults. */
VIBE_API void vibe_parser_set_limits(VibeParser* parser, const VibeLimits* limits);

/* ============================================================================
 * PARSING
 * ============================================================================
 * All return a heap-allocated root VibeValue (of type OBJECT) on success, or
 * NULL on failure — call vibe_get_last_error() for details. Free the result
 * with vibe_value_free(). The input is not retained after the call returns.
 */

/** Parse a NUL-terminated string. Embedded NUL bytes truncate the input;
 *  use vibe_parse_buffer() for data that may contain NUL. */
VIBE_API VibeValue* vibe_parse_string(VibeParser* parser, const char* input);

/** Parse exactly `length` bytes (NUL-safe, length-aware). */
VIBE_API VibeValue* vibe_parse_buffer(VibeParser* parser, const char* data, size_t length);

/** Read a file fully and parse it. */
VIBE_API VibeValue* vibe_parse_file(VibeParser* parser, const char* filename);

/** Stateless one-shot parse: no parser object required. On failure returns NULL
 *  and, if out_error is non-NULL, fills it with an owned copy of the error that
 *  the caller frees with vibe_error_free(). */
VIBE_API VibeValue* vibe_parse(const char* data, size_t length, VibeError* out_error);

/* ============================================================================
 * ERROR RETRIEVAL
 * ============================================================================ */

/** Snapshot the parser's last error. The returned message aliases the parser's
 *  buffer; do not free it and do not call vibe_error_free() on this copy. */
VIBE_API VibeError vibe_get_last_error(VibeParser* parser);

/** Free an owned error (only the one produced by vibe_parse()). NULL-safe. */
VIBE_API void vibe_error_free(VibeError* error);

/* ============================================================================
 * VALUE CONSTRUCTORS
 * ============================================================================
 * Each returns a new heap value, or NULL on allocation failure. String
 * constructors copy their input. Free the whole tree with vibe_value_free().
 */
VIBE_API VibeValue* vibe_value_new_null(void);
VIBE_API VibeValue* vibe_value_new_integer(int64_t value);
VIBE_API VibeValue* vibe_value_new_float(double value);
VIBE_API VibeValue* vibe_value_new_boolean(bool value);
VIBE_API VibeValue* vibe_value_new_string(const char* value);
VIBE_API VibeValue* vibe_value_new_string_len(const char* value, size_t length);
VIBE_API VibeValue* vibe_value_new_array(void);
VIBE_API VibeValue* vibe_value_new_object(void);

/** Deep-copy a value and everything it owns. NULL on failure. */
VIBE_API VibeValue* vibe_value_clone(const VibeValue* value);

/** Deep structural equality. Two values are equal iff they have the same type
 *  and equal contents: scalars compare by value (strings byte-for-byte, numbers
 *  exactly — note 1 and 1.0 differ in type so are NOT equal); arrays compare
 *  element-wise in order; objects compare as sets of key/value pairs regardless
 *  of insertion order. NULL == NULL. */
VIBE_API bool vibe_value_equals(const VibeValue* a, const VibeValue* b);

/* ============================================================================
 * VALUE INSPECTION  (type-safe, path-free access to a value in hand)
 * ============================================================================
 * When you already hold a VibeValue* (e.g. from vibe_array_get or
 * vibe_object_get) these read it without a dotted path and without touching
 * the union directly. The typed readers return a sentinel (0 / false / NULL /
 * empty) when the value is NULL or of the wrong type; the *_or forms let you
 * choose the fallback.
 */
VIBE_API VibeType vibe_value_type(const VibeValue* value);
VIBE_API bool vibe_is_null(const VibeValue* value);
VIBE_API bool vibe_is_integer(const VibeValue* value);
VIBE_API bool vibe_is_float(const VibeValue* value);
VIBE_API bool vibe_is_boolean(const VibeValue* value);
VIBE_API bool vibe_is_string(const VibeValue* value);
VIBE_API bool vibe_is_array(const VibeValue* value);
VIBE_API bool vibe_is_object(const VibeValue* value);

VIBE_API int64_t     vibe_value_int(const VibeValue* value);
VIBE_API double      vibe_value_float(const VibeValue* value);
VIBE_API bool        vibe_value_bool(const VibeValue* value);
VIBE_API const char* vibe_value_string(const VibeValue* value);
VIBE_API VibeArray*  vibe_value_array(const VibeValue* value);
VIBE_API VibeObject* vibe_value_object(const VibeValue* value);

VIBE_API int64_t     vibe_value_int_or(const VibeValue* value, int64_t fallback);
VIBE_API double      vibe_value_float_or(const VibeValue* value, double fallback);
VIBE_API bool        vibe_value_bool_or(const VibeValue* value, bool fallback);
VIBE_API const char* vibe_value_string_or(const VibeValue* value, const char* fallback);

/* ============================================================================
 * PATH ACCESS  (dotted paths, e.g. "server.tls.port")
 * ============================================================================
 * Convenience readers that walk objects by dotted key. The plain readers return
 * a zero/NULL sentinel when the path is missing or the wrong type; the *_or
 * variants let you supply the fallback explicitly.
 */
VIBE_API VibeValue* vibe_get(VibeValue* root, const char* path);
VIBE_API const char* vibe_get_string(VibeValue* value, const char* path);
VIBE_API int64_t vibe_get_int(VibeValue* value, const char* path);
VIBE_API double vibe_get_float(VibeValue* value, const char* path);
VIBE_API bool vibe_get_bool(VibeValue* value, const char* path);
VIBE_API VibeArray* vibe_get_array(VibeValue* value, const char* path);
VIBE_API VibeObject* vibe_get_object(VibeValue* value, const char* path);

VIBE_API const char* vibe_get_string_or(VibeValue* value, const char* path, const char* fallback);
VIBE_API int64_t vibe_get_int_or(VibeValue* value, const char* path, int64_t fallback);
VIBE_API double vibe_get_float_or(VibeValue* value, const char* path, double fallback);
VIBE_API bool vibe_get_bool_or(VibeValue* value, const char* path, bool fallback);

/* ============================================================================
 * CONTAINER MANIPULATION
 * ============================================================================ */

/** Insert or replace `key` in `object`, taking ownership of `value`. Replacing
 *  an existing key frees the old value (last-wins, matching the parser).
 *  Returns true if the value is now stored under `key`; returns false only on
 *  allocation failure or a NULL argument, in which case `value` is freed (so
 *  ownership is always consumed and a failed set never leaks). */
VIBE_API bool vibe_object_set(VibeObject* object, const char* key, VibeValue* value);

/** Look up a key. Returns the stored value (not a copy), or NULL if absent. */
VIBE_API VibeValue* vibe_object_get(VibeObject* object, const char* key);

/** Number of entries in an object (0 if NULL). */
VIBE_API size_t vibe_object_size(const VibeObject* object);

/** True if `key` is present. */
VIBE_API bool vibe_object_has(const VibeObject* object, const char* key);

/** Remove `key`, freeing its value. Returns true if a key was removed. Preserves
 *  the insertion order of the remaining entries. */
VIBE_API bool vibe_object_remove(VibeObject* object, const char* key);

/** Iterate in insertion order: the key / value at position `index` (0-based),
 *  or NULL if `index` is out of range. Together with vibe_object_size() these
 *  let you walk an object without touching its internals. */
VIBE_API const char* vibe_object_key_at(const VibeObject* object, size_t index);
VIBE_API VibeValue*  vibe_object_value_at(const VibeObject* object, size_t index);

/** Convenience setters: construct a scalar value and set it under `key` (taking
 *  care of ownership). Return true on success, false on allocation failure. */
VIBE_API bool vibe_object_set_string(VibeObject* object, const char* key, const char* value);
VIBE_API bool vibe_object_set_int(VibeObject* object, const char* key, int64_t value);
VIBE_API bool vibe_object_set_float(VibeObject* object, const char* key, double value);
VIBE_API bool vibe_object_set_bool(VibeObject* object, const char* key, bool value);
VIBE_API bool vibe_object_set_null(VibeObject* object, const char* key);

/** Append `value` to `array`, taking ownership. Per the First Law, arrays hold
 *  scalars only: pushing an ARRAY or OBJECT is rejected. Returns true if the
 *  value was appended; returns false on a First-Law violation, allocation
 *  failure, or a NULL argument — in every failure case `value` is freed (so
 *  ownership is always consumed and a rejected push never leaks). */
VIBE_API bool vibe_array_push(VibeArray* array, VibeValue* value);

/** Element at `index` (not a copy), or NULL if out of range. */
VIBE_API VibeValue* vibe_array_get(VibeArray* array, size_t index);

/** Number of elements in an array (0 if NULL). */
VIBE_API size_t vibe_array_size(const VibeArray* array);

/** Remove and free the element at `index`, shifting later elements down.
 *  Returns true if an element was removed. */
VIBE_API bool vibe_array_remove(VibeArray* array, size_t index);

/** Remove and free every element, leaving an empty array. */
VIBE_API void vibe_array_clear(VibeArray* array);

/** Convenience appenders for scalar elements. Return true on success. */
VIBE_API bool vibe_array_push_string(VibeArray* array, const char* value);
VIBE_API bool vibe_array_push_int(VibeArray* array, int64_t value);
VIBE_API bool vibe_array_push_float(VibeArray* array, double value);
VIBE_API bool vibe_array_push_bool(VibeArray* array, bool value);

/* ============================================================================
 * EMITTING  (serialise a value tree back to canonical VIBE text)
 * ============================================================================ */

/** Serialise a value to canonical VIBE text. The root should be an OBJECT (its
 *  entries are emitted at the document level); any value type is accepted. The
 *  result is heap-allocated — free it with vibe_free(). NULL on failure. */
VIBE_API char* vibe_emit(const VibeValue* value);

/** Emit to a file. On POSIX the write is ATOMIC (temp file + fsync + rename),
 *  so a crash or write error never leaves the target truncated or partial — it
 *  keeps either the old content or the fully-written new content. Returns true
 *  on success. */
VIBE_API bool vibe_emit_file(const VibeValue* value, const char* path);

/* ============================================================================
 * MISC
 * ============================================================================ */

/** Human-readable name of a type, e.g. "object". Never NULL. */
VIBE_API const char* vibe_type_name(VibeType type);

/** Recursively free a value and everything it owns. NULL-safe. */
VIBE_API void vibe_value_free(VibeValue* value);

/** Pretty-print a value to stdout (debug aid; not canonical VIBE). */
VIBE_API void vibe_value_print(VibeValue* value, int indent);

#ifdef __cplusplus
}  /* extern "C" */
#endif

/* ============================================================================
 * ============================================================================
 *  IMPLEMENTATION
 * ============================================================================
 * Everything below compiles only where VIBE_IMPLEMENTATION is defined (once).
 * ============================================================================ */
#ifdef VIBE_IMPLEMENTATION

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
/* _POSIX_C_SOURCE is defined above, so stat()/S_ISREG are declared even under a
 * strict -std=c11. Used to reject non-regular files (e.g. directories) in
 * vibe_parse_file(). */
#  include <sys/stat.h>
#  include <unistd.h>   /* fsync, write, close, getpid */
#  include <fcntl.h>    /* open, O_* */
#  if defined(S_ISREG)
#    define VIBE_HAVE_STAT 1
#  endif
/* Atomic file replace (temp + fsync + rename) is available on POSIX. */
#  define VIBE_HAVE_ATOMIC_WRITE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Initial capacity for objects/arrays; they grow geometrically. */
#define VIBE_INITIAL_CAPACITY 16

/* Below this many keys, a linear scan beats hashing (cache-friendly, no index
 * allocation). At or above it, objects build and maintain a hash index. */
#ifndef VIBE_OBJECT_HASH_THRESHOLD
#define VIBE_OBJECT_HASH_THRESHOLD 8
#endif

/* Hard ceiling on how deep the recursive tree walkers (emit, clone, equals,
 * print) will descend before failing closed. The value-builder API lets a
 * caller nest objects arbitrarily deep — past the parser's max_depth — so
 * without this a pathological tree would overflow the C stack (a crash / DoS).
 * vibe_value_free is exempt: it uses an explicit heap worklist and never
 * recurses, so it always frees any depth. Override at compile time if you
 * genuinely need deeper trees AND run with a large stack. */
#ifndef VIBE_MAX_RECURSION_DEPTH
#define VIBE_MAX_RECURSION_DEPTH 10000
#endif

/* ============================================================================
 * Allocator hooks
 * ============================================================================
 * All heap traffic routes through these. A custom allocator installed via
 * vibe_set_allocators() governs both allocation and freeing of the value tree,
 * which is why it must be installed before any value is created. Compile-time
 * overrides (VIBE_MALLOC/REALLOC/FREE) win and cannot be changed at runtime. */
#if defined(VIBE_MALLOC) || defined(VIBE_REALLOC) || defined(VIBE_FREE)
#  if !defined(VIBE_MALLOC) || !defined(VIBE_REALLOC) || !defined(VIBE_FREE)
#    error "Define all of VIBE_MALLOC, VIBE_REALLOC and VIBE_FREE, or none."
#  endif
static void* (*g_malloc)(size_t) = NULL;
static void* (*g_realloc)(void*, size_t) = NULL;
static void  (*g_free)(void*) = NULL;
#  define VIBE_HAVE_FIXED_ALLOC 1
#else
static void* (*g_malloc)(size_t) = malloc;
static void* (*g_realloc)(void*, size_t) = realloc;
static void  (*g_free)(void*) = free;
#endif

void vibe_set_allocators(void* (*malloc_fn)(size_t),
                         void* (*realloc_fn)(void*, size_t),
                         void  (*free_fn)(void*)) {
#ifdef VIBE_HAVE_FIXED_ALLOC
    (void)malloc_fn; (void)realloc_fn; (void)free_fn; /* compile-time fixed */
#else
    if (malloc_fn)  g_malloc  = malloc_fn;
    if (realloc_fn) g_realloc = realloc_fn;
    if (free_fn)    g_free    = free_fn;
#endif
}

#ifdef VIBE_MALLOC
#  define VIBE__MALLOC(n)     VIBE_MALLOC(n)
#  define VIBE__REALLOC(p, n) VIBE_REALLOC((p), (n))
#  define VIBE__FREE(p)       VIBE_FREE(p)
#else
#  define VIBE__MALLOC(n)     (g_malloc ? g_malloc(n) : malloc(n))
#  define VIBE__REALLOC(p, n) (g_realloc ? g_realloc((p), (n)) : realloc((p), (n)))
#  define VIBE__FREE(p)       (g_free ? g_free(p) : free(p))
#endif

static void* vibe__malloc(size_t size) { return VIBE__MALLOC(size); }
static void* vibe__calloc(size_t n, size_t size) {
    size_t total = n * size;
    if (n != 0 && total / n != size) return NULL; /* overflow */
    void* p = VIBE__MALLOC(total);
    if (p) memset(p, 0, total);
    return p;
}
static void* vibe__realloc(void* ptr, size_t size) { return VIBE__REALLOC(ptr, size); }
static void  vibe__free(void* ptr) { VIBE__FREE(ptr); }

/* Guard against size_t overflow in the count*size product used for every array
 * allocation. Returns 0 (an impossible byte count for a non-empty request) when
 * n*size would wrap, so callers can treat 0 as "refuse the allocation". */
static size_t vibe__mul_size(size_t n, size_t size) {
    if (n != 0 && size != 0 && n > SIZE_MAX / size) return 0;
    return n * size;
}

/* realloc `ptr` to hold `count` elements of `elem` bytes, refusing (returns
 * NULL, leaves the original block intact) any request that would overflow. */
static void* vibe__realloc_array(void* ptr, size_t count, size_t elem) {
    size_t bytes = vibe__mul_size(count, elem);
    if (bytes == 0) return NULL;   /* overflow or zero-size: refuse */
    return vibe__realloc(ptr, bytes);
}

/* Grow a capacity by doubling, but never overflow: caps growth so that
 * new_cap * elem still fits in size_t. Returns 0 if even the minimum next step
 * cannot be represented (caller must then refuse to grow). */
static size_t vibe__grow_cap(size_t cap, size_t want_min, size_t elem) {
    size_t max_cap = elem ? (SIZE_MAX / elem) : SIZE_MAX;
    if (want_min > max_cap) return 0;               /* can't even hold the ask */
    size_t ncap = cap ? cap : VIBE_INITIAL_CAPACITY;
    while (ncap < want_min) {
        if (ncap > max_cap / 2) { ncap = want_min; break; } /* doubling would overflow */
        ncap *= 2;
    }
    if (ncap > max_cap) ncap = max_cap;
    return ncap;
}

static char* vibe__strndup(const char* s, size_t n) {
    char* d = (char*)vibe__malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}
static char* vibe__strdup(const char* s) {
    return vibe__strndup(s, strlen(s));
}

/* FNV-1a over a NUL-terminated key. Fast, good spread for short config keys. */
static uint32_t vibe__hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* Strict UTF-8 decode of the codepoint starting at s[0] (len bytes remain).
 * On success returns the number of bytes consumed (1..4) and writes the code
 * point to *cp. On any ill-formed sequence — unexpected continuation byte,
 * truncation, overlong encoding, surrogate (U+D800..U+DFFF), or a value above
 * U+10FFFF — returns 0. This is the RFC 3629 well-formedness table, so the
 * parser rejects exactly what the spec forbids. */
static int vibe__utf8_decode(const unsigned char* s, size_t len, uint32_t* cp) {
    unsigned char b0 = s[0];
    if (b0 < 0x80) { *cp = b0; return 1; }            /* ASCII */

    /* Determine sequence length and the valid range of the second byte, which
     * is what rules out overlong forms and surrogates without extra checks. */
    unsigned char lo, hi;
    int n;
    if (b0 >= 0xC2 && b0 <= 0xDF)      { n = 2; lo = 0x80; hi = 0xBF; }
    else if (b0 == 0xE0)              { n = 3; lo = 0xA0; hi = 0xBF; } /* no overlong */
    else if (b0 >= 0xE1 && b0 <= 0xEC) { n = 3; lo = 0x80; hi = 0xBF; }
    else if (b0 == 0xED)             { n = 3; lo = 0x80; hi = 0x9F; } /* no surrogate */
    else if (b0 >= 0xEE && b0 <= 0xEF) { n = 3; lo = 0x80; hi = 0xBF; }
    else if (b0 == 0xF0)             { n = 4; lo = 0x90; hi = 0xBF; } /* no overlong */
    else if (b0 >= 0xF1 && b0 <= 0xF3) { n = 4; lo = 0x80; hi = 0xBF; }
    else if (b0 == 0xF4)             { n = 4; lo = 0x80; hi = 0x8F; } /* <= U+10FFFF */
    else return 0;                    /* 0x80..0xC1, 0xF5..0xFF: never a lead */

    if (len < (size_t)n) return 0;    /* truncated */
    if (s[1] < lo || s[1] > hi) return 0;
    for (int i = 2; i < n; i++)
        if (s[i] < 0x80 || s[i] > 0xBF) return 0;

    uint32_t c = b0 & (0x7Fu >> n);
    for (int i = 1; i < n; i++) c = (c << 6) | (s[i] & 0x3Fu);
    *cp = c;
    return n;
}

/* Encode code point cp (already validated, BMP for \u) as UTF-8 into out[0..3].
 * Returns the number of bytes written (1..4). */
static int vibe__utf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ============================================================================
 * Defaults, error strings, type names
 * ============================================================================ */

const char* vibe_version(void) { return VIBE_VERSION_STRING; }
int vibe_version_number(void) { return VIBE_VERSION_NUMBER; }
const char* vibe_format_version(void) { return VIBE_FORMAT_VERSION; }

/* Free a buffer returned by the library (e.g. vibe_emit). Routes through the
 * active allocator so callers never need to know which free() to use. */
void vibe_free(void* ptr) { vibe__free(ptr); }

/* Allocate through the active allocator. Pair with vibe_free(). */
void* vibe_malloc(size_t size) { return vibe__malloc(size); }

VibeLimits vibe_default_limits(void) {
    VibeLimits limits;
    limits.max_document_size = 16 * 1024 * 1024;
    limits.max_depth         = 64;
    limits.max_string_length = 1024 * 1024;
    limits.max_key_length    = 1024;
    limits.max_array_elements = 65536;
    limits.max_object_keys   = 65536;
    limits.max_number_digits = 1024;
    return limits;
}

const char* vibe_error_code_string(VibeErrorCode code) {
    switch (code) {
        case VIBE_OK:                      return "ok";
        case VIBE_ERROR_UNEXPECTED_TOKEN:  return "unexpected-token";
        case VIBE_ERROR_UNCLOSED_OBJECT:   return "unclosed-object";
        case VIBE_ERROR_UNCLOSED_ARRAY:    return "unclosed-array";
        case VIBE_ERROR_UNTERMINATED_STRING: return "unterminated-string";
        case VIBE_ERROR_NESTED_CONTAINER:  return "nested-container";
        case VIBE_ERROR_INVALID_ESCAPE:    return "invalid-escape";
        case VIBE_ERROR_ILLEGAL_CHARACTER: return "illegal-character";
        case VIBE_ERROR_ENCODING:          return "encoding-error";
        case VIBE_ERROR_INVALID_NUMBER:    return "invalid-number";
        case VIBE_ERROR_LIMIT_EXCEEDED:    return "limit-exceeded";
        case VIBE_ERROR_IO:                return "io-error";
        case VIBE_ERROR_OUT_OF_MEMORY:     return "out-of-memory";
    }
    return "unknown-error";
}

const char* vibe_type_name(VibeType type) {
    switch (type) {
        case VIBE_TYPE_NULL:    return "null";
        case VIBE_TYPE_INTEGER: return "integer";
        case VIBE_TYPE_FLOAT:   return "float";
        case VIBE_TYPE_BOOLEAN: return "boolean";
        case VIBE_TYPE_STRING:  return "string";
        case VIBE_TYPE_ARRAY:   return "array";
        case VIBE_TYPE_OBJECT:  return "object";
    }
    return "unknown";
}

/* ============================================================================
 * Lexer / parser internal types
 * ============================================================================ */

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_BOOLEAN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_NEWLINE,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char* value;
    long line;
    long column;
} Token;

typedef enum {
    STATE_ROOT,
    STATE_OBJECT,
    STATE_ARRAY
} ParseState;

typedef struct {
    ParseState state;
    VibeValue* container;
} StateFrame;

/* Forward declarations */
static void set_error(VibeParser* parser, VibeErrorCode code, const char* fmt, ...);
static bool is_identifier_start(int c);
static bool is_identifier_char(int c);
static bool is_unquoted_string_char(int c);
static bool is_unquoted_start_char(int c);
static bool is_valid_number(const char* str);
static Token next_token(VibeParser* parser);
static void token_free(Token* token);
static VibeValue* parse_value_from_token(Token* token);

/* ============================================================================
 * Parser lifecycle
 * ============================================================================ */

VibeParser* vibe_parser_new(void) {
    VibeParser* parser = (VibeParser*)vibe__calloc(1, sizeof(VibeParser));
    if (!parser) return NULL;
    parser->line = 1;
    parser->column = 1;
    parser->limits = vibe_default_limits();
    return parser;
}

void vibe_parser_free(VibeParser* parser) {
    if (!parser) return;
    vibe__free(parser->error.message);
    vibe__free(parser);
}

void vibe_parser_set_limits(VibeParser* parser, const VibeLimits* limits) {
    if (!parser) return;
    parser->limits = limits ? *limits : vibe_default_limits();
}

/* ============================================================================
 * Error handling
 * ============================================================================ */

static void set_error(VibeParser* parser, VibeErrorCode code, const char* fmt, ...) {
    if (parser->error.has_error) return; /* keep the first error */

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    parser->error.has_error = true;
    parser->error.code = code;
    vibe__free(parser->error.message);
    parser->error.message = vibe__strdup(buffer);
    /* VibeError.line/column are int; clamp so a document larger than INT_MAX
     * bytes (only reachable with a raised max_document_size) can't overflow. */
    parser->error.line = (int)(parser->line > INT_MAX ? INT_MAX : parser->line);
    parser->error.column = (int)(parser->column > INT_MAX ? INT_MAX : parser->column);
}

VibeError vibe_get_last_error(VibeParser* parser) {
    if (!parser) {
        VibeError e;
        memset(&e, 0, sizeof(e));
        return e;
    }
    return parser->error;
}

void vibe_error_free(VibeError* error) {
    if (error && error->message) {
        vibe__free(error->message);
        error->message = NULL;
    }
}

/* ============================================================================
 * Character classification (all take int; callers pass unsigned-char values)
 * ============================================================================ */

static bool is_identifier_start(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_identifier_char(int c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

/* Printable ASCII 0x21..0x7E, excluding the structural characters and the
 * double quote (which always begins a quoted string, so it can never appear
 * literally inside a bare token). */
static bool is_unquoted_string_char(int c) {
    unsigned char uc = (unsigned char)c;
    if (uc <= 0x20 || uc > 0x7E) return false;
    if (uc == '{' || uc == '}' || uc == '[' || uc == ']' || uc == '#' || uc == '"') return false;
    return true;
}

/* The characters that may BEGIN an unquoted token (mirrors next_token). */
static bool is_unquoted_start_char(int c) {
    return is_identifier_start(c) || isdigit((unsigned char)c) ||
           c == '-' || c == '/' || c == '.' || c == '~';
}

static bool is_valid_number(const char* str) {
    if (!str || *str == '\0') return false;
    const char* p = str;
    if (*p == '-') p++;
    if (!isdigit((unsigned char)*p)) return false;

    bool has_dot = false;
    bool has_digit_after_dot = false;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            if (has_dot) has_digit_after_dot = true;
            p++;
        } else if (*p == '.') {
            if (has_dot) return false;
            has_dot = true;
            p++;
        } else {
            return false;
        }
    }
    if (has_dot && !has_digit_after_dot) return false;
    return true;
}

/* ============================================================================
 * Lexer
 * ============================================================================ */

static void skip_whitespace(VibeParser* parser) {
    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            parser->pos++;
            parser->column++;
        } else {
            break;
        }
    }
}

static void skip_comment(VibeParser* parser) {
    if (parser->pos < parser->length && parser->input[parser->pos] == '#') {
        while (parser->pos < parser->length && parser->input[parser->pos] != '\n') {
            parser->pos++;
        }
    }
}

/* Read a quoted string into a dynamically grown buffer. Enforces the decoded
 * string-length limit, rejects raw control characters (all C0 except tab, plus
 * DEL) and ill-formed UTF-8, and decodes escapes including the VIBE 1.1
 * \uXXXX form. Returns a heap string the caller owns, or NULL on error (with
 * the parser error set). */
static char* parse_quoted_string(VibeParser* parser) {
    parser->pos++; /* opening quote */
    parser->column++;

    size_t cap = 64;
    size_t len = 0;
    char* buf = (char*)vibe__malloc(cap);
    if (!buf) {
        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }

    /* Append n bytes, growing as needed. Sets an OOM error and jumps to fail. */
#define VIBE_SB_APPEND(bytes, n)                                              \
    do {                                                                      \
        size_t need_ = (n);                                                   \
        if (need_ + 1 > cap - len) {  /* len + need_ + 1 > cap, no overflow */ \
            if (need_ > SIZE_MAX - 1 - len) {                                  \
                set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "String too large"); goto fail; \
            }                                                                 \
            size_t ncap_ = cap;                                              \
            while (need_ + 1 > ncap_ - len) {                                 \
                if (ncap_ > SIZE_MAX / 2) { ncap_ = len + need_ + 1; break; }  \
                ncap_ *= 2;                                                   \
            }                                                                 \
            char* nb_ = (char*)vibe__realloc(buf, ncap_);                     \
            if (!nb_) { set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory"); goto fail; } \
            buf = nb_; cap = ncap_;                                          \
        }                                                                     \
        memcpy(buf + len, (bytes), need_);                                    \
        len += need_;                                                         \
    } while (0)

    while (parser->pos < parser->length) {
        unsigned char c = (unsigned char)parser->input[parser->pos];

        if (len > parser->limits.max_string_length) {
            set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                      "String exceeds the maximum length of %zu bytes",
                      parser->limits.max_string_length);
            goto fail;
        }

        if (c == '"') {
            parser->pos++;
            parser->column++;
            buf[len] = '\0';
            return buf;
        }

        if (c == '\\') {
            if (parser->pos + 1 >= parser->length) break; /* trailing backslash -> unterminated */
            char next = parser->input[parser->pos + 1];
            char out;
            switch (next) {
                case '"':  out = '"';  break;
                case '\\': out = '\\'; break;
                case 'n':  out = '\n'; break;
                case 't':  out = '\t'; break;
                case 'r':  out = '\r'; break;
                case 'u': {
                    /* \uXXXX — exactly four hex digits, BMP code point (VIBE 1.1). */
                    if (parser->pos + 6 > parser->length) {
                        set_error(parser, VIBE_ERROR_INVALID_ESCAPE,
                                  "Truncated \\u escape (needs four hex digits)");
                        goto fail;
                    }
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = parser->input[parser->pos + 2 + i];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (uint32_t)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (uint32_t)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (uint32_t)(h - 'A' + 10);
                        else {
                            set_error(parser, VIBE_ERROR_INVALID_ESCAPE,
                                      "Invalid hex digit '%c' in \\u escape", h);
                            goto fail;
                        }
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) {
                        set_error(parser, VIBE_ERROR_INVALID_ESCAPE,
                                  "\\u escape encodes a UTF-16 surrogate (U+%04X); write the character directly", cp);
                        goto fail;
                    }
                    char enc[4];
                    int en = vibe__utf8_encode(cp, enc);
                    VIBE_SB_APPEND(enc, (size_t)en);
                    parser->pos += 6;
                    parser->column += 6;
                    continue;
                }
                default:
                    set_error(parser, VIBE_ERROR_INVALID_ESCAPE,
                              "Invalid escape sequence '\\%c'", next);
                    goto fail;
            }
            VIBE_SB_APPEND(&out, 1);
            parser->pos += 2;
            parser->column += 2;
            continue;
        }

        if (c == '\n') {
            set_error(parser, VIBE_ERROR_UNTERMINATED_STRING,
                      "Unterminated string (newline before closing quote)");
            goto fail;
        }

        /* Reject raw control characters: C0 (U+0001..U+001F) except tab, and
         * DEL (U+007F). These must be written as escapes, not embedded raw. */
        if ((c < 0x20 && c != '\t') || c == 0x7F) {
            set_error(parser, VIBE_ERROR_ILLEGAL_CHARACTER,
                      "Raw control character U+%04X in string; use an escape sequence", c);
            goto fail;
        }

        if (c < 0x80) {
            VIBE_SB_APPEND((const char*)&c, 1);
            parser->pos++;
            parser->column++;
        } else {
            /* Multi-byte UTF-8: validate and copy the whole sequence verbatim. */
            uint32_t cp;
            int n = vibe__utf8_decode((const unsigned char*)parser->input + parser->pos,
                                      parser->length - parser->pos, &cp);
            if (n == 0) {
                set_error(parser, VIBE_ERROR_ENCODING,
                          "Ill-formed UTF-8 byte sequence in string (byte 0x%02X)", c);
                goto fail;
            }
            VIBE_SB_APPEND(parser->input + parser->pos, (size_t)n);
            parser->pos += n;
            parser->column++; /* one column per code point */
        }
    }

    set_error(parser, VIBE_ERROR_UNTERMINATED_STRING,
              "Unterminated string (end of input before closing quote)");
fail:
    vibe__free(buf);
    return NULL;
#undef VIBE_SB_APPEND
}

static Token next_token(VibeParser* parser) {
    Token token;
    memset(&token, 0, sizeof(token));

    skip_whitespace(parser);
    skip_comment(parser);
    skip_whitespace(parser);

    if (parser->pos >= parser->length) {
        token.type = TOKEN_EOF;
        return token;
    }

    char c = parser->input[parser->pos];
    token.line = parser->line;
    token.column = parser->column;

    if (c == '\n') {
        token.type = TOKEN_NEWLINE;
        parser->pos++;
        parser->line++;
        parser->column = 1;
        return token;
    }
    if (c == '{') { token.type = TOKEN_LEFT_BRACE;    parser->pos++; parser->column++; return token; }
    if (c == '}') { token.type = TOKEN_RIGHT_BRACE;   parser->pos++; parser->column++; return token; }
    if (c == '[') { token.type = TOKEN_LEFT_BRACKET;  parser->pos++; parser->column++; return token; }
    if (c == ']') { token.type = TOKEN_RIGHT_BRACKET; parser->pos++; parser->column++; return token; }

    if (c == '"') {
        token.type = TOKEN_STRING;
        token.value = parse_quoted_string(parser);
        if (!token.value) token.type = TOKEN_ERROR;
        return token;
    }

    if (is_unquoted_start_char(c)) {
        const char* input = parser->input;
        size_t len = parser->length;
        size_t start = parser->pos;
        size_t pos = start;

        /* A leading '-' immediately followed by a digit is part of a number. */
        if (c == '-' && pos + 1 < len && isdigit((unsigned char)input[pos + 1])) {
            pos++;
        }

        /* Scan the token in one tight loop over locals (no per-byte writes to
         * parser->pos/column), and classify ON THE FLY: track whether every
         * byte so far is an identifier char so we never re-walk the token to
         * decide IDENTIFIER vs STRING. */
        bool all_ident = is_identifier_start((unsigned char)input[start]);
        while (pos < len) {
            unsigned char ch = (unsigned char)input[pos];
            if (!is_unquoted_string_char(ch)) break;
            if (all_ident && !is_identifier_char(ch)) all_ident = false;
            pos++;
        }
        size_t tlen = pos - start;

        /* Advance the parser cursor once. Column tracking is per-byte for ASCII
         * tokens; a token can only contain multi-byte UTF-8 when it is a STRING
         * (identifiers/numbers are ASCII), and column is a diagnostic aid, so
         * counting bytes here is acceptable and matches the previous behaviour
         * for the common ASCII case. */
        parser->pos = pos;
        parser->column += tlen;

        token.value = vibe__strndup(input + start, tlen);
        if (!token.value) {
            set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
            token.type = TOKEN_ERROR;
            return token;
        }

        /* Classify. `true`/`false` are the only keywords; check length first so
         * the strcmp only runs on 4/5-byte tokens. */
        if ((tlen == 4 && memcmp(token.value, "true", 4) == 0) ||
            (tlen == 5 && memcmp(token.value, "false", 5) == 0)) {
            token.type = TOKEN_BOOLEAN;
        } else if (is_valid_number(token.value)) {
            if (tlen > parser->limits.max_number_digits) {
                set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                          "Numeric token exceeds the maximum of %zu characters",
                          parser->limits.max_number_digits);
                token_free(&token);
                token.type = TOKEN_ERROR;
                return token;
            }
            token.type = TOKEN_NUMBER;
        } else {
            /* all_ident was computed during the scan; no second walk. */
            token.type = all_ident ? TOKEN_IDENTIFIER : TOKEN_STRING;
        }
        return token;
    }

    set_error(parser, VIBE_ERROR_ILLEGAL_CHARACTER, "Unexpected character '%c' (0x%02X)",
              (c >= 0x20 && c < 0x7F) ? c : '?', (unsigned char)c);
    token.type = TOKEN_ERROR;
    return token;
}

static void token_free(Token* token) {
    if (token && token->value) {
        vibe__free(token->value);
        token->value = NULL;
    }
}

/* ============================================================================
 * Value constructors
 * ============================================================================ */

static VibeValue* value_alloc(VibeType type) {
    VibeValue* v = (VibeValue*)vibe__calloc(1, sizeof(VibeValue));
    if (v) v->type = type;
    return v;
}

VibeValue* vibe_value_new_null(void) { return value_alloc(VIBE_TYPE_NULL); }

VibeValue* vibe_value_new_integer(int64_t value) {
    VibeValue* v = value_alloc(VIBE_TYPE_INTEGER);
    if (v) v->as_integer = value;
    return v;
}

VibeValue* vibe_value_new_float(double value) {
    VibeValue* v = value_alloc(VIBE_TYPE_FLOAT);
    if (v) v->as_float = value;
    return v;
}

VibeValue* vibe_value_new_boolean(bool value) {
    VibeValue* v = value_alloc(VIBE_TYPE_BOOLEAN);
    if (v) v->as_boolean = value;
    return v;
}

VibeValue* vibe_value_new_string_len(const char* value, size_t length) {
    VibeValue* v = value_alloc(VIBE_TYPE_STRING);
    if (!v) return NULL;
    v->as_string = vibe__strndup(value ? value : "", value ? length : 0);
    if (!v->as_string) { vibe__free(v); return NULL; }
    return v;
}

VibeValue* vibe_value_new_string(const char* value) {
    return vibe_value_new_string_len(value, value ? strlen(value) : 0);
}

VibeValue* vibe_value_new_array(void) {
    VibeValue* v = value_alloc(VIBE_TYPE_ARRAY);
    if (!v) return NULL;
    v->as_array = (VibeArray*)vibe__calloc(1, sizeof(VibeArray));
    if (!v->as_array) { vibe__free(v); return NULL; }
    v->as_array->capacity = VIBE_INITIAL_CAPACITY;
    v->as_array->values = (VibeValue**)vibe__calloc(VIBE_INITIAL_CAPACITY, sizeof(VibeValue*));
    if (!v->as_array->values) { vibe__free(v->as_array); vibe__free(v); return NULL; }
    return v;
}

VibeValue* vibe_value_new_object(void) {
    VibeValue* v = value_alloc(VIBE_TYPE_OBJECT);
    if (!v) return NULL;
    v->as_object = (VibeObject*)vibe__calloc(1, sizeof(VibeObject));
    if (!v->as_object) { vibe__free(v); return NULL; }
    v->as_object->capacity = VIBE_INITIAL_CAPACITY;
    v->as_object->entries = (VibeObjectEntry*)vibe__calloc(VIBE_INITIAL_CAPACITY, sizeof(VibeObjectEntry));
    if (!v->as_object->entries) { vibe__free(v->as_object); vibe__free(v); return NULL; }
    return v;
}

static VibeValue* vibe__value_clone(const VibeValue* value, int depth) {
    if (!value) return NULL;
    if (depth > VIBE_MAX_RECURSION_DEPTH) return NULL; /* fail closed on deep trees */
    switch (value->type) {
        case VIBE_TYPE_NULL:    return vibe_value_new_null();
        case VIBE_TYPE_INTEGER: return vibe_value_new_integer(value->as_integer);
        case VIBE_TYPE_FLOAT:   return vibe_value_new_float(value->as_float);
        case VIBE_TYPE_BOOLEAN: return vibe_value_new_boolean(value->as_boolean);
        case VIBE_TYPE_STRING:  return vibe_value_new_string(value->as_string);
        case VIBE_TYPE_ARRAY: {
            VibeValue* arr = vibe_value_new_array();
            if (!arr) return NULL;
            for (size_t i = 0; i < value->as_array->count; i++) {
                VibeValue* el = vibe__value_clone(value->as_array->values[i], depth + 1);
                if (!el) { vibe_value_free(arr); return NULL; }
                if (!vibe_array_push(arr->as_array, el)) { vibe_value_free(arr); return NULL; }
            }
            return arr;
        }
        case VIBE_TYPE_OBJECT: {
            VibeValue* obj = vibe_value_new_object();
            if (!obj) return NULL;
            for (size_t i = 0; i < value->as_object->count; i++) {
                VibeValue* cv = vibe__value_clone(value->as_object->entries[i].value, depth + 1);
                if (!cv) { vibe_value_free(obj); return NULL; }
                if (!vibe_object_set(obj->as_object, value->as_object->entries[i].key, cv)) {
                    vibe_value_free(obj); return NULL;
                }
            }
            return obj;
        }
    }
    return NULL;
}

VibeValue* vibe_value_clone(const VibeValue* value) {
    return vibe__value_clone(value, 0);
}

static bool vibe__value_equals(const VibeValue* a, const VibeValue* b, int depth) {
    if (a == b) return true;              /* same pointer, incl. both NULL */
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    /* Fail closed on pathologically deep trees: refuse to descend further and
     * report "not equal" rather than overflow the C stack. */
    if (depth > VIBE_MAX_RECURSION_DEPTH) return false;
    switch (a->type) {
        case VIBE_TYPE_NULL:    return true;
        case VIBE_TYPE_INTEGER: return a->as_integer == b->as_integer;
        case VIBE_TYPE_FLOAT:
            /* Bitwise-identical doubles are equal; this makes NaN==NaN true and
             * keeps +0.0 != -0.0, which matches "same stored value". */
            return memcmp(&a->as_float, &b->as_float, sizeof(double)) == 0;
        case VIBE_TYPE_BOOLEAN: return a->as_boolean == b->as_boolean;
        case VIBE_TYPE_STRING:  return strcmp(a->as_string, b->as_string) == 0;
        case VIBE_TYPE_ARRAY: {
            const VibeArray* x = a->as_array; const VibeArray* y = b->as_array;
            if (x->count != y->count) return false;
            for (size_t i = 0; i < x->count; i++)
                if (!vibe__value_equals(x->values[i], y->values[i], depth + 1)) return false;
            return true;
        }
        case VIBE_TYPE_OBJECT: {
            const VibeObject* x = a->as_object; const VibeObject* y = b->as_object;
            if (x->count != y->count) return false;
            /* Order-insensitive: two objects are equal as key/value sets. Uses
             * y's hash index when present, so this is O(n) not O(n^2). */
            for (size_t i = 0; i < x->count; i++) {
                VibeValue* yv = vibe_object_get((VibeObject*)y, x->entries[i].key);
                if (!yv || !vibe__value_equals(x->entries[i].value, yv, depth + 1)) return false;
            }
            return true;
        }
    }
    return false;
}

bool vibe_value_equals(const VibeValue* a, const VibeValue* b) {
    return vibe__value_equals(a, b, 0);
}

/* ============================================================================
 * Value inspection (type-safe, path-free)
 * ============================================================================ */

VibeType vibe_value_type(const VibeValue* v) { return v ? v->type : VIBE_TYPE_NULL; }

bool vibe_is_null(const VibeValue* v)    { return !v || v->type == VIBE_TYPE_NULL; }
bool vibe_is_integer(const VibeValue* v) { return v && v->type == VIBE_TYPE_INTEGER; }
bool vibe_is_float(const VibeValue* v)   { return v && v->type == VIBE_TYPE_FLOAT; }
bool vibe_is_boolean(const VibeValue* v) { return v && v->type == VIBE_TYPE_BOOLEAN; }
bool vibe_is_string(const VibeValue* v)  { return v && v->type == VIBE_TYPE_STRING; }
bool vibe_is_array(const VibeValue* v)   { return v && v->type == VIBE_TYPE_ARRAY; }
bool vibe_is_object(const VibeValue* v)  { return v && v->type == VIBE_TYPE_OBJECT; }

int64_t vibe_value_int(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_INTEGER) ? v->as_integer : 0;
}
double vibe_value_float(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_FLOAT) ? v->as_float : 0.0;
}
bool vibe_value_bool(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_BOOLEAN) ? v->as_boolean : false;
}
const char* vibe_value_string(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_STRING) ? v->as_string : NULL;
}
VibeArray* vibe_value_array(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_ARRAY) ? v->as_array : NULL;
}
VibeObject* vibe_value_object(const VibeValue* v) {
    return (v && v->type == VIBE_TYPE_OBJECT) ? v->as_object : NULL;
}

int64_t vibe_value_int_or(const VibeValue* v, int64_t fallback) {
    return (v && v->type == VIBE_TYPE_INTEGER) ? v->as_integer : fallback;
}
double vibe_value_float_or(const VibeValue* v, double fallback) {
    return (v && v->type == VIBE_TYPE_FLOAT) ? v->as_float : fallback;
}
bool vibe_value_bool_or(const VibeValue* v, bool fallback) {
    return (v && v->type == VIBE_TYPE_BOOLEAN) ? v->as_boolean : fallback;
}
const char* vibe_value_string_or(const VibeValue* v, const char* fallback) {
    return (v && v->type == VIBE_TYPE_STRING) ? v->as_string : fallback;
}

static VibeValue* parse_value_from_token(Token* token) {
    if (!token) return NULL;
    switch (token->type) {
        case TOKEN_BOOLEAN:
            return vibe_value_new_boolean(strcmp(token->value, "true") == 0);
        case TOKEN_NUMBER: {
            const char* s = token->value;
            char* endptr;
            /* Fast path for plain base-10 integers (the overwhelmingly common
             * case). next_token already validated the token as a number, so a
             * token containing '.', 'e' or 'E' is a float (-> strtod); anything
             * else is an integer we can accumulate directly, far cheaper than
             * strtoll. Out-of-range integers are rejected (matching ERANGE). */
            if (!strpbrk(s, ".eE")) {
                const char* p = s;
                bool neg = false;
                if (*p == '-') { neg = true; p++; }
                else if (*p == '+') { p++; }
                uint64_t acc = 0;
                bool ok = (*p != '\0');
                for (; *p; p++) {
                    if (*p < '0' || *p > '9') { ok = false; break; }
                    uint64_t prev = acc;
                    acc = acc * 10u + (uint64_t)(*p - '0');
                    if (acc < prev) { ok = false; break; }   /* overflow */
                }
                if (!ok) return NULL;                         /* malformed / overflow */
                if (neg) {
                    if (acc > (uint64_t)INT64_MAX + 1u) return NULL;
                    return vibe_value_new_integer(acc == (uint64_t)INT64_MAX + 1u
                        ? INT64_MIN : -(int64_t)acc);
                }
                if (acc > (uint64_t)INT64_MAX) return NULL;
                return vibe_value_new_integer((int64_t)acc);
            }
            /* Float: hand to strtod for correct rounding. */
            errno = 0;
            double val = strtod(s, &endptr);
            if (endptr == s || *endptr != '\0' || errno == ERANGE) return NULL;
            return vibe_value_new_float(val);
        }
        case TOKEN_STRING:
        case TOKEN_IDENTIFIER:
            return vibe_value_new_string(token->value);
        default:
            return NULL;
    }
}

/* ============================================================================
 * Object / array operations
 * ============================================================================
 * Objects keep an insertion-ordered `entries` array (canonical) plus an
 * optional open-addressed hash index that maps key-hash -> entry slot. The
 * index is built lazily once an object crosses VIBE_OBJECT_HASH_THRESHOLD, so
 * small objects pay nothing and large ones get O(1) lookups (no more O(n^2)
 * blow-up when parsing a big flat object). */

/* Round up to the next power of two, min 16. Saturates instead of overflowing:
 * for n beyond the largest representable power of two it returns that maximum. */
static size_t vibe__next_pow2(size_t n) {
    size_t top = ((size_t)1) << (sizeof(size_t) * 8 - 1); /* highest power of two */
    if (n > top) return top;
    size_t c = 16;
    while (c < n) c <<= 1;
    return c;
}

/* Insert entry #slot into an already-sized index using a precomputed hash. */
static void vibe__index_put_h(uint32_t* index, size_t cap, uint32_t hash, size_t slot) {
    size_t mask = cap - 1;
    size_t i = hash & mask;
    while (index[i] != 0) i = (i + 1) & mask;   /* linear probe */
    index[i] = (uint32_t)(slot + 1);            /* store slot+1 (0 = empty) */
}

/* Insert entry #slot into an already-sized index (no growth, no dup check). */
static void vibe__index_put(uint32_t* index, size_t cap, const char* key, size_t slot) {
    vibe__index_put_h(index, cap, vibe__hash(key), slot);
}

/* Build or rebuild obj->index to hold obj->count entries comfortably. */
static void vibe__obj_reindex(VibeObject* obj, size_t want) {
    /* Slots are stored as uint32_t (slot+1); beyond that range the index can't
     * address every entry, so fall back to the always-correct linear scan. */
    if (obj->count >= 0xFFFFFFFFu) {
        vibe__free(obj->index);
        obj->index = NULL;
        obj->index_cap = 0;
        return;
    }
    /* Size the table for ~4x the current entry count. A larger table means the
     * insert path trips "count*2 > cap" (a full rehash) only about half as
     * often, so over a big parse we do fewer O(n) rebuilds — at the cost of a
     * little more memory and a lower load factor (both fine here). */
    size_t want4 = (want > SIZE_MAX / 4) ? SIZE_MAX : want * 4;
    size_t cap = vibe__next_pow2(want4);
    uint32_t* index = (uint32_t*)vibe__calloc(cap, sizeof(uint32_t));
    if (!index) return;                         /* stay index-less; still correct */
    for (size_t s = 0; s < obj->count; s++)
        vibe__index_put(index, cap, obj->entries[s].key, s);
    vibe__free(obj->index);
    obj->index = index;
    obj->index_cap = cap;
}

/* Find the entry slot for key using a precomputed hash, or (size_t)-1. */
static size_t vibe__obj_find_h(const VibeObject* obj, const char* key, uint32_t hash) {
    if (obj->index) {
        size_t mask = obj->index_cap - 1;
        size_t i = hash & mask;
        for (;;) {
            uint32_t slot = obj->index[i];
            if (slot == 0) return (size_t)-1;
            if (strcmp(obj->entries[slot - 1].key, key) == 0) return slot - 1;
            i = (i + 1) & mask;
        }
    }
    for (size_t i = 0; i < obj->count; i++)
        if (strcmp(obj->entries[i].key, key) == 0) return i;
    return (size_t)-1;
}

/* Find the entry slot for key, or (size_t)-1. Uses the index when present. */
static size_t vibe__obj_find(const VibeObject* obj, const char* key) {
    return vibe__obj_find_h(obj, key, vibe__hash(key));
}

/* Core insert. `kdup` MUST be a heap key this function takes ownership of
 * (freed on any failure, and on a last-wins replace where it isn't stored).
 * `khash` is vibe__hash(kdup) precomputed by the caller so we never hash the
 * same key twice. vibe_object_set() is the public wrapper that dups the key. */
static bool vibe__object_put_owned(VibeObject* obj, char* kdup, uint32_t khash, VibeValue* value) {
    size_t found = vibe__obj_find_h(obj, kdup, khash);
    if (found != (size_t)-1) {
        vibe_value_free(obj->entries[found].value);
        obj->entries[found].value = value; /* last-wins */
        vibe__free(kdup);                  /* existing key kept; drop the dup */
        return true;
    }

    if (obj->count >= obj->capacity) {
        size_t ncap = vibe__grow_cap(obj->capacity, obj->count + 1, sizeof(VibeObjectEntry));
        VibeObjectEntry* ne = ncap ? (VibeObjectEntry*)vibe__realloc_array(obj->entries, ncap, sizeof(VibeObjectEntry)) : NULL;
        if (!ne) { vibe_value_free(value); vibe__free(kdup); return false; }
        obj->entries = ne;
        obj->capacity = ncap;
    }

    size_t slot = obj->count;
    obj->entries[slot].key = kdup;
    obj->entries[slot].value = value;
    obj->count++;

    /* Maintain the hash index once the object is big enough to benefit. */
    if (obj->index) {
        if (obj->count * 2 > obj->index_cap) vibe__obj_reindex(obj, obj->count);
        else vibe__index_put_h(obj->index, obj->index_cap, khash, slot);
    } else if (obj->count >= VIBE_OBJECT_HASH_THRESHOLD) {
        vibe__obj_reindex(obj, obj->count);
    }
    return true;
}

bool vibe_object_set(VibeObject* obj, const char* key, VibeValue* value) {
    if (!obj || !key || !value) { vibe_value_free(value); return false; }
    char* kdup = vibe__strdup(key);
    if (!kdup) { vibe_value_free(value); return false; }
    return vibe__object_put_owned(obj, kdup, vibe__hash(kdup), value);
}

VibeValue* vibe_object_get(VibeObject* obj, const char* key) {
    if (!obj || !key) return NULL;
    size_t slot = vibe__obj_find(obj, key);
    return slot == (size_t)-1 ? NULL : obj->entries[slot].value;
}

size_t vibe_object_size(const VibeObject* obj) { return obj ? obj->count : 0; }

bool vibe_object_has(const VibeObject* obj, const char* key) {
    return obj && key && vibe__obj_find(obj, key) != (size_t)-1;
}

const char* vibe_object_key_at(const VibeObject* obj, size_t index) {
    if (!obj || index >= obj->count) return NULL;
    return obj->entries[index].key;
}

VibeValue* vibe_object_value_at(const VibeObject* obj, size_t index) {
    if (!obj || index >= obj->count) return NULL;
    return obj->entries[index].value;
}

bool vibe_object_remove(VibeObject* obj, const char* key) {
    if (!obj || !key) return false;
    size_t slot = vibe__obj_find(obj, key);
    if (slot == (size_t)-1) return false;

    vibe__free(obj->entries[slot].key);
    vibe_value_free(obj->entries[slot].value);
    /* Shift later entries down to preserve insertion order. */
    memmove(&obj->entries[slot], &obj->entries[slot + 1],
            (obj->count - slot - 1) * sizeof(VibeObjectEntry));
    obj->count--;

    /* Slot numbers shifted, so the hash index is stale: rebuild it if we still
     * want one, else drop it (small objects fall back to linear scan). */
    if (obj->index) {
        vibe__free(obj->index);
        obj->index = NULL;
        obj->index_cap = 0;
        if (obj->count >= VIBE_OBJECT_HASH_THRESHOLD) vibe__obj_reindex(obj, obj->count);
    }
    return true;
}

bool vibe_object_set_string(VibeObject* obj, const char* key, const char* value) {
    VibeValue* v = vibe_value_new_string(value);
    if (!v) return false;
    return vibe_object_set(obj, key, v);
}
bool vibe_object_set_int(VibeObject* obj, const char* key, int64_t value) {
    VibeValue* v = vibe_value_new_integer(value);
    if (!v) return false;
    return vibe_object_set(obj, key, v);
}
bool vibe_object_set_float(VibeObject* obj, const char* key, double value) {
    VibeValue* v = vibe_value_new_float(value);
    if (!v) return false;
    return vibe_object_set(obj, key, v);
}
bool vibe_object_set_bool(VibeObject* obj, const char* key, bool value) {
    VibeValue* v = vibe_value_new_boolean(value);
    if (!v) return false;
    return vibe_object_set(obj, key, v);
}
bool vibe_object_set_null(VibeObject* obj, const char* key) {
    VibeValue* v = vibe_value_new_null();
    if (!v) return false;
    return vibe_object_set(obj, key, v);
}

bool vibe_array_push(VibeArray* arr, VibeValue* value) {
    if (!arr || !value) { vibe_value_free(value); return false; }
    /* The First Law: arrays hold scalars only. A container element is a bug in
     * the caller — refuse it rather than build a document the parser would
     * reject on the way back in. */
    if (value->type == VIBE_TYPE_ARRAY || value->type == VIBE_TYPE_OBJECT) {
        vibe_value_free(value);
        return false;
    }

    if (arr->count >= arr->capacity) {
        size_t ncap = vibe__grow_cap(arr->capacity, arr->count + 1, sizeof(VibeValue*));
        VibeValue** nv = ncap ? (VibeValue**)vibe__realloc_array(arr->values, ncap, sizeof(VibeValue*)) : NULL;
        if (!nv) { vibe_value_free(value); return false; }
        arr->values = nv;
        arr->capacity = ncap;
    }
    arr->values[arr->count++] = value;
    return true;
}

VibeValue* vibe_array_get(VibeArray* arr, size_t index) {
    if (!arr || index >= arr->count) return NULL;
    return arr->values[index];
}

size_t vibe_array_size(const VibeArray* arr) { return arr ? arr->count : 0; }

bool vibe_array_remove(VibeArray* arr, size_t index) {
    if (!arr || index >= arr->count) return false;
    vibe_value_free(arr->values[index]);
    memmove(&arr->values[index], &arr->values[index + 1],
            (arr->count - index - 1) * sizeof(VibeValue*));
    arr->count--;
    return true;
}

void vibe_array_clear(VibeArray* arr) {
    if (!arr) return;
    for (size_t i = 0; i < arr->count; i++) vibe_value_free(arr->values[i]);
    arr->count = 0;
}

bool vibe_array_push_string(VibeArray* arr, const char* value) {
    VibeValue* v = vibe_value_new_string(value);
    if (!v) return false;
    return vibe_array_push(arr, v);
}
bool vibe_array_push_int(VibeArray* arr, int64_t value) {
    VibeValue* v = vibe_value_new_integer(value);
    if (!v) return false;
    return vibe_array_push(arr, v);
}
bool vibe_array_push_float(VibeArray* arr, double value) {
    VibeValue* v = vibe_value_new_float(value);
    if (!v) return false;
    return vibe_array_push(arr, v);
}
bool vibe_array_push_bool(VibeArray* arr, bool value) {
    VibeValue* v = vibe_value_new_boolean(value);
    if (!v) return false;
    return vibe_array_push(arr, v);
}

/* ============================================================================
 * Cleanup
 * ============================================================================ */

/* Free a value tree WITHOUT recursion. A naive recursive free overflows the C
 * stack on a deeply nested tree (the value-builder API can nest objects past
 * the parser's max_depth). Instead we push every child container onto an
 * explicit heap worklist and free scalars/keys inline. If the worklist itself
 * can't grow (OOM), we fall back to a bounded recursive drain of that subtree
 * so we still make progress and never leak more than the current node's
 * children — in practice the worklist growth is geometric and effectively
 * never fails for a tree that fit in memory to begin with. */
void vibe_value_free(VibeValue* value) {
    if (!value) return;

    VibeValue** stack = NULL;
    size_t sp = 0, scap = 0;

    /* Push a container onto the worklist; returns false on OOM. */
    #define VIBE_FREE_PUSH(v)                                                 \
        (((sp < scap) ||                                                      \
          (scap = scap ? scap * 2 : 64,                                       \
           (stack = (VibeValue**)vibe__realloc(stack, scap * sizeof(*stack))) != NULL)) \
         ? (stack[sp++] = (v), true) : false)

    VibeValue* node = value;
    for (;;) {
        switch (node->type) {
            case VIBE_TYPE_STRING:
                vibe__free(node->as_string);
                break;
            case VIBE_TYPE_ARRAY:
                if (node->as_array) {
                    /* Arrays hold only scalars (the First Law), so free them
                     * inline — no container children to enqueue. */
                    for (size_t i = 0; i < node->as_array->count; i++)
                        vibe_value_free(node->as_array->values[i]);
                    vibe__free(node->as_array->values);
                    vibe__free(node->as_array);
                }
                break;
            case VIBE_TYPE_OBJECT:
                if (node->as_object) {
                    VibeObject* o = node->as_object;
                    for (size_t i = 0; i < o->count; i++) {
                        vibe__free(o->entries[i].key);
                        VibeValue* child = o->entries[i].value;
                        if (child &&
                            (child->type == VIBE_TYPE_OBJECT ||
                             child->type == VIBE_TYPE_ARRAY)) {
                            if (!VIBE_FREE_PUSH(child)) {
                                /* Worklist OOM: drain this child recursively.
                                 * Rare; bounded by remaining depth. */
                                vibe_value_free(child);
                            }
                        } else {
                            vibe_value_free(child); /* scalar / NULL: inline */
                        }
                    }
                    vibe__free(o->entries);
                    vibe__free(o->index);
                    vibe__free(o);
                }
                break;
            default:
                break;
        }
        vibe__free(node);
        if (sp == 0) break;
        node = stack[--sp];
    }

    #undef VIBE_FREE_PUSH
    vibe__free(stack);
}

/* ============================================================================
 * Core parse loop
 * ============================================================================ */

VibeValue* vibe_parse_buffer(VibeParser* parser, const char* data, size_t length) {
    if (!parser || !data) return NULL;

    /* Fresh parse: clear any prior error. */
    vibe__free(parser->error.message);
    parser->error.message = NULL;
    parser->error.has_error = false;
    parser->error.code = VIBE_OK;

    parser->input = data;
    parser->length = length;
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;

    if (length > parser->limits.max_document_size) {
        set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                  "Document exceeds the maximum size of %zu bytes",
                  parser->limits.max_document_size);
        return NULL;
    }

    /* Reject a leading UTF-8 byte order mark (U+FEFF). */
    if (length >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        set_error(parser, VIBE_ERROR_ENCODING,
                  "Byte order mark (U+FEFF) is not permitted; encode documents as plain UTF-8");
        return NULL;
    }

    /* Validate the ENTIRE byte stream up front: it must be well-formed UTF-8
     * with no embedded NUL. Doing it once here (a single linear pass) means the
     * lexer downstream can trust every byte — comments, whitespace, keys and
     * values are all covered, not just quoted-string bodies. Column/line are
     * tracked so the error points at the offending byte. */
    {
        long vline = 1, vcol = 1;
        size_t i = 0;
        while (i < length) {
            /* Fast path: gallop over runs of plain ASCII (no high bit, no NUL,
             * no newline) eight bytes at a time using SWAR bit tricks. This is
             * the common case — keys, numbers, ASCII values — so it keeps the
             * up-front validation nearly free. */
            while (i + 8 <= length) {
                uint64_t w;
                memcpy(&w, data + i, 8);
                /* high bit set in any byte? -> non-ASCII, fall to slow path */
                if (w & 0x8080808080808080ULL) break;
                /* any NUL byte? (Mycroft's test) */
                if ((w - 0x0101010101010101ULL) & ~w & 0x8080808080808080ULL) break;
                /* any newline byte? need per-byte line tracking, so bail */
                uint64_t nl = w ^ 0x0A0A0A0A0A0A0A0AULL;
                if ((nl - 0x0101010101010101ULL) & ~nl & 0x8080808080808080ULL) break;
                i += 8;
                if (vcol <= INT_MAX) vcol += 8;   /* saturate: diagnostics only */
            }
            if (i >= length) break;

            unsigned char c = (unsigned char)data[i];
            if (c == 0x00) {
                parser->line = vline; parser->column = vcol;
                set_error(parser, VIBE_ERROR_ILLEGAL_CHARACTER,
                          "Embedded NUL byte (U+0000) is not permitted in a document");
                return NULL;
            }
            if (c < 0x80) {
                if (c == '\n') { vline++; vcol = 1; } else { vcol++; }
                i++;
            } else {
                uint32_t cp;
                int n = vibe__utf8_decode((const unsigned char*)data + i, length - i, &cp);
                if (n == 0) {
                    parser->line = vline; parser->column = vcol;
                    set_error(parser, VIBE_ERROR_ENCODING,
                              "Ill-formed UTF-8 byte sequence (byte 0x%02X at offset %zu)", c, i);
                    return NULL;
                }
                i += (size_t)n;
                vcol++;
            }
        }
    }

    VibeValue* root = vibe_value_new_object();
    if (!root) {
        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }

    size_t nframes = parser->limits.max_depth + 2;
    StateFrame* frames = (StateFrame*)vibe__calloc(nframes, sizeof(StateFrame));
    if (!frames) {
        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
        vibe_value_free(root);
        return NULL;
    }
    int depth = 0;
    frames[0].state = STATE_ROOT;
    frames[0].container = root;

    char* current_key = NULL;

#define VIBE_FAIL()                       \
    do {                                  \
        vibe__free(current_key);          \
        vibe__free(frames);               \
        vibe_value_free(root);            \
        return NULL;                      \
    } while (0)

    for (;;) {
        Token token = next_token(parser);

        if (token.type == TOKEN_ERROR || parser->error.has_error) {
            token_free(&token);
            VIBE_FAIL();
        }

        if (token.type == TOKEN_EOF) {
            token_free(&token);
            if (depth > 0) {
                StateFrame* open = &frames[depth];
                if (open->state == STATE_ARRAY)
                    set_error(parser, VIBE_ERROR_UNCLOSED_ARRAY,
                              "Unexpected end of input: unclosed array (missing ']')");
                else
                    set_error(parser, VIBE_ERROR_UNCLOSED_OBJECT,
                              "Unexpected end of input: unclosed object (missing '}')");
                VIBE_FAIL();
            }
            break;
        }

        if (token.type == TOKEN_NEWLINE) { token_free(&token); continue; }

        StateFrame* frame = &frames[depth];

        if (frame->state == STATE_ROOT || frame->state == STATE_OBJECT) {
            /* Keys may be bare identifiers or quoted strings. */
            if (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_STRING) {
                /* Take ownership of the token's heap key buffer directly — no
                 * extra strdup. token_free() below must not free it, so we null
                 * token.value after the steal. */
                current_key = token.value;
                token.value = NULL;
                token_free(&token);
                if (!current_key) { set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory"); VIBE_FAIL(); }

                if (current_key[0] == '\0') {
                    set_error(parser, VIBE_ERROR_UNEXPECTED_TOKEN, "Empty key is not allowed");
                    VIBE_FAIL();
                }
                if (strlen(current_key) > parser->limits.max_key_length) {
                    set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                              "Key exceeds the maximum length of %zu bytes",
                              parser->limits.max_key_length);
                    VIBE_FAIL();
                }

                /* Hash the key ONCE and reuse it for the duplicate-key limit
                 * check and the insert below. */
                uint32_t khash = vibe__hash(current_key);

                VibeObject* obj = frame->container->as_object;
                if (obj->count >= parser->limits.max_object_keys &&
                    vibe__obj_find_h(obj, current_key, khash) == (size_t)-1) {
                    set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                              "Object exceeds the maximum of %zu keys",
                              parser->limits.max_object_keys);
                    VIBE_FAIL();
                }

                Token next = next_token(parser);
                if (parser->error.has_error) { token_free(&next); VIBE_FAIL(); }

                if (next.type == TOKEN_LEFT_BRACE) {
                    token_free(&next);
                    VibeValue* child = vibe_value_new_object();
                    if (!child) { set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory"); VIBE_FAIL(); }
                    if (!vibe__object_put_owned(obj, current_key, khash, child)) {
                        /* put freed both `child` and `current_key` */
                        current_key = NULL;
                        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
                        VIBE_FAIL();
                    }
                    current_key = NULL;   /* ownership moved into the object */
                    depth++;
                    if ((size_t)depth > parser->limits.max_depth) {
                        set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                                  "Nesting depth exceeds the maximum of %zu levels",
                                  parser->limits.max_depth);
                        VIBE_FAIL();
                    }
                    frames[depth].state = STATE_OBJECT;
                    frames[depth].container = child;
                } else if (next.type == TOKEN_LEFT_BRACKET) {
                    token_free(&next);
                    VibeValue* child = vibe_value_new_array();
                    if (!child) { set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory"); VIBE_FAIL(); }
                    if (!vibe__object_put_owned(obj, current_key, khash, child)) {
                        current_key = NULL;
                        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
                        VIBE_FAIL();
                    }
                    current_key = NULL;
                    depth++;
                    if ((size_t)depth > parser->limits.max_depth) {
                        set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                                  "Nesting depth exceeds the maximum of %zu levels",
                                  parser->limits.max_depth);
                        VIBE_FAIL();
                    }
                    frames[depth].state = STATE_ARRAY;
                    frames[depth].container = child;
                } else {
                    VibeValue* val = parse_value_from_token(&next);
                    if (!val) {
                        set_error(parser, VIBE_ERROR_INVALID_NUMBER,
                                  "Invalid value for key '%s' (number out of range or malformed token)",
                                  current_key);
                        token_free(&next);
                        VIBE_FAIL();
                    }
                    if (!vibe__object_put_owned(obj, current_key, khash, val)) {
                        current_key = NULL;
                        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
                        token_free(&next);
                        VIBE_FAIL();
                    }
                    current_key = NULL;
                    token_free(&next);
                }
            } else if (token.type == TOKEN_RIGHT_BRACE) {
                token_free(&token);
                if (depth > 0) {
                    depth--;
                } else {
                    set_error(parser, VIBE_ERROR_UNEXPECTED_TOKEN,
                              "Unexpected '}' with no matching '{'");
                    VIBE_FAIL();
                }
            } else {
                set_error(parser, VIBE_ERROR_UNEXPECTED_TOKEN,
                          "Unexpected token where a key was expected");
                token_free(&token);
                VIBE_FAIL();
            }
        } else { /* STATE_ARRAY */
            if (token.type == TOKEN_RIGHT_BRACKET) {
                token_free(&token);
                if (depth > 0) depth--;
            } else if (token.type == TOKEN_LEFT_BRACE) {
                set_error(parser, VIBE_ERROR_NESTED_CONTAINER,
                          "Objects cannot be placed inside arrays (the First Law of VIBE)");
                token_free(&token);
                VIBE_FAIL();
            } else if (token.type == TOKEN_LEFT_BRACKET) {
                set_error(parser, VIBE_ERROR_NESTED_CONTAINER,
                          "Arrays cannot be nested inside other arrays (the First Law of VIBE)");
                token_free(&token);
                VIBE_FAIL();
            } else {
                VibeArray* arr = frame->container->as_array;
                if (arr->count >= parser->limits.max_array_elements) {
                    set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                              "Array exceeds the maximum of %zu elements",
                              parser->limits.max_array_elements);
                    token_free(&token);
                    VIBE_FAIL();
                }
                VibeValue* val = parse_value_from_token(&token);
                if (!val) {
                    set_error(parser, VIBE_ERROR_INVALID_NUMBER,
                              "Invalid array element (number out of range or malformed token)");
                    token_free(&token);
                    VIBE_FAIL();
                }
                if (!vibe_array_push(arr, val)) {
                    set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory");
                    token_free(&token);
                    VIBE_FAIL();
                }
                token_free(&token);
            }
        }
    }

#undef VIBE_FAIL
    vibe__free(current_key);
    vibe__free(frames);
    return root;
}

VibeValue* vibe_parse_string(VibeParser* parser, const char* input) {
    if (!parser || !input) return NULL;
    return vibe_parse_buffer(parser, input, strlen(input));
}

VibeValue* vibe_parse_file(VibeParser* parser, const char* filename) {
    if (!parser || !filename) return NULL;

    vibe__free(parser->error.message);
    parser->error.message = NULL;
    parser->error.has_error = false;
    parser->error.code = VIBE_OK;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        set_error(parser, VIBE_ERROR_IO, "Cannot open file '%s'", filename);
        return NULL;
    }

    /* On POSIX, fopen() happily opens a directory; fseek/ftell then report a
     * bogus size and fread reads nothing, so a directory would silently parse
     * as an empty (valid) document. Reject anything that is not a regular file.
     * Uses path-based stat() to avoid fileno(), which is hidden under -std=c11. */
#if defined(VIBE_HAVE_STAT)
    {
        struct stat st;
        if (stat(filename, &st) == 0 && !S_ISREG(st.st_mode)) {
            fclose(file);
            set_error(parser, VIBE_ERROR_IO, "'%s' is not a regular file", filename);
            return NULL;
        }
    }
#endif

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(parser, VIBE_ERROR_IO, "Cannot seek in '%s' (not a regular file?)", filename);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        set_error(parser, VIBE_ERROR_IO, "Cannot determine size of '%s'", filename);
        return NULL;
    }
    if ((size_t)size > parser->limits.max_document_size) {
        fclose(file);
        set_error(parser, VIBE_ERROR_LIMIT_EXCEEDED,
                  "File '%s' exceeds the maximum size of %zu bytes",
                  filename, parser->limits.max_document_size);
        return NULL;
    }
    rewind(file);

    char* buffer = (char*)vibe__malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        set_error(parser, VIBE_ERROR_OUT_OF_MEMORY, "Out of memory reading '%s'", filename);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    int read_ok = (bytes_read == (size_t)size) || feof(file);
    fclose(file);
    if (!read_ok) {
        vibe__free(buffer);
        set_error(parser, VIBE_ERROR_IO, "Failed to read '%s' completely", filename);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    VibeValue* result = vibe_parse_buffer(parser, buffer, bytes_read);
    vibe__free(buffer);
    return result;
}

VibeValue* vibe_parse(const char* data, size_t length, VibeError* out_error) {
    VibeParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.line = 1;
    parser.column = 1;
    parser.limits = vibe_default_limits();

    VibeValue* root = vibe_parse_buffer(&parser, data, length);
    if (!root && out_error) {
        *out_error = parser.error;      /* transfer ownership of the message */
        parser.error.message = NULL;
    } else {
        vibe__free(parser.error.message);
    }
    return root;
}

/* ============================================================================
 * Path access
 * ============================================================================ */

VibeValue* vibe_get(VibeValue* root, const char* path) {
    if (!root || !path) return NULL;

    VibeValue* current = root;
    const char* p = path;
    char seg[256];

    /* Walk dot-separated segments without strtok_r (which needs POSIX and
     * mutates its buffer). Long segments simply won't match a key. */
    while (*p && current) {
        size_t n = 0;
        while (p[n] && p[n] != '.') n++;
        if (n == 0) { p++; continue; }   /* collapse empty segments ("a..b", ".a") */
        if (current->type != VIBE_TYPE_OBJECT) return NULL;

        if (n < sizeof(seg)) {
            memcpy(seg, p, n);
            seg[n] = '\0';
            current = vibe_object_get(current->as_object, seg);
        } else {
            char* big = (char*)vibe__malloc(n + 1);
            if (!big) return NULL;
            memcpy(big, p, n);
            big[n] = '\0';
            current = vibe_object_get(current->as_object, big);
            vibe__free(big);
        }

        p += n;
        if (*p == '.') p++;   /* skip the separator */
    }
    return current;
}

const char* vibe_get_string(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_STRING) ? v->as_string : NULL;
}
int64_t vibe_get_int(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_INTEGER) ? v->as_integer : 0;
}
double vibe_get_float(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_FLOAT) ? v->as_float : 0.0;
}
bool vibe_get_bool(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_BOOLEAN) ? v->as_boolean : false;
}
VibeArray* vibe_get_array(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_ARRAY) ? v->as_array : NULL;
}
VibeObject* vibe_get_object(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_OBJECT) ? v->as_object : NULL;
}

const char* vibe_get_string_or(VibeValue* value, const char* path, const char* fallback) {
    const char* s = vibe_get_string(value, path);
    return s ? s : fallback;
}
int64_t vibe_get_int_or(VibeValue* value, const char* path, int64_t fallback) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_INTEGER) ? v->as_integer : fallback;
}
double vibe_get_float_or(VibeValue* value, const char* path, double fallback) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_FLOAT) ? v->as_float : fallback;
}
bool vibe_get_bool_or(VibeValue* value, const char* path, bool fallback) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    return (v && v->type == VIBE_TYPE_BOOLEAN) ? v->as_boolean : fallback;
}

/* ============================================================================
 * Emitter — serialise a value tree back to canonical VIBE text
 * ============================================================================ */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool oom;
} StrBuf;

static void sb_reserve(StrBuf* sb, size_t extra) {
    if (sb->oom) return;
    if (extra + 1 > sb->cap - sb->len) {  /* sb->len < sb->cap invariant holds */
        if (extra > SIZE_MAX - 1 - sb->len) { sb->oom = true; return; }
        size_t ncap = sb->cap ? sb->cap : 128;
        while (extra + 1 > ncap - sb->len) {
            if (ncap > SIZE_MAX / 2) { ncap = sb->len + extra + 1; break; }
            ncap *= 2;
        }
        char* nd = (char*)vibe__realloc(sb->data, ncap);
        if (!nd) { sb->oom = true; return; }
        sb->data = nd;
        sb->cap = ncap;
    }
}
static void sb_putc(StrBuf* sb, char c) {
    sb_reserve(sb, 1);
    if (sb->oom) return;
    sb->data[sb->len++] = c;
}
static void sb_putn(StrBuf* sb, const char* s, size_t n) {
    sb_reserve(sb, n);
    if (sb->oom) return;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
}
static void sb_puts(StrBuf* sb, const char* s) { sb_putn(sb, s, strlen(s)); }
static void sb_indent(StrBuf* sb, int n) { for (int i = 0; i < n; i++) sb_putn(sb, "  ", 2); }

static void sb_put_quoted(StrBuf* sb, const char* s) {
    static const char hexd[] = "0123456789abcdef";
    sb_putc(sb, '"');
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
            case '"':  sb_putn(sb, "\\\"", 2); break;
            case '\\': sb_putn(sb, "\\\\", 2); break;
            case '\n': sb_putn(sb, "\\n", 2);  break;
            case '\t': sb_putn(sb, "\\t", 2);  break;
            case '\r': sb_putn(sb, "\\r", 2);  break;
            default:
                if (*p < 0x20 || *p == 0x7F) {
                    /* Any other control char -> \u00XX so the output re-parses
                     * (raw control chars are rejected by the lexer). */
                    char esc[6] = { '\\', 'u', '0', '0', 0, 0 };
                    esc[4] = hexd[(*p >> 4) & 0xF];
                    esc[5] = hexd[*p & 0xF];
                    sb_putn(sb, esc, 6);
                } else {
                    sb_putc(sb, (char)*p);   /* ASCII or UTF-8 continuation byte */
                }
                break;
        }
    }
    sb_putc(sb, '"');
}

/* A string round-trips bare only if it re-lexes to the same STRING token. */
static bool string_is_bare_safe(const char* s) {
    if (!s || s[0] == '\0') return false;
    if (!is_unquoted_start_char((unsigned char)s[0])) return false;
    for (const char* p = s; *p; p++) {
        if (!is_unquoted_string_char((unsigned char)*p)) return false;
        if (*p == '"') return false;
    }
    if (is_valid_number(s)) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0) return false;
    return true;
}

static bool key_is_bare_safe(const char* s) {
    if (!s || s[0] == '\0') return false;
    if (!is_identifier_start((unsigned char)s[0])) return false;
    for (const char* p = s; *p; p++)
        if (!is_identifier_char((unsigned char)*p)) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0) return false;
    return true;
}

static void sb_put_double(StrBuf* sb, double d) {
    char buf[512];
    if (!isfinite(d)) {
        /* VIBE has no inf/nan literal; emit a valid (string) token. */
        sb_put_quoted(sb, isnan(d) ? "nan" : (d < 0 ? "-inf" : "inf"));
        return;
    }
    int dec;
    for (dec = 0; dec <= 17; dec++) {
        snprintf(buf, sizeof(buf), "%.*f", dec, d);
        if (strtod(buf, NULL) == d) break;
    }
    if (dec > 17) snprintf(buf, sizeof(buf), "%.17g", d); /* extreme magnitude */
    sb_puts(sb, buf);
    if (!strpbrk(buf, ".eE")) sb_puts(sb, ".0"); /* keep it lexing as a float */
}

static void emit_value(StrBuf* sb, const VibeValue* v, int indent, int depth);

/* Fast base-10 formatting of a signed 64-bit integer into buf (which must hold
 * at least 21 bytes: up to 20 digits for INT64_MIN's magnitude plus a sign).
 * Returns the number of bytes written (no NUL). Much cheaper than snprintf. */
static size_t vibe__fmt_i64(int64_t v, char* buf) {
    char tmp[20];
    size_t n = 0;
    /* Work in unsigned to format the magnitude, handling INT64_MIN safely
     * (its positive value overflows int64_t). */
    uint64_t u = (v < 0) ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    do { tmp[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    size_t out = 0;
    if (v < 0) buf[out++] = '-';
    while (n) buf[out++] = tmp[--n];   /* reverse the digits */
    return out;
}

static void emit_scalar(StrBuf* sb, const VibeValue* v) {
    char numbuf[32];
    switch (v->type) {
        case VIBE_TYPE_NULL:    sb_put_quoted(sb, ""); break;
        case VIBE_TYPE_BOOLEAN: sb_putn(sb, v->as_boolean ? "true" : "false", v->as_boolean ? 4 : 5); break;
        case VIBE_TYPE_INTEGER: {
            size_t n = vibe__fmt_i64(v->as_integer, numbuf);
            sb_putn(sb, numbuf, n);
            break;
        }
        case VIBE_TYPE_FLOAT: sb_put_double(sb, v->as_float); break;
        case VIBE_TYPE_STRING:
            if (string_is_bare_safe(v->as_string)) sb_puts(sb, v->as_string);
            else sb_put_quoted(sb, v->as_string);
            break;
        default: break;
    }
}

static void emit_key(StrBuf* sb, const char* key) {
    if (key_is_bare_safe(key)) sb_puts(sb, key);
    else sb_put_quoted(sb, key);
}

static void emit_value(StrBuf* sb, const VibeValue* v, int indent, int depth) {
    /* Fail closed on pathologically deep trees instead of overflowing the C
     * stack. Marking the buffer OOM makes vibe_emit return NULL. */
    if (depth > VIBE_MAX_RECURSION_DEPTH) { sb->oom = true; return; }
    if (v->type == VIBE_TYPE_OBJECT) {
        if (v->as_object->count == 0) { sb_puts(sb, "{}"); return; }
        sb_puts(sb, "{\n");
        for (size_t i = 0; i < v->as_object->count; i++) {
            sb_indent(sb, indent + 1);
            emit_key(sb, v->as_object->entries[i].key);
            sb_putc(sb, ' ');
            emit_value(sb, v->as_object->entries[i].value, indent + 1, depth + 1);
            sb_putc(sb, '\n');
        }
        sb_indent(sb, indent);
        sb_putc(sb, '}');
    } else if (v->type == VIBE_TYPE_ARRAY) {
        if (v->as_array->count == 0) { sb_puts(sb, "[]"); return; }
        sb_puts(sb, "[\n");
        for (size_t i = 0; i < v->as_array->count; i++) {
            sb_indent(sb, indent + 1);
            emit_value(sb, v->as_array->values[i], indent + 1, depth + 1);
            sb_putc(sb, '\n');
        }
        sb_indent(sb, indent);
        sb_putc(sb, ']');
    } else {
        emit_scalar(sb, v);
    }
}

char* vibe_emit(const VibeValue* value) {
    if (!value) return NULL;
    StrBuf sb;
    memset(&sb, 0, sizeof(sb));

    if (value->type == VIBE_TYPE_OBJECT) {
        /* Root object: emit entries at the document level, no wrapping braces. */
        for (size_t i = 0; i < value->as_object->count; i++) {
            emit_key(&sb, value->as_object->entries[i].key);
            sb_putc(&sb, ' ');
            emit_value(&sb, value->as_object->entries[i].value, 0, 1);
            sb_putc(&sb, '\n');
        }
    } else {
        emit_value(&sb, value, 0, 0);
        sb_putc(&sb, '\n');
    }

    if (sb.oom) { vibe__free(sb.data); return NULL; }
    if (!sb.data) { /* empty document */
        sb.data = (char*)vibe__malloc(1);
        if (!sb.data) return NULL;
        sb.len = 0;
    }
    sb.data[sb.len] = '\0';
    return sb.data;
}

/* Write `text` (n bytes) to a brand-new file at `path`, flushing and — where
 * available — fsync'ing before close so the bytes are durable. Returns true iff
 * every byte reached the file and close() succeeded. */
static bool vibe__write_all(const char* path, const char* text, size_t n) {
#if defined(VIBE_HAVE_ATOMIC_WRITE)
    /* Raw POSIX I/O so we can fsync the descriptor without fileno() (which is
     * awkward to expose under a strict -std=c11). */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = true;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, text + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;   /* retry a signal-interrupted write */
            ok = false;
            break;
        }
        off += (size_t)w;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    return ok;
#else
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = (fwrite(text, 1, n, f) == n);
    if (ok && fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
#endif
}

bool vibe_emit_file(const VibeValue* value, const char* path) {
    if (!value || !path) return false;
    char* text = vibe_emit(value);
    if (!text) return false;
    size_t n = strlen(text);

#if defined(VIBE_HAVE_ATOMIC_WRITE)
    /* Atomic replace: write to a sibling temp file, fsync it, then rename() over
     * the target. rename() is atomic on POSIX, so a crash / disk-full / write
     * error can never leave the caller's original file truncated or
     * half-written — it is either the complete old content or the complete new
     * content. This is what makes `vibe fmt -w` safe on a real config file.
     * The temp lives in the SAME directory so rename() stays within one
     * filesystem (cross-device rename would fail with EXDEV). */
    {
        size_t plen = strlen(path);
        /* "<path>.vibe-tmp-<pid>" ; pid as unsigned long is <= 20 digits. */
        size_t tlen = plen + sizeof(".vibe-tmp-") + 24;
        char* tmp = (char*)vibe__malloc(tlen);
        if (tmp) {
            snprintf(tmp, tlen, "%s.vibe-tmp-%lu", path, (unsigned long)getpid());
            if (vibe__write_all(tmp, text, n)) {
                if (rename(tmp, path) == 0) {
                    vibe__free(tmp);
                    vibe__free(text);
                    return true;
                }
            }
            remove(tmp);          /* best-effort cleanup of the temp on any failure */
            vibe__free(tmp);
            /* Fall through to a direct write only if the temp path itself could
             * not be used (e.g. unwritable directory); this preserves the
             * previous best-effort behaviour rather than silently failing. */
        }
    }
#endif

    bool ok = vibe__write_all(path, text, n);
    vibe__free(text);
    return ok;
}

/* ============================================================================
 * Debug printer (not canonical VIBE)
 * ============================================================================ */

void vibe_value_print(VibeValue* value, int indent) {
    if (!value) return;
    /* `indent` doubles as a depth counter here; stop before overflowing the C
     * stack on a pathologically deep tree. */
    if (indent > VIBE_MAX_RECURSION_DEPTH) { printf("..."); return; }
    const char* istr = "  ";
    switch (value->type) {
        case VIBE_TYPE_INTEGER: printf("%lld", (long long)value->as_integer); break;
        case VIBE_TYPE_FLOAT:   printf("%g", value->as_float); break;
        case VIBE_TYPE_BOOLEAN: printf("%s", value->as_boolean ? "true" : "false"); break;
        case VIBE_TYPE_STRING:  printf("\"%s\"", value->as_string); break;
        case VIBE_TYPE_ARRAY:
            printf("[\n");
            for (size_t i = 0; i < value->as_array->count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("%s", istr);
                vibe_value_print(value->as_array->values[i], indent + 1);
                printf("\n");
            }
            for (int j = 0; j < indent; j++) printf("%s", istr);
            printf("]");
            break;
        case VIBE_TYPE_OBJECT:
            printf("{\n");
            for (size_t i = 0; i < value->as_object->count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("%s", istr);
                printf("%s: ", value->as_object->entries[i].key);
                vibe_value_print(value->as_object->entries[i].value, indent + 1);
                printf("\n");
            }
            for (int j = 0; j < indent; j++) printf("%s", istr);
            printf("}");
            break;
        default:
            printf("null");
            break;
    }
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* VIBE_IMPLEMENTATION */

#endif /* VIBE_H */
