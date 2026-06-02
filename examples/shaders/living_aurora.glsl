// living_aurora.glsl — neowall reactive showcase (no audio needed)
// A northern-lights sky that:
//   - darkens to night / brightens toward noon via iSun
//   - ripples faster when the network is busy (iNetDown)
//   - sparks embers when you type fast (iKeyEnergy)
//   - dims the stars when the battery is low (iBattery)
// Uses the neowall std-lib (nwFbm, nwPalette, nwHash21, pulse, dayNight()).

float aurora(vec2 uv, float t) {
    float v = 0.0;
    for (float i = 1.0; i <= 4.0; i++) {
        float band = nwFbm(vec2(uv.x * (1.5 + i) + t * (0.2 + 0.1 * i),
                                uv.y * 0.6 + i), 5);
        float curtain = smoothstep(0.0, 1.0, band) *
                        exp(-abs(uv.y - (0.1 * i - 0.2) - band * 0.4) * 6.0);
        v += curtain / i;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float day = dayNight();                 // 0 night, 1 noon
    float netRush = pulse(iNetDown);        // network activity
    float t = iTime * (0.3 + 0.7 * netRush);

    // Sky gradient: deep night blue -> warm daylight, driven by the sun.
    vec3 night = vec3(0.02, 0.03, 0.08);
    vec3 dusk  = vec3(0.18, 0.10, 0.30);
    vec3 daysky = vec3(0.45, 0.62, 0.85);
    vec3 sky = mix(mix(night, dusk, smoothstep(0.0, 0.4, day)),
                   daysky, smoothstep(0.4, 1.0, day));
    sky *= mix(0.7, 1.0, uv.y);

    // Stars (only at night), dimmed by low battery.
    float starField = step(0.997, nwHash21(floor(fragCoord * 0.5)));
    float twinkle = 0.5 + 0.5 * sin(iTime * 3.0 + nwHash21(floor(fragCoord)) * 30.0);
    float stars = starField * twinkle * (1.0 - day) * mix(0.2, 1.0, iBattery);
    sky += stars;

    // The aurora curtains, colored by an IQ palette and shaped by time of day.
    vec2 auv = vec2(p.x, p.y * 0.9 + 0.15);
    float a = aurora(auv, t);
    vec3 auroraCol = nwPalette(0.35 + auv.x * 0.15 + iTime * 0.02,
                               vec3(0.3, 0.5, 0.4), vec3(0.3, 0.4, 0.3),
                               vec3(1.0, 1.0, 1.0), vec3(0.0, 0.33, 0.55));
    sky += auroraCol * a * (1.2 - 0.8 * day);   // aurora fades in daylight

    // Typing embers: little rising sparks when iKeyEnergy is high.
    float embers = 0.0;
    if (iKeyEnergy > 0.05) {
        vec2 ep = uv * vec2(40.0, 20.0);
        ep.y -= iTime * 2.0;
        vec2 cell = floor(ep);
        float h = nwHash21(cell);
        float spark = smoothstep(0.96, 1.0, h) * fract(h * 13.0);
        embers = spark * iKeyEnergy * smoothstep(0.0, 0.3, uv.y);
    }
    sky += embers * vec3(1.0, 0.6, 0.2);

    fragColor = vec4(nwGamma(nwTonemap(sky)), 1.0);
}
