// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: NeoWall contributors
// glass_orb.glsl - a mirrored orb in a slowly turning sky.
//
// Shows a Cube A pass: mainCubemap draws the sky once into a cube map, and
// the image pass reflects and refracts it. The cube is only redrawn when its
// own inputs change, so the costly sky is cheap to look up.

// Common
#define TAU 6.28318530718

vec3 skyColor(vec3 rd) {
    float y = rd.y;
    vec3 zen = vec3(0.10, 0.22, 0.48), hor = vec3(0.86, 0.62, 0.48), gnd = vec3(0.10, 0.08, 0.09);
    vec3 c = y > 0.0 ? mix(hor, zen, pow(y, 0.45)) : mix(hor * 0.6, gnd, pow(-y, 0.35));
    // a low sun and its glow
    vec3 sun = normalize(vec3(0.6, 0.12, 0.8));
    float s = max(dot(rd, sun), 0.0);
    c += vec3(1.0, 0.75, 0.45) * (pow(s, 600.0) * 6.0 + pow(s, 12.0) * 0.35);
    // thin bands of cloud
    float band = sin(rd.y * 40.0 + sin(atan(rd.z, rd.x) * 3.0) * 1.5);
    c = mix(c, c * 1.15 + 0.04, smoothstep(0.85, 1.0, band) * smoothstep(0.0, 0.3, y));
    return c;
}

// Cube A
void mainCubemap(out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir) {
    fragColor = vec4(skyColor(rayDir), 1.0);
}

// Image
vec2 sphere(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd), c = dot(ro, ro) - r * r, h = b * b - c;
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float a = iTime * 0.08 + (iMouse.z > 0.0 ? iMouse.x / iResolution.x * TAU : 0.0);
    vec3 ro = vec3(sin(a) * 3.2, 0.35, cos(a) * 3.2);
    vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0, 1, 0), fw)), up = cross(fw, rt);
    vec3 rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);

    vec3 col = texture(iChannel0, rd).rgb;
    vec2 t = sphere(ro, rd, 1.0);
    if (t.x > 0.0) {
        vec3 p = ro + rd * t.x, n = normalize(p);
        float fres = 0.04 + 0.96 * pow(1.0 - max(dot(-rd, n), 0.0), 5.0);
        vec3 refl = texture(iChannel0, reflect(rd, n)).rgb;
        vec3 refr = texture(iChannel0, refract(rd, n, 1.0 / 1.45)).rgb * vec3(0.85, 0.95, 1.0);
        col = mix(refr, refl, fres);
    }
    col = col / (1.0 + col);                 // tonemap
    col = pow(col, vec3(0.4545));            // gamma
    col *= 1.0 - 0.25 * dot(uv * 0.5, uv * 0.5);  // vignette
    fragColor = vec4(col, 1.0);
}
