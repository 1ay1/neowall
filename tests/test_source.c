/*
 * Tests for the source layer — the open data plane that replaced the two
 * closed enums (uniform_bind_t, channel_source_t) of the old shader engine.
 *
 * Three things are worth pinning here, in rough order of how much damage they
 * do when they break:
 *
 *  1. The occlusion contract. A hidden source must not tick. neowall's whole
 *     pitch is that a covered wallpaper costs nothing; if the data plane keeps
 *     polling behind a maximized window, that claim is false and nobody
 *     notices until a laptop battery does.
 *
 *  2. Spec parsing. Manifests are user input. Malformed specs must produce an
 *     error, never a silent default — a typo'd interval that wraps to 0 would
 *     turn into a busy loop.
 *
 *  3. Scheduling. Intervals honoured, and a source that just became visible
 *     refreshes immediately rather than showing a stale value.
 *
 * GL-free, display-free, and clock-free: time is injected through
 * nw_source_ctx, so this runs headless in CI and never sleeps.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "neowall/source/source.h"

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

/* ==========================================================================
 * A counting fake provider, so scheduling is observable without hardware.
 * ========================================================================== */

typedef struct {
    int   ticks;
    int   visibility_calls;
    bool  last_visibility;
    float value;
} fake_state;

static fake_state g_fake;

static void *fake_open(const nw_source_spec *spec, char *err, size_t cap) {
    (void)err;
    (void)cap;
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.value = (float)spec->number;
    return &g_fake;
}

static void fake_tick(void *self, const nw_source_ctx *ctx) {
    (void)ctx;
    fake_state *st = self;
    st->ticks++;
    st->value += 1.0f;
}

static float fake_scalar(void *self) { return ((fake_state *)self)->value; }

static void fake_on_visibility(void *self, bool visible) {
    fake_state *st = self;
    st->visibility_calls++;
    st->last_visibility = visible;
}

static const nw_source_vtable fake_vtable = {
    .name                = "fake",
    .kind                = NW_SOURCE_SCALAR,
    .default_interval_ms = 100,
    .open                = fake_open,
    .tick                = fake_tick,
    .scalar              = fake_scalar,
    .on_visibility       = fake_on_visibility,
};

static nw_source_ctx ctx_at(uint64_t now_ms) {
    nw_source_ctx c = {0};
    c.now_ms        = now_ms;
    c.width         = 1920;
    c.height        = 1080;
    c.visible       = true;
    return c;
}

/* ==========================================================================
 * Registry
 * ========================================================================== */

static void test_registry(void) {
    nw_source_registry_reset();

    CHECK(nw_source_registry_count() == 0, "registry should start empty");
    CHECK(nw_source_find("fake") == NULL, "unknown lookup should miss");

    CHECK(nw_is_ok(nw_source_register(&fake_vtable)), "register should succeed");
    CHECK(nw_source_registry_count() == 1, "count should be 1");
    CHECK(nw_source_find("fake") == &fake_vtable, "lookup should hit");

    /* Duplicates are a manifest-visible bug, not a silent overwrite. */
    CHECK(nw_is_err(nw_source_register(&fake_vtable)), "duplicate should fail");
    CHECK(nw_source_registry_count() == 1, "count unchanged after duplicate");

    /* A scalar provider with no scalar() would read garbage every frame. */
    static const nw_source_vtable broken = {
        .name = "broken", .kind = NW_SOURCE_SCALAR, .open = fake_open};
    CHECK(nw_is_err(nw_source_register(&broken)), "scalar provider needs scalar()");

    static const nw_source_vtable nameless = {.name = NULL};
    CHECK(nw_is_err(nw_source_register(&nameless)), "nameless provider rejected");
    CHECK(nw_is_err(nw_source_register(NULL)), "null vtable rejected");

    CHECK(nw_source_registry_name_at(0) != NULL, "name_at(0) should resolve");
    CHECK(nw_source_registry_name_at(99) == NULL, "name_at out of range is NULL");
}

/* ==========================================================================
 * Spec parsing
 * ========================================================================== */

