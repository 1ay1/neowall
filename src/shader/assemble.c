/* Assemble a pass's full fragment shader. GL-free.
 *
 * Layout:
 *   #version 330 core + Shadertoy uniforms + GLSL ES shims
 *   reactive uniforms, stdlib (nw* names, always)
 *   friendly aliases (sdBox, pulse, ...) the shader doesn't define itself
 *   scene kit, if the shader defines nwMap
 *   @param uniforms, manifest uniforms
 *   #line-tagged common, #line-tagged pass body
 *   main()
 *
 * The map gives, for every generated line, the file:line it came from, so a
 * driver error at generated line N is reported where the author wrote it,
 * including inside #include'd files. */
#include "compat.h"
#include "neowall/shader/shader_source.h"
#include "neowall/log.h"

#include <stdio.h>
#include <string.h>

#include "neowall/shader/glsl_shadow.h"
#include "neowall/shader/shader_stdlib.h"

static const char prefix[] =
    "#version 330 core\n"
    "uniform float iTime;\n"
    "uniform vec3  iResolution;\n"
    "uniform vec4  iMouse;\n"
    "uniform vec2  iSpanOffset;\n"
    "uniform int   iFrame;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform vec4  iDate;\n"
    "uniform float iSampleRate;\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n"
    "uniform vec3  iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "// Live terminal (terminal wallpapers): integer cell grid + glyph atlas.\n"
    "uniform highp usampler2D iTermCells;\n"
    "uniform highp usampler2D iTermChange;\n"
    "uniform sampler2D iTermAtlas;\n"
    "uniform sampler2D iTermColorAtlas;\n"
    "uniform vec4 iTermInfo;\n"
    "uniform vec2 iTermAtlasSize;\n"
    "uniform vec3 iTermCursor;\n"
    "uniform vec4 iTermCursorPrev;\n"
    "uniform vec4 iTermFX;\n"
    "uniform vec2 iTermFade;\n"
    "#define NW_HAS_ITERMFX 1\n"
    "#define texture2D texture\n"
    "#define texture2DLod textureLod\n"
    "#define texture2DLodEXT textureLod\n"
    "#define texture2DProj textureProj\n"
    "#define textureCube texture\n"
    "#define textureCubeLod textureLod\n"
    "#define iGlobalTime iTime\n"
    "#define HW_PERFORMANCE 1\n"
    "out vec4 fragColor;\n";

static const char suffix[] =
    "void main() {\n"
    "    mainImage(fragColor, gl_FragCoord.xy + iSpanOffset);\n"
    "}\n";

typedef struct {
    nw_str text;
    NW_LIST(nw_src_loc) map;
    const char *cur_file; /* file the next #line-tagged line belongs to */
    int cur_line;
} builder;

/* Append text that came from nowhere in particular (injected). */
static bool put_injected(builder *b, const char *s) {
    for (const char *p = s; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) + 1 : strlen(p);
        if (!nw_str_append_n(&b->text, p, n) || !nw_list_push(&b->map, ((nw_src_loc){NULL, 0})))
            return false;
        p += n;
    }
    if (b->text.len && b->text.data[b->text.len - 1] != '\n') return nw_str_append(&b->text, "\n");
    return true;
}

/* Append author text carrying `#line N S` directives (from source.c). The
 * directive itself is emitted, and N and S seed the map for what follows. */
static bool put_tagged(builder *b, const nw_shader_source *src, const char *s) {
    int line = 1;
    const char *file = src->path;
    for (const char *p = s; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) + 1 : strlen(p);
        if (!strncmp(p, "#line ", 6)) {
            int sn = 0;
            if (sscanf(p + 6, "%d %d", &line, &sn) == 2 && sn >= 0 && (size_t)sn < src->includes.len)
                file = src->includes.data[sn];
            if (!nw_str_append_n(&b->text, p, n) || !nw_list_push(&b->map, ((nw_src_loc){NULL, 0})))
                return false;
        } else {
            if (!nw_str_append_n(&b->text, p, n) || !nw_list_push(&b->map, ((nw_src_loc){file, line})))
                return false;
            line++;
        }
        p += n;
    }
    return true;
}

static bool decl_param(builder *b, const nw_param *p) {
    char line[160];
    snprintf(line, sizeof(line), "uniform %s %s;\n", p->type, p->name);
    return put_injected(b, line);
}

