// system_deck.glsl — a "command deck" HUD that reads your machine's vitals like
// a real dashboard: radial CPU + RAM gauges, a scrolling CPU history graph,
// per-core meters, network up/down traces, a battery cell, and a live clock —
// on a dark neon grid with scanlines.
//
// Two passes:
//   Buffer A : a 1px-tall scrolling ring buffer of CPU/NET history (R=cpu,
//              G=netDown, B=netUp). Each frame it shifts left and writes the
//              newest sample at the right edge (self-feedback).
//   Image    : draws the HUD, sampling Buffer A for the history graph.
//
// Sidecar system_deck.neowall binds the channels. No audio required.

// ============================ Buffer A ============================
// A rolling history strip. We only use the top row (uv.y near 1.0) but the
// whole buffer ping-pongs; sampling uses x as "time".
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy;

    // Shift the existing history one pixel to the left by reading from the
    // right of our own previous frame. RGBA = cpu, netDown, netUp, diskIO.
    vec4 prev = texture(iChannel0, uv + vec2(px.x, 0.0));

    // At the right edge, write the newest live sample.
    if (uv.x > 1.0 - px.x) {
        prev = vec4(iCpu, iNetDown, iNetUp, max(iDiskRead, iDiskWrite));
    }

    if (iFrame < 2) prev = vec4(0.0);
    fragColor = prev;
}

// ============================== Image =============================

// --- tiny 5x3 bitmap digit renderer (0-9) for the clock/percent readouts ---
// Returns 1.0 inside a lit pixel of digit d at local cell uv in [0,1]^2.
float digit(int d, vec2 p) {
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 0.0;
    ivec2 g = ivec2(floor(p.x * 3.0), floor((1.0 - p.y) * 5.0));
    if (g.x < 0 || g.x > 2 || g.y < 0 || g.y > 4) return 0.0;
    // Each digit is 5 rows of 3 bits, packed MSB-first per row into 15 bits.
    int rows[10];
    rows[0]=0x7B6F; rows[1]=0x2492; rows[2]=0x73E7; rows[3]=0x73CF;
    rows[4]=0x5BC9; rows[5]=0x79CF; rows[6]=0x79EF; rows[7]=0x7249;
    rows[8]=0x7BEF; rows[9]=0x7BCF;
    int bit = 14 - (g.y * 3 + g.x);
    return float((rows[d] >> bit) & 1);
}

// Draw a right-aligned integer; returns lit mask. `cell` = size of one digit.
float number(int value, vec2 p, vec2 cell, int maxDigits) {
    float m = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i >= maxDigits) break;
        int dv = value;
        for (int k = 0; k < i; k++) dv /= 10;
        int d = dv - (dv / 10) * 10;
        vec2 lp = vec2((p.x - float(i) * cell.x * 1.3) / cell.x, p.y / cell.y);
        m = max(m, digit(d, vec2(1.0 - lp.x, lp.y)));
        if (value < 1 && i > 0 && dv == 0) {} // keep leading zeros off-screen naturally
    }
    return m;
}

float ring(vec2 p, float r, float w) {
    return smoothstep(w, 0.0, abs(length(p) - r));
}