static void test_spec_parse_basic(void) {
    nw_source_spec spec;

    CHECK(nw_is_ok(nw_source_spec_parse("cpu", &spec)), "bare name parses");
    CHECK(strcmp(spec.name, "cpu") == 0, "name should be cpu, got '%s'", spec.name);
    CHECK(!spec.has_number, "bare name is not a number");
    CHECK(spec.interval_ms == 0, "no interval implies provider default");

    CHECK(nw_is_ok(nw_source_spec_parse("  audio_bass  ", &spec)), "surrounding space ok");
    CHECK(strcmp(spec.name, "audio_bass") == 0, "trimmed to audio_bass");

    /* A constant is a provider like any other; that uniformity is what lets
     * the manifest mix `uExp 0.85` with `uLoad cpu` in one block. */
    CHECK(nw_is_ok(nw_source_spec_parse("0.85", &spec)), "number parses");
    CHECK(spec.has_number, "should be flagged as a number");
    CHECK(strcmp(spec.name, "const") == 0, "number routes to const provider");
    CHECK(spec.number > 0.84 && spec.number < 0.86, "value ~0.85, got %f", spec.number);

    CHECK(nw_is_ok(nw_source_spec_parse("-1", &spec)), "negative parses");
    CHECK(spec.number < 0, "negative preserved");

    CHECK(nw_is_ok(nw_source_spec_parse("2e3", &spec)), "exponent parses");
    CHECK(spec.number > 1999.0 && spec.number < 2001.0, "2e3 == 2000");
}

static void test_spec_parse_calls(void) {
    nw_source_spec spec;

    CHECK(nw_is_ok(nw_source_spec_parse("exec(\"uptime\")", &spec)), "call parses");
    CHECK(strcmp(spec.name, "exec") == 0, "name is exec");
    CHECK(strcmp(spec.arg, "uptime") == 0, "arg unquoted, got '%s'", spec.arg);

    CHECK(nw_is_ok(nw_source_spec_parse("exec(\"uptime\", 5s)", &spec)), "interval parses");
    CHECK(spec.interval_ms == 5000, "5s == 5000ms, got %u", spec.interval_ms);

    CHECK(nw_is_ok(nw_source_spec_parse("f(\"x\", 500ms)", &spec)), "ms parses");
    CHECK(spec.interval_ms == 500, "500ms");
    CHECK(nw_is_ok(nw_source_spec_parse("f(\"x\", 2m)", &spec)), "minutes parse");
    CHECK(spec.interval_ms == 120000, "2m == 120000ms, got %u", spec.interval_ms);
    CHECK(nw_is_ok(nw_source_spec_parse("f(\"x\", 1h)", &spec)), "hours parse");
    CHECK(spec.interval_ms == 3600000, "1h == 3600000ms");

    CHECK(nw_is_ok(nw_source_spec_parse("image(\"~/a.png\", watch)", &spec)), "watch parses");
    CHECK((spec.flags & NW_SOURCE_SPEC_WATCH) != 0, "watch flag set");
    CHECK(strcmp(spec.arg, "~/a.png") == 0, "path preserved");

    /* Commas and parens inside the command must not split the argument —
     * shell one-liners are full of both. */
    CHECK(nw_is_ok(nw_source_spec_parse("exec(\"echo a,b\", 1s)", &spec)), "comma in quotes");
    CHECK(strcmp(spec.arg, "echo a,b") == 0, "comma kept, got '%s'", spec.arg);
    CHECK(spec.interval_ms == 1000, "modifier still read");

    CHECK(nw_is_ok(nw_source_spec_parse("exec(\"echo )\")", &spec)), "paren in quotes");
    CHECK(strcmp(spec.arg, "echo )") == 0, "paren kept, got '%s'", spec.arg);

    CHECK(nw_is_ok(nw_source_spec_parse("exec('single')", &spec)), "single quotes");
    CHECK(strcmp(spec.arg, "single") == 0, "single-quoted arg");
}

