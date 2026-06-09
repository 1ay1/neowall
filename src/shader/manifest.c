/* .neowall manifest parser — see manifest.h. Uses the project VIBE parser. */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "neowall/shader/manifest.h"
#include "neowall/config/vibe.h"
#include "neowall/neowall.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Read a whole file into a malloc'd NUL-terminated buffer, or NULL. */
static char *slurp(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 256 * 1024) {
        return NULL;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    char *buf = malloc(st.st_size + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, st.st_size, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

/* Build the sidecar manifest path for a shader: "foo.glsl" -> "foo.neowall".
 * If shader_path already ends in ".neowall", copy it verbatim. */
static void manifest_path_for(const char *shader_path, char *out, size_t cap) {
    size_t len = strlen(shader_path);
    const char *dot = strrchr(shader_path, '.');
    if (dot && strcmp(dot, ".neowall") == 0) {
        snprintf(out, cap, "%s", shader_path);
        return;
    }
    if (dot) {
        size_t base = (size_t)(dot - shader_path);
        if (base > cap - 9) base = cap - 9;
        memcpy(out, shader_path, base);
        snprintf(out + base, cap - base, ".neowall");
    } else {
        snprintf(out, cap, "%.*s.neowall", (int)len, shader_path);
    }
}

/* Apply a per-pass channel block: each entry is "chN source" (e.g. ch0 audio).
 * The key's leading non-digits are skipped, so ch0 / channel0 / 0 all work. */
static void apply_channel_block(multipass_shader_t *shader, VibeObject *chans,
                                multipass_type_t pass_type) {
    if (!chans) return;
    for (size_t i = 0; i < chans->count; i++) {
        const char *key = chans->entries[i].key;       /* "ch0".."ch3" or "0".."3" */
        VibeValue *val = chans->entries[i].value;
        if (!val || val->type != VIBE_TYPE_STRING) continue;
        /* Accept "ch0", "channel0", or a bare "0". */
        const char *digits = key;
        while (*digits && (*digits < '0' || *digits > '9')) digits++;
        int ch = atoi(digits);
        if (ch < 0 || ch >= MULTIPASS_MAX_CHANNELS) continue;
        channel_source_t src = multipass_channel_source_from_name(val->as_string);
        if (src == CHANNEL_SOURCE_NONE) {
            log_info("Manifest: unknown channel source '%s' for channel %d",
                     val->as_string, ch);
            continue;
        }
        multipass_set_channel(shader, pass_type, ch, src);
    }
}

bool manifest_resolve_shader_path(const char *shader_path, char *out, size_t out_size) {
    if (!shader_path) return false;
    const char *dot = strrchr(shader_path, '.');
    if (!dot || strcmp(dot, ".neowall") != 0) {
        return false;  /* not a manifest */
    }

    char *content = slurp(shader_path);
    if (!content) return false;

    VibeParser *parser = vibe_parser_new();
    if (!parser) { free(content); return false; }
    VibeValue *root = vibe_parse_string(parser, content);
    free(content);

    bool resolved = false;
    if (root && root->type == VIBE_TYPE_OBJECT) {
        VibeValue *sh = vibe_object_get(root->as_object, "shader");
        if (sh && sh->type == VIBE_TYPE_STRING) {
            /* Resolve relative to the manifest's directory. */
            const char *slash = strrchr(shader_path, '/');
            if (slash && sh->as_string[0] != '/') {
                size_t dirlen = (size_t)(slash - shader_path) + 1;
                snprintf(out, out_size, "%.*s%s", (int)dirlen, shader_path, sh->as_string);
            } else {
                snprintf(out, out_size, "%s", sh->as_string);
            }
            resolved = true;
        }
    }
    if (root) vibe_value_free(root);
    vibe_parser_free(parser);
    return resolved;
}

bool manifest_apply(multipass_shader_t *shader, const char *shader_path) {
    if (!shader || !shader_path) return false;

    /* Build the sidecar manifest name, then look for it next to the shader.
     * We search the same dirs the shader loader uses but quietly (a missing
     * manifest is the common case and must not log an error). */
    char mname[256];
    manifest_path_for(shader_path, mname, sizeof(mname));

    char *content = NULL;
    if (mname[0] == '/' || mname[0] == '~' || strchr(mname, '/')) {
        content = slurp(mname);
    } else {
        const char *home = getenv("HOME");
        const char *xdg = getenv("XDG_CONFIG_HOME");
        char cand[1024];
        if (!content && xdg && xdg[0]) {
            snprintf(cand, sizeof(cand), "%s/neowall/shaders/%s", xdg, mname);
            content = slurp(cand);
        }
        if (!content && home && home[0]) {
            snprintf(cand, sizeof(cand), "%s/.config/neowall/shaders/%s", home, mname);
            content = slurp(cand);
        }
        if (!content) {
            snprintf(cand, sizeof(cand), "/usr/share/neowall/shaders/%s", mname);
            content = slurp(cand);
        }
        if (!content) {
            snprintf(cand, sizeof(cand), "/usr/local/share/neowall/shaders/%s", mname);
            content = slurp(cand);
        }
    }
    if (!content) return false;   /* no manifest — heuristic stays in charge */

    VibeParser *parser = vibe_parser_new();
    if (!parser) { free(content); return false; }
    VibeValue *root = vibe_parse_string(parser, content);
    free(content);

    if (!root || root->type != VIBE_TYPE_OBJECT) {
        if (root) vibe_value_free(root);
        vibe_parser_free(parser);
        log_info("Manifest: %s is not a valid object, ignoring", mname);
        return false;
    }

    log_info("Manifest: applying %s", mname);

    /* --- channels: top-level channel0..3 keys target the Image pass --- */
    for (int c = 0; c < MULTIPASS_MAX_CHANNELS; c++) {
        char key[16];
        snprintf(key, sizeof(key), "channel%d", c);
        VibeValue *v = vibe_object_get(root->as_object, key);
        if (v && v->type == VIBE_TYPE_STRING) {
            channel_source_t src = multipass_channel_source_from_name(v->as_string);
            if (src != CHANNEL_SOURCE_NONE) {
                multipass_set_channel(shader, PASS_TYPE_IMAGE, c, src);
            } else {
                log_info("Manifest: unknown channel source '%s' for channel%d",
                         v->as_string, c);
            }
        }
    }

    /* --- explicit per-pass channel blocks: image{}, bufferA{}, ... --- */
    static const struct { const char *key; multipass_type_t type; } pass_keys[] = {
        {"image",   PASS_TYPE_IMAGE},
        {"bufferA", PASS_TYPE_BUFFER_A},
        {"bufferB", PASS_TYPE_BUFFER_B},
        {"bufferC", PASS_TYPE_BUFFER_C},
        {"bufferD", PASS_TYPE_BUFFER_D},
    };
    for (size_t p = 0; p < sizeof(pass_keys) / sizeof(pass_keys[0]); p++) {
        VibeValue *blk = vibe_object_get(root->as_object, pass_keys[p].key);
        if (blk && blk->type == VIBE_TYPE_OBJECT) {
            apply_channel_block(shader, blk->as_object, pass_keys[p].type);
        }
    }

    /* --- uniforms block: name -> source-keyword | number --- */
    VibeValue *uni = vibe_object_get(root->as_object, "uniforms");
    if (uni && uni->type == VIBE_TYPE_OBJECT) {
        VibeObject *o = uni->as_object;
        for (size_t i = 0; i < o->count; i++) {
            const char *name = o->entries[i].key;
            VibeValue *v = o->entries[i].value;
            if (!v) continue;
            if (v->type == VIBE_TYPE_STRING) {
                uniform_bind_t bind = multipass_bind_from_name(v->as_string);
                if (bind == UNIFORM_BIND_CONST) {
                    /* string that wasn't a known signal -> try numeric literal */
                    float lit = (float)atof(v->as_string);
                    multipass_add_user_uniform(shader, name, UNIFORM_BIND_CONST, lit);
                } else {
                    multipass_add_user_uniform(shader, name, bind, 0.0f);
                }
            } else if (v->type == VIBE_TYPE_FLOAT) {
                multipass_add_user_uniform(shader, name, UNIFORM_BIND_CONST, (float)v->as_float);
            } else if (v->type == VIBE_TYPE_INTEGER) {
                multipass_add_user_uniform(shader, name, UNIFORM_BIND_CONST, (float)v->as_integer);
            }
        }
    }

    vibe_value_free(root);
    vibe_parser_free(parser);
    return true;
}
