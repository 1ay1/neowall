/*
 * Tests for the render graph (slice 4).
 *
 * Two things here are worth real effort.
 *
 * 1. Feedback vs cycle. A pass sampling its own previous frame is legal and is
 *    the basis of every accumulation buffer; A->B->A within one frame is
 *    unsatisfiable. Both look like "a cycle" to a naive sort. Getting the first
 *    one wrong breaks every Buffer A shader in the wild; getting the second one
 *    wrong means rendering something subtly and permanently incorrect.
 *
 * 2. The ordering contract. Everything downstream assumes that when a pass
 *    runs, everything it samples has already run this frame. If the sort is
 *    wrong the symptom is a one-frame lag somewhere in a multipass chain, which
 *    is nearly impossible to spot by eye and trivial to assert here.
 *
 * Also checks the thing this slice exists for: N passes and M bindings, past
 * the old MULTIPASS_MAX_PASSES 5 / MULTIPASS_MAX_CHANNELS 4 ceilings.
 *
 * Construction and ordering need no GL, so those run pure. State building goes
 * through the GL stub.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/graph/graph.h"

static int checks   = 0;
static int failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

static char *dup_src(const char *s) {
    char *out = malloc(strlen(s) + 1);
    if (out) {
        strcpy(out, s);
    }
    return out;
}

/* Position of a pass in the execution order, or -1. */
static int order_pos(const nw_graph *g, int pass) {
    for (size_t i = 0; i < g->order_len; i++) {
        if (g->order[i] == pass) {
            return (int)i;
        }
    }
    return -1;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

static void test_add_and_find(void) {
    nw_graph g;
    nw_graph_init(&g);

    CHECK(g.output == -1, "empty graph has no output");
    CHECK(!nw_graph_is_finalized(&g), "empty graph is not finalized");

    int a   = nw_graph_add_pass(&g, "Buffer A", dup_src("a"), 0);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    CHECK(a == 0, "first pass is index 0");
    CHECK(img == 1, "second pass is index 1");
    CHECK(g.output == img, "OUTPUT flag records the output pass");

    CHECK(nw_graph_find_pass(&g, "Buffer A") == a, "find by exact name");
    CHECK(nw_graph_find_pass(&g, "buffer a") == a, "find is case-insensitive");
    CHECK(nw_graph_find_pass(&g, "Image") == img, "find image");
    CHECK(nw_graph_find_pass(&g, "Buffer Z") == -1, "absent pass is -1");
    CHECK(nw_graph_find_pass(&g, NULL) == -1, "NULL name is -1");

    nw_graph_free(&g);
}

/* The ceiling this slice removes. The old engine could not express either of
 * these: 5 passes and 4 channels were array bounds. */
static void test_beyond_the_old_limits(void) {
    nw_graph g;
    nw_graph_init(&g);

    /* Twelve passes, where the old cap was five. */
    char name[32];
    for (int i = 0; i < 12; i++) {
        snprintf(name, sizeof(name), "Pass%d", i);
        int idx = nw_graph_add_pass(&g, name, dup_src("x"), 0);
        CHECK(idx == i, "pass %d added at the right index", i);
    }
    int out = nw_graph_add_pass(&g, "Out", dup_src("o"), NW_PASS_FLAG_OUTPUT);
    CHECK(g.passes.len == 13, "13 passes, got %zu", g.passes.len);

    /* Nine bindings on one pass, where the old cap was four. */
    for (int slot = 0; slot < 9; slot++) {
        CHECK(nw_is_ok(nw_graph_bind_pass(&g, out, slot, slot)), "bind slot %d", slot);
    }
    CHECK(g.passes.data[out].bindings.len == 9, "9 bindings, got %zu",
          g.passes.data[out].bindings.len);

    CHECK(nw_is_ok(nw_graph_finalize(&g)), "13-pass graph finalizes");
    CHECK(g.order_len == 13, "order covers every pass");

    nw_graph_free(&g);
}

/* Binding the same slot twice must replace, not stack, so a manifest can
 * override a frontend's guess. */
static void test_rebinding_a_slot_replaces(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a   = nw_graph_add_pass(&g, "A", dup_src("a"), 0);
    int b   = nw_graph_add_pass(&g, "B", dup_src("b"), 0);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    nw_graph_bind_pass(&g, img, 0, a);
    CHECK(g.passes.data[img].bindings.len == 1, "one binding");
    CHECK(g.passes.data[img].bindings.data[0].pass == a, "bound to A");

    nw_graph_bind_pass(&g, img, 0, b);
    CHECK(g.passes.data[img].bindings.len == 1, "still one binding, not two");
    CHECK(g.passes.data[img].bindings.data[0].pass == b, "now bound to B");

    nw_graph_free(&g);
}

static void test_rejects_bad_bindings(void) {
    nw_graph g;
    nw_graph_init(&g);

    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    CHECK(nw_is_err(nw_graph_bind_pass(&g, 99, 0, img)), "bind to nonexistent pass fails");
    CHECK(nw_is_err(nw_graph_bind_pass(&g, img, 0, 99)), "bind from nonexistent pass fails");
    CHECK(nw_is_err(nw_graph_bind_pass(&g, img, -1, img)), "negative slot fails");
    CHECK(nw_is_err(nw_graph_bind_source(&g, img, 0, NULL)), "null source fails");
    CHECK(nw_is_err(nw_graph_add_uniform(&g, img, NULL, (nw_source *)1)), "null name fails");
    CHECK(nw_is_err(nw_graph_add_uniform(&g, img, "u", NULL)), "null source fails");
    CHECK(nw_is_err(nw_graph_add_uniform(&g, 99, "u", (nw_source *)1)), "bad pass fails");

    nw_graph_free(&g);
}

static void test_uniform_replace(void) {
    nw_graph g;
    nw_graph_init(&g);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    nw_source *s1 = (nw_source *)0x1;
    nw_source *s2 = (nw_source *)0x2;

    CHECK(nw_is_ok(nw_graph_add_uniform(&g, img, "uGlow", s1)), "add uniform");
    CHECK(g.passes.data[img].uniforms.len == 1, "one uniform");

    CHECK(nw_is_ok(nw_graph_add_uniform(&g, img, "uGlow", s2)), "redeclare uniform");
    CHECK(g.passes.data[img].uniforms.len == 1, "still one, replaced not stacked");
    CHECK(g.passes.data[img].uniforms.data[0].source == s2, "later declaration wins");

    nw_graph_free(&g);
}

/* ==========================================================================
 * Ordering — the part everything downstream depends on
 * ========================================================================== */

/* A self-edge is feedback and must be legal, because every accumulation buffer
 * in every Shadertoy shader is exactly this. */
static void test_self_feedback_is_legal(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a   = nw_graph_add_pass(&g, "Buffer A", dup_src("a"), 0);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    CHECK(nw_is_ok(nw_graph_bind_pass(&g, a, 0, a)), "pass may sample itself");
    CHECK((g.passes.data[a].flags & NW_PASS_FLAG_FEEDBACK) != 0,
          "self-binding should mark the pass as feedback automatically");

    nw_graph_bind_pass(&g, img, 0, a);

    CHECK(nw_is_ok(nw_graph_finalize(&g)), "self-feedback must not be read as a cycle");
    CHECK(order_pos(&g, a) < order_pos(&g, img), "A still runs before Image");

    nw_graph_free(&g);
}

/* A real cycle must be refused. */
static void test_real_cycle_is_refused(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a = nw_graph_add_pass(&g, "A", dup_src("a"), 0);
    int b = nw_graph_add_pass(&g, "B", dup_src("b"), 0);
    nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    nw_graph_bind_pass(&g, a, 0, b); /* A reads B */
    nw_graph_bind_pass(&g, b, 0, a); /* B reads A */

    CHECK(nw_is_err(nw_graph_finalize(&g)), "A<->B cycle must be refused");

    nw_graph_free(&g);
}

/* Three-hop cycles too, not just adjacent pairs. */
static void test_long_cycle_is_refused(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a = nw_graph_add_pass(&g, "A", dup_src("a"), 0);
    int b = nw_graph_add_pass(&g, "B", dup_src("b"), 0);
    int c = nw_graph_add_pass(&g, "C", dup_src("c"), 0);
    nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);

    nw_graph_bind_pass(&g, a, 0, c);
    nw_graph_bind_pass(&g, b, 0, a);
    nw_graph_bind_pass(&g, c, 0, b);

    CHECK(nw_is_err(nw_graph_finalize(&g)), "A->B->C->A cycle must be refused");

    nw_graph_free(&g);
}

