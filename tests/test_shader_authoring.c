/* Unit tests for shader-authoring plumbing that is easy to break silently:
 * multipass pass-marker detection, the virtual shader clock, and the
 * `#pragma neowall requires` version floor.
 *
 * All three are GL-free, so these run headless in CI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neowall/clock.h"
#include "neowall/shader/shader_multipass.h"

/* Stub for multipass_type_name — the real one lives in shader_multipass.c,
 * which we deliberately do not link (it pulls in GL). The parser only calls it
 * for log messages, so a placeholder is fine. Mirrors fuzz_multipass.c. */
const char *multipass_type_name(multipass_type_t type) {
    (void)type;
    return "<test>";
}

/* Logging stubs: the real ones live in utils.c, which would drag in pthreads
 * and the whole state machine for no benefit here. */
#include <stdarg.h>
void log_info (const char *fmt, ...) { (void)fmt; }
void log_debug(const char *fmt, ...) { (void)fmt; }
void log_warn (const char *fmt, ...) { (void)fmt; }
void log_error(const char *fmt, ...) { (void)fmt; }

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

    multipass_parse_result_t *r = multipass_parse_shader(src);
    CHECK(r != NULL);
    if (!r) return;

    CHECK(r->pass_count == 2);
    CHECK(r->pass_types[0] == PASS_TYPE_BUFFER_A);
    /* The trailing "Buffer A" mention must NOT drag the Image pass back to
     * being a buffer. */
    CHECK(r->pass_types[1] == PASS_TYPE_IMAGE);

    multipass_free_parse_result(r);
}

/* A marker only counts when it is the comment on (or just above) the pass. */
static void test_marker_prefers_nearest_comment(void) {
    static const char *src =
        "// Image\n"
        "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.5); }\n";

    multipass_parse_result_t *r = multipass_parse_shader(src);
    CHECK(r != NULL);
    if (!r) return;

    CHECK(r->pass_count == 1);
    CHECK(r->pass_types[0] == PASS_TYPE_IMAGE);

    multipass_free_parse_result(r);
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
    CHECK(shader_check_required_version("void mainImage() {}", "t.glsl"));

    /* Ancient requirement: satisfied. */
    CHECK(shader_check_required_version(
        "#pragma neowall requires 0.1\nvoid mainImage() {}", "t.glsl"));

    /* Far-future requirement: refused, rather than silently rendering with
     * dead uniforms. */
    CHECK(!shader_check_required_version(
        "#pragma neowall requires 99.0\nvoid mainImage() {}", "t.glsl"));

    /* Leading whitespace is legal for a preprocessor directive. */
    CHECK(!shader_check_required_version(
        "   #pragma neowall requires 99.0.1\nvoid mainImage() {}", "t.glsl"));

    /* A mention inside a comment is not a directive. */
    CHECK(shader_check_required_version(
        "// #pragma neowall requires 99.0\nvoid mainImage() {}", "t.glsl"));

    /* Malformed version is a warning, not a hard refusal. */
    CHECK(shader_check_required_version(
        "#pragma neowall requires\nvoid mainImage() {}", "t.glsl"));
}

int main(void) {
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
