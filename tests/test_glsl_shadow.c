/* Tests for the GLSL namespace-collision guard (issue #82).
 *
 * neowall injects a standard library ahead of every shader it compiles. In
 * GLSL a duplicate function definition is a hard compile error, so any
 * unprefixed name the library emits is a landmine for shader authors:
 *
 *     0:86(7): error: function `sdBox' redefined
 *
 * Two layers are tested here.
 *
 * 1. The scanner (glsl_shadow.c) must correctly report which names a shader
 *    defines -- without being fooled by comments, calls, locals, or prose.
 *
 * 2. The INVARIANT that keeps the bug from coming back: every unprefixed
 *    function defined in shader_stdlib.h must have an entry in the alias
 *    table, so it is suppressible. This test parses the header itself, which
 *    means a future edit that adds `float sdCross(...)` to the library and
 *    forgets the alias fails CI instead of shipping a redefinition crash.
 *
 * All GL-free, so it runs headless in CI.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/shader/glsl_shadow.h"
#include "neowall/shader/shader_stdlib.h"

/* The std-lib header defines every chunk as a `static const char *` — pointer
 * variables, so they cannot initialise a file-scope array. Hand them out from a
 * function instead, which also keeps -Werror=unused-variable happy: every chunk
 * is referenced exactly once, here. Doubles as documentation of what a shader
 * actually gets prepended to it.
 *
 * `include_uniforms` selects whether the reactive uniform block (which declares
 * uniforms, not functions) is included along with the library proper. */
#define MAX_INJECTED_CHUNKS 11

static size_t injected_chunks(const char *out[MAX_INJECTED_CHUNKS],
                              bool include_uniforms) {
    size_t n = 0;
    if (include_uniforms) out[n++] = neowall_reactive_uniforms;
    out[n++] = neowall_glsl_stdlib;
    out[n++] = neowall_glsl_stdlib2;
    out[n++] = neowall_glsl_stdlib3;
    out[n++] = neowall_glsl_stdlib4;
    out[n++] = neowall_glsl_stdlib5;
    out[n++] = neowall_glsl_stdlib6;
    out[n++] = neowall_glsl_stdlib7;
    /* The scene kit is conditionally injected, but its namespace discipline
     * must hold all the same -- it is still neowall-authored GLSL landing in
     * the user's translation unit. */
    out[n++] = neowall_glsl_stdlib8;
    out[n++] = neowall_glsl_stdlib8b;
    out[n++] = neowall_glsl_stdlib8c;
    return n;
}

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define CHECK_MSG(cond, ...)                                                   \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ *
 * Scanner behaviour
 * ------------------------------------------------------------------ */

/* Convenience: does scanning `src` yield `name`? */
static bool defines(const char *src, const char *name) {
    glsl_shadow_set *s = glsl_shadow_create();
    if (!s) return false;
    glsl_shadow_scan(s, src);
    bool got = glsl_shadow_contains(s, name);
    glsl_shadow_free(s);
    return got;
}

static void test_detects_definitions(void) {
    /* The exact shape from issue #82: retro_wave.glsl's own sdBox. */
    CHECK(defines("float sdBox(vec3 p, vec3 radius){\n"
                  "    vec3 d = abs(p) - radius;\n"
                  "    return min(max(d.x, max(d.y, d.z)), 0.0);\n"
                  "}\n",
                  "sdBox"));

    /* Prototypes count: a prototype plus our alias is still a redefinition. */
    CHECK(defines("float sdBox(vec3 p, vec3 b);\n", "sdBox"));

    /* Function-like and object-like macros both claim the name. */
    CHECK(defines("#define pulse(x) ((x)*(x))\n", "pulse"));

    /* Multiple definitions in one source. */
    glsl_shadow_set *s = glsl_shadow_create();
    glsl_shadow_scan(s, "float sdBox(vec2 p, vec2 b){ return 0.0; }\n"
                        "vec3 timeOfDayTint(){ return vec3(1.0); }\n"
                        "float helper(float x){ return x; }\n");
    CHECK(glsl_shadow_contains(s, "sdBox"));
    CHECK(glsl_shadow_contains(s, "timeOfDayTint"));
    CHECK(glsl_shadow_contains(s, "helper"));
    CHECK(glsl_shadow_count(s) == 3);
    glsl_shadow_free(s);
}

