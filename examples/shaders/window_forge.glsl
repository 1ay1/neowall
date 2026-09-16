// window_forge.glsl — your desktop, rendered as molten glass over dark metal.
//
// Every window on your screen becomes a luminous pane. The desktop beneath is
// polished metal that MIRRORS them, ripples where their light lands, and is
// wired together by energy conduits running between the panes. Move a window
// and the reflection, the ripples and the wiring all follow it.
//
// This is a Hyprland-only effect today: it needs real window geometry, which
// only Hyprland's IPC exposes. Everywhere else iWindowCount is 0 and you get
// the idle forge — still animated, just with nothing to react to.
//
//   neowall watch window_forge.glsl

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Pixel -> compositor space (y down), matching how iWindows[] is reported.
vec2 toWin(vec2 frag) { return vec2(frag.x, iResolution.y - frag.y); }

// Rounded-rect signed distance, in pixels.
float paneSDF(vec2 p, vec4 w, float r) {
    vec2 c = w.xy + w.zw * 0.5;
    vec2 d = abs(p - c) - (w.zw * 0.5 - r);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

// A window's own colour, keyed off its position so neighbours differ.
vec3 paneHue(vec4 w) {
    float k = nwHash21(floor(w.xy / 64.0));
    return nwPalette(k, vec3(0.5), vec3(0.45), vec3(1.0),
                     vec3(0.00, 0.28, 0.55));
}

// Distance to the segment ab — used for the conduits between panes.
float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return length(pa - ba * h);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p  = toWin(fragCoord);
    vec2 uv = fragCoord / iResolution.xy;

    // --- the forge floor ---------------------------------------------------
    // Dark brushed metal: fine anisotropic streaks plus a slow heat shimmer.
    float grain  = nwFbm(vec2(p.x * 0.004, p.y * 0.05));
    float streak = nwFbm(vec2(p.x * 0.002, p.y * 0.18 + iTime * 0.02));
    vec3  col    = mix(vec3(0.014, 0.016, 0.021),
                       vec3(0.030, 0.034, 0.045), grain * 0.7 + streak * 0.3);

    // Faint machined grid, so motion has something to register against.
    vec2  g    = abs(fract(p / 96.0) - 0.5);
    float grid = smoothstep(0.49, 0.5, max(g.x, g.y));
    col += vec3(0.05, 0.07, 0.10) * grid * 0.35;

    // Embers drifting up from the metal, brighter when the machine works.
    float ember = nwFbm(vec2(p.x * 0.01, p.y * 0.01 + iTime * 0.25));
    col += vec3(0.30, 0.10, 0.02) * pow(ember, 6.0) * (0.3 + iCpu);

    // --- the panes ---------------------------------------------------------
    float nearest   = 1e6;   // distance to the closest pane edge
    vec3  spill     = vec3(0.0);
    vec3  mirror    = vec3(0.0);
    float rippleAcc = 0.0;

    // The reflection wobble is a property of the FLOOR, not of any one pane,
    // so evaluate the noise once here rather than once per window. With 16
    // panes that is the difference between 1 and 16 fbm calls per pixel, and
    // fbm is by far the most expensive thing in this shader.
    float wobBase = nwFbm(vec2(p.x * 0.01, iTime * 0.35));

    for (int i = 0; i < NW_MAX_WINDOWS; i++) {
        if (i >= iWindowCount) break;
        vec4  w   = iWindows[i];
        vec3  hue = paneHue(w);

        float d = paneSDF(p, w, 14.0);
        nearest = min(nearest, d);

        // Light spilling outward onto the metal. Two falloffs: a tight bright
        // rim and a wide soft pool, which is what reads as a real light source
        // rather than a flat halo.
        float rim  = exp(-max(d, 0.0) / 10.0);
        float pool = exp(-max(d, 0.0) / 190.0);
        spill += hue * (rim * 1.30 + pool * 0.42);

        // MIRROR: reflect the pane in the floor below its lower edge. Fold the
        // pixel back up across that edge; if it lands inside the pane, this
        // pixel is showing its reflection.
        float bottom = w.y + w.w;
        if (p.y > bottom) {
            float fold   = 2.0 * bottom - p.y;       // mirrored y
            float depth  = p.y - bottom;             // how far below
            // Wobble so the floor reads as liquid, not glass. Depth shifts the
            // shared noise rather than resampling it.
            float wob    = wobBase + depth * 0.0004;
            vec2  mp     = vec2(p.x + (fract(wob) - 0.5) * depth * 0.10, fold);
            float md     = paneSDF(mp, w, 14.0);
            float inside = smoothstep(2.0, -6.0, md);
            float fade   = exp(-depth / 260.0);      // reflections die with distance
            mirror += hue * inside * fade * 0.55;
        }

        // Concentric ripples radiating from each pane, as if its light were
        // disturbing the surface.
        float ring = sin(max(d, 0.0) * 0.055 - iTime * 2.2 + float(i));
        rippleAcc += ring * exp(-max(d, 0.0) / 300.0);
    }

    // --- conduits ----------------------------------------------------------
    // Wire the focused pane to every other one and run pulses along the wire.
    // Anchored on the focused window so this stays O(n) rather than O(n^2).
    if (iFocusedWindow.z > 0.0 && iWindowCount > 1) {
        vec2 hub = iFocusedWindow.xy + iFocusedWindow.zw * 0.5;
        for (int i = 0; i < NW_MAX_WINDOWS; i++) {
            if (i >= iWindowCount) break;
            vec4 w = iWindows[i];
            vec2 c = w.xy + w.zw * 0.5;
            if (distance(c, hub) < 4.0) continue;     // that's the hub itself

            float sd = segDist(p, hub, c);
            float wire = exp(-sd / 2.2) * 0.5;

            // A pulse travelling hub -> pane, its speed set by CPU load.
            float t  = clamp(dot(p - hub, c - hub) / max(dot(c - hub, c - hub), 1e-4), 0.0, 1.0);
            float ph = fract(t * 1.6 - iTime * (0.35 + 0.9 * iCpu) + float(i) * 0.19);
            float pulse = exp(-sd / 7.0) * pow(1.0 - ph, 14.0);

            col += vec3(0.30, 0.70, 1.00) * wire;
            col += vec3(0.65, 0.90, 1.00) * pulse * 1.5;
        }
    }

    // --- composite ---------------------------------------------------------
    col += mirror;
    col += spill * (0.85 + 0.55 * iAudioBeat);
    col += vec3(0.10, 0.16, 0.26) * rippleAcc * 0.16;

    // Under a pane nothing is visible, so spend no light there — but leave a
    // faint inner shimmer so a translucent window still shows something.
    float covered = smoothstep(1.0, -1.0, nearest);
    col = mix(col, col * 0.06 + vec3(0.015, 0.020, 0.032), covered);

    // A crowded desktop banks the forge down; a clear one lets it burn.
    col *= mix(1.0, 0.62, nwWinBusy());

    // Idle state: with no windows, keep a slow breathing glow at the centre so
    // the wallpaper never looks broken on unsupported compositors.
    if (iWindowCount == 0) {
        float r = length((uv - 0.5) * vec2(iResolution.x / iResolution.y, 1.0));
        col += vec3(0.35, 0.16, 0.05) * exp(-r * 2.6)
             * (0.35 + 0.28 * sin(iTime * 0.7)) * (0.5 + iAudioLevel);
    }

    // Vignette, then filmic tonemap so the bright rims roll off instead of
    // clipping to white.
    col *= 1.0 - 0.5 * dot(uv - 0.5, uv - 0.5) * 1.8;
    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
