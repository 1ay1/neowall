/*
 * VIBE Parser Implementation
 *
 * This file does all the heavy lifting - tokenizing, parsing, building the value tree.
 * The approach is pretty straightforward: read characters, turn them into tokens,
 * then build a tree structure from those tokens.
 */

// We use strdup which is POSIX, not standard C
// This macro tells the compiler we want POSIX features
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "vibe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

// How deep can we nest? 64 levels should be plenty
// (if you need more than this, your config is probably too complex)
#define MAX_NESTING_DEPTH 64

// Start with space for 16 items, grow as needed
#define INITIAL_CAPACITY 16

// Tokens are the "words" we recognize while scanning the input
// Think of this like breaking a sentence into words and punctuation
typedef enum {
    TOKEN_EOF = 0,          // End of file - we're done
    TOKEN_IDENTIFIER,       // Names like 'port', 'database', 'server'
    TOKEN_STRING,           // Quoted text: "hello world"
    TOKEN_NUMBER,           // Anything numeric: 42, 3.14, -7
    TOKEN_BOOLEAN,          // Just 'true' or 'false'
    TOKEN_LEFT_BRACE,       // { - starts an object
    TOKEN_RIGHT_BRACE,      // } - ends an object
    TOKEN_LEFT_BRACKET,     // [ - starts an array
    TOKEN_RIGHT_BRACKET,    // ] - ends an array
    TOKEN_NEWLINE,          // Line breaks matter in VIBE
    TOKEN_ERROR             // Something went wrong
} TokenType;

// A token is a piece of text we recognized, with location info for errors
typedef struct {
    TokenType type;         // What kind of token is this?
    char* value;            // The actual text (NULL for punctuation)
    int line;               // Where did we find it?
    int column;             // Exact position for error messages
} Token;

// While parsing, we need to track what context we're in
// Are we at the top level? Inside an object? Inside an array?
typedef enum {
    STATE_ROOT,             // Top level of the file
    STATE_OBJECT,           // Inside a { } block
    STATE_ARRAY,            // Inside a [ ] block
    STATE_VALUE             // Expecting a value
} ParseState;

// Each nesting level needs to remember what it's building
typedef struct {
    ParseState state;       // What context are we in?
    VibeValue* container;   // The object/array we're filling
    char* current_key;      // For objects, the last key we saw
} StateFrame;

// Stack to track nesting - push when we enter {[, pop when we exit ]}
typedef struct {
    StateFrame frames[MAX_NESTING_DEPTH];
    int depth;              // How deep are we nested right now?
} ParseStack;

// Forward declarations for all our helper functions
static void set_error(VibeParser* parser, const char* fmt, ...);
static bool is_identifier_start(char c);
static bool is_identifier_char(char c);
static bool is_unquoted_string_char(char c);
static bool is_valid_number(const char* str);
static char* strdup_range(const char* start, const char* end);
static void skip_whitespace(VibeParser* parser);
static void skip_comment(VibeParser* parser);
static Token next_token(VibeParser* parser);
static void token_free(Token* token);
static VibeValue* parse_value_from_token(Token* token);

/* ============================================================================
 * Parser Setup and Teardown
 * ============================================================================ */

VibeParser* vibe_parser_new(void) {
    VibeParser* parser = calloc(1, sizeof(VibeParser));
    if (!parser) return NULL;

    parser->line = 1;
    parser->column = 1;
    return parser;
}

void vibe_parser_free(VibeParser* parser) {
    if (!parser) return;

    if (parser->error.message) {
        free(parser->error.message);
    }

    free(parser);
}

/* ===== Error Handling ===== */

static void set_error(VibeParser* parser, const char* fmt, ...) {
    if (parser->error.has_error) return; /* Keep first error */

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    parser->error.has_error = true;
    parser->error.message = strdup(buffer);
    parser->error.line = parser->line;
    parser->error.column = parser->column;
}

VibeError vibe_get_last_error(VibeParser* parser) {
    return parser->error;
}

void vibe_error_free(VibeError* error) {
    if (error && error->message) {
        free(error->message);
        error->message = NULL;
    }
}

/* ===== Character Classification ===== */

static bool is_identifier_start(char c) {
    return isalpha(c) || c == '_';
}

static bool is_identifier_char(char c) {
    return isalnum(c) || c == '_' || c == '-';
}

