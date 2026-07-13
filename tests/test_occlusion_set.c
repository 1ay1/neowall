/* Unit tests for the Wayland toplevel output set (occlusion_set.c).
 *
 * wlr-foreign-toplevel-management sends one output_enter per output a toplevel
 * is visible on, and the protocol allows a toplevel to be visible on several
 * outputs at once, so occlusion is a set-membership question. Tracking only the
 * most recently entered output loses every other one, and a window is then
 * missed as an occluder on the outputs it also covers.
 *
 * The set stores opaque wl_output* handles, so these tests use fake pointers
 * and need no display.
 */
#include <stdbool.h>
#include <stdio.h>

#include "occlusion_set.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

/* Stand-ins for wl_output handles; only their addresses matter. */
static char out_a, out_b, out_c;
#define A ((void *)&out_a)
#define B ((void *)&out_b)
#define C ((void *)&out_c)

static void test_empty_set_contains_nothing(void) {
    output_set s;
    output_set_init(&s);
    CHECK(!output_set_contains(&s, A));
    CHECK(!output_set_contains(&s, NULL));
    output_set_free(&s);
}

static void test_membership_is_per_output(void) {
    output_set s;
    output_set_init(&s);
    CHECK(output_set_add(&s, A));
    CHECK(output_set_contains(&s, A));
    CHECK(!output_set_contains(&s, B));

    /* Entering a second output must not evict the first (the single-pointer
     * bug: last enter wins, so a window straddling A and B is only ever
     * attributed to B). */
    CHECK(output_set_add(&s, B));
    CHECK(output_set_contains(&s, A));
    CHECK(output_set_contains(&s, B));
    CHECK(!output_set_contains(&s, C));
    CHECK(s.len == 2);
    output_set_free(&s);
}

static void test_add_is_idempotent(void) {
    output_set s;
    output_set_init(&s);
    CHECK(output_set_add(&s, A));
    CHECK(output_set_add(&s, A));
    CHECK(output_set_add(&s, A));
    CHECK(s.len == 1);
    CHECK(!output_set_add(&s, NULL));
    CHECK(s.len == 1);
    output_set_free(&s);
}

static void test_leave_removes_only_that_output(void) {
    output_set s;
    output_set_init(&s);
    output_set_add(&s, A);
    output_set_add(&s, B);
    output_set_add(&s, C);

    /* Leaving one output must not drop the toplevel's other outputs (the
     * single-pointer bug: leaving the tracked output cleared it outright, so a
     * window still fullscreen elsewhere stopped counting as an occluder). */
    output_set_remove(&s, B);
    CHECK(s.len == 2);
    CHECK(output_set_contains(&s, A));
    CHECK(!output_set_contains(&s, B));
    CHECK(output_set_contains(&s, C));

    /* Removing an absent output is a no-op. */
    output_set_remove(&s, B);
    CHECK(s.len == 2);

    output_set_remove(&s, A);
    output_set_remove(&s, C);
    CHECK(s.len == 0);
    CHECK(!output_set_contains(&s, A));
    output_set_free(&s);
}

static void test_reenter_after_leave(void) {
    output_set s;
    output_set_init(&s);
    output_set_add(&s, A);
    output_set_remove(&s, A);
    CHECK(!output_set_contains(&s, A));
    CHECK(output_set_add(&s, A));
    CHECK(output_set_contains(&s, A));
    CHECK(s.len == 1);
    output_set_free(&s);
}

static void test_grows_past_initial_capacity(void) {
    static char outs[64];
    output_set s;
    output_set_init(&s);
    for (int i = 0; i < 64; i++) {
        CHECK(output_set_add(&s, &outs[i]));
    }
    CHECK(s.len == 64);
    for (int i = 0; i < 64; i++) {
        CHECK(output_set_contains(&s, &outs[i]));
    }
    output_set_free(&s);
    CHECK(s.len == 0);
}

int main(void) {
    test_empty_set_contains_nothing();
    test_membership_is_per_output();
    test_add_is_idempotent();
    test_leave_removes_only_that_output();
    test_reenter_after_leave();
    test_grows_past_initial_capacity();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
