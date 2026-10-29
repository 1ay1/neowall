/* libFuzzer harness for the GLSL source transforms a downloaded shader goes
 * through before the driver ever sees it: shadertoy_compat_fix (rewrites
 * Shadertoy idioms) and glsl_shadow_scan (finds user definitions that shadow
 * the stdlib). Both hand-parse C-like source, so comments, strings,
 * preprocessor lines and unbalanced braces are all fair game.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/shader/glsl_shadow.h"
#include "neowall/shader/shadertoy_compat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *src = malloc(size + 1);
    if (!src) return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    char *fixed = shadertoy_compat_fix(src);
    if (fixed) {
        /* the rewrite must stay a C string we can scan again */
        (void)strlen(fixed);
    }

    glsl_shadow_set *set = glsl_shadow_create();
    if (set) {
        glsl_shadow_scan(set, src);
        if (fixed) glsl_shadow_scan(set, fixed);
        (void)glsl_shadow_contains(set, "main");
        (void)glsl_shadow_count(set);
        glsl_shadow_free(set);
    }
    free(fixed);
    free(src);
    return 0;
}