static bool is_unquoted_string_char(char c) {
    /* Allow all printable ASCII except structural characters */
    /* Range: 0x21 (!) to 0x7E (~) inclusive */
    if (c <= 0x20 || c > 0x7E) return false;  /* Must be printable ASCII */
    /* Disallow structural characters */
    if (c == '{' || c == '}' || c == '[' || c == ']' || c == '#') return false;
    /* Allow common path characters: slash, dot, tilde, dash, underscore */
    return true;
}

/* ===== Number Validation ===== */

static bool is_valid_number(const char* str) {
    if (!str || *str == '\0') return false;

    const char* p = str;

    /* Skip optional negative sign */
    if (*p == '-') p++;

    /* Must have at least one digit */
    if (!isdigit(*p)) return false;

    /* Count digits and dots */
    bool has_dot = false;
    bool has_digit_after_dot = false;

    while (*p) {
        if (isdigit(*p)) {
            if (has_dot) has_digit_after_dot = true;
            p++;
        } else if (*p == '.') {
            if (has_dot) return false;  /* Multiple dots = not a number */
            has_dot = true;
            p++;
        } else {
            return false;  /* Non-numeric character */
        }
    }

    /* If there's a dot, there must be digits after it */
    if (has_dot && !has_digit_after_dot) return false;

    return true;
}

/* ===== String Utilities ===== */

static char* strdup_range(const char* start, const char* end) {
    size_t len = end - start;
    char* result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

/* ===== Lexer ===== */

// Skip over whitespace (but not newlines - those matter in VIBE!)
// Spaces, tabs, and carriage returns are just ignored
static void skip_whitespace(VibeParser* parser) {
    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            parser->pos++;
            parser->column++;
        } else {
            break;  // Hit something interesting
        }
    }
}

// Skip from # to the end of the line
// Comments are just for humans, parser doesn't care about them
static void skip_comment(VibeParser* parser) {
    if (parser->pos < parser->length && parser->input[parser->pos] == '#') {
        while (parser->pos < parser->length && parser->input[parser->pos] != '\n') {
            parser->pos++;
        }
    }
}

static char* parse_quoted_string(VibeParser* parser) {
    parser->pos++; /* Skip opening quote */
    parser->column++;

    char buffer[4096];
    size_t buf_pos = 0;

    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];

        if (c == '"') {
            parser->pos++;
            parser->column++;
            buffer[buf_pos] = '\0';
            return strdup(buffer);
        } else if (c == '\\' && parser->pos + 1 < parser->length) {
            parser->pos++;
            parser->column++;
            char next = parser->input[parser->pos];

            switch (next) {
                case '"': buffer[buf_pos++] = '"'; break;
                case '\\': buffer[buf_pos++] = '\\'; break;
                case 'n': buffer[buf_pos++] = '\n'; break;
                case 't': buffer[buf_pos++] = '\t'; break;
                case 'r': buffer[buf_pos++] = '\r'; break;
                default:
                    set_error(parser, "Invalid escape sequence '\\%c'", next);
                    return NULL;
            }
            parser->pos++;
            parser->column++;
        } else if (c == '\n') {
            set_error(parser, "Unterminated string");
            return NULL;
        } else {
            buffer[buf_pos++] = c;
            parser->pos++;
            parser->column++;
        }

        if (buf_pos >= sizeof(buffer) - 1) {
            set_error(parser, "String too long");
            return NULL;
        }
    }

    set_error(parser, "Unterminated string");
    return NULL;
}