static void test_ignores_non_definitions(void) {
    /* A call is not a definition -- this is the false positive that would
     * strip an alias the shader actually relies on. */
    CHECK(!defines("void mainImage(out vec4 o, vec2 u){\n"
                   "    float d = sdBox(p, vec3(1.0));\n"
                   "}\n",
                   "sdBox"));

    /* Prose must never vote on the namespace. */
    CHECK(!defines("// float sdBox(vec3 p, vec3 b){ }\n", "sdBox"));
    CHECK(!defines("/* float sdBox(vec3 p, vec3 b){ } */\n", "sdBox"));
    CHECK(!defines("/* multi\n * float pulse(float x){ }\n */\n", "pulse"));

    /* Control flow is not a definition. */
    CHECK(!defines("void f(){ if (x) return; while (y) { } }\n", "if"));
    CHECK(!defines("void f(){ if (x) return; }\n", "while"));

    /* A nested (local) function-shaped thing at depth > 0 is a call. */
    CHECK(!defines("void mainImage(out vec4 o, vec2 u){ beat(); }\n", "beat"));

    /* mainImage itself is a definition, which proves the scanner is running. */
    CHECK(defines("void mainImage(out vec4 o, vec2 u){ beat(); }\n", "mainImage"));
}

static void test_edge_cases(void) {
    /* NULL and empty sources are no-ops, not crashes. */
    glsl_shadow_set *s = glsl_shadow_create();
    CHECK(glsl_shadow_scan(s, NULL));
    CHECK(glsl_shadow_scan(s, ""));
    CHECK(glsl_shadow_count(s) == 0);

    /* Accumulating across sources (Common block + pass body). */
    glsl_shadow_scan(s, "float a(){ return 0.0; }\n");
    glsl_shadow_scan(s, "float b(){ return 0.0; }\n");
    CHECK(glsl_shadow_contains(s, "a"));
    CHECK(glsl_shadow_contains(s, "b"));

    /* Duplicates are collapsed. */
    glsl_shadow_scan(s, "float a(){ return 1.0; }\n");
    CHECK(glsl_shadow_count(s) == 2);
    glsl_shadow_free(s);

    /* Unterminated block comment must terminate the scan, not run off. */
    CHECK(!defines("/* unterminated\nfloat sdBox(vec3 p, vec3 b){ }\n", "sdBox"));

    /* Whitespace between name and paren is legal GLSL. */
    CHECK(defines("float sdBox  (vec3 p, vec3 b){ return 0.0; }\n", "sdBox"));

    /* Newline between type and name, too. */
    CHECK(defines("float\nsdBox(vec3 p, vec3 b){ return 0.0; }\n", "sdBox"));
}

/* ------------------------------------------------------------------ *
 * The invariant: no un-aliasable unprefixed name in the std-lib
 * ------------------------------------------------------------------ */

static bool alias_table_has(const char *name) {
    for (size_t i = 0; i < NEOWALL_STDLIB_ALIAS_COUNT; i++) {
        if (strcmp(neowall_stdlib_aliases[i].name, name) == 0) return true;
    }
    return false;
}

/* Scan every chunk of the library for the names it defines, and require that
 * each one is either nw*-prefixed (collision-proof) or present in the alias
 * table (suppressible). This is the check that makes #82 unrepeatable. */
