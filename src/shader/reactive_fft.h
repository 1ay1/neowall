/* Radix-2 Cooley-Tukey FFT used by the reactive audio path.
 *
 * Extracted from reactive.c so it can be unit-tested without spinning up the
 * audio capture thread or parec subprocess. The implementation is identical
 * to what was inlined; reactive.c now includes this header-style file.
 *
 * `re` and `im` are length `n` (must be a power of two), modified in place.
 *
 * Compiled as a static helper inside reactive.c (default) or as the public
 * symbol `neowall_test_fft` when NEOWALL_TESTING is defined (the test target
 * sets this).
 */

#ifndef NEOWALL_FFT_H_INCLUDED
#define NEOWALL_FFT_H_INCLUDED

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef NEOWALL_TESTING
#define NW_FFT_LINKAGE
#define NW_FFT_NAME neowall_test_fft
void neowall_test_fft(float *re, float *im, int n);
#else
#define NW_FFT_LINKAGE static
#define NW_FFT_NAME fft
#endif

NW_FFT_LINKAGE void NW_FFT_NAME(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float tr = cr * re[b] - ci * im[b];
                float ti = cr * im[b] + ci * re[b];
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

#endif /* NEOWALL_FFT_H_INCLUDED */
