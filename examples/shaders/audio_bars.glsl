// audio_bars.glsl — a glowing spectrum analyzer that reacts to system audio.
// Interactive: play music and the bars dance; bass slams the floor glow,
// treble lights the tips, beats flash the whole field. Uses iAudio (FFT),
// the std-lib palette + tonemap. No manifest needed (iChannel0 unused).
//
//   default { shader audio_bars.glsl }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Number of bars across the screen.
    const float BARS = 64.0;
    float bx = floor(uv.x * BARS) / BARS;
    float cellX = fract(uv.x * BARS);

    // Sample the spectrum for this bar (log-ish: low freqs get more space).
    float f = pow(bx, 1.4);
    float h = spectrum(f);
    h = pow(h, 0.8) * (0.85 + 0.5 * iAudioLevel);

    // Bar body + soft edges between bars.
    float bar = smoothstep(h, h - 0.02, uv.y);
    float gap = smoothstep(0.04, 0.12, cellX) * smoothstep(0.04, 0.12, 1.0 - cellX);
    float intensity = bar * gap;

    // Color: low bars red/orange, high bars cyan/violet — palette over freq.
    vec3 col = nwPalette(0.6 + f * 0.5 + iTime * 0.02);

    // Glow at the tip of each bar.
    float tip = exp(-abs(uv.y - h) * 40.0) * gap;
    col += tip * (0.6 + iAudioTreble) * vec3(1.0, 0.9, 1.0);

    // Floor glow rising with bass.
    float floorGlow = exp(-uv.y * 6.0) * (0.3 + iAudioBass * 1.5);
    col += floorGlow * vec3(1.0, 0.3, 0.5);

    // Reflection below (mirror, dimmer).
    float ry = -uv.y;
    float rh = spectrum(f);
    float refl = smoothstep(rh * 0.4, rh * 0.4 - 0.02, ry) * gap * 0.25;

    vec3 outc = col * intensity + col * refl;

    // Beat flash.
    outc += beat() * 0.25 * nwPalette(f);

    // Idle hint when nothing is playing.
    if (iAudioActive < 0.5) {
        float idle = 0.04 + 0.03 * sin(uv.x * 30.0 + iTime * 2.0);
        outc += idle * smoothstep(0.5, 0.0, abs(uv.y - 0.5));
    }

    fragColor = vec4(nwGamma(nwTonemap(outc * 1.4)), 1.0);
}
