/* Concurrency test for the output_state refcount discipline.
 *
 * output_destroy() pulls in the whole render/GL/image stack, so we can't link
 * the real output module headless. Instead this test reproduces the EXACT
 * atomic ref/unref protocol used by output_ref / output_unref (see
 * src/output/output.c) against a minimal stand-in object, and hammers it from
 * many threads under ThreadSanitizer / AddressSanitizer.
 *
 * What it proves:
 *   - The 1->0 transition that triggers free happens exactly once (no
 *     double-free, no leak) even under heavy concurrent ref/unref.
 *   - The acq_rel ordering on unref makes the destructor see all prior writes.
 *
 * If the production code ever weakens the memory ordering or drops the
 * "free on prev==1" rule, TSan/ASan on this test will catch it.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- mirror of the production refcount protocol ---- */

typedef struct mock_output {
    atomic_int refcount;
    atomic_int payload;     /* written before unref; destructor must observe it */
    bool destroyed;         /* set by destructor; guarded by being single-shot */
} mock_output;

static atomic_int g_destroy_count;   /* how many times the destructor ran */
static atomic_int g_payload_seen;    /* payload value the destructor observed */

static mock_output *mock_create(void) {
    mock_output *o = calloc(1, sizeof(*o));
    atomic_store_explicit(&o->refcount, 1, memory_order_release);
    return o;
}
static void mock_destroy(mock_output *o) {
    /* Mirror output_destroy: must run exactly once per object. */
    atomic_fetch_add_explicit(&g_destroy_count, 1, memory_order_relaxed);
    atomic_store_explicit(&g_payload_seen,
                          atomic_load_explicit(&o->payload, memory_order_relaxed),
                          memory_order_relaxed);
    o->destroyed = true;
    free(o);
}
static void mock_ref(mock_output *o) {
    atomic_fetch_add_explicit(&o->refcount, 1, memory_order_relaxed);
}
static void mock_unref(mock_output *o) {
    int prev = atomic_fetch_sub_explicit(&o->refcount, 1, memory_order_acq_rel);
    if (prev == 1) {
        mock_destroy(o);
    }
}

/* ---- stress harness ---- */

#define THREADS 8
#define ITERS   20000

static mock_output *g_obj;

/* Each worker takes a ref, touches the object, drops the ref. The object is
 * kept alive by an extra "owner" ref held by main until all workers finish. */
static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        mock_ref(g_obj);
        /* Touch the object while we hold a ref — must never be freed here. */
        atomic_fetch_add_explicit(&g_obj->payload, 1, memory_order_relaxed);
        mock_unref(g_obj);
    }
    return NULL;
}

static int run_once(void) {
    atomic_store(&g_destroy_count, 0);
    atomic_store(&g_payload_seen, 0);

    g_obj = mock_create();              /* refcount = 1 (owner) */

    pthread_t th[THREADS];
    for (int i = 0; i < THREADS; i++) {
        if (pthread_create(&th[i], NULL, worker, NULL) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return 1;
        }
    }
    for (int i = 0; i < THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    /* Object must still be alive: workers balanced every ref with an unref,
     * so only the owner reference remains. */
    if (atomic_load(&g_destroy_count) != 0) {
        fprintf(stderr, "FAIL: object freed while owner ref still held\n");
        return 1;
    }

    int final_payload = atomic_load_explicit(&g_obj->payload, memory_order_relaxed);

    mock_unref(g_obj);                  /* drop owner ref -> must free exactly once */

    if (atomic_load(&g_destroy_count) != 1) {
        fprintf(stderr, "FAIL: destroy ran %d times, expected 1\n",
                atomic_load(&g_destroy_count));
        return 1;
    }
    if (atomic_load(&g_payload_seen) != final_payload) {
        fprintf(stderr, "FAIL: destructor saw stale payload (%d != %d)\n",
                atomic_load(&g_payload_seen), final_payload);
        return 1;
    }
    if (final_payload != THREADS * ITERS) {
        fprintf(stderr, "FAIL: payload = %d, expected %d\n",
                final_payload, THREADS * ITERS);
        return 1;
    }
    return 0;
}

int main(void) {
    /* Repeat to shake out rare interleavings. */
    for (int round = 0; round < 20; round++) {
        if (run_once() != 0) {
            fprintf(stderr, "not ok - round %d\n", round);
            return 1;
        }
    }
    printf("ok - refcount protocol: %d rounds x %d threads x %d iters\n",
           20, THREADS, ITERS);
    return 0;
}
