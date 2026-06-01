/* Unit tests for the foundation primitives: result, vec, defer.
 *
 * All three are header-only and dependency-free, so this runs headless in CI
 * under ASan/UBSan/LSan with no display server.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/result.h"
#include "neowall/vec.h"
#include "neowall/defer.h"

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

/* ===== result ===== */

static nw_result inner_fail(void) {
    return nw_err(NW_ERR_PARSE, "bad token");
}
static nw_result outer_propagates(void) {
    NW_TRY(nw_ok());          /* should not short-circuit */
    NW_TRY(inner_fail());     /* should return the error */
    return nw_ok();           /* unreachable */
}

static void test_result(void) {
    CHECK(nw_is_ok(nw_ok()));
    CHECK(!nw_is_err(nw_ok()));
    CHECK(nw_ok().status == NW_OK);   /* NW_OK must be the zero value */

    nw_result e = nw_err(NW_ERR_OOM, "alloc");
    CHECK(nw_is_err(e));
    CHECK(e.status == NW_ERR_OOM);
    CHECK(strcmp(e.context, "alloc") == 0);
    CHECK(strcmp(nw_status_str(NW_ERR_OOM), "out-of-memory") == 0);

    nw_result prop = outer_propagates();
    CHECK(prop.status == NW_ERR_PARSE);
    CHECK(strcmp(prop.context, "bad token") == 0);

    /* zero-init defaults to success */
    nw_result zero = {0};
    CHECK(nw_is_ok(zero));
}

/* ===== vec: void core ===== */

static void test_vec_core(void) {
    nw_vec v;
    nw_vec_init(&v, sizeof(int));
    CHECK(v.len == 0 && v.data == NULL);

    for (int i = 0; i < 1000; i++) {
        CHECK(nw_vec_push(&v, &i));
    }
    CHECK(v.len == 1000);
    CHECK(v.cap >= 1000);
    CHECK(*(int *)nw_vec_at(&v, 0) == 0);
    CHECK(*(int *)nw_vec_at(&v, 999) == 999);
    CHECK(nw_vec_at(&v, 1000) == NULL);   /* out of range */

    nw_vec_clear(&v, NULL);
    CHECK(v.len == 0);
    CHECK(v.cap >= 1000);                 /* capacity retained */

    nw_vec_free(&v, NULL);
    CHECK(v.data == NULL && v.cap == 0);
}

/* vec of owning pointers, freed via dtor */
static void free_str(void *p) { free(*(char **)p); }

static void test_vec_owning(void) {
    nw_vec v;
    nw_vec_init(&v, sizeof(char *));
    for (int i = 0; i < 5; i++) {
        char *s = malloc(16);
        snprintf(s, 16, "item-%d", i);
        CHECK(nw_vec_push(&v, &s));
    }
    CHECK(strcmp(*(char **)nw_vec_at(&v, 2), "item-2") == 0);
    nw_vec_free(&v, free_str);   /* must not leak (LSan checks) */
    CHECK(v.data == NULL);
}

/* ===== vec: typed wrapper ===== */
NW_VEC_DEFINE_STATIC(int_vec, int)

static void test_vec_typed(void) {
    int_vec v;
    int_vec_init(&v);
    for (int i = 0; i < 100; i++) {
        CHECK(int_vec_push(&v, i * i));
    }
    CHECK(v.len == 100);
    CHECK(*int_vec_at(&v, 10) == 100);
    CHECK(v.data[99] == 99 * 99);     /* direct indexing works */
    CHECK(int_vec_at(&v, 100) == NULL);
    int_vec_free(&v);
    CHECK(v.data == NULL && v.len == 0);
}

/* ===== defer ===== */

static int g_freed = 0;
static void counting_free(void *p) {
    void **pp = (void **)p;
    if (*pp) { g_freed++; free(*pp); }
}

static void defer_scope(void) {
    NW_DEFER(counting_free) char *a = malloc(8);
    (void)a;
    NW_DEFER(counting_free) char *b = malloc(8);
    (void)b;
    /* early return: both a and b must be freed */
    return;
}

static void test_defer(void) {
#if NW_HAVE_DEFER
    g_freed = 0;
    defer_scope();
    CHECK(g_freed == 2);   /* both cleaned up on scope exit */

    /* NW_DEFER_FREE on a normal scope exit, no leak (LSan verifies) */
    {
        NW_DEFER_FREE char *buf = malloc(64);
        CHECK(buf != NULL);
        memset(buf, 0, 64);
    }
#else
    fprintf(stderr, "SKIP defer: cleanup attribute unavailable\n");
#endif
}

int main(void) {
    test_result();
    test_vec_core();
    test_vec_owning();
    test_vec_typed();
    test_defer();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
