// audio_pulse.glsl — neowall reactive showcase
// An audio-reactive neon tunnel that breathes with your music and warms with
// the time of day. Demonstrates the neowall std-lib (no #include needed):
//   audioBand(), spectrum(), beat(), nwFbm(), nwPalette(), nwTonemap(), pulse(),
//   dayNight(), timeOfDayTint(), plus the reactive uniforms iAudioBass/iSun/iCpu.
//
// Pair it with audio_pulse.neowall (sidecar manifest) for explicit bindings.
//
// Drop into ~/.config/neowall/shaders/ and point your config at it:
//   default { shader audio_pulse.glsl }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // Live audio energy (works even if the manifest isn't used).
    float bass   = max(iAudioBass,   audioBand(0.0, 0.12));
    float mid    = max(iAudioMid,    audioBand(0.12, 0.5));
    float treble = max(iAudioTreble, audioBand(0.5, 1.0));
    float pump   = 0.6 + 0.8 * bass + 0.4 * beat();

    // Polar tunnel coordinates; bass widens the throat, treble adds shimmer.
    float r = length(uv) * (1.0 - 0.25 * bass);
    float a = atan(uv.y, uv.x);

    // Animated rings driven by time + audio.
    float depth = 0.35 / (r + 0.06);
    float t = iTime * (0.4 + 0.6 * mid) + depth;

    // Spectrum ring: sample the FFT around the circle.
    float specA = spectrum(fract(a / NW_TAU + 0.5));
    float rings = 0.5 + 0.5 * sin(t * 6.2831 + specA * 8.0);

    // Flowing nebula via curl-noise warped fbm.
    vec2 flow = nwCurl(uv * 2.0 + iTime * 0.05) * 0.3;
    float neb = nwFbm(uv * 3.0 + flow + iTime * 0.1, 5);

    // Color: neon palette modulated by audio + ring phase.
    float hue = depth * 0.5 + iTime * 0.03 + treble * 0.4;
    vec3 col = nwPalette(hue);
    col *= rings * pump;
    col += neb * vec3(0.15, 0.25, 0.5) * (0.4 + treble);

    // Beat flash + radial vignette.
    col += beat() * 0.4 * vec3(1.0, 0.7, 0.9) * smoothstep(0.7, 0.0, r);
    col *= smoothstep(1.3, 0.1, r);

    // Time-of-day warmth and CPU "stress" reddening (subtle).
    col *= timeOfDayTint();
    col = mix(col, col * vec3(1.3, 0.7, 0.6), pulse(iCpu) * 0.25);

    // Tone-map + gamma for a clean, punchy result.
    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
