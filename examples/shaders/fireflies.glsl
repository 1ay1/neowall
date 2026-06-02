// fireflies.glsl — a swarm of fireflies that drift through the dark and are
// drawn toward your cursor. Interactive: move the mouse and the swarm gathers
// around it; on a music beat they all flare at once. Warm at night, cooler by
// day (timeOfDayTint). Single pass.

#define FLIES 48

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float aspect = iResolution.x / iResolution.y;

    vec2 mouse = (iMouse.x > 1.0)
        ? (iMouse.xy - 0.5 * iResolution.xy) / iResolution.y
        : vec2(0.0);
    bool hasMouse = iMouse.x > 1.0;

    // Dark base: deep night blue, slightly lifted at "daytime".
    vec3 bg = mix(vec3(0.01, 0.015, 0.03), vec3(0.06, 0.07, 0.10), dayNight() * 0.5);
    // Soft fbm fog so the dark isn't flat.
    bg += nwFbm(uv * 1.5 + iTime * 0.02, 4) * vec3(0.02, 0.02, 0.04);
    vec3 col = bg;

    float beatFlare = beat();

    for (int i = 0; i < FLIES; i++) {
        float fi = float(i);
        vec2 seed = vec2(fi * 0.137, fi * 0.911);

        // Each fly wanders on its own lissajous-ish path.
        float sp = 0.3 + nwHash21(seed) * 0.6;
        vec2 home = (nwHash22(seed) - 0.5) * vec2(aspect, 1.0) * 1.8;
        vec2 wander = vec2(
            sin(iTime * sp + fi),
            cos(iTime * sp * 0.8 + fi * 1.3)
        ) * 0.25;
        vec2 pos = home + wander;

        // Drift toward the cursor with a per-fly strength.
        if (hasMouse) {
            float pull = 0.3 + 0.5 * nwHash21(seed.yx);
            pos = mix(pos, mouse, pull * smoothstep(2.0, 0.0, distance(pos, mouse)));
        }

        // Glow.
        float d = distance(uv, pos);
        float blink = 0.5 + 0.5 * sin(iTime * (2.0 + sp * 4.0) + fi * 2.0);
        float bright = (0.5 + blink) * (0.4 + 0.8 * beatFlare);
        vec3 tint = mix(vec3(1.0, 0.8, 0.3), vec3(0.6, 1.0, 0.7), nwHash21(seed * 3.0));
        col += tint * bright * (0.004 / (d * d + 0.0008));
    }

    // A faint halo around the cursor itself.
    if (hasMouse) {
        col += vec3(0.2, 0.3, 0.4) * smoothstep(0.35, 0.0, distance(uv, mouse)) * 0.3;
    }

    col *= timeOfDayTint();
    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
