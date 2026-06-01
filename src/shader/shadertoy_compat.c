/* Shadertoy → desktop-GL source compatibility transforms (implementation).
 * Pure C, no OpenGL — see shadertoy_compat.h. */

#include "neowall/shader/shadertoy_compat.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

char *shadertoy_compat_fix(const char *source) {
    if (!source) return NULL;

    size_t src_len = strlen(source);
    /* Worst case adds ".xy" (3 bytes) per token; 2x is comfortably safe. */
    size_t alloc_size = src_len * 2 + 16;
    char *result = malloc(alloc_size);
    if (!result) return NULL;

    const char *src = source;
    char *dst = result;
    bool at_line_start = true;  /* true when only whitespace seen on this line */

    while (*src) {
        /* Pass through line comments verbatim. */
        if (src[0] == '/' && src[1] == '/') {
            while (*src && *src != '\n') *dst++ = *src++;
            continue;
        }
        /* Pass through block comments verbatim. */
        if (src[0] == '/' && src[1] == '*') {
            *dst++ = *src++;
            *dst++ = *src++;
            while (*src && !(src[0] == '*' && src[1] == '/')) *dst++ = *src++;
            if (*src) { *dst++ = *src++; *dst++ = *src++; }
            at_line_start = false;
            continue;
        }
        /* Pass through string literals verbatim. */
        if (*src == '"') {
            *dst++ = *src++;
            while (*src && *src != '"') {
                if (*src == '\\' && src[1]) *dst++ = *src++;
                *dst++ = *src++;
            }
            if (*src) *dst++ = *src++;
            at_line_start = false;
            continue;
        }

        /* A user `#version ...` directive must not survive: the wrapper emits
         * its own, and two #version lines is a hard compile error. Drop it. */
        if (at_line_start && strncmp(src, "#version", 8) == 0) {
            while (*src && *src != '\n') src++;
            continue;  /* at_line_start stays true; newline copied next iter */
        }

        /* iChannelResolution[n] -> iChannelResolution[n].xy when no component
         * access already follows (vec3 uniform used as vec2). */
        if (strncmp(src, "iChannelResolution[", 19) == 0) {
            memcpy(dst, src, 19);
            dst += 19; src += 19;
            while (*src && *src != ']') *dst++ = *src++;   /* index */
            if (*src == ']') *dst++ = *src++;              /* closing bracket */
            if (*src != '.' && *src != '[') {
                memcpy(dst, ".xy", 3);
                dst += 3;
            }
            at_line_start = false;
            continue;
        }

        /* Track whether we are still at the start of a line (whitespace only). */
        if (*src == '\n') {
            at_line_start = true;
        } else if (*src != ' ' && *src != '\t' && *src != '\r') {
            at_line_start = false;
        }
        *dst++ = *src++;
    }

    *dst = '\0';
    return result;
}
