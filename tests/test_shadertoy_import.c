/* Shadertoy JSON export -> neowall shader. GL-free. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/log.h"
#include "neowall/shader/shader_source.h"
#include "neowall/shader/shadertoy_import.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static nw_import import(const char *json, bool want_ok) {
    nw_import im = {0};
    char err[256] = "";
    nw_result r = nw_shadertoy_import(json, strlen(json), &im, err, sizeof(err));
    CHECK(nw_is_ok(r) == want_ok, "import %s: %s", want_ok ? "should work" : "should fail", err);
    if (!want_ok) CHECK(err[0], "a failure says why");
    return im;
}

/* the API's format: {"Shader": {info, renderpass[{inputs[{channel, ctype, src, sampler}], code, name}]}} */
static const char api[] =
    "{\"Shader\":{\"ver\":\"0.1\",\"info\":{\"id\":\"abc123\",\"name\":\"Wave \\\"Pool\\\" \\u00e9t\\u00e9\","
    "\"username\":\"someone\"},\"renderpass\":["
    "{\"inputs\":[],\"outputs\":[],\"code\":\"float wave(float x){ return sin(x); }\",\"name\":\"Common\",\"type\":\"common\"},"
    "{\"inputs\":[{\"id\":257,\"src\":\"/media/previz/buffer00.png\",\"ctype\":\"buffer\",\"channel\":0,"
    "\"sampler\":{\"filter\":\"linear\",\"wrap\":\"clamp\"}},"
    "{\"id\":17,\"src\":\"/media/a/0c7bf5fe9462d5bffbd11126e82908e39be3ce56220d900f633d58fb432e56f5.png\","
    "\"ctype\":\"texture\",\"channel\":1}],"
    "\"outputs\":[{\"id\":257,\"channel\":0}],"
    "\"code\":\"void mainImage(out vec4 c, in vec2 f){\\n  vec4 p = texture(iChannel0, f/iResolution.xy);\\n"
    "  c = p + vec4(wave(iTime)) * texture(iChannel1, f/256.).r;\\n}\",\"name\":\"Buffer A\",\"type\":\"buffer\"},"
    "{\"inputs\":[{\"src\":\"/media/previz/buffer00.png\",\"ctype\":\"buffer\",\"channel\":0},"
    "{\"src\":\"/media/a/585f9546c092f53ded45332b343144396c0b2d70d9965f585ebc172080d8aa58.jpg\",\"ctype\":\"cubemap\",\"channel\":2},"
    "{\"src\":\"/media/a/whatever.mp3\",\"ctype\":\"music\",\"channel\":3}],"
    "\"outputs\":[],\"code\":\"void mainImage(out vec4 c, in vec2 f){ c = texture(iChannel0, f/iResolution.xy)"
    " + texture(iChannel2, vec3(1)) * texture(iChannel3, vec2(0.1, 0.25)).r; }\",\"name\":\"Image\",\"type\":\"image\"},"
    "{\"inputs\":[],\"outputs\":[],\"code\":\"vec2 mainSound(int s, float t){ return vec2(0); }\",\"name\":\"Sound\",\"type\":\"sound\"}"
    "]}}";

/* the site's format: [ {info, renderpass[{inputs[{id, filepath, type, channel}], ...}]} ] */
static const char site[] =
    "[{\"ver\":\"0.1\",\"info\":{\"id\":\"xyz\",\"name\":\"Cubed\"},\"renderpass\":["
    "{\"inputs\":[{\"id\":\"4dX3Rr\",\"filepath\":\"/media/previz/cubemap00.png\",\"type\":\"cubemap\",\"channel\":0}],"
    "\"outputs\":[],\"code\":\"void mainImage(out vec4 c, in vec2 f){ c = texture(iChannel0, normalize(vec3(f, 1.0))); }\","
    "\"name\":\"Image\",\"type\":\"image\"},"
    "{\"inputs\":[],\"outputs\":[],\"code\":\"void mainCubemap(out vec4 c, in vec2 f, in vec3 ro, in vec3 rd){ c = vec4(rd, 1); }\","
    "\"name\":\"Cube A\",\"type\":\"cubemap\"}"
    "]}]";

