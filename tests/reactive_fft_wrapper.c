/* Wrapper TU that materialises the FFT under its test-only name. The actual
 * implementation is in reactive_fft.h (single source of truth) and the
 * NEOWALL_TESTING flag flips it from `static fft` to non-static
 * `neowall_test_fft`. The test target compiles this file and the test driver
 * (test_reactive_fft.c) together. */
#define NEOWALL_TESTING 1
#include "../src/shader/reactive_fft.h"
