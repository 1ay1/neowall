/* libFuzzer harness for the terminal emulator (VT parser + screen model).
 *
 * The terminal wallpaper shows whatever its program prints, so every byte
 * here is untrusted. Feeds the input in random-sized chunks and resizes the
 * grid now and then, because resize vs. saved state is where bugs hide.
 */
#include <stddef.h>
#include <stdint.h>

#include "neowall/terminal/terminal.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    term_screen *s = term_screen_create(1 + data[0] % 200, 1 + data[1] % 80);
    if (!s) return 0;
    data += 2;
    size -= 2;

    size_t i = 0;
    while (i < size) {
        size_t chunk = 1 + data[i] % 64;
        if (chunk > size - i) chunk = size - i;
        term_screen_feed(s, data + i, chunk);
        i += chunk;
        /* a 0xFF chunk end means "resize now", sized by the next two bytes */
        if (i + 2 <= size && data[i - 1] == 0xFF) {
            term_screen_resize(s, 1 + data[i] % 240, 1 + data[i + 1] % 90);
            i += 2;
        }
    }
    term_screen_destroy(s);
    return 0;
}
