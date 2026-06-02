// starfield_warp.glsl — fly through stars; speed up by moving the mouse.
// Interactive: mouse motion (iMouseEnergy) and audio (iAudioLevel/beat) push
// the warp speed; the cursor position steers the direction of travel. Beats
// trigger a hyperspace streak. Single pass, pure procedural.

#define LAYERS 14

float star(vec2 p, float seed) {
    vec2 g = nwHash22(vec2(seed, seed * 1.7));
    p -= g;
    float d = length(p);
    return smoothstep(0.06, 0.0, d) + smoothstep(0.25, 0.0, d) * 0.3;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // Steer toward the cursor (or center if no pointer).
    vec2 center = (iMouse.x > 1.0)
        ? (iMouse.xy - 0.5 * iResolution.xy) / iResolution.y
        : vec2(0.0);
    uv -= center * 0.4;

    // Warp speed: base drift + mouse energy + audio.
    float speed = 0.15 + iMouseEnergy * 1.2 + iAudioLevel * 0.8 + beat() * 1.5;
    float t = iTime * speed;

    vec3 col = vec3(0.0);
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    // Radial star layers receding into the distance.
    for (int i = 0; i < LAYERS; i++) {
        float fi = float(i) / float(LAYERS);
        float depth = fract(fi + t * 0.3);
        float scale = mix(0.02, 1.4, depth);
        vec2 sp = vec2(a * 0.6, log(r + 0.02) + depth * 3.0);
        sp *= 8.0;
        vec2 cell = floor(sp);
        float s = star(fract(sp) - 0.5, cell.x + cell.y * 31.0);
        // Fade in from the center, out at the edge.
        float fade = smoothstep(0.0, 0.2, depth) * smoothstep(1.0, 0.7, depth);
        vec3 tint = nwPalette(fract(cell.x * 0.13 + 0.6));
        col += s * fade * mix(vec3(1.0), tint, 0.5) * (0.6 + iAudioTreble);
    }

    // Motion streaks toward the edges when going fast (hyperspace feel).
    float streak = pow(r, 1.5) * speed * 0.6;
    col += streak * nwPalette(0.6 + a * 0.1) * smoothstep(0.2, 1.2, r);

    // Subtle nebula backdrop.
    float neb = nwFbm(uv * 2.0 + t * 0.1, 4);
    col += neb * vec3(0.05, 0.04, 0.12);

    // Beat bloom from the center.
    col += beat() * exp(-r * 2.5) * vec3(0.7, 0.8, 1.0);

    fragColor = vec4(nwGamma(nwTonemap(col * 1.3)), 1.0);
}