static Token next_token(VibeParser* parser) {
    Token token = {0};

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

    /* Newline */
    if (c == '\n') {
        token.type = TOKEN_NEWLINE;
        parser->pos++;
        parser->line++;
        parser->column = 1;
        return token;
    }

    /* Braces and brackets */
    if (c == '{') {
        token.type = TOKEN_LEFT_BRACE;
        parser->pos++;
        parser->column++;
        return token;
    }
    if (c == '}') {
        token.type = TOKEN_RIGHT_BRACE;
        parser->pos++;
        parser->column++;
        return token;
    }
    if (c == '[') {
        token.type = TOKEN_LEFT_BRACKET;
        parser->pos++;
        parser->column++;
        return token;
    }
    if (c == ']') {
        token.type = TOKEN_RIGHT_BRACKET;
        parser->pos++;
        parser->column++;
        return token;
    }

    /* Quoted string */
    if (c == '"') {
        token.type = TOKEN_STRING;
        token.value = parse_quoted_string(parser);
        if (!token.value) {
            token.type = TOKEN_ERROR;
        }
        return token;
    }

    /* Identifier or unquoted string/number/boolean */
    if (is_identifier_start(c) || isdigit(c) || c == '-' || c == '/' || c == '.' || c == '~') {
        const char* start = parser->input + parser->pos;

        /* Handle negative numbers */
        if (c == '-' && parser->pos + 1 < parser->length && isdigit(parser->input[parser->pos + 1])) {
            parser->pos++;
            parser->column++;
            c = parser->input[parser->pos];
        }

        /* Collect all valid unquoted string characters */
        while (parser->pos < parser->length) {
            c = parser->input[parser->pos];
            if (is_unquoted_string_char(c)) {
                parser->pos++;
                parser->column++;
            } else {
                break;
            }
        }

        const char* end = parser->input + parser->pos;
        token.value = strdup_range(start, end);

        /* Determine token type based on content */
        if (strcmp(token.value, "true") == 0 || strcmp(token.value, "false") == 0) {
            token.type = TOKEN_BOOLEAN;
        } else if (is_valid_number(token.value)) {
            token.type = TOKEN_NUMBER;
        } else if (is_identifier_start(token.value[0])) {
            /* Check if it's a valid identifier (only contains identifier chars) */
            bool is_valid_identifier = true;
            for (const char* p = token.value; *p; p++) {
                if (!is_identifier_char(*p)) {
                    is_valid_identifier = false;
                    break;
                }
            }
            token.type = is_valid_identifier ? TOKEN_IDENTIFIER : TOKEN_STRING;
        } else {
            token.type = TOKEN_STRING;
        }

        return token;
    }

    set_error(parser, "Unexpected character '%c'", c);
    token.type = TOKEN_ERROR;
    return token;
}

static void token_free(Token* token) {
    if (token && token->value) {
        free(token->value);
        token->value = NULL;
    }
}

/* ===== Value Creation ===== */

VibeValue* vibe_value_new_integer(int64_t value) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_INTEGER;
    v->as_integer = value;
    return v;
}

VibeValue* vibe_value_new_float(double value) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_FLOAT;
    v->as_float = value;
    return v;
}

VibeValue* vibe_value_new_boolean(bool value) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_BOOLEAN;
    v->as_boolean = value;
    return v;
}

VibeValue* vibe_value_new_string(const char* value) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_STRING;
    v->as_string = strdup(value);
    return v;
}

VibeValue* vibe_value_new_array(void) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_ARRAY;
    v->as_array = calloc(1, sizeof(VibeArray));
    if (!v->as_array) {
        free(v);
        return NULL;
    }

    v->as_array->capacity = INITIAL_CAPACITY;
    v->as_array->values = calloc(INITIAL_CAPACITY, sizeof(VibeValue*));
    return v;
}

VibeValue* vibe_value_new_object(void) {
    VibeValue* v = calloc(1, sizeof(VibeValue));
    if (!v) return NULL;

    v->type = VIBE_TYPE_OBJECT;
    v->as_object = calloc(1, sizeof(VibeObject));
    if (!v->as_object) {
        free(v);
        return NULL;
    }

    v->as_object->capacity = INITIAL_CAPACITY;
    v->as_object->entries = calloc(INITIAL_CAPACITY, sizeof(VibeObjectEntry));
    return v;
}

static VibeValue* parse_value_from_token(Token* token) {
    if (!token) return NULL;

    switch (token->type) {
        case TOKEN_BOOLEAN:
            return vibe_value_new_boolean(strcmp(token->value, "true") == 0);

        case TOKEN_NUMBER: {
            if (strchr(token->value, '.')) {
                return vibe_value_new_float(atof(token->value));
            } else {
                return vibe_value_new_integer(atoll(token->value));
            }
        }

        case TOKEN_STRING:
        case TOKEN_IDENTIFIER:
            return vibe_value_new_string(token->value);

        default:
            return NULL;
    }
}

/* ===== Object Operations ===== */

void vibe_object_set(VibeObject* obj, const char* key, VibeValue* value) {
    if (!obj || !key || !value) return;

    /* Check if key exists */
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->entries[i].key, key) == 0) {
            /* Replace existing value */
            vibe_value_free(obj->entries[i].value);
            obj->entries[i].value = value;
            return;
        }
    }

    /* Add new entry */
    if (obj->count >= obj->capacity) {
        size_t new_capacity = obj->capacity * 2;
        VibeObjectEntry* new_entries = realloc(obj->entries, new_capacity * sizeof(VibeObjectEntry));
        if (!new_entries) return;

        obj->entries = new_entries;
        obj->capacity = new_capacity;
    }

    obj->entries[obj->count].key = strdup(key);
    obj->entries[obj->count].value = value;
    obj->count++;
}