/*
 * The ordering invariant, stated directly: for every pass, everything it
 * samples (other than itself) appears earlier in the order.
 */
static void test_dependencies_run_first(void) {
    nw_graph g;
    nw_graph_init(&g);

    /* A deliberately awkward shape: declared in an order that is NOT the
     * execution order, so a sort that just returned 0..n-1 would pass the
     * length check and fail here. */
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);
    int d   = nw_graph_add_pass(&g, "D", dup_src("d"), 0);
    int c   = nw_graph_add_pass(&g, "C", dup_src("c"), 0);
    int b   = nw_graph_add_pass(&g, "B", dup_src("b"), 0);
    int a   = nw_graph_add_pass(&g, "A", dup_src("a"), 0);

    nw_graph_bind_pass(&g, b, 0, a);   /* B needs A */
    nw_graph_bind_pass(&g, c, 0, b);   /* C needs B */
    nw_graph_bind_pass(&g, d, 0, c);   /* D needs C */
    nw_graph_bind_pass(&g, img, 0, d); /* Image needs D */
    nw_graph_bind_pass(&g, a, 0, a);   /* A feeds back on itself */

    CHECK(nw_is_ok(nw_graph_finalize(&g)), "chain finalizes");

    for (size_t i = 0; i < g.passes.len; i++) {
        const nw_pass *p = &g.passes.data[i];
        for (size_t bi = 0; bi < p->bindings.len; bi++) {
            const nw_binding *bind = &p->bindings.data[bi];
            if (bind->kind != NW_BIND_PASS || (size_t)bind->pass == i) {
                continue;
            }
            CHECK(order_pos(&g, bind->pass) < order_pos(&g, (int)i),
                  "pass %zu samples %d, which must run earlier (%d vs %d)", i, bind->pass,
                  order_pos(&g, bind->pass), order_pos(&g, (int)i));
        }
    }

    CHECK(order_pos(&g, a) < order_pos(&g, b), "A before B");
    CHECK(order_pos(&g, b) < order_pos(&g, c), "B before C");
    CHECK(order_pos(&g, c) < order_pos(&g, d), "C before D");
    CHECK(order_pos(&g, d) < order_pos(&g, img), "D before Image");

    nw_graph_free(&g);
}

