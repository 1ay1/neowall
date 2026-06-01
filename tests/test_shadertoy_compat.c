/* Unit tests for the Shadertoy → desktop-GL source compatibility transforms.
 *
 * shadertoy_compat.c is intentionally GL-free, so these run headless in CI.
 * They lock down the rewrites that determine whether an arbitrary Shadertoy
 * shader compiles under `#version 330 core`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/shader/shadertoy_compat.h"

static int failures = 0;
static int checks = 0;

/* result contains needle */
#define CHECK_CONTAINS(src, needle)                                            \
    do {                                                                       \
        checks++;                                                              \
        char *r = shadertoy_compat_fix(src);                                   \
        if (!r || !strstr(r, needle)) {                                        \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: expected to find \"%s\" in:\n%s\n",   \
                    __FILE__, __LINE__, needle, r ? r : "(null)");             \
        }                                                                      \
        free(r);                                                               \
    } while (0)

/* result does NOT contain needle */
#define CHECK_LACKS(src, needle)                                               \
    do {                                                                       \
        checks++;                                                              \
        char *r = shadertoy_compat_fix(src);                                   \
        if (!r || strstr(r, needle)) {                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: did NOT expect \"%s\" in:\n%s\n",     \
                    __FILE__, __LINE__, needle, r ? r : "(null)");             \
        }                                                                      \
        free(r);                                                               \
    } while (0)

/* exact equality */
#define CHECK_EQ(src, expected)                                                \
    do {                                                                       \
        checks++;                                                              \
        char *r = shadertoy_compat_fix(src);                                   \
        if (!r || strcmp(r, expected) != 0) {                                  \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d:\n  got:      %s\n  expected: %s\n",   \
                    __FILE__, __LINE__, r ? r : "(null)", expected);           \
        }                                                                      \
        free(r);                                                               \
    } while (0)

static void test_null_and_empty(void) {
    checks++;
    if (shadertoy_compat_fix(NULL) != NULL) {
        failures++;
        fprintf(stderr, "FAIL: NULL input should return NULL\n");
    }
    CHECK_EQ("", "");
}

static void test_version_stripped(void) {
    /* A stray #version directive must be removed (we emit our own). */
    CHECK_LACKS("#version 300 es\nvoid mainImage(){}", "#version");
    CHECK_CONTAINS("#version 300 es\nvoid mainImage(){}", "mainImage");
    /* Leading whitespace before #version still counts as line start. */
    CHECK_LACKS("   #version 130\nfoo", "#version");
}

static void test_ichannelresolution_swizzle(void) {
    /* Bare use gets .xy appended. */
    CHECK_CONTAINS("vec2 r = iChannelResolution[0];", "iChannelResolution[0].xy");
    /* Already-swizzled use is left alone (no double .xy). */
    CHECK_LACKS("vec2 r = iChannelResolution[1].xy;", ".xy.xy");
    /* Indexed re-access (rare) is left alone. */
    CHECK_LACKS("float f = iChannelResolution[2][0];", "].xy");
}

static void test_comment_and_string_safety(void) {
    /* A token inside a line comment must NOT be rewritten. */
    CHECK_LACKS("// iChannelResolution[0] in a comment\nint x;",
                "iChannelResolution[0].xy");
    /* Inside a block comment, also untouched. */
    CHECK_LACKS("/* iChannelResolution[0] */ int x;",
                "iChannelResolution[0].xy");
    /* #version inside a comment is preserved (not a real directive). */
    CHECK_CONTAINS("// #version 100 is just text\nint x;", "#version 100");
}

static void test_passthrough(void) {
    /* Ordinary code is returned byte-for-byte. */
    const char *plain = "void mainImage(out vec4 c, in vec2 p){ c = vec4(1.0); }";
    CHECK_EQ(plain, plain);
}

int main(void) {
    test_null_and_empty();
    test_version_stripped();
    test_ichannelresolution_swizzle();
    test_comment_and_string_safety();
    test_passthrough();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
