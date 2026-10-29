/* nw_compat_fix: Shadertoy (GLSL ES / WebGL) source made to compile as
 * desktop GLSL 330 core. Pure string work, so it runs headless.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

static int failures = 0;
static int checks = 0;

static char *fix(const char *src) {
    char *r = nw_compat_fix(src);
    /* a trailing newline is added to the last line; compare without it */
    size_t n = r ? strlen(r) : 0;
    if (n && r[n - 1] == '\n' && (!*src || src[strlen(src) - 1] != '\n')) r[n - 1] = '\0';
    return r;
}

#define CHECK(cond, src, ...)                                                  \
    do {                                                                       \
        checks++;                                                              \
        char *r = fix(src);                                                    \
        if (!(r && (cond))) {                                                  \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n--- got:\n%s\n", r ? r : "(null)");             \
        }                                                                      \
        free(r);                                                               \
    } while (0)

#define HAS(src, needle) CHECK(strstr(r, needle), src, "expected \"%s\"", needle)
#define LACKS(src, needle) CHECK(!strstr(r, needle), src, "did not expect \"%s\"", needle)
#define SAME(src) CHECK(!strcmp(r, src), src, "expected it unchanged")

int main(void) {
    SAME("");

    /* a stray #version goes (we emit our own), even indented; in a comment it stays */
    LACKS("#version 300 es\nvoid mainImage(){}", "#version");
    HAS("#version 300 es\nvoid mainImage(){}", "mainImage");
    LACKS("   #version 130\nfoo", "#version");
    HAS("// #version 100 is just text\nint x;", "#version 100");

    /* iChannelResolution is a vec3 here: a bare use gets .xy */
    HAS("vec2 r = iChannelResolution[0];", "iChannelResolution[0].xy");
    LACKS("vec2 r = iChannelResolution[1].xy;", ".xy.xy");
    LACKS("float f = iChannelResolution[2][0];", "].xy");
    LACKS("// iChannelResolution[0] in a comment\nint x;", "iChannelResolution[0].xy");
    LACKS("/* iChannelResolution[0] */ int x;", "iChannelResolution[0].xy");

    /* uniforms the engine provides, and precision lines, are commented out */
    LACKS("uniform float iTime;\nvoid f(){}", "\nuniform float iTime;");
    HAS("uniform float iTime;\n", "provided by the engine");
    HAS("precision highp float;\n", "provided by the engine");
    HAS("uniform float uMine;\n", "uniform float uMine;");

    /* a file-scope const that isn't a constant expression on desktop loses
     * `const`; inside a function it's fine as is */
    LACKS("const vec3 L = normalize(vec3(1));\n", "const vec3 L");
    HAS("const float K = 2.0 * 3.0;\n", "const float K");
    HAS("const float K = PI * 2.0;\n", "const float K");
    HAS("void f(){ const float a = sin(1.0); }\n", "const float a");
    /* a const built from a demoted one is demoted too (PI = acos(-1.)) */
    LACKS("const float PI = acos(-1.);\nconst float TAU = PI * 2.;\n", "const float TAU");
    LACKS("const float PI = acos(-1.);\nconst float TAU = PI * 2.;\n", "const float PI");
    HAS("#define PI 3.14159\nconst float TAU = PI * 2.;\n", "const float TAU");

    /* plain code is untouched */
    SAME("void mainImage(out vec4 c, in vec2 p){ c = vec4(1.0); }");

    /* words reserved on desktop but free in GLSL ES get an underscore */
    HAS("float packed = 1.0; float noise1(float x){ return x; }\n", "float packed_ = 1.0");
    HAS("float noise1(float x){ return x; } void f(){ float a = noise1(2.); }\n", "noise1_(2.)");
    HAS("float x; // packed in a comment\n", "// packed in a comment");
    LACKS("float unpacked = 1.0;\n", "unpacked_");

    /* a local declared with no value is zeroed (WebGL does, desktop doesn't) */
    HAS("void f(){ vec3 acc; acc += 1.0; }", "vec3 acc = vec3(0);");
    HAS("void f(){ float a, b; }", "float a = float(0), b = float(0);");
    HAS("void f(){ bool hit; }", "bool hit = false;");
    HAS("void f(){ float a = 2.0; }", "float a = 2.0;");
    LACKS("void f(){ float a[4]; }", "= float(0)");          /* arrays: left alone */
    LACKS("float g; void f(){}", "= float(0)");               /* globals are zero already */
    LACKS("void f(in vec3 p, out float d){ d = 1.; }", "= ""vec3(0)"); /* parameters */
    LACKS("void f(){ for (int i; i < 3; i++) {} }", "int i = int(0)"); /* inside parens */
    /* struct members can't have initialisers: never touched */
    LACKS("struct Ray { vec3 o; vec3 d; };\n", "vec3 o = vec3(0)");
    LACKS("void f(){ struct S { float x; }; S s; }", "float x = ");

    /* a #define body isn't parsed as code */
    HAS("#define DECL float q;\nvoid f(){}", "#define DECL float q;");

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