static void test_finalize_validation(void) {
    nw_graph g;

    nw_graph_init(&g);
    CHECK(nw_is_err(nw_graph_finalize(&g)), "empty graph is refused");
    nw_graph_free(&g);

    /* No output pass. */
    nw_graph_init(&g);
    nw_graph_add_pass(&g, "A", dup_src("a"), 0);
    CHECK(nw_is_err(nw_graph_finalize(&g)), "graph with no output is refused");
    nw_graph_free(&g);

    CHECK(nw_is_err(nw_graph_finalize(NULL)), "NULL graph is refused");

    /* Structural change must invalidate a previous order. */
    nw_graph_init(&g);
    nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);
    CHECK(nw_is_ok(nw_graph_finalize(&g)), "single output pass finalizes");
    CHECK(nw_graph_is_finalized(&g), "is finalized");
    nw_graph_add_pass(&g, "Late", dup_src("l"), 0);
    CHECK(!nw_graph_is_finalized(&g), "adding a pass invalidates the order");
    nw_graph_free(&g);
}

/* ==========================================================================
 * GL state (through the stub)
 * ========================================================================== */

static void test_state_build_and_resize(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a   = nw_graph_add_pass(&g, "Buffer A", dup_src("a"), 0);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);
    nw_graph_bind_pass(&g, a, 0, a);
    nw_graph_bind_pass(&g, img, 0, a);

    nw_graph_state s;
    nw_graph_state_init(&s);

    /* Building before finalize is a programming error, not a silent success. */
    CHECK(nw_is_err(nw_graph_state_build(&s, &g, 1920, 1080)), "build before finalize refused");

    CHECK(nw_is_ok(nw_graph_finalize(&g)), "finalize");
    CHECK(nw_is_ok(nw_graph_state_build(&s, &g, 1920, 1080)), "build");

    CHECK(s.passes.len == 2, "state has one entry per pass");
    CHECK(nw_graph_state_target(&s, a) != NULL, "buffer pass has a target");
    CHECK(nw_graph_state_target(&s, img) == NULL, "output pass has no target");

    /* Feedback must have produced a double-buffered target. */
    nw_gfx_target *t = nw_graph_state_target(&s, a);
    CHECK(t != NULL, "feedback pass has a target");
    if (t) {
        CHECK(t->feedback, "self-sampling pass got a feedback target");
        CHECK(nw_gfx_target_read(t) != nw_gfx_target_write(t), "feedback halves differ");
    }

    CHECK(nw_is_ok(nw_graph_state_resize(&s, &g, 1280, 720)), "resize");
    CHECK(s.width == 1280 && s.height == 720, "state records the new size");
    CHECK(s.passes.data[a].width == 1280, "pass resized, got %d", s.passes.data[a].width);

    CHECK(nw_is_err(nw_graph_state_build(&s, &g, 0, 100)), "zero width refused");
    CHECK(nw_is_err(nw_graph_state_resize(&s, &g, -1, 100)), "negative resize refused");

    nw_graph_state_free(&s);
    nw_graph_free(&g);
}

