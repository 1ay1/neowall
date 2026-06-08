/* libFuzzer harness for the VIBE config parser.
 *
 * The parser ingests untrusted user input (~/.config/neowall/config.vibe and
 * .neowall sidecars), so it must never crash, leak, or read out of bounds on
 * malformed input. This harness feeds arbitrary bytes through the full
 * parse -> free cycle; run it under ASan/UBSan to catch memory bugs.
 *
 * Build (needs clang):
 *   meson setup build-fuzz -Dfuzz=true -Db_sanitize=address,undefined \
 *     --native-file - <<EOF
 *   [binaries]
 *   c = 'clang'
 *   EOF
 *   ninja -C build-fuzz fuzz_vibe
 *   ./build-fuzz/fuzz_vibe -max_total_time=60
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/config/vibe.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* vibe_parse_string expects a NUL-terminated C string; the fuzzer buffer
     * is not, so copy into a terminated scratch buffer. */
    char *buf = malloc(size + 1);
    if (!buf) {
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';

    VibeParser *parser = vibe_parser_new();
    if (parser) {
        VibeValue *root = vibe_parse_string(parser, buf);
        if (root) {
            vibe_value_free(root);
        }
        vibe_parser_free(parser);
    }

    free(buf);
    return 0;
}
