// window_light.glsl — the wallpaper knows where your windows are.
//
// Light leaks out from behind every window and pools on the desktop. Move a
// window and the glow follows it; close everything and the wallpaper goes
// quiet and dark.
//
// This uses geometry the compositor reports, which today means Hyprland. On
// anything else iWindowCount is 0 and you get the plain animated background —
// the shader stays correct, it just has nothing to react to.
//
//   neowall watch window_light.glsl

vec3 background(vec2 uv, float t) {
    // Slow drifting haze so an empty desktop is not a flat colour.
    float n = nwFbm(uv * 2.0 + vec2(t * 0.02, t * 0.013));
    vec3 deep = mix(vec3(0.015, 0.018, 0.030), vec3(0.03, 0.05, 0.08), n);
    return deep * mix(0.6, 1.0, nwDayNight());
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p  = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    vec3 col = background(p, iTime);

    // --- the part that watches your desktop ---------------------------------

    // Distance in pixels to the nearest window edge.
    float d = nwWinNearest(fragCoord);

    // Wide ambient pool: a soft bloom that fills the gaps between windows.
    float pool = nwWinGlow(fragCoord, 260.0);

    // Tight rim: a bright line hugging each window border.
    float rim = exp(-max(d, 0.0) / 14.0);

    // Colour the leak by time of day — cool at night, warm by day — and let
    // the audio beat breathe through it.
    vec3 warm = mix(vec3(0.25, 0.45, 1.00), vec3(1.00, 0.72, 0.35), nwDayNight());
    warm *= 0.9 + 0.5 * iAudioBeat;

    col += warm * pool * 0.55;
    col += warm * rim  * 0.40;

    // The focused window gets its own stronger halo, so you can see at a
    // glance which one has your attention.
    if (iFocusedWindow.z > 0.0) {
        float fd = nwWinDist(fragCoord, iFocusedWindow);
        col += vec3(1.0, 0.85, 0.55) * exp(-max(fd, 0.0) / 90.0) * 0.5;
    }

    // Under a window nothing is visible anyway, so spend no brightness there.
    col *= 1.0 - 0.85 * nwWinCovered(fragCoord);

    // A busy desktop dims the wallpaper; a clear one lets it breathe.
    col *= mix(1.0, 0.55, nwWinBusy());

    // Gentle vignette, then tonemap.
    col *= 1.0 - 0.45 * dot(uv - 0.5, uv - 0.5) * 2.0;
    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