/* A scaled pass renders smaller, and must never round to zero. */
static void test_pass_scale(void) {
    nw_graph g;
    nw_graph_init(&g);

    int half = nw_graph_add_pass(&g, "Half", dup_src("h"), 0);
    nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);
    g.passes.data[half].scale = 0.5f;

    int w = 0, h = 0;
    nw_graph_pass_size(&g, half, 1920, 1080, &w, &h);
    CHECK(w == 960 && h == 540, "0.5 scale halves the size, got %dx%d", w, h);

    /* Absurd downscale on a small output must clamp, not produce a 0x0 target
     * that GL rejects much later and much less clearly. */
    g.passes.data[half].scale = 0.001f;
    nw_graph_pass_size(&g, half, 100, 100, &w, &h);
    CHECK(w >= 1 && h >= 1, "scale never rounds to zero, got %dx%d", w, h);

    /* A nonsense scale falls back to native rather than producing garbage. */
    g.passes.data[half].scale = 0.0f;
    nw_graph_pass_size(&g, half, 800, 600, &w, &h);
    CHECK(w == 800 && h == 600, "zero scale falls back to native, got %dx%d", w, h);

    nw_graph_free(&g);
}

/* Binding resolution: a feedback pass must resolve to the texture it is NOT
 * writing, and an unbound slot must resolve to 0 (black) rather than leaving
 * whatever was last in the sampler unit. */
static void test_binding_resolution(void) {
    nw_graph g;
    nw_graph_init(&g);

    int a   = nw_graph_add_pass(&g, "Buffer A", dup_src("a"), 0);
    int img = nw_graph_add_pass(&g, "Image", dup_src("i"), NW_PASS_FLAG_OUTPUT);
    nw_graph_bind_pass(&g, a, 0, a);
    nw_graph_bind_pass(&g, img, 0, a);
    nw_graph_finalize(&g);

    nw_graph_state s;
    nw_graph_state_init(&s);
    nw_graph_state_build(&s, &g, 640, 480);

    nw_gfx_target *t = nw_graph_state_target(&s, a);
    CHECK(t != NULL, "buffer pass target exists");
    if (!t) {
        nw_graph_state_free(&s);
        nw_graph_free(&g);
        return;
    }

    unsigned resolved = nw_graph_state_resolve_binding(&s, &g, a, 0);
    CHECK(resolved != 0, "feedback binding resolves to a texture");
    CHECK(resolved == nw_gfx_target_read(t)->id, "resolves to the READ half");
    CHECK(resolved != nw_gfx_target_write(t)->id, "never the half being written");

    /* And it must follow the flip. */
    nw_gfx_target_swap(t);
    unsigned after = nw_graph_state_resolve_binding(&s, &g, a, 0);
    CHECK(after != resolved, "resolution follows the ping-pong flip");
    CHECK(after == nw_gfx_target_read(t)->id, "still the read half after swap");

    CHECK(nw_graph_state_resolve_binding(&s, &g, img, 3) == 0, "unbound slot resolves to 0");
    CHECK(nw_graph_state_resolve_binding(&s, &g, 99, 0) == 0, "bad pass resolves to 0");
    CHECK(nw_graph_state_resolve_binding(NULL, &g, 0, 0) == 0, "null state resolves to 0");

    /* The output pass has no target, so sampling it yields nothing rather than
     * a dangling id. */
    nw_graph_bind_pass(&g, a, 1, img);
    CHECK(nw_graph_state_resolve_binding(&s, &g, a, 1) == 0,
          "binding the targetless output pass resolves to 0");

    nw_graph_state_free(&s);
    nw_graph_free(&g);
}

static void test_safe_on_zeroed(void) {
    nw_graph_init(NULL);
    nw_graph_free(NULL);
    nw_graph_state_init(NULL);
    nw_graph_state_free(NULL);

    CHECK(nw_graph_add_pass(NULL, "x", NULL, 0) == -1, "add to NULL graph is -1");
    CHECK(nw_graph_find_pass(NULL, "x") == -1, "find in NULL graph is -1");
    CHECK(!nw_graph_is_finalized(NULL), "NULL graph is not finalized");
    CHECK(nw_graph_state_target(NULL, 0) == NULL, "target of NULL state is NULL");
    CHECK(nw_graph_state_error(NULL, 0) == NULL, "error of NULL state is NULL");
    CHECK(!nw_graph_state_is_ready(NULL, NULL), "NULL state is not ready");

    nw_graph g;
    nw_graph_init(&g);
    nw_graph_free(&g);
    nw_graph_free(&g); /* double free must be safe */
    CHECK(true, "double free survives");
}

int main(void) {
    test_add_and_find();
    test_beyond_the_old_limits();
    test_rebinding_a_slot_replaces();
    test_rejects_bad_bindings();
    test_uniform_replace();
    test_self_feedback_is_legal();
    test_real_cycle_is_refused();
    test_long_cycle_is_refused();
    test_dependencies_run_first();
    test_finalize_validation();
    test_state_build_and_resize();
    test_pass_scale();
    test_binding_resolution();
    test_safe_on_zeroed();

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
