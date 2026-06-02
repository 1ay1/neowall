// system_monitor.glsl — your machine's vitals as living abstract art.
// Interactive with what your COMPUTER does:
//   iCpu      → turbulent heat-haze; the screen "boils" under load
//   iRam      → a rising tide line across the bottom
//   iNetDown  → downward meteor pulses on the right
//   iNetUp    → upward spark pulses on the left
//   iBattery  → overall brightness (dims when low), red when discharging+low
//   iCpuCores → a row of core bars along the top
// No audio required. Great as an ambient "is my box busy?" wallpaper.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p  = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float cpu = iCpu;
    float load = pulse(cpu);

    // --- Background: cool when idle, hot turbulence when busy ---
    vec2 flow = nwCurl(uv * 3.0 + iTime * (0.05 + 0.4 * load)) * (0.1 + 0.6 * load);
    float heat = nwFbm(uv * 4.0 + flow + iTime * 0.1 * (1.0 + load), 5);
    vec3 cool = vec3(0.03, 0.05, 0.10);
    vec3 hot  = vec3(0.9, 0.35, 0.1);
    vec3 col = mix(cool, mix(cool, hot, heat), load);

    // --- RAM tide: a glowing waterline that rises with memory usage ---
    float tide = iRam * 0.6;
    float wave = tide + 0.02 * sin(uv.x * 18.0 + iTime * 1.5) * (0.5 + iRam);
    float water = smoothstep(wave + 0.004, wave - 0.004, uv.y);
    col = mix(col, mix(col, vec3(0.1, 0.4, 0.7), 0.6), water * 0.7);
    col += exp(-abs(uv.y - wave) * 60.0) * vec3(0.3, 0.7, 1.0) * (0.4 + iRam);

    // --- Network pulses: meteors down the right (down), sparks up the left (up) ---
    float dn = pulse(iNetDown);
    float up = pulse(iNetUp);
    {
        float lane = smoothstep(0.78, 0.82, uv.x);
        float trail = fract(uv.y * 3.0 + iTime * (1.0 + 4.0 * dn));
        float meteor = exp(-trail * 6.0) * lane * dn;
        col += meteor * vec3(0.5, 0.8, 1.0) * 2.0;
    }
    {
        float lane = smoothstep(0.18, 0.22, 1.0 - uv.x);
        float trail = fract(-uv.y * 3.0 + iTime * (1.0 + 4.0 * up));
        float spark = exp(-trail * 6.0) * lane * up;
        col += spark * vec3(1.0, 0.7, 0.3) * 2.0;
    }

    // --- CPU core bars across the very top ---
    if (uv.y > 0.94) {
        int cores = max(iCpuCoreCount, 1);
        float bx = uv.x * float(cores);
        int idx = int(bx);
        if (idx < cores && idx < 8) {
            float v = iCpuCores[idx];
            float fillY = (uv.y - 0.94) / 0.06;   // 0..1 within the strip
            float on = step(fillY, v);
            float cell = smoothstep(0.03, 0.08, fract(bx)) *
                         smoothstep(0.03, 0.08, 1.0 - fract(bx));
            col = mix(col, mix(vec3(0.1, 0.8, 0.3), vec3(1.0, 0.3, 0.2), v),
                      on * cell);
        }
    }

    // --- Battery: global brightness + low-battery red warning pulse ---
    float bright = mix(0.45, 1.0, iBattery);
    col *= bright;
    if (iCharging < 0.5 && iBattery < 0.2) {
        col = mix(col, vec3(0.6, 0.05, 0.05),
                  (0.5 + 0.5 * sin(iTime * 4.0)) * (0.2 - iBattery) * 3.0);
    }

    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