static void test_stdlib_namespace_is_clean(void) {
    const char *chunks[MAX_INJECTED_CHUNKS];
    size_t nchunks = injected_chunks(chunks, false);

    glsl_shadow_set *lib = glsl_shadow_create();
    CHECK(lib != NULL);
    if (!lib) return;

    for (size_t i = 0; i < nchunks; i++) {
        CHECK(glsl_shadow_scan(lib, chunks[i]));
    }

    /* The library must define something, or the scan silently passed. */
    CHECK(glsl_shadow_count(lib) > 20);

    for (size_t i = 0; i < glsl_shadow_count(lib); i++) {
        const char *name = glsl_shadow_at(lib, i);

        /* Namespaced and therefore collision-proof: `nwFoo` functions and
         * `NW_FOO` macros (the latter are additionally #ifndef-guarded, so a
         * shader defining its own NW_PI simply wins). */
        if (strncmp(name, "nw", 2) == 0) continue;
        if (strncmp(name, "NW_", 3) == 0) continue;

        /* mainImage is the Shadertoy entry point the wrapper calls, not a
         * helper we inject; the shader is *required* to define it. */
        if (strcmp(name, "mainImage") == 0) continue;

        CHECK_MSG(alias_table_has(name),
                  "std-lib defines unprefixed `%s` with no entry in "
                  "neowall_stdlib_aliases[] -- a shader defining `%s` will "
                  "fail to compile. Give it an nw*-prefixed name and add an "
                  "alias entry, or #ifndef-guard it under NW_.",
                  name, name);
    }

    glsl_shadow_free(lib);
}

/* Every alias must forward to a canonical nw* symbol that actually exists,
 * so suppressing an alias can never leave dangling GLSL. */
static void test_aliases_forward_to_real_symbols(void) {
    const char *chunks[MAX_INJECTED_CHUNKS];
    size_t nchunks = injected_chunks(chunks, false);

    glsl_shadow_set *lib = glsl_shadow_create();
    for (size_t i = 0; i < nchunks; i++) {
        glsl_shadow_scan(lib, chunks[i]);
    }

    for (size_t i = 0; i < NEOWALL_STDLIB_ALIAS_COUNT; i++) {
        const neowall_stdlib_alias *a = &neowall_stdlib_aliases[i];

        /* The alias body must mention an nw* target. */
        CHECK_MSG(strstr(a->decl, "nw") != NULL,
                  "alias `%s` does not forward to an nw* symbol", a->name);

        /* Derive nwFoo from foo and require the library defines it. */
        char canonical[64];
        snprintf(canonical, sizeof(canonical), "nw%c%s",
                 a->name[0] - 32, a->name + 1);
        CHECK_MSG(glsl_shadow_contains(lib, canonical),
                  "alias `%s` forwards to `%s`, which the std-lib does not define",
                  a->name, canonical);

        /* The alias must define the name it claims. */
        CHECK_MSG(defines(a->decl, a->name),
                  "alias entry `%s` does not actually define `%s`",
                  a->name, a->name);
    }

    glsl_shadow_free(lib);
}

/* The table itself must be free of duplicates -- two entries for one name
 * would emit two definitions and cause the very error we are preventing. */
static void test_alias_table_has_no_duplicates(void) {
    for (size_t i = 0; i < NEOWALL_STDLIB_ALIAS_COUNT; i++) {
        for (size_t j = i + 1; j < NEOWALL_STDLIB_ALIAS_COUNT; j++) {
            CHECK_MSG(strcmp(neowall_stdlib_aliases[i].name,
                             neowall_stdlib_aliases[j].name) != 0,
                      "duplicate alias entry for `%s`",
                      neowall_stdlib_aliases[i].name);
        }
    }
}

/* Simulate the injection decision for the exact shader from issue #82: the
 * sdBox alias must be withheld, every other alias must still be emitted. */
static void test_issue_82_regression(void) {
    const char *retro_wave_excerpt =
        "// Retro wave city\n"
        "#define MAX_STEPS 100\n"
        "float sdBox(vec3 p, vec3 radius){\n"
        "    vec3 d = abs(p) - radius;\n"
        "    return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0));\n"
        "}\n"
        "float map(vec3 p){\n"
        "    return sdBox(p, vec3(1.0));\n"
        "}\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord){\n"
        "    fragColor = vec4(map(vec3(fragCoord, 0.0)));\n"
        "}\n";

    glsl_shadow_set *s = glsl_shadow_create();
    CHECK(glsl_shadow_scan(s, retro_wave_excerpt));

    int suppressed = 0, emitted = 0;
    for (size_t i = 0; i < NEOWALL_STDLIB_ALIAS_COUNT; i++) {
        if (glsl_shadow_contains(s, neowall_stdlib_aliases[i].name)) {
            suppressed++;
            CHECK(strcmp(neowall_stdlib_aliases[i].name, "sdBox") == 0);
        } else {
            emitted++;
        }
    }

    CHECK_MSG(suppressed == 1, "expected exactly sdBox suppressed, got %d",
              suppressed);
    CHECK_MSG(emitted == (int)NEOWALL_STDLIB_ALIAS_COUNT - 1,
              "unrelated aliases were withheld");

    /* The shader's own helpers are seen; its calls are not mistaken for defs. */
    CHECK(glsl_shadow_contains(s, "map"));
    CHECK(glsl_shadow_contains(s, "mainImage"));
    CHECK(glsl_shadow_contains(s, "MAX_STEPS"));

    glsl_shadow_free(s);
}

