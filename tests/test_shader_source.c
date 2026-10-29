/* shader source model: passes, common, markers, includes, params, manifests,
 * default bindings, ordering, line maps. No GL. */
#define _DEFAULT_SOURCE
#include "neowall/shader/shader_source.h"
#include "neowall/shader/shader_hint.h"
#include "neowall/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static nw_shader_source *load(const char *text, const char *manifest) {
    nw_shader_source *s = NULL;
    nw_result r = nw_shader_source_parse(text, NULL, manifest, &s);
    CHECK(!nw_is_err(r), "should parse: %s", r.context ? r.context : "");
    return s;
}

static bool rejects(const char *text, const char *manifest) {
    nw_shader_source *s = NULL;
    nw_result r = nw_shader_source_parse(text, NULL, manifest, &s);
    if (!nw_is_err(r)) nw_shader_source_free(s);
    return nw_is_err(r);
}

static int pass(const nw_shader_source *s, const char *name) { return nw_shader_source_find_pass(s, name); }

static const char single[] =
    "float wave(float x) { return sin(x); }\n"
    "void mainImage(out vec4 c, in vec2 f) {\n"
    "    c = vec4(wave(iTime));\n"
    "}\n";

static const char multi[] =
    "// Common\n"
    "float hash(float x) { return fract(sin(x) * 43758.5); }\n"
    "\n"
    "// Buffer A\n"
    "void mainImage(out vec4 c, in vec2 f) {\n"
    "    c = texture(iChannel0, f / iResolution.xy) * 0.99 + hash(iTime);\n"
    "}\n"
    "\n"
    "// Buffer B\n"
    "void mainImage(out vec4 c, in vec2 f) {\n"
    "    c = texture(iChannel0, f / iResolution.xy);\n"
    "}\n"
    "\n"
    "// Image\n"
    "void mainImage(out vec4 c, in vec2 f) {\n"
    "    c = texture(iChannel0, f / iResolution.xy) + texture(iChannel1, f / iResolution.xy);\n"
    "}\n";

