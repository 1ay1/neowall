/*
 * Tests for the gfx layer's pure logic (slice 3).
 *
 * The interesting part of nw_gfx_target is not the GL calls, it is the
 * question "which of these two textures am I allowed to read this frame".
 * Getting that wrong gives you a shader sampling the buffer it is currently
 * writing — which on most drivers is undefined rather than an error, so it
 * shows up as flicker or feedback that decays wrong, not as a crash. That is
 * worth pinning, and it is pinnable without a GL context.
 *
 * So this links a tiny GL stub instead of a driver: the calls are recorded and
 * ignored, and the read/write/swap invariants are checked on the struct. No
 * display server, no EGL, runs anywhere.
 */

#include <stdio.h>
#include <string.h>

#include "neowall/gfx/gfx.h"

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

/* Build a target the way nw_gfx_target_create would, without touching GL.
 * The stub hands out ids, but going through create() here would drag in
 * framebuffer-completeness checks that only mean something on a real driver. */
static void fake_target(nw_gfx_target *t, bool feedback) {
    memset(t, 0, sizeof(*t));
    t->fbo            = 1;
    t->feedback       = feedback;
    t->front          = 0;
    t->needs_clear    = true;
    t->textures[0].id = 10;
    t->textures[0].width = 1920;
    t->textures[0].height = 1080;
    if (feedback) {
        t->textures[1].id = 11;
        t->textures[1].width = 1920;
        t->textures[1].height = 1080;
    }
}

/* The invariant that matters: never read the texture being written. */
static void test_feedback_read_write_never_alias(void) {
    nw_gfx_target t;
    fake_target(&t, true);

    for (int frame = 0; frame < 8; frame++) {
        const nw_gfx_texture *r = nw_gfx_target_read(&t);
        const nw_gfx_texture *w = nw_gfx_target_write(&t);

        CHECK(r != NULL && w != NULL, "frame %d: both halves should resolve", frame);
        CHECK(r != w, "frame %d: read and write must not be the same texture", frame);
        CHECK(r->id != w->id, "frame %d: aliased ids %u", frame, r->id);
        nw_gfx_target_swap(&t);
    }
}

/* A swap must actually exchange them, not just toggle a flag. */
static void test_swap_exchanges(void) {
    nw_gfx_target t;
    fake_target(&t, true);

    const nw_gfx_texture *r0 = nw_gfx_target_read(&t);
    const nw_gfx_texture *w0 = nw_gfx_target_write(&t);

    nw_gfx_target_swap(&t);

    const nw_gfx_texture *r1 = nw_gfx_target_read(&t);
    const nw_gfx_texture *w1 = nw_gfx_target_write(&t);

    CHECK(r1 == w0, "after swap, read should be what was written");
    CHECK(w1 == r0, "after swap, write should be what was read");

    /* Two swaps return to the start: the pair is a 2-cycle. */
    nw_gfx_target_swap(&t);
    CHECK(nw_gfx_target_read(&t) == r0, "two swaps return to start (read)");
    CHECK(nw_gfx_target_write(&t) == w0, "two swaps return to start (write)");
}

/* Single-buffered targets: read == write, and swapping is a no-op. Callers
 * call swap() unconditionally, so it must be safe here. */
static void test_single_buffered(void) {
    nw_gfx_target t;
    fake_target(&t, false);

    const nw_gfx_texture *r = nw_gfx_target_read(&t);
    const nw_gfx_texture *w = nw_gfx_target_write(&t);
    CHECK(r == w, "single-buffered read and write are the same texture");

    nw_gfx_target_swap(&t);
    CHECK(nw_gfx_target_read(&t) == r, "swap is a no-op when not double-buffered");
    CHECK(nw_gfx_target_write(&t) == w, "swap is a no-op when not double-buffered");
    CHECK(t.front == 0, "front index untouched");
}

/* Every accessor must tolerate a zeroed or partially-built object, because
 * error paths destroy half-constructed targets. */
