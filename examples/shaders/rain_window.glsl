// rain_window.glsl — rain running down a dark glass pane.
// Interactive: rain intensity rises with network activity (iNetDown), lightning
// flashes on audio beats, and the glass fogs/clears with the mouse (wipe it!).
// Droplets refract a moody city-light gradient behind the glass. Single pass.

float dropLayer(vec2 uv, float t, float scale) {
    uv *= scale;
    vec2 id = floor(uv);
    vec2 f = fract(uv) - 0.5;
    float n = nwHash21(id);

    // Each cell has a drop that slides down over time.
    float speed = 0.3 + n * 0.7;
    float y = fract(n - t * speed);
    vec2 dropPos = vec2((n - 0.5) * 0.6, (y - 0.5) * 0.9);
    float d = length((f - dropPos) * vec2(1.4, 1.0));
    float drop = smoothstep(0.12, 0.0, d);

    // Trail behind the drop.
    float trail = smoothstep(0.06, 0.0, abs(f.x - dropPos.x)) *
                  smoothstep(0.0, 0.5, f.y - dropPos.y) *
                  smoothstep(0.5, 0.0, f.y - dropPos.y) * 0.4;
    return drop + trail;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float rain = 0.3 + pulse(iNetDown) * 1.5;   // network → downpour
    float t = iTime;

    // Background: blurred city lights / dusk gradient behind the glass.
    vec3 bg = mix(vec3(0.02, 0.03, 0.06), vec3(0.10, 0.08, 0.14), uv.y);
    // Bokeh light blobs.
    for (int i = 0; i < 6; i++) {
        vec2 seed = vec2(float(i) * 1.3, float(i) * 2.1);
        vec2 lp = (nwHash22(seed) - 0.5) * vec2(1.6, 1.0);
        lp.x += 0.05 * sin(t * 0.3 + float(i));
        float d = length((p - lp));
        vec3 tint = nwPalette(nwHash21(seed));
        bg += tint * 0.06 / (d * d + 0.02);
    }

    // Rain droplet refraction: accumulate a couple of layers for the normal.
    vec2 e = vec2(0.002, 0.0);
    float dc = dropLayer(uv, t, 6.0) * rain + dropLayer(uv * 1.3, t * 1.2, 9.0) * rain * 0.7;
    float dx = (dropLayer(uv + e.xy, t, 6.0) - dropLayer(uv - e.xy, t, 6.0)) * rain;
    float dy = (dropLayer(uv + e.yx, t, 6.0) - dropLayer(uv - e.yx, t, 6.0)) * rain;

    // Refract the background through the droplets.
    vec3 col = bg;
    vec3 refr = mix(vec3(0.02, 0.03, 0.06), vec3(0.10, 0.08, 0.14), uv.y + dy * 4.0);
    refr.x += dx * 0.5;
    col = mix(col, refr + vec3(0.3, 0.4, 0.5), clamp(dc, 0.0, 1.0));

    // Fog on the glass, wiped clear near the cursor.
    float fog = 0.25;
    if (iMouse.x > 1.0) {
        float m = distance(uv, iMouse.xy / iResolution.xy);
        fog *= smoothstep(0.0, 0.25, m);   // clearer near pointer
    }
    col = mix(col, vec3(0.25, 0.28, 0.32), fog * 0.5);

    // Lightning on the beat: a brief full-frame flash + brighten.
    float flash = beat();
    col += flash * vec3(0.5, 0.55, 0.7) * (0.4 + nwFbm(p * 3.0 + t, 3));

    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
