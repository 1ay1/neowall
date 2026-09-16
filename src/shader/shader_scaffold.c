/* See include/neowall/shader/shader_scaffold.h for the rationale. */

#define _POSIX_C_SOURCE 200809L

#include "neowall/shader/shader_scaffold.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "neowall/neowall.h"

typedef struct {
    const char *name;
    const char *desc;
    const char *body;
} scaffold_template;

/* ---- scene: the headline template -------------------------------------- *
 * Uses the scene kit, so the author gets lighting, shadows and reflections
 * without writing a raymarcher. Deliberately shows the material hooks, since
 * those are what turn a grey blob into something worth looking at. */
static const char *const tmpl_scene =
    "// %s.glsl -- a lit 3D scene.\n"
    "//\n"
    "// neowall supplies the renderer: marching, normals, soft shadows, ambient\n"
    "// occlusion, sky reflections, fog and tonemapping. You describe the world\n"
    "// with nwMap() and the surfaces with the three hooks below.\n"
    "//\n"
    "// Live-reload while you edit:   neowall watch %s.glsl\n"
    "\n"
    "// The world. Signed distance from p to the nearest surface.\n"
    "float nwMap(vec3 p) {\n"
    "    float ground = nwGround(p, -0.6);\n"
    "\n"
    "    // A shape that breathes with the audio, floating above the floor.\n"
    "    vec3 q = p - vec3(0.0, 0.5 + 0.12 * sin(iTime * 0.9), 0.0);\n"
    "    q.xz = nwRot(iTime * 0.3) * q.xz;\n"
    "    float body = nwSdRoundBox(q, vec3(0.7), 0.18);\n"
    "    float cut  = nwSdSphere(q, 0.92 + 0.10 * iAudioBass);\n"
    "    float shape = nwOpSmoothInter(body, cut, 0.06);\n"
    "\n"
    "    return nwOpSmoothUnion(ground, shape, 0.25);\n"
    "}\n"
    "\n"
    "// Base colour.\n"
    "vec3 nwMaterial(vec3 p, vec3 n) {\n"
    "    if (p.y < -0.58) {\n"
    "        float c = mod(floor(p.x) + floor(p.z), 2.0);\n"
    "        return mix(vec3(0.10, 0.11, 0.13), vec3(0.20, 0.22, 0.26), c);\n"
    "    }\n"
    "    return vec3(0.90, 0.55, 0.25);\n"
    "}\n"
    "\n"
    "// x = roughness (0 mirror .. 1 chalk), y = metalness.\n"
    "vec2 nwGloss(vec3 p, vec3 n) {\n"
    "    if (p.y < -0.58) return vec2(0.35, 0.0);   // matte floor\n"
    "    return vec2(0.18, 0.85);                    // polished metal\n"
    "}\n"
    "\n"
    "// Glow, added after lighting. Pulses on the beat.\n"
    "vec3 nwEmissive(vec3 p, vec3 n) {\n"
    "    if (p.y < -0.58) return vec3(0.0);\n"
    "    return vec3(1.0, 0.35, 0.12) * iAudioBeat * 0.6;\n"
    "}\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    // Orbit slowly; drag with the mouse to look around.\n"
    "    float yaw   = iTime * 0.15 + (iMouse.x / iResolution.x - 0.5) * 3.0;\n"
    "    float pitch = 0.28 + (iMouse.y / iResolution.y - 0.5) * 0.5;\n"
    "    fragColor = vec4(nwRender(nwCameraOrbit(fragCoord, 4.5, yaw, pitch)), 1.0);\n"
    "}\n";

