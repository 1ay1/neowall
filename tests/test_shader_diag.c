/* Shader diagnostics: driver logs -> readable errors. No GL needed. */

#include <stdio.h>
#include <string.h>

#include "neowall/shader/shader_diag.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static const char *kSource =
    "// test\n"                                    /* 1 */
    "float f(vec2 p) {\n"                           /* 2 */
    "    float a = nwFbm(p)\n"                      /* 3: missing ; */
    "    float b = nwSdSpere(vec3(p, 0.0), 1.0);\n" /* 4: typo */
    "    return a + b + glow;\n"                    /* 5: undeclared */
    "}\n";

int main(void) {
    const char *known[] = {
        "uniform float iTime; uniform vec3 iResolution; uniform float iAudioBass;\n",
        "float nwFbm(vec2 p) { return 0.0; }\n"
        "float nwSdSphere(vec3 p, float r) { return 0.0; }\n"
        "float nwSdBox(vec3 p, vec3 b) { return 0.0; }\n",
    };
    shader_diag_set_known(known, 2);

    /* NVIDIA, with the duplicate the driver often prints. */
    const char *nv =
        "0(4) : error C0000: syntax error, unexpected reserved word \"float\", expecting ',' or ';' at token \"float\"\n"
        "0(4) : error C1503: undefined variable \"nwSdSpere\"\n"
        "0(5) : error C1503: undefined variable \"glow\"\n"
        "0(5) : error C1503: undefined variable \"glow\"\n";
    shader_diag d[8];
    size_t n = shader_diag_parse(nv, d, 8);
    CHECK(n == 3, "nvidia: %zu entries (dups dropped)", n);
    CHECK(d[0].line == 4 && !d[0].warning, "nvidia line");
    CHECK(!strncmp(d[1].message, "undefined variable", 18), "C1503 code stripped: '%s'", d[1].message);

    /* Mesa. */
    const char *mesa =
        "0:5(20): error: `glow' undeclared\n"
        "0:5(20): error: operands to arithmetic operators must be numeric\n"
        "0:3(9): warning: `a' used uninitialized\n";
    n = shader_diag_parse(mesa, d, 8);
    CHECK(n == 3, "mesa: %zu", n);
    CHECK(d[0].line == 5 && d[0].column == 20, "mesa line/col %d/%d", d[0].line, d[0].column);
    CHECK(d[2].warning, "mesa warning");

    /* ANGLE / mobile style. */
    n = shader_diag_parse("ERROR: 0:5: 'glow' : undeclared identifier\n", d, 8);
    CHECK(n == 1 && d[0].line == 5, "angle style");

    /* Garbage in: nothing parsed, caller falls back to the raw log. */
    char buf[4096];
    CHECK(shader_diag_format("Link failed: out of memory\n", kSource, "x.glsl", false, buf,
                             sizeof(buf)) == 0, "unparseable -> 0");

    /* Suggestions. */
    CHECK(shader_diag_suggest("nwSdSpere") && !strcmp(shader_diag_suggest("nwSdSpere"), "nwSdSphere"),
          "typo -> nwSdSphere");
    CHECK(shader_diag_suggest("fbm") && !strcmp(shader_diag_suggest("fbm"), "nwFbm"), "fbm -> nwFbm");
    CHECK(shader_diag_suggest("itime") && !strcmp(shader_diag_suggest("itime"), "iTime"), "itime -> iTime");
    CHECK(!shader_diag_suggest("glow"), "no wild guess for glow");
    CHECK(!shader_diag_suggest("nwFbm"), "an existing name is not a typo");

    /* The full report. */
    size_t w = shader_diag_format(nv, kSource, "/home/x/w.glsl", false, buf, sizeof(buf));
    CHECK(w > 0, "formatted");
    CHECK(strstr(buf, "w.glsl:4: error:"), "file:line uses the basename");
    CHECK(strstr(buf, "    4 |     float b = nwSdSpere"), "source line shown");
    CHECK(strstr(buf, "did you mean nwSdSphere?"), "suggestion shown");
    CHECK(strstr(buf, "line 3 may be missing a ';'"), "missing ; points at the line before");
    CHECK(strstr(buf, "3 errors"), "count");
    /* Caret under the complained-about name (the second block for line 4). */
    const char *blk = strstr(buf, "undefined variable \"nwSdSpere\"");
    const char *src_line = blk ? strstr(blk, "    4 |") : NULL;
    const char *caret_line = src_line ? strchr(src_line, '\n') + 1 : NULL;
    CHECK(src_line && caret_line &&
          strchr(caret_line, '^') - strchr(caret_line, '|') ==
          strstr(src_line, "nwSdSpere") - strchr(src_line, '|'),
          "caret under nwSdSpere");

    if (failures) fputs(buf, stderr);
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