int main(void) {
    log_quiet_errors(true);

    /* single pass: everything is Image, no common */
    nw_shader_source *s = load(single, NULL);
    CHECK(s->passes.len == 1 && s->image_pass == 0 && s->passes.data[0].is_image, "one Image pass");
    CHECK(strstr(s->passes.data[0].body, "float wave"), "helpers stay with the pass");
    CHECK(!s->common, "no common");
    CHECK(s->animated, "uses iTime");
    nw_shader_source_free(s);

    /* static shader */
    s = load("void mainImage(out vec4 c, in vec2 f) { c = vec4(f.x); }\n", NULL);
    CHECK(!s->animated, "static shader isn't animated");
    nw_shader_source_free(s);

    /* multipass with markers and common */
    s = load(multi, NULL);
    CHECK(s->passes.len == 3, "3 passes, got %zu", s->passes.len);
    CHECK(s->common && strstr(s->common, "float hash"), "common extracted");
    CHECK(!strstr(s->passes.data[pass(s, "Buffer A")].body, "float hash"), "common not duplicated into a pass");
    int a = pass(s, "Buffer A"), b = pass(s, "Buffer B"), img = pass(s, "Image");
    CHECK(a >= 0 && b >= 0 && img == s->image_pass, "names found");
    CHECK(pass(s, "bufferA") == a && pass(s, "a") == a && pass(s, "buffer_b") == b, "shorthands");
    /* default bindings */
    const nw_pass_src *pa = &s->passes.data[a], *pb = &s->passes.data[b], *pi = &s->passes.data[img];
    CHECK(pa->ch[0].kind == NW_IN_PASS && pa->ch[0].pass == a, "A reads itself (feedback)");
    CHECK(pb->ch[0].kind == NW_IN_PASS && pb->ch[0].pass == a, "B ch0 = A");
    CHECK(pi->ch[0].kind == NW_IN_PASS && pi->ch[0].pass == a, "Image ch0 = A");
    CHECK(pi->ch[1].kind == NW_IN_PASS && pi->ch[1].pass == b, "Image ch1 = B");
    CHECK(pi->ch[2].kind == NW_IN_NONE, "unused channel is none");
    /* order: A before B before Image */
    CHECK(a < b && b < img && img == (int)s->passes.len - 1, "A, B, Image order");
    nw_shader_source_free(s);

    /* noise detection in a single-pass shader */
    s = load("void mainImage(out vec4 c, in vec2 f) {\n"
             "  c = texture(iChannel0, f / 256.0);\n  c += texture(iChannel1, f/256.);\n}\n", NULL);
    CHECK(s->passes.data[0].ch[0].kind == NW_IN_NOISE && s->passes.data[0].ch[0].wrap_repeat, "noise ch0");
    nw_shader_source_free(s);

    /* unmarked multipass: Buffer A..., last is Image */
    s = load("void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }\n"
             "void mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f/iResolution.xy); }\n", NULL);
    CHECK(s->passes.len == 2 && !strcmp(s->passes.data[0].name, "Buffer A") &&
          !strcmp(s->passes.data[1].name, "Image"), "positional names");
    nw_shader_source_free(s);

    /* named passes: // Pass: Blur */
    s = load("// Pass: Sim\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f/iResolution.xy); }\n"
             "// Pass: Blur\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(1); }\n"
             "// Image\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(0); }\n",
             "pass {\n  Sim {\n    format rgba32f\n    size 256x128\n    filter nearest\n    ch0 self\n  }\n"
             "  Blur {\n    scale 0.5\n    filter mipmap\n    ch0 Sim\n  }\n}\nimage {\n  ch0 Blur\n}\n");
    int sim = pass(s, "Sim"), blur = pass(s, "Blur");
    CHECK(sim >= 0 && blur >= 0, "custom pass names");
    const nw_pass_src *ps = &s->passes.data[sim];
    CHECK(ps->format == NW_FMT_RGBA32F && ps->fixed_w == 256 && ps->fixed_h == 128 &&
          ps->filter == NW_FILTER_NEAREST, "pass target settings");
    CHECK(ps->ch[0].kind == NW_IN_PASS && ps->ch[0].pass == sim, "ch0 self");
    CHECK(s->passes.data[blur].scale == 0.5 && s->passes.data[blur].filter == NW_FILTER_MIPMAP, "blur settings");
    CHECK(sim < blur, "Sim runs before Blur, which reads it");
    CHECK(s->explicit_bindings, "manifest bindings are explicit");
    nw_shader_source_free(s);

    /* ordering: Image declared first still runs last; B before A when A reads B */
    s = load("// Image\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f); }\n"
             "// Buffer A\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel1, f); }\n"
             "// Buffer B\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(1); }\n",
             "image { ch0 a }\nbufferA { ch1 b }\n");
    CHECK(pass(s, "Buffer B") < pass(s, "Buffer A") && pass(s, "Buffer A") < pass(s, "Image"),
          "dependency order B, A, Image");
    CHECK(s->passes.data[pass(s, "Image")].ch[0].pass == pass(s, "Buffer A"), "binding remapped after sort");
    nw_shader_source_free(s);

    /* a cycle between buffers is legal (one read becomes last-frame) */
    s = load("// Buffer A\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f); }\n"
             "// Buffer B\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f); }\n"
             "// Image\nvoid mainImage(out vec4 c, in vec2 f) { c = texture(iChannel0, f); }\n",
             "bufferA { ch0 b }\nbufferB { ch0 a }\nimage { ch0 a }\n");
    CHECK(s->passes.len == 3 && s->image_pass == 2, "cycle resolved, Image last");
    nw_shader_source_free(s);

    /* old manifest forms */
    s = load(single, "channel0 audio\nuniforms {\n  uWind cpu\n  uGlow audio_bass\n  uExp 0.8\n}\n");
    CHECK(s->passes.data[0].ch[0].kind == NW_IN_AUDIO, "channel0 audio");
    CHECK(s->binds.len == 3 && !strcmp(s->binds.data[0].name, "uWind") &&
          !strcmp(s->binds.data[0].source, "cpu") && !strcmp(s->binds.data[2].source, "0.8"),
          "uniform binds (%zu)", s->binds.len);
    nw_shader_source_free(s);

    /* @param tweakables */
    s = load("// @param 0.1 .. 4.0 step 0.1 \"cloud speed\"\n"
             "uniform float uSpeed = 1.5;\n"
             "// @param\n"
             "uniform vec3 uTint = vec3(1.0, 0.5, 0.25);\n"
             "// @param\n"
             "uniform vec3 uGrey = vec3(0.5);\n"
             "void mainImage(out vec4 c, in vec2 f) { c = vec4(uTint * uSpeed + uGrey, 1); }\n", NULL);
    CHECK(s->params.len == 3, "3 params, got %zu", s->params.len);
    if (s->params.len == 3) {
        const nw_param *p0 = &s->params.data[0], *p1 = &s->params.data[1], *p2 = &s->params.data[2];
        CHECK(!strcmp(p0->name, "uSpeed") && !strcmp(p0->type, "float") && p0->def[0] == 1.5f, "float param");
        CHECK(p0->min == 0.1f && p0->max == 4.0f && p0->step == 0.1f && !strcmp(p0->help, "cloud speed"),
              "range/step/help");
        CHECK(!strcmp(p1->type, "vec3") && p1->def[0] == 1.0f && p1->def[1] == 0.5f && p1->def[2] == 0.25f,
              "vec3 default");
        CHECK(p2->def[0] == 0.5f && p2->def[1] == 0.5f && p2->def[2] == 0.5f, "vec3(0.5) splat");
    }
    CHECK(!strstr(s->passes.data[0].body, "uniform float uSpeed"), "param uniform line removed from body");
    nw_shader_source_free(s);

    /* assembly: declarations, stdlib, main, line map */
    s = load(multi, NULL);
    char *text;
    nw_src_loc *map;
    size_t n;
    nw_result r = nw_shader_assemble(s, pass(s, "Image"), &text, &map, &n);
    CHECK(!nw_is_err(r), "assemble");
    if (!nw_is_err(r)) {
        CHECK(!strncmp(text, "#version 330 core\n", 18), "version first");
        CHECK(strstr(text, "uniform vec3  iChannelResolution[4];"), "Shadertoy uniforms");
        CHECK(strstr(text, "nwFbm"), "stdlib present");
        CHECK(strstr(text, "float hash"), "common included");
        CHECK(strstr(text, "mainImage(fragColor, gl_FragCoord.xy + iSpanOffset)"), "main calls mainImage");
        /* find the generated line of the Image body's texture() call and
         * check it maps back to the author's line 16 */
        int gen = 0, mapped_line = -1;
        for (const char *p = text; *p; gen++) {
            const char *nl = strchr(p, '\n');
            if (strstr(p, "texture(iChannel1") && (!nl || strstr(p, "texture(iChannel1") < nl)) {
                mapped_line = (size_t)gen < n ? map[gen].line : -2;
                break;
            }
            p = nl ? nl + 1 : p + strlen(p);
        }
        CHECK(mapped_line == 16, "line map: Image body line maps to 16, got %d", mapped_line);
        free(text);
        free(map);
    }
    nw_shader_source_free(s);

    /* includes, from real files */
    char dir[64];
    snprintf(dir, sizeof(dir), "/tmp/nwsh-%d", (int)getpid());
    mkdir(dir, 0700);
    char p1[128], p2[128], p3[128];
    snprintf(p1, sizeof(p1), "%s/lib.glsl", dir);
    snprintf(p2, sizeof(p2), "%s/main.glsl", dir);
    snprintf(p3, sizeof(p3), "%s/loop.glsl", dir);
    FILE *f = fopen(p1, "w");
    fputs("// shared\nfloat twice(float x) {\n  return 2.0 * x;\n}\n", f);
    fclose(f);
    f = fopen(p2, "w");
    fputs("#include \"lib.glsl\"\nvoid mainImage(out vec4 c, in vec2 x) {\n  c = vec4(twice(1.0));\n}\n", f);
    fclose(f);
    f = fopen(p3, "w");
    fputs("#include \"loop.glsl\"\nvoid mainImage(out vec4 c, in vec2 x) { c = vec4(1); }\n", f);
    fclose(f);
    r = nw_shader_source_load(p2, &s);
    CHECK(!nw_is_err(r), "load with include");
    if (!nw_is_err(r)) {
        CHECK(strstr(s->passes.data[0].body, "float twice"), "include expanded");
        CHECK(s->includes.len == 2, "both files recorded for watch (%zu)", s->includes.len);
        r = nw_shader_assemble(s, 0, &text, &map, &n);
        int lib_line = -1;
        const char *lib_file = NULL;
        int gen = 0;
        for (const char *p = text; *p; gen++) {
            const char *nl = strchr(p, '\n');
            if (!strncmp(p, "  return 2.0", 12)) {
                lib_line = map[gen].line;
                lib_file = map[gen].file;
                break;
            }
            p = nl ? nl + 1 : p + strlen(p);
        }
        CHECK(lib_line == 3 && lib_file && strstr(lib_file, "lib.glsl"),
              "included line maps to lib.glsl:3, got %s:%d", lib_file ? lib_file : "?", lib_line);
        free(text);
        free(map);
        nw_shader_source_free(s);
    }
    CHECK(nw_is_err(nw_shader_source_load(p3, &s)), "include cycle rejected");
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");

    /* helpers between passes are shared, not owned by the next pass */
    s = load("// Buffer A\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(blur(1.0)); }\n"
             "float blur(float x) { return x * 0.5; }\n"
             "// Image\nvoid mainImage(out vec4 c, in vec2 f) { c = vec4(blur(2.0)); }\n", NULL);
    CHECK(s->common && strstr(s->common, "float blur"), "helper after a pass lands in common");
    CHECK(!strstr(s->passes.data[pass(s, "Image")].body, "float blur"), "and not in the next pass");
    CHECK(!strstr(s->common, "mainImage"), "common holds no mainImage");
    nw_shader_source_free(s);

    /* more old manifest forms */
    s = load(single, "channel0 texture:art.png\nchannel1 noise.png\nchannel2 terminal\n");
    CHECK(s->passes.data[0].ch[0].kind == NW_IN_IMAGE && !strcmp(s->passes.data[0].ch[0].path, "art.png"),
          "texture:FILE is an image");
    CHECK(s->passes.data[0].ch[1].kind == NW_IN_IMAGE, "noise.png is a file, not the built-in");
    CHECK(s->passes.data[0].ch[2].kind == NW_IN_NONE, "terminal is read via iTermCells, not a channel");
    nw_shader_source_free(s);

    s = load(multi, "state bufferA\n");
    CHECK(s->state_pass && pass(s, s->state_pass) == pass(s, "Buffer A"), "state names a pass");
    nw_shader_source_free(s);

    CHECK(rejects("#pragma neowall requires 99.0\n" "void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }\n", NULL),
          "a shader for a newer neowall is refused");

    /* rejections */
    CHECK(shader_hint_suggest("nwSdSpere") && !strcmp(shader_hint_suggest("nwSdSpere"), "nwSdSphere"),
          "typo -> nwSdSphere");
    CHECK(shader_hint_suggest("fbm") && !strcmp(shader_hint_suggest("fbm"), "nwFbm"), "fbm -> nwFbm");
    CHECK(shader_hint_suggest("itime") && !strcmp(shader_hint_suggest("itime"), "iTime"), "itime -> iTime");
    CHECK(!shader_hint_suggest("glow"), "no wild guess for glow");
    CHECK(!shader_hint_suggest("nwFbm"), "an existing name is not a typo");
    {
        char hint[160];
        CHECK(shader_hint("`iTimee' undeclared", NULL, 4, hint, sizeof(hint)) &&
                  !strcmp(hint, "did you mean iTime?"), "mesa undeclared -> hint");
        char tmpl[] = "/tmp/nwhint-XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "w");
            fputs("void f() {\n    float a = 1.0\n    // note\n    float b = 2.0;\n}\n", f);
            fclose(f);
            CHECK(shader_hint("syntax error, unexpected IDENTIFIER, expecting ',' or ';'", tmpl, 4, hint,
                              sizeof(hint)) && strstr(hint, "line 2"),
                  "missing ; points at the code line before: %s", hint);
            unlink(tmpl);
        }
    }

    CHECK(rejects("float x;\n", NULL), "no mainImage");
    CHECK(rejects(single, "channel0 sparkles\n"), "unknown channel source");
    CHECK(rejects(single, "bufferQ { ch0 self }\n"), "manifest names a missing pass");
    CHECK(rejects(multi, "pass {\n  \"Buffer A\" {\n    scale 9\n  }\n}\n"), "scale out of range");
    CHECK(rejects(multi, "pass {\n  \"Buffer A\" {\n    format rgb565\n  }\n}\n"), "bad format");
    CHECK(rejects("// Image\nvoid mainImage(out vec4 c, in vec2 f) {}\n// Image\nvoid mainImage(out vec4 c, in vec2 f) {}\n",
                  NULL), "two Image passes");
    CHECK(rejects("#include \"x.glsl\"\nvoid mainImage(out vec4 c, in vec2 f) {}\n", NULL),
          "include without a file");

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
