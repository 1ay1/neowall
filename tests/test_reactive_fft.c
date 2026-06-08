/* Test the in-place radix-2 FFT used by the reactive audio path.
 *
 * Built with -DNEOWALL_TESTING which exposes the otherwise-static `fft`
 * symbol as `neowall_test_fft`. We don't pull the audio thread, parec, or
 * any /proc sampling — the FFT is a pure function we can pound on directly.
 *
 * Coverage:
 *   1. Inverse round-trip (FFT then IFFT recovers the input within FP error).
 *   2. Single-bin sinusoid lands its energy in the expected bin.
 *   3. DC input concentrates in bin 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void neowall_test_fft(float *re, float *im, int n);

/* Inverse FFT via conjugate-FFT-conjugate, then 1/n scale. */
static void ifft(float *re, float *im, int n) {
    for (int i = 0; i < n; i++) im[i] = -im[i];
    neowall_test_fft(re, im, n);
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) {
        re[i] *= inv;
        im[i] = -im[i] * inv;
    }
}

static int expect_close(const char *name, float got, float want, float eps) {
    if (fabsf(got - want) > eps) {
        fprintf(stderr, "FAIL: %s: expected %g, got %g (diff %g > %g)\n",
                name, want, got, fabsf(got - want), eps);
        return 1;
    }
    return 0;
}

static int test_roundtrip(void) {
    enum { N = 64 };
    float re[N], im[N], orig_re[N];
    for (int i = 0; i < N; i++) {
        orig_re[i] = re[i] = sinf(2.0f * (float)M_PI * 3.0f * i / N) +
                             0.5f * cosf(2.0f * (float)M_PI * 7.0f * i / N);
        im[i] = 0.0f;
    }
    neowall_test_fft(re, im, N);
    ifft(re, im, N);
    int fails = 0;
    for (int i = 0; i < N; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "roundtrip[%d]", i);
        fails += expect_close(nm, re[i], orig_re[i], 1e-4f);
    }
    return fails;
}

static int test_single_bin_sinusoid(void) {
    /* A pure sinusoid at bin k=5 (k cycles per N samples) should produce two
     * spectral peaks at bins 5 and N-5 of equal magnitude. */
    enum { N = 64, K = 5 };
    float re[N], im[N];
    for (int i = 0; i < N; i++) {
        re[i] = cosf(2.0f * (float)M_PI * K * i / N);
        im[i] = 0.0f;
    }
    neowall_test_fft(re, im, N);
    int peak = 0;
    float peak_mag = 0.0f;
    for (int i = 1; i < N / 2; i++) {
        float mag = sqrtf(re[i] * re[i] + im[i] * im[i]);
        if (mag > peak_mag) { peak_mag = mag; peak = i; }
    }
    if (peak != K) {
        fprintf(stderr, "FAIL: single_bin_sinusoid: expected peak at %d, got %d\n", K, peak);
        return 1;
    }
    /* Expected peak magnitude for cos input is N/2. */
    return expect_close("single_bin_peak_magnitude", peak_mag, N / 2.0f, 1e-3f);
}

static int test_dc(void) {
    enum { N = 32 };
    float re[N], im[N];
    for (int i = 0; i < N; i++) { re[i] = 1.0f; im[i] = 0.0f; }
    neowall_test_fft(re, im, N);
    int fails = 0;
    fails += expect_close("dc_bin_0_real", re[0], (float)N, 1e-4f);
    fails += expect_close("dc_bin_0_imag", im[0], 0.0f, 1e-4f);
    for (int i = 1; i < N; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "dc_bin_%d_real", i);
        fails += expect_close(nm, re[i], 0.0f, 1e-3f);
    }
    return fails;
}

int main(void) {
    int fails = 0;
    fails += test_roundtrip();
    fails += test_single_bin_sinusoid();
    fails += test_dc();
    if (fails) {
        fprintf(stderr, "%d FFT test failures\n", fails);
        return 1;
    }
    printf("OK: all FFT tests passed\n");
    return 0;
}
