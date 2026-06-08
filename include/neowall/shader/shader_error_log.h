/* Shader Error Log — single process-wide buffer used by both shader_core
 * (transitions, basic effects) and shader_multipass (Shadertoy pipeline).
 *
 * Historically each implementation kept its own static `g_last_error_log`
 * which meant "the last shader error" was ambiguous depending on which path
 * compiled most recently. Now there is one buffer; getters in each module
 * delegate here so the public API (`shader_get_last_error_log`,
 * `multipass_get_error_log`) is unchanged.
 *
 * Not thread-safe by design — shader compilation runs on the GL thread.
 */

#ifndef NEOWALL_SHADER_ERROR_LOG_H
#define NEOWALL_SHADER_ERROR_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reset the buffer to empty. Call at the start of a compile attempt. */
void shader_error_log_clear(void);

/* Printf-style append. Silently truncates at SHADER_ERROR_LOG_SIZE. */
void shader_error_log_append(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* Borrowed pointer to the NUL-terminated buffer. Valid until next clear/append. */
const char *shader_error_log_get(void);

/* Bytes written so far (excluding NUL). */
size_t shader_error_log_size(void);

#ifdef __cplusplus
}
#endif

#endif /* NEOWALL_SHADER_ERROR_LOG_H */
