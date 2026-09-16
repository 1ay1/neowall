/* Tests for `neowall new` scaffolding.
 *
 * The generated shaders are the first thing a new author sees, so the bar is
 * that every template produces GLSL that actually compiles -- a template that
 * does not is worse than no template at all. Compiling needs a GPU, so that is
 * verified by hand; what runs here is everything that does NOT need one:
 * name handling, template selection, and the refusal paths.
 *
 * The refusal paths matter most. This command writes files into the user's
 * config directory from a name they supply, so path traversal must be rejected
 * and existing work must never be silently overwritten.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "neowall/shader/shader_scaffold.h"

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

/* Point HOME at a scratch dir so the real ~/.config is never touched. */
static char scratch[] = "/tmp/nw-scaffold-XXXXXX";

static bool file_exists(const char *rel) {
    char p[512];
    snprintf(p, sizeof(p), "%s/.config/neowall/shaders/%s", scratch, rel);
    struct stat st;
    return stat(p, &st) == 0;
}

static long file_size_of(const char *rel) {
    char p[512];
    snprintf(p, sizeof(p), "%s/.config/neowall/shaders/%s", scratch, rel);
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* Does the generated file contain this snippet? */
static bool file_contains(const char *rel, const char *needle) {
    char p[512];
    snprintf(p, sizeof(p), "%s/.config/neowall/shaders/%s", scratch, rel);
    FILE *f = fopen(p, "r");
    if (!f) return false;

    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

static void test_creates_each_template(void) {
    /* Every template must produce a non-trivial file. */
    CHECK(neowall_scaffold_create("t_scene", "scene", false));
    CHECK(file_exists("t_scene.glsl"));
    CHECK(file_size_of("t_scene.glsl") > 500);

    CHECK(neowall_scaffold_create("t_flat", "flat", false));
    CHECK(file_exists("t_flat.glsl"));

    CHECK(neowall_scaffold_create("t_react", "reactive", false));
    CHECK(file_exists("t_react.glsl"));

    /* NULL template means the default, which must be the scene kit -- that is
     * the whole point of the command. */
    CHECK(neowall_scaffold_create("t_default", NULL, false));
    CHECK(file_contains("t_default.glsl", "nwMap"));
}

static void test_templates_are_wired_correctly(void) {
    /* The scene template must use the kit, or the author gets a grey screen
     * and no hint that materials exist. */
    CHECK(file_contains("t_scene.glsl", "float nwMap(vec3 p)"));
    CHECK(file_contains("t_scene.glsl", "nwRender"));
    CHECK(file_contains("t_scene.glsl", "nwMaterial"));
    CHECK(file_contains("t_scene.glsl", "nwGloss"));
    CHECK(file_contains("t_scene.glsl", "nwEmissive"));

    /* The flat template must NOT define nwMap, or it would drag in the scene
     * kit it does not use. */
    CHECK(!file_contains("t_flat.glsl", "float nwMap"));
    CHECK(file_contains("t_flat.glsl", "mainImage"));

    /* The reactive template exists to advertise the uniforms. */
    CHECK(file_contains("t_react.glsl", "iCpu"));
    CHECK(file_contains("t_react.glsl", "iAudioLevel"));

    /* Every template tells the author how to iterate. */
    CHECK(file_contains("t_scene.glsl", "neowall watch"));
    CHECK(file_contains("t_flat.glsl", "neowall watch"));
    CHECK(file_contains("t_react.glsl", "neowall watch"));

    /* The name is substituted into the header comment, not left as a literal
     * format specifier. */
    CHECK(file_contains("t_scene.glsl", "t_scene.glsl"));
    CHECK(!file_contains("t_scene.glsl", "%s"));
}

static void test_suffix_handling(void) {
    /* `new foo` and `new foo.glsl` must both give foo.glsl, never
     * foo.glsl.glsl. */
    CHECK(neowall_scaffold_create("suffixed.glsl", "flat", false));
    CHECK(file_exists("suffixed.glsl"));
    CHECK(!file_exists("suffixed.glsl.glsl"));
}

static void test_refuses_to_clobber(void) {
    CHECK(neowall_scaffold_create("precious", "flat", false));
    long first = file_size_of("precious.glsl");

    /* Second attempt without --force must fail and leave the file alone. */
    CHECK(!neowall_scaffold_create("precious", "scene", false));
    CHECK(file_size_of("precious.glsl") == first);

    /* With force it is replaced, and by a different template. */
    CHECK(neowall_scaffold_create("precious", "scene", true));
    CHECK(file_contains("precious.glsl", "nwMap"));
}

static void test_rejects_bad_names(void) {
    /* Path traversal and separators: this writes into the user's config dir
     * from a user-supplied name, so these must never be honoured. */
    CHECK(!neowall_scaffold_create("../escape", "flat", false));
    CHECK(!neowall_scaffold_create("sub/dir", "flat", false));
    CHECK(!neowall_scaffold_create("..", "flat", false));
    CHECK(!neowall_scaffold_create(".", "flat", false));
    CHECK(!neowall_scaffold_create("", "flat", false));
    CHECK(!neowall_scaffold_create(NULL, "flat", false));

    /* A bare ".glsl" reduces to an empty stem and must be refused. */
    CHECK(!neowall_scaffold_create(".glsl", "flat", false));

    /* Nothing escaped into the parent directory. */
    char p[512];
    snprintf(p, sizeof(p), "%s/.config/neowall/escape.glsl", scratch);
    struct stat st;
    CHECK(stat(p, &st) != 0);
}

static void test_rejects_unknown_template(void) {
    CHECK(!neowall_scaffold_create("whatever", "no-such-template", false));
    CHECK(!file_exists("whatever.glsl"));
}

int main(void) {
    if (!mkdtemp(scratch)) {
        fprintf(stderr, "could not create scratch dir\n");
        return 1;
    }
    setenv("HOME", scratch, 1);

    test_creates_each_template();
    test_templates_are_wired_correctly();
    test_suffix_handling();
    test_refuses_to_clobber();
    test_rejects_bad_names();
    test_rejects_unknown_template();

    /* Best-effort cleanup; the scratch dir is under /tmp either way. */
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", scratch);
    if (system(cmd) != 0) { /* nothing useful to do on failure */ }

    printf("shader_scaffold: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
