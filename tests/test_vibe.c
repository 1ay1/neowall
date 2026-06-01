/* Unit tests for the VIBE config parser (src/config/vibe.c).
 *
 * vibe.c has no GL / Wayland / filesystem deps in its string-parse path, so
 * these run headless in CI. They lock down both the happy path and several
 * silent-correctness bugs that previously produced wrong config instead of an
 * error:
 *
 *   1. A key whose value sat on the *next* line bound `(null)` to the key and
 *      silently dropped the real value.
 *   2. Integer literals that overflow int64 were clamped to INT64_MAX with no
 *      diagnostic (strtoll saturates).
 *   3. A dangling key at EOF was accepted.
 *
 * Run: meson test vibe   (or build the exe and run it directly)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/config/vibe.h"

static int failures = 0;
static int checks = 0;

/* Parse `src`; assert it SUCCEEDS and yields the given string at `path`. */
#define CHECK_STR(src, path, expect)                                           \
    do {                                                                       \
        checks++;                                                              \
        VibeParser *p = vibe_parser_new();                                     \
        VibeValue *r = vibe_parse_string(p, (src));                            \
        const char *got = r ? vibe_get_string(r, (path)) : NULL;               \
        if (!got || strcmp(got, (expect)) != 0) {                              \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s = \"%s\", expected \"%s\"\n",      \
                    __FILE__, __LINE__, (path), got ? got : "(null)",          \
                    (expect));                                                 \
        }                                                                      \
        if (r) vibe_value_free(r);                                             \
        vibe_parser_free(p);                                                   \
    } while (0)

/* Parse `src`; assert it FAILS (returns NULL and sets an error). */
#define CHECK_REJECT(src)                                                      \
    do {                                                                       \
        checks++;                                                              \
        VibeParser *p = vibe_parser_new();                                     \
        VibeValue *r = vibe_parse_string(p, (src));                            \
        VibeError e = vibe_get_last_error(p);                                  \
        if (r != NULL || !e.has_error) {                                       \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: expected reject, got accept for:\n%s\n",\
                    __FILE__, __LINE__, (src));                                \
        }                                                                      \
        if (r) vibe_value_free(r);                                             \
        vibe_parser_free(p);                                                   \
    } while (0)

/* Parse `src`; assert it SUCCEEDS and yields the given int64 at `path`. */
#define CHECK_INT(src, path, expect)                                           \
    do {                                                                       \
        checks++;                                                              \
        VibeParser *p = vibe_parser_new();                                     \
        VibeValue *r = vibe_parse_string(p, (src));                            \
        int64_t got = r ? vibe_get_int(r, (path)) : -1;                        \
        if (!r || got != (expect)) {                                           \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s = %lld, expected %lld\n",          \
                    __FILE__, __LINE__, (path), (long long)got,                \
                    (long long)(expect));                                      \
        }                                                                      \
        if (r) vibe_value_free(r);                                             \
        vibe_parser_free(p);                                                   \
    } while (0)

int main(void) {
    /* --- happy path --- */
    CHECK_STR("default {\n  shader retro.glsl\n}\n", "default.shader", "retro.glsl");
    CHECK_INT("default {\n  duration 300\n}\n", "default.duration", 300);
    CHECK_INT("default {\n  duration -7\n}\n", "default.duration", -7);
    CHECK_STR("output {\n  DP-1 { shader matrix.glsl }\n}\n",
              "output.DP-1.shader", "matrix.glsl");
    CHECK_STR("default { path ~/Pictures/Wallpapers/ }\n",
              "default.path", "~/Pictures/Wallpapers/");

    /* --- bug 1: value on the next line must not be silently dropped --- */
    CHECK_REJECT("default {\n  shader\n  retro.glsl\n}\n");

    /* --- bug 2: integer overflow must be rejected, not clamped --- */
    CHECK_REJECT("default {\n  duration 99999999999999999999\n}\n");

    /* --- bug 3: dangling key at EOF must be rejected --- */
    CHECK_REJECT("default {\n  shader");

    /* `12abc` is not a number, but IS a valid unquoted string token; the
     * parser accepts it as a string and leaves int-vs-string validation to
     * the config layer (vibe_get_int returns 0 for a non-int). */
    CHECK_STR("default {\n  duration 12abc\n}\n", "default.duration", "12abc");

    /* --- arrays --- */
    /* scalar arrays parse */
    {
        checks++;
        VibeParser *p = vibe_parser_new();
        VibeValue *r = vibe_parse_string(p, "x [ a b c ]\n");
        VibeArray *arr = r ? vibe_get_array(r, "x") : NULL;
        if (!arr || arr->count != 3) {
            failures++;
            fprintf(stderr, "FAIL %s:%d: scalar array count=%zu, expected 3\n",
                    __FILE__, __LINE__, arr ? arr->count : (size_t)0);
        }
        if (r) vibe_value_free(r);
        vibe_parser_free(p);
    }
    /* objects-in-arrays via braces parse */
    {
        checks++;
        VibeParser *p = vibe_parser_new();
        VibeValue *r = vibe_parse_string(p, "x [ { a 1 } { b 2 } ]\n");
        VibeArray *arr = r ? vibe_get_array(r, "x") : NULL;
        bool ok = arr && arr->count == 2 &&
                  vibe_array_get(arr, 0)->type == VIBE_TYPE_OBJECT &&
                  vibe_get_int(vibe_array_get(arr, 0), "a") == 1 &&
                  vibe_get_int(vibe_array_get(arr, 1), "b") == 2;
        if (!ok) {
            failures++;
            fprintf(stderr, "FAIL %s:%d: objects-in-array did not parse\n",
                    __FILE__, __LINE__);
        }
        if (r) vibe_value_free(r);
        vibe_parser_free(p);
    }

    printf("test_vibe: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