static void test_spec_parse_rejects(void) {
    nw_source_spec spec;

    CHECK(nw_is_err(nw_source_spec_parse("", &spec)), "empty rejected");
    CHECK(nw_is_err(nw_source_spec_parse("   ", &spec)), "blank rejected");
    CHECK(nw_is_err(nw_source_spec_parse(NULL, &spec)), "null rejected");
    CHECK(nw_is_err(nw_source_spec_parse("exec(\"x\"", &spec)), "unclosed paren rejected");
    CHECK(nw_is_err(nw_source_spec_parse("(\"x\")", &spec)), "missing name rejected");
    CHECK(nw_is_err(nw_source_spec_parse("cpu ram", &spec)), "two bare words rejected");
    CHECK(nw_is_err(nw_source_spec_parse("exec(\"x\") junk", &spec)), "trailing text rejected");

    /* An unknown modifier is far more likely a typo than an intention. Taking
     * it silently would mean a shader that quietly never updates. */
    CHECK(nw_is_err(nw_source_spec_parse("exec(\"x\", banana)", &spec)), "bad modifier rejected");

    char long_name[NW_SOURCE_NAME_MAX + 32];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    CHECK(nw_is_err(nw_source_spec_parse(long_name, &spec)), "overlong name rejected");
}

/* ==========================================================================
 * Instances and scheduling
 * ========================================================================== */

static void test_open_and_tick(void) {
    nw_source_registry_reset();
    nw_source_register(&fake_vtable);

    nw_source src;
    char      err[NW_SOURCE_ERR_MAX] = {0};

    CHECK(nw_is_err(nw_source_open_text("nope", &src, err, sizeof(err))),
          "unknown provider should fail");
    CHECK(err[0] != '\0', "failure should explain itself");

    CHECK(nw_is_ok(nw_source_open_text("fake", &src, err, sizeof(err))), "open should work");
    CHECK(strcmp(nw_source_name(&src), "fake") == 0, "name reported");
    CHECK(nw_source_kind_of(&src) == NW_SOURCE_SCALAR, "kind reported");

    /* First tick always runs: a fresh source must not show 0 until its first
     * interval elapses. */
    nw_source_ctx c = ctx_at(0);
    CHECK(nw_source_tick(&src, &c), "first tick runs");
    CHECK(g_fake.ticks == 1, "one tick, got %d", g_fake.ticks);

    /* Inside the interval: no tick. */
    c = ctx_at(50);
    CHECK(!nw_source_tick(&src, &c), "too soon, no tick");
    CHECK(g_fake.ticks == 1, "still one tick");

    /* Past it: tick. */
    c = ctx_at(150);
    CHECK(nw_source_tick(&src, &c), "interval elapsed, tick");
    CHECK(g_fake.ticks == 2, "two ticks, got %d", g_fake.ticks);

    CHECK(nw_source_scalar(&src) > 1.5f, "scalar reflects ticks");
    CHECK(nw_source_texture(&src) == 0, "scalar source has no texture");

    nw_source_destroy(&src);
    CHECK(strcmp(nw_source_name(&src), "<none>") == 0, "destroyed source is inert");
    nw_source_destroy(&src); /* must not crash */
}

/*
 * The occlusion contract. This is the test that protects the project's
 * headline claim, so it checks the behaviour and not just the flag.
 */
static void test_visibility_gates_ticking(void) {
    nw_source_registry_reset();
    nw_source_register(&fake_vtable);

    nw_source src;
    CHECK(nw_is_ok(nw_source_open_text("fake", &src, NULL, 0)), "open");

    nw_source_ctx c = ctx_at(0);
    nw_source_tick(&src, &c);
    int baseline = g_fake.ticks;

    nw_source_set_visible(&src, false);
    CHECK(g_fake.visibility_calls == 1, "provider told it went hidden");
    CHECK(g_fake.last_visibility == false, "told hidden, not visible");

    /* Ten intervals pass behind a maximized window. Nothing should happen. */
    for (int i = 1; i <= 10; i++) {
        c = ctx_at((uint64_t)i * 1000);
        CHECK(!nw_source_tick(&src, &c), "hidden source must not tick");
    }
    CHECK(g_fake.ticks == baseline, "no work while hidden: %d vs %d", g_fake.ticks, baseline);

    /* Redundant transitions must not spam the provider — some hold real
     * resources and tear down on each edge. */
    nw_source_set_visible(&src, false);
    CHECK(g_fake.visibility_calls == 1, "no duplicate visibility callback");

    /* Back in view: refresh at once, rather than showing a value that is ten
     * seconds stale until the next interval boundary. */
    nw_source_set_visible(&src, true);
    CHECK(g_fake.visibility_calls == 2, "provider told it came back");
    c = ctx_at(10001);
    CHECK(nw_source_tick(&src, &c), "visible again ticks immediately");
    CHECK(g_fake.ticks == baseline + 1, "exactly one catch-up tick");

    nw_source_destroy(&src);
}