// radial gauge: filled arc from -135deg..+135deg proportional to v (0..1)
float gauge(vec2 p, float v) {
    float r = length(p);
    float a = atan(p.x, -p.y);          // 0 at top, +/- pi around
    float span = 2.35;                  // ~135 deg each side
    float t = clamp((a + span) / (2.0 * span), 0.0, 1.0);
    float arc = smoothstep(0.02, 0.0, abs(r - 0.34)) * step(abs(a), span);
    float fill = arc * step(t, v);
    return fill;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = fragCoord / res;
    float aspect = res.x / res.y;
    vec2 P = (fragCoord - 0.5 * res) / res.y;   // centered, y in [-0.5,0.5]

    // ---- background: dark with a faint neon grid + vignette ----
    vec3 col = vec3(0.015, 0.02, 0.03);
    vec2 grid = abs(fract(uv * vec2(40.0 * aspect, 40.0)) - 0.5);
    float gl = smoothstep(0.48, 0.5, max(grid.x, grid.y));
    col += gl * vec3(0.02, 0.06, 0.08);
    col += nwFbm(uv * 3.0 + iTime * 0.02, 4) * vec3(0.01, 0.015, 0.03);

    vec3 cyan = vec3(0.2, 0.9, 1.0);
    vec3 amber = vec3(1.0, 0.7, 0.2);
    vec3 green = vec3(0.3, 1.0, 0.5);
    vec3 red = vec3(1.0, 0.3, 0.25);

    // ============ gauges: CPU (left), GPU (center-low), RAM (right) ============
    // Each gauge has a thin temperature bar beneath it (CPU/GPU only).
    {
        // CPU
        vec2 c = P - vec2(-0.62 * aspect * 0.6, 0.12);
        vec3 gcol = mix(green, red, pulse(iCpu));
        col += gauge(c, iCpu) * gcol * 1.6;
        col += ring(c, 0.34, 0.004) * cyan * 0.4;
        col += ring(c, 0.30, 0.002) * cyan * 0.2;
        int pct = int(iCpu * 100.0 + 0.5);
        col += number(pct, (c + vec2(0.07, -0.03)) / vec2(0.05, 0.08), vec2(1.0), 3) * gcol * 1.5;
        // CPU temp bar under the gauge
        if (iCpuTempC > 1.0) {
            vec2 bp = c - vec2(0.0, 0.40);
            float bar = step(abs(bp.y), 0.012) * step(abs(bp.x), 0.18);
            float fill = bar * step((bp.x + 0.18) / 0.36, iCpuTemp);
            col += bar * cyan * 0.15;
            col += fill * mix(green, red, iCpuTemp) * 1.0;
        }
    }
    {
        // GPU (slightly lower-center so the three form a triangle)
        vec2 c = P - vec2(0.0, -0.18);
        vec3 gcol = mix(vec3(0.4, 0.6, 1.0), red, pulse(iGpu));
        col += gauge(c, iGpu) * gcol * 1.6;
        col += ring(c, 0.34, 0.004) * vec3(0.4, 0.6, 1.0) * 0.4;
        col += ring(c, 0.30, 0.002) * vec3(0.4, 0.6, 1.0) * 0.2;
        int pct = int(iGpu * 100.0 + 0.5);
        col += number(pct, (c + vec2(0.07, -0.03)) / vec2(0.05, 0.08), vec2(1.0), 3) * gcol * 1.5;
        if (iGpuTempC > 1.0) {
            vec2 bp = c - vec2(0.0, 0.40);
            float bar = step(abs(bp.y), 0.012) * step(abs(bp.x), 0.18);
            float fill = bar * step((bp.x + 0.18) / 0.36, iGpuTemp);
            col += bar * vec3(0.4, 0.6, 1.0) * 0.15;
            col += fill * mix(vec3(0.4, 0.6, 1.0), red, iGpuTemp) * 1.0;
        }
    }
    {
        // RAM
        vec2 c = P - vec2(0.62 * aspect * 0.6, 0.12);
        vec3 gcol = mix(cyan, amber, iRam);
        col += gauge(c, iRam) * gcol * 1.6;
        col += ring(c, 0.34, 0.004) * cyan * 0.4;
        int pct = int(iRam * 100.0 + 0.5);
        col += number(pct, (c + vec2(0.07, -0.03)) / vec2(0.05, 0.08), vec2(1.0), 3) * gcol * 1.5;
        // swap bar under RAM
        vec2 bp = c - vec2(0.0, 0.40);
        float bar = step(abs(bp.y), 0.012) * step(abs(bp.x), 0.18);
        float fill = bar * step((bp.x + 0.18) / 0.36, iSwap);
        col += bar * amber * 0.15;
        col += fill * amber * 0.9;
    }

    // ============ CPU history graph (center top) ============
    {
        // band across the top middle
        if (uv.y > 0.62 && uv.y < 0.92 && uv.x > 0.2 && uv.x < 0.8) {
            float gx = (uv.x - 0.2) / 0.6;          // 0..1 across the band
            float baseY = 0.62, h = 0.30;
            vec4 hist = texture(iChannel1, vec2(gx, 0.5));
            float cpuLine  = baseY + hist.r * h;
            float dnLine   = baseY + hist.g * h;
            float upLine   = baseY + hist.b * h;
            float diskLine = baseY + hist.a * h;
            // filled CPU area + crisp line
            float area = smoothstep(cpuLine, cpuLine - 0.005, uv.y) *
                         step(baseY, uv.y);
            col += area * mix(green, red, hist.r) * 0.25;
            col += smoothstep(0.006, 0.0, abs(uv.y - cpuLine)) * mix(green, red, hist.r) * 1.5;
            // network traces
            col += smoothstep(0.004, 0.0, abs(uv.y - dnLine)) * cyan * 1.2;
            col += smoothstep(0.004, 0.0, abs(uv.y - upLine)) * amber * 1.2;
            // disk I/O trace (magenta)
            col += smoothstep(0.004, 0.0, abs(uv.y - diskLine)) * vec3(0.9, 0.3, 1.0) * 1.2;
            // frame
            col += smoothstep(0.004, 0.0, abs(uv.y - 0.92)) * cyan * 0.3;
            col += smoothstep(0.004, 0.0, abs(uv.y - 0.62)) * cyan * 0.3;
        }
    }

    // ============ per-core meters (bottom band) ============
    {
        int cores = max(iCpuCoreCount, 1);
        if (uv.y < 0.34 && uv.y > 0.06 && uv.x > 0.12 && uv.x < 0.88) {
            float bx = (uv.x - 0.12) / 0.76 * float(cores);
            int idx = int(bx);
            if (idx >= 0 && idx < cores && idx < 8) {
                float v = iCpuCores[idx];
                float fy = (uv.y - 0.06) / 0.28;        // 0..1 in the band
                float lit = step(fy, v);
                float cell = smoothstep(0.06, 0.18, fract(bx)) *
                             smoothstep(0.06, 0.18, 1.0 - fract(bx));
                vec3 bc = mix(green, red, v);
                col += lit * cell * bc * 1.3;
                // segment ticks
                float seg = step(0.45, fract(fy * 12.0));
                col *= mix(1.0, 0.55, lit * cell * seg);
            }
        }
    }

    // ============ network up/down arrows (left + right edges) ============
    {
        float dn = pulse(iNetDown), up = pulse(iNetUp);
        float lane = smoothstep(0.06, 0.05, abs(uv.x - 0.06));
        float trail = fract(uv.y * 4.0 + iTime * (1.0 + 6.0 * dn));
        col += lane * exp(-trail * 6.0) * dn * cyan * 2.0;
        float lane2 = smoothstep(0.06, 0.05, abs(uv.x - 0.94));
        float trail2 = fract(-uv.y * 4.0 + iTime * (1.0 + 6.0 * up));
        col += lane2 * exp(-trail2 * 6.0) * up * amber * 2.0;
    }

    // ============ battery cell (bottom-right corner) ============
    {
        vec2 bp = uv - vec2(0.86, 0.05);
        vec2 hb = vec2(0.05, 0.018);
        float body = step(abs(bp.x), hb.x) * step(abs(bp.y), hb.y);
        float outline = body * (1.0 - step(abs(bp.x), hb.x - 0.003) * step(abs(bp.y), hb.y - 0.003));
        float fill = body * step((bp.x + hb.x) / (2.0 * hb.x), iBattery);
        vec3 bcol = iBattery < 0.2 ? red : (iCharging > 0.5 ? green : cyan);
        col += outline * cyan * 0.8;
        col += fill * bcol * 1.1;
        // charging bolt blink
        if (iCharging > 0.5) col += body * (0.3 + 0.3 * sin(iTime * 6.0)) * green * 0.3;
    }

    // ============ clock (top-right): HH MM from iTimeOfDay ============
    {
        float secs = iTimeOfDay * 86400.0;
        int hh = int(secs / 3600.0);
        int mm = int(mod(secs / 60.0, 60.0));
        vec2 base = vec2(0.80, 0.93);
        vec2 cell = vec2(0.018, 0.03);
        // hours (2 digits) then minutes (2 digits)
        vec2 hp = (uv - base) / cell;
        col += number(hh, hp + vec2(2.6, 0.0), vec2(1.0), 2) * cyan * 1.4;
        col += number(mm, (uv - base - vec2(0.085, 0.0)) / cell + vec2(2.6, 0.0), vec2(1.0), 2) * cyan * 1.4;
        // blinking colon
        col += step(0.5, fract(iTime)) * smoothstep(0.01, 0.0,
               length(uv - base - vec2(0.072, 0.012))) * cyan;
        col += step(0.5, fract(iTime)) * smoothstep(0.01, 0.0,
               length(uv - base - vec2(0.072, 0.030))) * cyan;
    }

    // ============ load / uptime / processes (top-left readouts) ============
    {
        vec2 cell = vec2(0.012, 0.02);
        // load average x100 (e.g. 1.85 -> 185), top-left
        int loadx = int(iLoadRaw * 100.0 + 0.5);
        col += number(loadx, (uv - vec2(0.10, 0.93)) / cell + vec2(2.6, 0.0), vec2(1.0), 3)
               * mix(green, red, iLoad) * 1.3;
        // uptime in hours, just below
        int uph = int(iUptimeHours);
        col += number(uph, (uv - vec2(0.10, 0.89)) / cell + vec2(3.6, 0.0), vec2(1.0), 4) * cyan * 1.1;
        // process count, below that
        col += number(iProcCount, (uv - vec2(0.10, 0.85)) / cell + vec2(3.6, 0.0), vec2(1.0), 4)
               * amber * 1.1;
    }

    // ---- CRT scanlines + flicker + vignette ----
    col *= 0.85 + 0.15 * sin(fragCoord.y * 2.2);
    col *= 0.97 + 0.03 * sin(iTime * 40.0);
    float vig = smoothstep(1.2, 0.3, length(P));
    col *= vig;

    fragColor = vec4(nwGamma(nwTonemap(col * 1.2)), 1.0);
}