/* ---- flat: 2D, for people who want pixels not geometry ----------------- */
static const char *const tmpl_flat =
    "// %s.glsl -- a 2D shader.\n"
    "//\n"
    "// Plain Shadertoy-style fragment work. neowall's standard library is\n"
    "// available throughout: nwFbm, nwPalette, nwHash21, nwWorley, nwRot,\n"
    "// nwTonemap, and the reactive uniforms (iCpu, iAudioBass, iSun, ...).\n"
    "//\n"
    "// Live-reload while you edit:   neowall watch %s.glsl\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    // Centred, aspect-corrected coordinates: -0.5..0.5 vertically.\n"
    "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
    "\n"
    "    // Drifting fractal noise, warped by a slow rotation.\n"
    "    vec2 q = nwRot(iTime * 0.05) * uv * 2.4;\n"
    "    float n = nwFbm(q + vec2(iTime * 0.08, 0.0));\n"
    "\n"
    "    // Colour it with a cosine palette; the accent shifts with CPU load.\n"
    "    vec3 col = nwPalette(n + 0.2 * iCpu,\n"
    "                         vec3(0.5), vec3(0.5), vec3(1.0),\n"
    "                         vec3(0.00, 0.33, 0.67));\n"
    "\n"
    "    // Audio adds a bloom toward the centre.\n"
    "    col += vec3(0.6, 0.3, 0.9) * iAudioBeat * 0.25 * (1.0 - length(uv));\n"
    "\n"
    "    // Vignette, then tonemap so highlights roll off instead of clipping.\n"
    "    col *= 1.0 - 0.7 * dot(uv, uv);\n"
    "    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);\n"
    "}\n";

/* ---- reactive: a dashboard of the live machine ------------------------- */
static const char *const tmpl_reactive =
    "// %s.glsl -- a wallpaper that watches the machine.\n"
    "//\n"
    "// Every uniform here is live system state. Full catalogue in\n"
    "// docs/SHADER_NOTES.md; the useful ones are below.\n"
    "//\n"
    "// Live-reload while you edit:   neowall watch %s.glsl\n"
    "\n"
    "// Horizontal bar: fills to `v`, glows at the tip.\n"
    "vec3 bar(vec2 uv, float y, float v, vec3 tint) {\n"
    "    float row  = smoothstep(0.030, 0.026, abs(uv.y - y));\n"
    "    float fill = step(uv.x, v);\n"
    "    float tip  = smoothstep(0.02, 0.0, abs(uv.x - v));\n"
    "    return tint * row * (fill * 0.55 + tip * 1.4);\n"
    "}\n"
    "\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "    vec2 uv = fragCoord / iResolution.xy;\n"
    "\n"
    "    // Background drifts from night blue to day warmth with the real sun.\n"
    "    vec3 col = mix(vec3(0.02, 0.03, 0.06), vec3(0.08, 0.09, 0.13), nwDayNight());\n"
    "\n"
    "    // A readout of the machine, one bar per signal.\n"
    "    col += bar(uv, 0.62, iCpu,        vec3(0.20, 0.90, 0.55));  // CPU load\n"
    "    col += bar(uv, 0.56, iRam,        vec3(0.35, 0.65, 1.00));  // memory\n"
    "    col += bar(uv, 0.50, iGpu,        vec3(0.95, 0.45, 0.30));  // GPU\n"
    "    col += bar(uv, 0.44, iAudioLevel, vec3(0.85, 0.35, 0.95));  // audio\n"
    "    col += bar(uv, 0.38, iBattery,    vec3(0.95, 0.85, 0.30));  // battery\n"
    "\n"
    "    // Thermal warning: the whole screen flushes red as the CPU heats up.\n"
    "    col += vec3(0.35, 0.02, 0.0) * smoothstep(0.7, 1.0, iThermal);\n"
    "\n"
    "    // Typing and mouse activity ripple outward from the centre.\n"
    "    vec2 c = (uv - 0.5) * vec2(iResolution.x / iResolution.y, 1.0);\n"
    "    float ring = sin(length(c) * 40.0 - iTime * 4.0) * 0.5 + 0.5;\n"
    "    col += vec3(0.1, 0.3, 0.5) * ring * iKeyEnergy * 0.35;\n"
    "\n"
    "    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);\n"
    "}\n";

static const scaffold_template templates[] = {
    {"scene",    "lit 3D scene using the scene kit (default)", NULL},
    {"flat",     "2D fragment shader with noise and palettes", NULL},
    {"reactive", "live system stats as a visual dashboard",    NULL},
};
#define TEMPLATE_COUNT (sizeof(templates) / sizeof(templates[0]))