size_t nw_shader_preamble_chunks(const char **out, size_t max) {
    const char *all[] = {
        prefix, neowall_reactive_uniforms,
        neowall_glsl_stdlib, neowall_glsl_stdlib2, neowall_glsl_stdlib3,
        neowall_glsl_stdlib4, neowall_glsl_stdlib5, neowall_glsl_stdlib6,
        neowall_glsl_stdlib7, neowall_glsl_stdlib8, neowall_glsl_stdlib8a2,
        neowall_glsl_stdlib8c, neowall_glsl_stdlib8d,
    };
    size_t n = NW_COUNTOF(all) < max ? NW_COUNTOF(all) : max;
    for (size_t i = 0; i < n; i++) out[i] = all[i];
    return n;
}

nw_result nw_shader_assemble(const nw_shader_source *src, int pass, char **text, nw_src_loc **map,
                             size_t *map_len) {
    if (pass < 0 || (size_t)pass >= src->passes.len) return nw_err(NW_ERR_INVALID_ARG, "no such pass");
    const nw_pass_src *p = &src->passes.data[pass];

    /* Normalise Shadertoy-isms the macros can't (stray #version etc). */
    char *body = nw_compat_fix(p->body);
    char *common = src->common ? nw_compat_fix(src->common) : NULL;
    if (!body || (src->common && !common)) {
        free(body);
        free(common);
        return nw_err(NW_ERR_OOM, "shader compat");
    }

    /* Which friendly names the shader defines itself: those aliases yield. */
    glsl_shadow_set *shadow = glsl_shadow_create();
    bool shadow_ok = shadow && glsl_shadow_scan(shadow, body) && (!common || glsl_shadow_scan(shadow, common));

    builder b = {0};
    bool ok = put_injected(&b, prefix) && put_injected(&b, neowall_reactive_uniforms);
    const char *const lib[] = {neowall_glsl_stdlib,  neowall_glsl_stdlib2, neowall_glsl_stdlib3,
                               neowall_glsl_stdlib4, neowall_glsl_stdlib5, neowall_glsl_stdlib6,
                               neowall_glsl_stdlib7};
    for (size_t i = 0; ok && i < NW_COUNTOF(lib); i++) ok = put_injected(&b, lib[i]);
    for (size_t i = 0; ok && i < NEOWALL_STDLIB_ALIAS_COUNT; i++)
        if (shadow_ok && !glsl_shadow_contains(shadow, neowall_stdlib_aliases[i].name))
            ok = put_injected(&b, neowall_stdlib_aliases[i].decl);

    if (ok && shadow_ok && glsl_shadow_contains(shadow, "nwMap")) {
        static const char *const hooks[] = {"nwMaterial", "nwGloss", "nwEmissive"};
        ok = put_injected(&b, neowall_glsl_stdlib8) && put_injected(&b, neowall_glsl_stdlib8a2);
        for (size_t i = 0; ok && i < NW_COUNTOF(hooks); i++)
            if (!glsl_shadow_contains(shadow, hooks[i])) ok = put_injected(&b, neowall_scene_hook_defaults[i]);
        ok = ok && put_injected(&b, neowall_glsl_stdlib8c) && put_injected(&b, neowall_glsl_stdlib8d);
    }
    glsl_shadow_free(shadow);

    for (size_t i = 0; ok && i < src->params.len; i++) ok = decl_param(&b, &src->params.data[i]);
    for (size_t i = 0; ok && i < src->binds.len; i++) {
        bool is_param = false;
        for (size_t k = 0; k < src->params.len; k++)
            if (!strcmp(src->params.data[k].name, src->binds.data[i].name)) is_param = true;
        if (!is_param) {
            char line[128];
            snprintf(line, sizeof(line), "uniform float %s;\n", src->binds.data[i].name);
            ok = put_injected(&b, line);
        }
    }

    if (ok && common) ok = put_tagged(&b, src, common);
    if (ok) ok = put_tagged(&b, src, body);
    /* main() is injected; give it its own source number so its errors
     * never land on an author's line */
    if (ok) ok = put_injected(&b, "#line 1 9999\n") && put_injected(&b, suffix);
    free(body);
    free(common);

    if (!ok || b.text.oom) {
        nw_str_free(&b.text);
        nw_list_free(&b.map);
        return nw_err(NW_ERR_OOM, "assemble");
    }
    *text = nw_str_take(&b.text);
    *map = b.map.data;
    *map_len = b.map.len;
    return nw_ok();
}