/* The reactive uniform block declares uniforms, never functions. If a helper
 * ever drifts into it, it would bypass the namespace audit above — so assert
 * the block stays function-free, and that the audit covers one more chunk than
 * the library-only view. */
static void test_uniform_block_defines_no_functions(void) {
    const char *with[MAX_INJECTED_CHUNKS];
    const char *without[MAX_INJECTED_CHUNKS];
    size_t n_with = injected_chunks(with, true);
    size_t n_without = injected_chunks(without, false);

    CHECK(n_with == n_without + 1);

    glsl_shadow_set *s = glsl_shadow_create();
    CHECK(glsl_shadow_scan(s, with[0])); /* the reactive block */
    CHECK_MSG(glsl_shadow_count(s) == 0,
              "reactive uniform block defines %zu function-like name(s); "
              "they would escape the std-lib namespace audit",
              glsl_shadow_count(s));
    glsl_shadow_free(s);
}

/* The scene kit is injected only when the shader defines nwMap, and its default
 * nwMaterial is withheld when the shader defines one. Both decisions run
 * through this scanner, so they inherit its comment-awareness: a shader that
 * merely MENTIONS nwMap in prose must not trigger injection, or every such
 * shader gets an unresolved forward declaration and fails to link. */
static void test_scene_kit_gating(void) {
    /* A real definition opts in. */
    CHECK(defines("float nwMap(vec3 p){ return length(p)-1.0; }\n", "nwMap"));

    /* Prose does not. */
    CHECK(!defines("// define nwMap(vec3 p) to use the scene kit\n", "nwMap"));
    CHECK(!defines("/* call nwMap(p) from your own loop */\n", "nwMap"));

    /* Calling it without defining it does not either -- that shader would be
     * broken anyway, but it must not be broken by US injecting a declaration. */
    CHECK(!defines("void mainImage(out vec4 o, vec2 u){ float d = nwMap(vec3(u,0)); }\n",
                   "nwMap"));

    /* A shader supplying its own material suppresses the kit's default. */
    CHECK(defines("vec3 nwMaterial(vec3 p, vec3 n){ return vec3(1.0); }\n",
                  "nwMaterial"));
    CHECK(!defines("// nwMaterial defaults to grey\n", "nwMaterial"));

    /* The realistic combination: both defined in one source. */
    glsl_shadow_set *s = glsl_shadow_create();
    glsl_shadow_scan(s,
        "vec3 nwMaterial(vec3 p, vec3 n){ return vec3(0.5); }\n"
        "float nwMap(vec3 p){ return nwGround(p, -0.5); }\n"
        "void mainImage(out vec4 o, vec2 u){ o = vec4(nwRender(nwCameraOrbit(u,4.,0.,0.3)),1.); }\n");
    CHECK(glsl_shadow_contains(s, "nwMap"));
    CHECK(glsl_shadow_contains(s, "nwMaterial"));
    /* Kit-provided names the shader only CALLS must stay unclaimed, or we would
     * wrongly suppress the very functions it is relying on. */
    CHECK(!glsl_shadow_contains(s, "nwRender"));
    CHECK(!glsl_shadow_contains(s, "nwCameraOrbit"));
    CHECK(!glsl_shadow_contains(s, "nwGround"));
    glsl_shadow_free(s);
}

int main(void) {
    test_detects_definitions();
    test_ignores_non_definitions();
    test_edge_cases();
    test_uniform_block_defines_no_functions();
    test_stdlib_namespace_is_clean();
    test_aliases_forward_to_real_symbols();
    test_alias_table_has_no_duplicates();
    test_issue_82_regression();
    test_scene_kit_gating();

    printf("glsl_shadow: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