static const char *template_body(const char *name) {
    if (strcmp(name, "scene") == 0)    return tmpl_scene;
    if (strcmp(name, "flat") == 0)     return tmpl_flat;
    if (strcmp(name, "reactive") == 0) return tmpl_reactive;
    return NULL;
}

void neowall_scaffold_list_templates(FILE *out) {
    fprintf(out, "Templates:\n");
    for (size_t i = 0; i < TEMPLATE_COUNT; i++) {
        fprintf(out, "  %-10s %s\n", templates[i].name, templates[i].desc);
    }
}

/* Reject names that would escape the shader directory or produce a file the
 * config cannot reference. Path separators are the dangerous case. */
static bool name_is_sane(const char *name) {
    if (!name || !*name) return false;
    if (strchr(name, '/') || strchr(name, '\\')) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return true;
}

bool neowall_scaffold_create(const char *name, const char *template_name, bool force) {
    if (!name_is_sane(name)) {
        log_error("Invalid shader name '%s' (no path separators)", name ? name : "");
        return false;
    }

    const char *tmpl = template_name ? template_name : "scene";
    const char *body = template_body(tmpl);
    if (!body) {
        log_error("Unknown template '%s'", tmpl);
        neowall_scaffold_list_templates(stderr);
        return false;
    }

    /* Strip a trailing .glsl so `neowall new foo` and `neowall new foo.glsl`
     * both produce foo.glsl rather than foo.glsl.glsl. Note >= 5, not > 5: a
     * bare ".glsl" must reduce to an empty stem and be rejected below, not
     * sail through as a dotfile named ".glsl". */
    char stem[256];
    snprintf(stem, sizeof(stem), "%s", name);
    size_t len = strlen(stem);
    if (len >= 5 && strcmp(stem + len - 5, ".glsl") == 0) stem[len - 5] = '\0';
    if (stem[0] == '\0') {
        log_error("Invalid shader name '%s'", name);
        return false;
    }

    char dir[MAX_PATH_LENGTH];
    if (!expand_path("~/.config/neowall/shaders", dir, sizeof(dir))) {
        log_error("Could not resolve ~/.config/neowall/shaders");
        return false;
    }

    /* mkdir -p, one level at a time. EEXIST is success. */
    char partial[MAX_PATH_LENGTH];
    snprintf(partial, sizeof(partial), "%s", dir);
    for (char *s = partial + 1; *s; s++) {
        if (*s != '/') continue;
        *s = '\0';
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) {
            log_error("Could not create %s: %s", partial, strerror(errno));
            return false;
        }
        *s = '/';
    }
    if (mkdir(partial, 0755) != 0 && errno != EEXIST) {
        log_error("Could not create %s: %s", dir, strerror(errno));
        return false;
    }

    char path[MAX_PATH_LENGTH];
    int n = snprintf(path, sizeof(path), "%s/%s.glsl", dir, stem);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        log_error("Shader path too long");
        return false;
    }

    /* Never clobber someone's work by accident. */
    if (!force) {
        FILE *probe = fopen(path, "r");
        if (probe) {
            fclose(probe);
            log_error("%s already exists (use --force to overwrite)", path);
            return false;
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("Could not write %s: %s", path, strerror(errno));
        return false;
    }
    /* Both %s slots in every template are the shader stem. */
    if (fprintf(f, body, stem, stem) < 0) {
        log_error("Could not write %s: %s", path, strerror(errno));
        fclose(f);
        return false;
    }
    if (fclose(f) != 0) {
        log_error("Could not finish writing %s: %s", path, strerror(errno));
        return false;
    }

    printf("Created %s  (template: %s)\n\n", path, tmpl);
    printf("Try it now:\n");
    printf("  neowall watch %s.glsl        # live-reload while you edit\n", stem);
    printf("\nUse it as your wallpaper:\n");
    printf("  default {\n    shader %s.glsl\n  }\n", stem);
    printf("\nReference: docs/SHADER_NOTES.md\n");
    return true;
}
