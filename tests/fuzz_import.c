/* libFuzzer harness for `neowall import`: a downloaded Shadertoy JSON
 * export goes through a hand-written JSON reader, so it must never crash,
 * leak or read out of bounds. Whatever imports is parsed back too. GL-free. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "neowall/log.h"
#include "neowall/shader/shader_source.h"
#include "neowall/shader/shadertoy_import.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static int quiet;
    if (!quiet) {
        log_quiet_errors(true);
        log_set_level(LOG_LEVEL_ERROR);
        quiet = 1;
    }
    nw_import im = {0};
    char err[128];
    if (nw_is_ok(nw_shadertoy_import((const char *)data, size, &im, err, sizeof(err)))) {
        nw_shader_source *s = NULL;
        if (nw_is_ok(nw_shader_source_parse(im.glsl, NULL, im.manifest, &s))) nw_shader_source_free(s);
        nw_import_free(&im);
    }
    return 0;
}