VibeValue* vibe_object_get(VibeObject* obj, const char* key) {
    if (!obj || !key) return NULL;

    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->entries[i].key, key) == 0) {
            return obj->entries[i].value;
        }
    }

    return NULL;
}

/* ===== Array Operations ===== */

void vibe_array_push(VibeArray* arr, VibeValue* value) {
    if (!arr || !value) return;

    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity * 2;
        VibeValue** new_values = realloc(arr->values, new_capacity * sizeof(VibeValue*));
        if (!new_values) return;

        arr->values = new_values;
        arr->capacity = new_capacity;
    }

    arr->values[arr->count++] = value;
}

VibeValue* vibe_array_get(VibeArray* arr, size_t index) {
    if (!arr || index >= arr->count) return NULL;
    return arr->values[index];
}

/* ===== Cleanup ===== */

void vibe_value_free(VibeValue* value) {
    if (!value) return;

    switch (value->type) {
        case VIBE_TYPE_STRING:
            free(value->as_string);
            break;

        case VIBE_TYPE_ARRAY:
            if (value->as_array) {
                for (size_t i = 0; i < value->as_array->count; i++) {
                    vibe_value_free(value->as_array->values[i]);
                }
                free(value->as_array->values);
                free(value->as_array);
            }
            break;

        case VIBE_TYPE_OBJECT:
            if (value->as_object) {
                for (size_t i = 0; i < value->as_object->count; i++) {
                    free(value->as_object->entries[i].key);
                    vibe_value_free(value->as_object->entries[i].value);
                }
                free(value->as_object->entries);
                free(value->as_object);
            }
            break;

        default:
            break;
    }

    free(value);
}

/* ===== Parser ===== */

VibeValue* vibe_parse_string(VibeParser* parser, const char* input) {
    if (!parser || !input) return NULL;

    parser->input = input;
    parser->length = strlen(input);
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->error.has_error = false;

    VibeValue* root = vibe_value_new_object();
    if (!root) return NULL;

    ParseStack stack = {0};
    stack.frames[0].state = STATE_ROOT;
    stack.frames[0].container = root;
    stack.depth = 0;

    Token token;
    char* current_key = NULL;

    while (true) {
        token = next_token(parser);

        if (token.type == TOKEN_ERROR || parser->error.has_error) {
            token_free(&token);
            vibe_value_free(root);
            free(current_key);
            return NULL;
        }

        if (token.type == TOKEN_EOF) {
            token_free(&token);
            break;
        }

        if (token.type == TOKEN_NEWLINE) {
            token_free(&token);
            continue;
        }

        StateFrame* frame = &stack.frames[stack.depth];

        if (frame->state == STATE_ROOT || frame->state == STATE_OBJECT) {
            if (token.type == TOKEN_IDENTIFIER) {
                current_key = strdup(token.value);
                token_free(&token);

                /* Peek next token */
                Token next = next_token(parser);

                if (next.type == TOKEN_LEFT_BRACE) {
                    /* Object */
                    VibeValue* obj = vibe_value_new_object();
                    vibe_object_set(frame->container->as_object, current_key, obj);

                    stack.depth++;
                    if (stack.depth >= MAX_NESTING_DEPTH) {
                        set_error(parser, "Maximum nesting depth exceeded");
                        token_free(&next);
                        free(current_key);
                        vibe_value_free(root);
                        return NULL;
                    }

                    stack.frames[stack.depth].state = STATE_OBJECT;
                    stack.frames[stack.depth].container = obj;
                    token_free(&next);
                    free(current_key);
                    current_key = NULL;
                } else if (next.type == TOKEN_LEFT_BRACKET) {
                    /* Array */
                    VibeValue* arr = vibe_value_new_array();
                    vibe_object_set(frame->container->as_object, current_key, arr);

                    stack.depth++;
                    if (stack.depth >= MAX_NESTING_DEPTH) {
                        set_error(parser, "Maximum nesting depth exceeded");
                        token_free(&next);
                        free(current_key);
                        vibe_value_free(root);
                        return NULL;
                    }

                    stack.frames[stack.depth].state = STATE_ARRAY;
                    stack.frames[stack.depth].container = arr;
                    token_free(&next);
                    free(current_key);
                    current_key = NULL;
                } else {
                    /* Simple value */
                    VibeValue* val = parse_value_from_token(&next);
                    if (val) {
                        vibe_object_set(frame->container->as_object, current_key, val);
                    }
                    token_free(&next);
                    free(current_key);
                    current_key = NULL;
                }
            } else if (token.type == TOKEN_RIGHT_BRACE) {
                if (stack.depth > 0) {
                    stack.depth--;
                }
                token_free(&token);
            } else {
                token_free(&token);
            }
        } else if (frame->state == STATE_ARRAY) {
            if (token.type == TOKEN_RIGHT_BRACKET) {
                if (stack.depth > 0) {
                    stack.depth--;
                }
                token_free(&token);
            } else if (token.type == TOKEN_LEFT_BRACE) {
                /* Object within array */
                VibeValue* obj = vibe_value_new_object();
                vibe_array_push(frame->container->as_array, obj);

                if (stack.depth < MAX_NESTING_DEPTH - 1) {
                    stack.depth++;
                    stack.frames[stack.depth].state = STATE_OBJECT;
                    stack.frames[stack.depth].container = obj;
                    stack.frames[stack.depth].current_key = NULL;
                }
                token_free(&token);
            } else {
                /* Parse value - this could be a simple value or start of an object */
                if (token.type == TOKEN_IDENTIFIER) {
                    /* This might be a key in an object within the array */
                    /* For now, treat as regular value - objects in arrays need parser rewrite */
                    VibeValue* val = parse_value_from_token(&token);
                    if (val) {
                        vibe_array_push(frame->container->as_array, val);
                    }
                } else {
                    VibeValue* val = parse_value_from_token(&token);
                    if (val) {
                        vibe_array_push(frame->container->as_array, val);
                    }
                }
                token_free(&token);
            }
        } else {
            token_free(&token);
        }
    }

    free(current_key);
    return root;
}

