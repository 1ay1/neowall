/* libFuzzer harness for the EXIF orientation parser.
 *
 * Any JPEG in a wallpaper folder reaches this, so the APP1 payload is
 * untrusted. The parser must never read out of bounds and must always
 * return a valid orientation (1..8).
 */
#include <stddef.h>
#include <stdint.h>

#include "neowall/image/exif.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int o = exif_parse_orientation(data, size);
    if (o < 1 || o > 8) __builtin_trap();
    return 0;
}
