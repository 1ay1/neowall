/* See shader_error_log.h. */

#include "neowall/shader/shader_error_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SHADER_ERROR_LOG_SIZE 16384

static char  g_buf[SHADER_ERROR_LOG_SIZE];
static size_t g_pos = 0;

void shader_error_log_clear(void) {
    g_buf[0] = '\0';
    g_pos = 0;
}

void shader_error_log_append(const char *fmt, ...) {
    if (g_pos >= SHADER_ERROR_LOG_SIZE - 1) return;

    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(g_buf + g_pos, SHADER_ERROR_LOG_SIZE - g_pos, fmt, ap);
    va_end(ap);

    if (written > 0) {
        g_pos += (size_t)written;
        if (g_pos >= SHADER_ERROR_LOG_SIZE) g_pos = SHADER_ERROR_LOG_SIZE - 1;
    }
}

const char *shader_error_log_get(void) { return g_buf; }
size_t      shader_error_log_size(void) { return g_pos; }
