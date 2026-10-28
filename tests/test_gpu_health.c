/* gpu_health: the pure parts (reset-status classifier, swap-failure streak).
 * The GL/EGL parts need a real driver and are exercised live. */
#include "neowall/egl/gpu_health.h"

#include <stdio.h>

/* gpu_health.c logs; these keep the test free of utils.c. */
void log_error(const char *f, ...) { (void)f; }
void log_warn(const char *f, ...) { (void)f; }
void log_info(const char *f, ...) { (void)f; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void) {
    CHECK(!gpu_health_status_is_lost(0));           /* GL_NO_ERROR */
    CHECK(gpu_health_status_is_lost(0x8253));       /* guilty */
    CHECK(gpu_health_status_is_lost(0x8254));       /* innocent */
    CHECK(gpu_health_status_is_lost(0x8255));       /* unknown */
    CHECK(!gpu_health_status_is_lost(0x0500));      /* GL_INVALID_ENUM isn't a reset */

    uint32_t streak = 0;
    for (int i = 1; i < NW_GPU_SWAP_FAIL_LIMIT; i++) CHECK(!gpu_health_swap_result(&streak, false));
    CHECK(gpu_health_swap_result(&streak, false));  /* hits the limit exactly once */
    CHECK(!gpu_health_swap_result(&streak, false)); /* doesn't fire again past it */
    CHECK(!gpu_health_swap_result(&streak, true));
    CHECK(streak == 0);                             /* a good swap resets it */
    CHECK(!gpu_health_swap_result(NULL, false));

    /* With no context, reset detection is off and never reports a loss. */
    CHECK(!gpu_health_lost());

    printf("gpu_health: %d failures\n", fails);
    return fails ? 1 : 0;
}
