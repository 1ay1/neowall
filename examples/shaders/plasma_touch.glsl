// plasma_touch.glsl — classic plasma you can push around with the mouse.
// Interactive: the cursor warps the plasma field (a gravity well that bends the
// flow), mouse motion energy speeds it up, and audio bass swells the colors.
// Click (iMouse.zw) detonates a bright pulse. Single pass, runs anywhere.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float t = iTime * (0.4 + iMouseEnergy * 1.5);

    // Cursor as a "gravity well" that bends the domain.
    vec2 warp = vec2(0.0);
    if (iMouse.x > 1.0) {
        vec2 m = (iMouse.xy - 0.5 * iResolution.xy) / iResolution.y;
        vec2 d = uv - m;
        float r = length(d) + 0.05;
        warp = -normalize(d) * 0.15 / (r * r + 0.3);
    }
    vec2 q = uv + warp;

    // Layered sinusoidal plasma + curl-noise turbulence.
    float v = 0.0;
    v += sin(q.x * 4.0 + t);
    v += sin((q.y * 4.0 + t) * 1.2);
    v += sin((q.x + q.y) * 3.0 + t * 0.7);
    vec2 cn = nwCurl(q * 1.5 + t * 0.1);
    v += sin(length(q + cn * 0.3) * 8.0 - t * 1.5);
    v *= 0.25;

    // Color via palette; bass swells the brightness and shifts the hue.
    float hue = 0.5 + v * 0.5 + iTime * 0.03 + iAudioBass * 0.3;
    vec3 col = nwPalette(hue);
    col *= 0.6 + 0.6 * abs(v) + iAudioBass * 0.8;

    // Click detonation: bright expanding ring from the click point.
    if (iMouse.z > 1.0) {
        vec2 c = (iMouse.zw - 0.5 * iResolution.xy) / iResolution.y;
        float d = distance(uv, c);
        col += smoothstep(0.05, 0.0, d) * vec3(1.0, 0.9, 0.7) * 2.0;
    }

    // Cursor glow.
    if (iMouse.x > 1.0) {
        vec2 m = (iMouse.xy - 0.5 * iResolution.xy) / iResolution.y;
        col += smoothstep(0.08, 0.0, distance(uv, m)) * vec3(0.4, 0.5, 0.8);
    }

    // Beat ripple across the whole field.
    col += beat() * 0.2 * nwPalette(hue + 0.3);

    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
