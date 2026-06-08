/* libFuzzer harness for the multipass shader parser.
 *
 * multipass_parse_shader ingests untrusted user-supplied GLSL (shaders dropped
 * into ~/.config/neowall/shaders/), so it must never crash, leak, or read out
 * of bounds on malformed input. This harness feeds arbitrary bytes through
 * the full parse -> free cycle.
 *
 * Run under ASan/UBSan (build instructions identical to fuzz_vibe.c). The
 * parser is GL-free, so the harness needs no display server and no GL.
 *
 *   meson setup build-fuzz -Dfuzz=true -Db_sanitize=address,undefined \
 *     --native-file - <<EOF
 *   [binaries]
 *   c = 'clang'
 *   EOF
 *   ninja -C build-fuzz fuzz_multipass
 *   ./build-fuzz/fuzz_multipass -max_total_time=60
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/shader/shader_multipass.h"

/* Stub for multipass_type_name — the real one lives in shader_multipass.c
 * which we don't link (GL deps). The parser only calls it for log messages,
 * so a placeholder string is fine. */
const char *multipass_type_name(multipass_type_t type) {
    (void)type;
    return "<fuzz>";
}

/* Logging stubs. shader_log.h sees NEOWALL_VERSION (a project-wide flag) and
 * defers to externs that live in utils.c; the fuzz harness doesn't link
 * utils.c (would pull pthread/atomics), so we provide no-op stubs. */
#include <stdarg.h>
void log_info (const char *fmt, ...) { (void)fmt; }
void log_debug(const char *fmt, ...) { (void)fmt; }
void log_warn (const char *fmt, ...) { (void)fmt; }
void log_error(const char *fmt, ...) { (void)fmt; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* multipass_parse_shader expects a NUL-terminated C string; the fuzzer
     * buffer is not, so copy into a terminated scratch buffer. */
    char *buf = malloc(size + 1);
    if (!buf) {
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';

    /* Exercise both the cheap detector and the full parse path. The detector
     * is the first thing the renderer calls so its crash-safety matters too. */
    (void)multipass_count_main_functions(buf);
    (void)multipass_detect(buf);

    char *common = multipass_extract_common(buf);
    free(common);

    multipass_parse_result_t *result = multipass_parse_shader(buf);
    if (result) {
        multipass_free_parse_result(result);
    }

    free(buf);
    return 0;
}