static void test_safe_on_zeroed(void) {
    nw_gfx_target t;
    memset(&t, 0, sizeof(t));

    CHECK(nw_gfx_target_read(&t) == NULL, "read of uncreated target is NULL");
    CHECK(nw_gfx_target_write(&t) == NULL, "write of uncreated target is NULL");

    /* None of these may crash. */
    nw_gfx_target_swap(&t);
    nw_gfx_target_mark_clear(&t);
    nw_gfx_target_destroy(&t);
    nw_gfx_target_destroy(&t); /* double destroy */

    CHECK(nw_gfx_target_read(NULL) == NULL, "NULL target reads NULL");
    CHECK(nw_gfx_target_write(NULL) == NULL, "NULL target writes NULL");
    nw_gfx_target_swap(NULL);
    nw_gfx_target_mark_clear(NULL);
    nw_gfx_target_destroy(NULL);

    nw_gfx_program p;
    memset(&p, 0, sizeof(p));
    CHECK(nw_gfx_program_uniform(&p, "iTime") == -1, "uniform on unbuilt program is -1");
    CHECK(nw_gfx_program_uniform(NULL, "iTime") == -1, "uniform on NULL program is -1");
    nw_gfx_program_destroy(&p);
    nw_gfx_program_destroy(NULL);

    nw_gfx_texture tex;
    memset(&tex, 0, sizeof(tex));
    nw_gfx_texture_destroy(&tex);
    nw_gfx_texture_destroy(NULL);
    nw_gfx_texture_gen_mipmaps(&tex);
    nw_gfx_texture_set_filter(&tex, NW_GFX_FILTER_MIPMAP);
    CHECK(true, "zeroed texture ops survive");
}

static void test_mark_clear(void) {
    nw_gfx_target t;
    fake_target(&t, true);

    t.needs_clear = false;
    nw_gfx_target_mark_clear(&t);
    CHECK(t.needs_clear, "mark_clear sets the flag");
}

/* The format table is indexed by enum, so a reordering that desynced it would
 * silently allocate the wrong internal format. */
static void test_format_table(void) {
    CHECK(strcmp(nw_gfx_format_name(NW_GFX_FMT_RGBA8), "RGBA8") == 0, "RGBA8 name");
    CHECK(strcmp(nw_gfx_format_name(NW_GFX_FMT_RGBA16F), "RGBA16F") == 0, "RGBA16F name");
    CHECK(strcmp(nw_gfx_format_name(NW_GFX_FMT_RGBA32F), "RGBA32F") == 0, "RGBA32F name");

    CHECK(nw_gfx_format_bpp(NW_GFX_FMT_RGBA8) == 4, "RGBA8 is 4 bytes");
    CHECK(nw_gfx_format_bpp(NW_GFX_FMT_RGBA16F) == 8, "RGBA16F is 8 bytes");
    CHECK(nw_gfx_format_bpp(NW_GFX_FMT_RGBA32F) == 16, "RGBA32F is 16 bytes");

    /* Out-of-range must clamp rather than read past the table. */
    CHECK(nw_gfx_format_name((nw_gfx_format)99) != NULL, "bad format still names something");
    CHECK(nw_gfx_format_bpp((nw_gfx_format)99) > 0, "bad format still has a size");
    CHECK(nw_gfx_format_name((nw_gfx_format)-5) != NULL, "negative format is safe");
}

/* Creation must reject degenerate sizes here rather than letting GL fail
 * confusingly much later. */
static void test_rejects_bad_dimensions(void) {
    nw_gfx_texture tex;
    CHECK(nw_is_err(nw_gfx_texture_create(&tex, 0, 100, NW_GFX_FMT_RGBA8,
                                          NW_GFX_FILTER_LINEAR, NW_GFX_WRAP_CLAMP, NULL)),
          "zero width rejected");
    CHECK(nw_is_err(nw_gfx_texture_create(&tex, 100, -1, NW_GFX_FMT_RGBA8,
                                          NW_GFX_FILTER_LINEAR, NW_GFX_WRAP_CLAMP, NULL)),
          "negative height rejected");
    CHECK(nw_is_err(nw_gfx_texture_create(NULL, 10, 10, NW_GFX_FMT_RGBA8,
                                          NW_GFX_FILTER_LINEAR, NW_GFX_WRAP_CLAMP, NULL)),
          "null out rejected");

    nw_gfx_texture zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    CHECK(nw_is_err(nw_gfx_texture_resize(&zeroed, 10, 10)), "resize of uncreated rejected");

    nw_gfx_program prog;
    CHECK(nw_is_err(nw_gfx_program_build(&prog, NULL, "x")), "null vertex source rejected");
    CHECK(nw_is_err(nw_gfx_program_build(&prog, "x", NULL)), "null fragment source rejected");
    CHECK(nw_is_err(nw_gfx_program_build(NULL, "x", "y")), "null out rejected");
}

int main(void) {
    test_feedback_read_write_never_alias();
    test_swap_exchanges();
    test_single_buffered();
    test_safe_on_zeroed();
    test_mark_clear();
    test_format_table();
    test_rejects_bad_dimensions();

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
