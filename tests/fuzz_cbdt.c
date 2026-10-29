/* libFuzzer harness for the color-emoji font reader (CBDT/CBLC + sbix).
 *
 * term_font / term_font_* can point at any font file a user downloads, so the
 * sfnt directory, cmap subtables and bitmap index are untrusted. Opens the
 * bytes as a font and looks up and renders a spread of codepoints.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "cbdt.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    cbdt_font *f = cbdt_open(data, size);
    if (!f) return 0;
    static const uint32_t cps[] = {
        0x41, 0x263A, 0x2764, 0xFFFF, 0x1F600, 0x1F44D, 0x1F680, 0x10FFFF, 0,
    };
    for (size_t i = 0; i < sizeof(cps) / sizeof(cps[0]); i++) {
        (void)cbdt_has(f, cps[i]);
        int w = 0, h = 0;
        uint8_t *px = cbdt_render(f, cps[i], &w, &h);
        if (px) {
            if (w <= 0 || h <= 0) __builtin_trap();
            volatile uint8_t sink = px[(size_t)w * h * 4 - 1];
            (void)sink;
            free(px);
        }
    }
    cbdt_close(f);
    return 0;
}