VibeValue* vibe_parse_file(VibeParser* parser, const char* filename) {
    if (!parser || !filename) return NULL;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        set_error(parser, "Cannot open file '%s'", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        set_error(parser, "Memory allocation failed");
        return NULL;
    }

    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    fclose(file);

    VibeValue* result = vibe_parse_string(parser, buffer);
    free(buffer);

    return result;
}

/* ===== Value Access ===== */

VibeValue* vibe_get(VibeValue* root, const char* path) {
    if (!root || !path) return NULL;

    VibeValue* current = root;
    char* path_copy = strdup(path);
    char* token = strtok(path_copy, ".");

    while (token && current) {
        if (current->type == VIBE_TYPE_OBJECT) {
            current = vibe_object_get(current->as_object, token);
        } else {
            free(path_copy);
            return NULL;
        }
        token = strtok(NULL, ".");
    }

    free(path_copy);
    return current;
}

const char* vibe_get_string(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_STRING) return NULL;
    return v->as_string;
}

int64_t vibe_get_int(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_INTEGER) return 0;
    return v->as_integer;
}

double vibe_get_float(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_FLOAT) return 0.0;
    return v->as_float;
}

bool vibe_get_bool(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_BOOLEAN) return false;
    return v->as_boolean;
}

VibeArray* vibe_get_array(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_ARRAY) return NULL;
    return v->as_array;
}

VibeObject* vibe_get_object(VibeValue* value, const char* path) {
    VibeValue* v = path ? vibe_get(value, path) : value;
    if (!v || v->type != VIBE_TYPE_OBJECT) return NULL;
    return v->as_object;
}

/* ===== Debug Helpers ===== */

void vibe_value_print(VibeValue* value, int indent) {
    if (!value) return;

    const char* indent_str = "  ";

    switch (value->type) {
        case VIBE_TYPE_INTEGER:
            printf("%lld", (long long)value->as_integer);
            break;

        case VIBE_TYPE_FLOAT:
            printf("%g", value->as_float);
            break;

        case VIBE_TYPE_BOOLEAN:
            printf("%s", value->as_boolean ? "true" : "false");
            break;

        case VIBE_TYPE_STRING:
            printf("\"%s\"", value->as_string);
            break;

        case VIBE_TYPE_ARRAY:
            printf("[\n");
            for (size_t i = 0; i < value->as_array->count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("%s", indent_str);
                vibe_value_print(value->as_array->values[i], indent + 1);
                printf("\n");
            }
            for (int j = 0; j < indent; j++) printf("%s", indent_str);
            printf("]");
            break;

        case VIBE_TYPE_OBJECT:
            printf("{\n");
            for (size_t i = 0; i < value->as_object->count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("%s", indent_str);
                printf("%s: ", value->as_object->entries[i].key);
                vibe_value_print(value->as_object->entries[i].value, indent + 1);
                printf("\n");
            }
            for (int j = 0; j < indent; j++) printf("%s", indent_str);
            printf("}");
            break;

        default:
            printf("null");
            break;
    }
}