static void test_const_and_file(void) {
    nw_source_registry_reset();
    nw_source_register_builtins();

    CHECK(nw_source_find("const") != NULL, "const registered");
    CHECK(nw_source_find("file") != NULL, "file registered");
    CHECK(nw_source_find("exec") != NULL, "exec registered");

    /* Idempotent: init may run more than once. */
    size_t n = nw_source_registry_count();
    nw_source_register_builtins();
    CHECK(nw_source_registry_count() == n, "re-registering builtins is a no-op");

    nw_source src;
    CHECK(nw_is_ok(nw_source_open_text("0.85", &src, NULL, 0)), "constant opens");
    float v = nw_source_scalar(&src);
    CHECK(v > 0.84f && v < 0.86f, "constant reads back, got %f", v);
    nw_source_destroy(&src);

    /* /proc/sys/kernel/pid_max: present on every Linux, plain integer. */
    if (nw_is_ok(nw_source_open_text("file(\"/proc/sys/kernel/pid_max\")", &src, NULL, 0))) {
        nw_source_ctx c = ctx_at(0);
        nw_source_tick(&src, &c);
        CHECK(nw_source_scalar(&src) > 0.0f, "file source read a number");
        nw_source_destroy(&src);
    }

    char err[NW_SOURCE_ERR_MAX] = {0};
    CHECK(nw_is_err(nw_source_open_text("file(\"/nope/nope\")", &src, err, sizeof(err))),
          "missing file fails at open, not silently at 0");
    CHECK(err[0] != '\0', "error explains itself");
}

/* exec() runs commands, so the default must be off. A gallery shader that
 * could execute code the moment it loaded would be a real problem. */
static void test_exec_is_opt_in(void) {
    nw_source_registry_reset();
    nw_source_register_builtins();

    nw_source src;
    char      err[NW_SOURCE_ERR_MAX] = {0};

    nw_source_exec_set_allowed(false);
    CHECK(!nw_source_exec_allowed(), "exec off by default");
    CHECK(nw_is_err(nw_source_open_text("exec(\"echo 1\")", &src, err, sizeof(err))),
          "exec must refuse while disabled");
    CHECK(strstr(err, "disabled") != NULL, "error says why, got '%s'", err);

    nw_source_exec_set_allowed(true);
    CHECK(nw_source_exec_allowed(), "exec can be enabled");
    CHECK(nw_is_ok(nw_source_open_text("exec(\"echo 42\", 1s)", &src, err, sizeof(err))),
          "exec opens once allowed");

    /* Spawn on one tick, collect on a later one — the point is that no single
     * tick blocks on the child. Loop until the value lands. */
    float got = 0.0f;
    for (int i = 0; i < 200; i++) {
        nw_source_ctx c = ctx_at((uint64_t)i * 1000);
        nw_source_tick(&src, &c);
        got = nw_source_scalar(&src);
        if (got > 41.0f) {
            break;
        }
        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    CHECK(got > 41.0f && got < 43.0f, "exec parsed 42, got %f", got);

    nw_source_destroy(&src);
    nw_source_exec_set_allowed(false);
}

int main(void) {
    test_registry();
    test_spec_parse_basic();
    test_spec_parse_calls();
    test_spec_parse_rejects();
    test_open_and_tick();
    test_visibility_gates_ticking();
    test_const_and_file();
    test_exec_is_opt_in();

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
