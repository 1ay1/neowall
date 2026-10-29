/* Unit tests for shader-authoring plumbing that is easy to break silently:
 * pass-marker detection, the virtual shader clock, and the
 * `#pragma neowall requires` version floor.
 *
 * All three are GL-free, so these run headless in CI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neowall/clock.h"
#include "neowall/log.h"
#include "neowall/shader/shader_source.h"

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

/* ------------------------------------------------------------------ *
 * Pass-marker detection
 * ------------------------------------------------------------------ */

/* The marker search used to run strstr() from the comment to END OF FILE, so a
 * later mention of "Buffer A" anywhere — even in prose describing the shader —
 * retroactively relabelled an earlier pass. This is that regression. */
static void test_marker_scan_is_line_bounded(void) {
    static const char *src =
        "// Buffer A\n"
        "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }\n"
        "\n"
        "// Image\n"
        "void mainImage(out vec4 c, in vec2 f) {\n"
        "    // we composite Buffer A here\n"
        "    c = vec4(1.0);\n"
        "}\n";

    nw_shader_source *s = NULL;
    CHECK(nw_is_ok(nw_shader_source_parse(src, NULL, NULL, &s)));
    if (!s) return;

    CHECK(s->passes.len == 2);
    /* The trailing "Buffer A" mention must NOT drag the Image pass back to
     * being a buffer. */
    CHECK(nw_shader_source_find_pass(s, "Buffer A") >= 0);
    CHECK(s->image_pass >= 0 && !strcmp(s->passes.data[s->image_pass].name, "Image"));

    nw_shader_source_free(s);
}

/* A marker only counts when it is the comment on (or just above) the pass. */
static void test_marker_prefers_nearest_comment(void) {
    static const char *src =
        "// Image\n"
        "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.5); }\n";

    nw_shader_source *s = NULL;
    CHECK(nw_is_ok(nw_shader_source_parse(src, NULL, NULL, &s)));
    if (!s) return;

    CHECK(s->passes.len == 1);
    CHECK(s->image_pass == 0);

    nw_shader_source_free(s);
}

/* ------------------------------------------------------------------ *
 * Virtual shader clock
 * ------------------------------------------------------------------ */

static void test_clock_offset_parsing(void) {
    long secs = 0;

    CHECK(nw_clock_parse_offset("+6d", &secs) && secs == 6 * 86400);
    CHECK(nw_clock_parse_offset("-2h", &secs) && secs == -2 * 3600);
    CHECK(nw_clock_parse_offset("90m", &secs) && secs == 90 * 60);
    CHECK(nw_clock_parse_offset("3600", &secs) && secs == 3600);
    CHECK(nw_clock_parse_offset("1w",  &secs) && secs == 7 * 86400);

    /* Garbage must be rejected, not silently treated as zero — a typo'd
     * offset that reads as "now" would look like the feature is broken. */
    CHECK(!nw_clock_parse_offset("tomorrow", &secs));
    CHECK(!nw_clock_parse_offset("6x", &secs));
    CHECK(!nw_clock_parse_offset("", &secs));
    CHECK(!nw_clock_parse_offset("6d junk", &secs));
}

static void test_clock_timelapse_parsing(void) {
    double scale = 0.0;

    /* 30 days compressed into 20 seconds. */
    CHECK(nw_clock_parse_timelapse("30d/20s", &scale));
    CHECK(scale > 129599.0 && scale < 129601.0);

    CHECK(nw_clock_parse_timelapse("1h/1s", &scale));
    CHECK(scale > 3599.0 && scale < 3601.0);

    CHECK(!nw_clock_parse_timelapse("30d", &scale));      /* no duration */
    CHECK(!nw_clock_parse_timelapse("/20s", &scale));     /* no span */
    CHECK(!nw_clock_parse_timelapse("30d/0s", &scale));   /* zero duration */
    CHECK(!nw_clock_parse_timelapse("abc/20s", &scale));
}

static void test_clock_default_is_real_time(void) {
    /* An untouched clock must be indistinguishable from time(NULL), so the
     * feature costs nothing when unused. */
    CHECK(!nw_clock_is_virtual());

    time_t real = time(NULL);
    time_t shader = nw_clock_now();
    CHECK(shader >= real && shader <= real + 1);
}

static void test_clock_offset_shifts_time(void) {
    time_t before = nw_clock_now();

    nw_clock_set_offset(6 * 86400);
    CHECK(nw_clock_is_virtual());

    time_t shifted = nw_clock_now();
    long delta = (long)(shifted - before);
    /* Six days ahead, allowing a second of slop for the calls themselves. */
    CHECK(delta >= 6 * 86400 - 2 && delta <= 6 * 86400 + 2);

    /* Restore so later tests see a clean clock. */
    nw_clock_set_offset(0);
    CHECK(!nw_clock_is_virtual());
}

/* ------------------------------------------------------------------ *
 * Version floor
 * ------------------------------------------------------------------ */

static void test_version_pragma(void) {
    /* No pragma: always runs. */
    CHECK(nw_shader_requires_ok("void mainImage() {}", "t.glsl"));

    /* Ancient requirement: satisfied. */
    CHECK(nw_shader_requires_ok("#pragma neowall requires 0.1\nvoid mainImage() {}", "t.glsl"));

    /* Far-future requirement: refused, rather than silently rendering with
     * dead uniforms. */
    CHECK(!nw_shader_requires_ok("#pragma neowall requires 99.0\nvoid mainImage() {}", "t.glsl"));

    /* Leading whitespace is legal for a preprocessor directive. */
    CHECK(!nw_shader_requires_ok("   #pragma neowall requires 99.0.1\nvoid mainImage() {}", "t.glsl"));

    /* A mention inside a comment is not a directive. */
    CHECK(nw_shader_requires_ok("// #pragma neowall requires 99.0\nvoid mainImage() {}", "t.glsl"));

    /* Malformed version is a warning, not a hard refusal. */
    CHECK(nw_shader_requires_ok("#pragma neowall requires\nvoid mainImage() {}", "t.glsl"));

    /* And the loader enforces it. */
    nw_shader_source *s = NULL;
    CHECK(nw_is_err(nw_shader_source_parse(
        "#pragma neowall requires 99.0\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(1); }\n",
        NULL, NULL, &s)));
    CHECK(s == NULL);
}

int main(void) {
    log_quiet_errors(true);
    test_marker_scan_is_line_bounded();
    test_marker_prefers_nearest_comment();

    test_clock_offset_parsing();
    test_clock_timelapse_parsing();
    test_clock_default_is_real_time();
    test_clock_offset_shifts_time();

    test_version_pragma();

    if (failures == 0) {
        printf("test_shader_authoring: all %d checks passed\n", checks);
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "test_shader_authoring: %d/%d checks FAILED\n", failures, checks);
    return EXIT_FAILURE;
}