/* a buffer that reads itself */
static const char feedback[] =
    "{\"Shader\":{\"info\":{\"name\":\"fb\"},\"renderpass\":["
    "{\"inputs\":[{\"src\":\"/media/previz/buffer00.png\",\"ctype\":\"buffer\",\"channel\":0}],"
    "\"code\":\"void mainImage(out vec4 c, in vec2 f){ c = texelFetch(iChannel0, ivec2(f), 0) * 0.99; }\",\"name\":\"Buffer A\"},"
    "{\"inputs\":[{\"src\":\"/media/previz/buffer00.png\",\"ctype\":\"buffer\",\"channel\":0}],"
    "\"code\":\"void mainImage(out vec4 c, in vec2 f){ c = texelFetch(iChannel0, ivec2(f), 0); }\",\"name\":\"Image\"}]}}";

static nw_shader_source *parse(const nw_import *im) {
    nw_shader_source *s = NULL;
    nw_result r = nw_shader_source_parse(im->glsl, NULL, im->manifest, &s);
    CHECK(nw_is_ok(r), "the imported shader parses: %s\n%s\n--\n%s", r.context ? r.context : "", im->glsl,
          im->manifest ? im->manifest : "(no manifest)");
    return s;
}

int main(void) {
    log_quiet_errors(true);

    nw_import im = import(api, true);
    CHECK(im.name && !strcmp(im.name, "Wave \"Pool\" \xc3\xa9t\xc3\xa9"), "title with escapes: %s", im.name ? im.name : "");
    CHECK(strstr(im.glsl, "shadertoy.com/view/abc123"), "links the original");
    CHECK(strstr(im.glsl, "// Common\nfloat wave"), "common first");
    char *a = strstr(im.glsl, "// Buffer A"), *img = strstr(im.glsl, "// Image");
    CHECK(a && img && a < img, "buffer before image");
    CHECK(!strstr(im.glsl, "mainSound"), "sound pass left out");
    CHECK(im.manifest && strstr(im.manifest, "bufferA {\n  ch0 self\n  ch1 noise\n}"), "buffer A bindings:\n%s",
          im.manifest ? im.manifest : "");
    CHECK(im.manifest && strstr(im.manifest, "image {\n  ch0 bufferA\n  ch2 sky\n  ch3 audio\n}"), "image bindings:\n%s",
          im.manifest ? im.manifest : "");
    CHECK(im.notes && strstr(im.notes, "texture") && strstr(im.notes, "cube map") && strstr(im.notes, "music") &&
              strstr(im.notes, "Sound"),
          "notes what was swapped:\n%s", im.notes ? im.notes : "");
    nw_shader_source *s = parse(&im);
    if (s) {
        int pa = nw_shader_source_find_pass(s, "Buffer A");
        CHECK(s->passes.len == 2 && pa >= 0, "two passes");
        CHECK(pa >= 0 && s->passes.data[pa].ch[0].kind == NW_IN_PASS && s->passes.data[pa].ch[0].pass == pa, "feedback");
        CHECK(s->passes.data[s->image_pass].ch[2].kind == NW_IN_SKY, "image ch2 is the sky");
        CHECK(s->passes.data[s->image_pass].ch[3].kind == NW_IN_AUDIO, "image ch3 is audio");
        nw_shader_source_free(s);
    }
    nw_import_free(&im);

    im = import(site, true);
    CHECK(im.manifest && strstr(im.manifest, "image {\n  ch0 cube\n}"), "site format: cube binding:\n%s",
          im.manifest ? im.manifest : "");
    s = parse(&im);
    if (s) {
        CHECK(s->passes.len == 2 && s->passes.data[s->image_pass].ch[0].kind == NW_IN_CUBE, "image reads Cube A");
        nw_shader_source_free(s);
    }
    nw_import_free(&im);

    im = import(feedback, true);
    s = parse(&im);
    if (s) nw_shader_source_free(s);
    nw_import_free(&im);

    /* bad input fails cleanly */
    nw_import bad;
    bad = import("", false);
    bad = import("{", false);
    bad = import("{\"Shader\":{}}", false);
    bad = import("{\"Shader\":{\"renderpass\":[{\"name\":\"Buffer A\",\"code\":\"x\"}]}}", false);
    bad = import("{\"a\":\"\\uD800\"} x", false);
    bad = import("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"
                 "]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
                 false);
    (void)bad;

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
