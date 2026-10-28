/* config.c, the real thing, headless.
 *
 * Drives config_load / config_reload with crafted files against two fake
 * outputs. output_apply_config is stubbed to record what each output was
 * given, so the parser, validation, precedence (default -> per-output) and the
 * reload-keeps-last-good contract are all tested without GL.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "neowall/neowall.h"
#include "neowall/config/config.h"
#include "neowall/output/output.h"
#include <stdatomic.h>

/* ---- stub: record the last config applied to each output ---- */
static int g_applied;
bool output_apply_config(struct output_state *o, struct wallpaper_config *c) {
    if (!o || !c) return false;
    g_applied++;
    if (!o->config) o->config = calloc(1, sizeof(*o->config));
    if (!o->config) return false;
    config_free_wallpaper(o->config);
    *o->config = *c;
    /* the caller frees its arrays; keep only the scalar/string fields */
    o->config->cycle_paths = NULL;
    o->config->cycle_count = 0;
    o->config->channel_paths = NULL;
    o->config->channel_count = 0;
    return true;
}

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static char dir[] = "/tmp/nw-config-test-XXXXXX";
static char cfg[512];

static void write_cfg(const char *text) {
    FILE *f = fopen(cfg, "w");
    if (!f) { perror(cfg); exit(1); }
    fputs(text, f);
    fclose(f);
}

static void touch(const char *name) {
    char p[600];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (f) { fputs("void mainImage(out vec4 c, in vec2 p){c=vec4(1);}\n", f); fclose(f); }
}

