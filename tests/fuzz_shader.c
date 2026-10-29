/* libFuzzer harness for the shader source parser.
 *
 * nw_shader_source_parse reads untrusted user GLSL and .neowall manifests
 * (shaders dropped into ~/.config/neowall/shaders), so it must never crash,
 * leak or read out of bounds. The input is split at the first NUL: the part
 * before is the shader, the part after (if any) the manifest. Every pass is
 * then assembled, which runs the compat rewrites and the stdlib injection.
 * GL-free.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/log.h"
#include "neowall/shader/shader_source.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static int quiet;
    if (!quiet) {
        log_quiet_errors(true);
        log_set_level(LOG_LEVEL_ERROR);
        quiet = 1;
    }
    char *buf = malloc(size + 2);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = buf[size + 1] = '\0';
    size_t split = strlen(buf);
    const char *manifest = split < size ? buf + split + 1 : NULL;

    nw_shader_source *src = NULL;
    if (nw_is_ok(nw_shader_source_parse(buf, NULL, manifest, &src))) {
        for (size_t i = 0; i < src->passes.len; i++) {
            char *text = NULL;
            nw_src_loc *map = NULL;
            size_t n = 0;
            if (nw_is_ok(nw_shader_assemble(src, (int)i, &text, &map, &n))) {
                free(text);
                free(map);
            }
        }
        nw_shader_source_free(src);
    }
    free(buf);
    return 0;
}