int main(void) {
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(cfg, sizeof(cfg), "%s/config.vibe", dir);
    touch("a.glsl");
    touch("b.glsl");
    char shaders[600];
    snprintf(shaders, sizeof(shaders), "%s/shaders", dir);
    mkdir(shaders, 0755);
    touch("shaders/one.glsl");
    touch("shaders/two.glsl");
    setenv("HOME", dir, 1);
    unsetenv("XDG_CONFIG_HOME");

    struct neowall_state st;
    memset(&st, 0, sizeof(st));
    pthread_rwlock_init(&st.output_list_lock, NULL);
    pthread_mutex_init(&st.state_mutex, NULL);
    pthread_mutex_init(&st.state_file_lock, NULL);

    struct output_state left, right;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    snprintf(left.connector_name, sizeof(left.connector_name), "DP-1");
    snprintf(left.model, sizeof(left.model), "Dell U2720Q");
    snprintf(right.connector_name, sizeof(right.connector_name), "HDMI-A-1");
    snprintf(right.model, sizeof(right.model), "LG 27GL850");
    left.next = &right;
    st.outputs = &left;
    st.output_count = 2;

    char a[600], b[600], text[2048];
    snprintf(a, sizeof(a), "%s/a.glsl", dir);
    snprintf(b, sizeof(b), "%s/b.glsl", dir);

    /* default applies everywhere; an output block overrides by connector */
    snprintf(text, sizeof(text),
             "default {\n  shader %s\n  shader_speed 0.5\n}\n"
             "output {\n  HDMI-A-1 {\n    shader %s\n    vsync false\n    shader_fps 30\n  }\n}\n",
             a, b);
    write_cfg(text);
    g_applied = 0;
    CHECK(config_load(&st, cfg));
    CHECK(g_applied == 2);   /* one apply per output, not default-then-override */
    CHECK(left.config && left.config->type == WALLPAPER_SHADER);
    CHECK(left.config && strstr(left.config->shader_path, "a.glsl"));
    CHECK(left.config && left.config->shader_speed == 0.5f);
    CHECK(right.config && strstr(right.config->shader_path, "b.glsl"));
    CHECK(right.config && !right.config->vsync && right.config->shader_fps == 30);

    /* outputs also match by model name */
    snprintf(text, sizeof(text), "output {\n  \"Dell U2720Q\" { shader %s }\n}\n", b);
    write_cfg(text);
    CHECK(config_reload(&st, cfg));
    CHECK(left.config && strstr(left.config->shader_path, "b.glsl"));

    /* reload is strict: each bad file is rejected and nothing changes */
    const char *bad[] = {
        "default {\n  shader a.glsl\n  path /x.png\n}\n",          /* two sources */
        "default {\n  shader_speed -3\n  shader a.glsl\n}\n",      /* out of range */
        "default {\n  shader_fps 2.5\n  shader a.glsl\n}\n",       /* non-integer fps */
        "default {\n  mode sideways\n  path /x.png\n}\n",          /* unknown mode */
        "default {\n  transition warp\n  path /x.png\n}\n",        /* unknown transition */
        "default {\n  channels 3\n  shader a.glsl\n}\n",           /* not an array */
        "default {\n  shader a.glsl\n",                            /* syntax */
        "default {\n  channels [ a b c d e ]\n  shader a.glsl\n}\n", /* > 4 channels */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        write_cfg(bad[i]);
        int before = g_applied;
        bool ok = config_reload(&st, cfg);
        if (ok) printf("  accepted bad config #%zu\n", i);
        CHECK(!ok);
        CHECK(g_applied == before);
        CHECK(left.config && strstr(left.config->shader_path, "b.glsl"));
    }

    /* a missing file on reload keeps the live config too */
    unlink(cfg);
    CHECK(!config_reload(&st, cfg));
    CHECK(left.config && strstr(left.config->shader_path, "b.glsl"));

    /* unknown keys are ignored (forward compatible), with a typo hint */
    CHECK(config_edit_distance("chanels", "channels") == 1);
    CHECK(config_edit_distance("shader_sped", "shader_speed") == 1);
    CHECK(config_edit_distance("vsync", "vsync") == 0);
    CHECK(config_edit_distance("", "abc") == 3);
    snprintf(text, sizeof(text), "default {\n  shader %s\n  shader_sped 9\n}\n", a);
    write_cfg(text);
    CHECK(config_reload(&st, cfg));
    CHECK(left.config && left.config->shader_speed == 1.0f);

    /* a shader directory becomes a cycle */
    snprintf(text, sizeof(text), "default {\n  shader %s/\n  duration 60\n  shuffle false\n}\n",
             shaders);
    write_cfg(text);
    CHECK(config_load(&st, cfg));
    CHECK(left.config && left.config->duration == 60.0f);

    /* a bad channel after a directory cycle is built: rejected, no leak (ASan) */
    snprintf(text, sizeof(text), "default {\n  shader %s/\n  channels [ \"rgba_noise\" 5 ]\n}\n",
             shaders);
    write_cfg(text);
    CHECK(!config_reload(&st, cfg));

    /* channels are for shaders only */
    write_cfg("default {\n  path /x.png\n  channels [ \"rgba_noise\" ]\n}\n");
    CHECK(!config_reload(&st, cfg));

    /* an empty directory with dir syntax is an error, not a single file */
    char empty[600];
    snprintf(empty, sizeof(empty), "%s/empty", dir);
    mkdir(empty, 0755);
    snprintf(text, sizeof(text), "default {\n  shader %s/\n}\n", empty);
    write_cfg(text);
    CHECK(!config_reload(&st, cfg));

    /* terminal block: command, ints, colours and clamped effects */
    write_cfg("default {\n  terminal \"htop -d 10\"\n  term_cols 120\n  term_fg \"#a0b0c0\"\n"
              "  term_bloom 3\n  term_crt 0.25\n}\n");
    CHECK(config_load(&st, cfg));
    CHECK(left.config && left.config->type == WALLPAPER_TERMINAL);
    CHECK(left.config && strcmp(left.config->term_cmd, "htop -d 10") == 0);
    CHECK(left.config && left.config->term_cols == 120);
    CHECK(left.config && left.config->term_fg == 0xa0b0c0);
    CHECK(left.config && left.config->term_bloom == 1.0f);   /* clamped */
    CHECK(left.config && left.config->term_crt == 0.25f);
    write_cfg("default {\n  terminal htop\n  term_fg \"blue\"\n}\n");
    CHECK(!config_reload(&st, cfg));
    write_cfg("default {\n  terminal htop\n  term_rows 9999\n}\n");
    CHECK(!config_reload(&st, cfg));

    /* top-level keys */
    snprintf(text, sizeof(text), "mouse_interaction false\ndefault {\n  shader %s\n}\n", a);
    write_cfg(text);
    atomic_store(&st.mouse_interaction, true);
    CHECK(config_load(&st, cfg));
    CHECK(!atomic_load(&st.mouse_interaction));

    /* first run with no file writes a starter config and loads it */
    char fresh[600];
    snprintf(fresh, sizeof(fresh), "%s/new/neowall/config.vibe", dir);
    CHECK(config_load(&st, fresh));
    CHECK(access(fresh, R_OK) == 0);

    free(left.config);
    free(right.config);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fails++;

    printf("config: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
