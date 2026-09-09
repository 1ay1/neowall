// garden.glsl — a wallpaper that grows over days.
//
// Every plant runs a real multi-day life cycle keyed to the system calendar:
// it sprouts, climbs, buds, blooms, then withers and reseeds over 8-20 *days*.
// Because the phase comes from iDate (plus iTimeOfDay for smooth motion within
// a day), the garden survives reboots, config reloads and shader swaps with no
// state file — come back tomorrow and it has visibly moved on.
//
// It also reacts to the machine:
//   iCpu / iKeyEnergy / iMouseEnergy -> wind strength and gusts
//   iSun / iTimeOfDay               -> sky, sun & moon arc, petals open/close
//   iCpuTemp                        -> drought: grass browns, air hazes
//   iRam                            -> foliage density (leaves per stem)
//   iBattery / iCharging            -> light level dims when unplugged and low
//   iAudioBeat                      -> blooms pulse on the beat (optional)
//
// Two passes:
//   Buffer A : pollen motes (day) / fireflies (night) with self-feedback, so
//              they leave slow-fading trails that drift on the wind.
//   Image    : sky, soil, grass, plants, flowers; composites Buffer A on top.
//
// Sidecar garden.neowall binds the channels. No audio required.

// ======================= common (shared by both passes) =======================

const float GROUND_Y = 0.30;   // horizon / soil line, in 0..1 screen height
const int   NPLANTS  = 12;
const int   SEGS     = 5;      // stem polyline segments
const int   MOTES    = 14;

// A monotonically increasing day counter from the real date. Exact calendar
// arithmetic does not matter — only that it steps by 1 each midnight and never
// runs backwards within a season.
float dayIndex() {
    return floor(iDate.x * 365.25 + iDate.y * 30.6 + iDate.z);
}

// Continuous "days since epoch", smooth across midnight.
float dayTime() {
    return dayIndex() + clamp(iTimeOfDay, 0.0, 1.0);
}

// Global wind: a calm baseline that rises with CPU load, plus gusts when you
// actually touch the machine.
float windAmount() {
    return 0.18 + 0.55 * iCpu + 0.40 * iKeyEnergy + 0.28 * iMouseEnergy;
}

// Horizontal displacement of a stem at normalised height t.
float windOffset(float t, float seed) {
    float w = windAmount();
    float sway = sin(iTime * 0.9 + seed * 6.283 + t * 2.2);
    float turb = nwFbm(vec2(iTime * 0.22 + seed * 3.0, seed * 5.0)) - 0.5;
    return (sway * 0.045 + turb * 0.05) * w * t * t;
}

struct Plant {
    float x;       // base position (aspect-corrected x)
    float age;     // 0..1 through its life
    float height;
    float bloom;   // 0..1 flower development
    float hue;
    float lean;
    float seed;
};

Plant getPlant(int i, float fieldW) {
    float fi = float(i);
    float s1 = nwHash11(fi * 1.37 + 0.11);
    float s2 = nwHash11(fi * 2.71 + 3.14);
    float s3 = nwHash11(fi * 4.19 + 7.77);
    float s4 = nwHash11(fi * 6.53 + 1.23);

    Plant p;
    p.seed = s1;
    p.x    = ((fi + 0.5) / float(NPLANTS) + (s1 - 0.5) * 0.055) * fieldW;

    // Life length in days, staggered so the bed is never uniform.
    float life = mix(8.0, 20.0, s2);
    p.age = fract((dayTime() + s3 * life * 4.0) / life);

    float grow = smoothstep(0.02, 0.45, p.age);
    float wilt = smoothstep(0.82, 1.00, p.age);

    p.height = mix(0.14, 0.46, s4) * grow * (1.0 - 0.55 * wilt);
    p.bloom  = smoothstep(0.40, 0.62, p.age) * (1.0 - smoothstep(0.78, 0.97, p.age));
    p.hue    = fract(s3 * 0.77 + s1 * 0.31);
    p.lean   = (s2 - 0.5) * 0.18;
    return p;
}

// Point on a stem at normalised height t (0 = soil, 1 = flower head).
vec2 stemPoint(Plant p, float t) {
    float x = p.x + p.lean * t * t * p.height + windOffset(t, p.seed);
    return vec2(x, GROUND_Y + p.height * t);
}

// How bright the world is: real sun, dimmed when the battery is low and the
// machine is unplugged (the desk lamp is running out).
float lightLevel() {
    float low = 1.0 - smoothstep(0.10, 0.45, iBattery);
    float dim = mix(1.0, 0.55, low * (1.0 - iCharging));
    return mix(0.16, 1.0, pulse(iSun)) * dim;
}

// ================================= Buffer A ==================================
// Pollen and fireflies with self-feedback. Trails linger far longer at night.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  uv     = fragCoord / iResolution.xy;
    float fieldW = iResolution.x / iResolution.y;
    vec2  p      = vec2(uv.x * fieldW, uv.y);

    float night = 1.0 - dayNight();

    vec3 prev = texture(iChannel0, uv).rgb;
    prev *= mix(0.870, 0.958, night);

    float w = windAmount();
    vec3  add = vec3(0.0);

    for (int i = 0; i < MOTES; i++) {
        float fi = float(i);
        float sp = mix(0.35, 1.0, nwHash11(fi * 3.11 + 0.7));
        float ph = nwHash11(fi * 5.71 + 2.3);

        // Drift right on the wind, wrapping across the field.
        float fx = fract(ph + iTime * (0.010 + 0.035 * sp) * (0.4 + w));
        float cy = GROUND_Y + 0.04 + nwHash11(fi * 7.33 + 4.1) * 0.58
                 + sin(iTime * 0.5 * sp + fi * 2.1) * 0.035;

        vec2 c = vec2(fx * fieldW, cy);

        // Fade near the wrap seam so trails never snap.
        float edge = smoothstep(0.0, 0.06, fx) * smoothstep(1.0, 0.94, fx);

        float d  = length(p - c);
        float r  = mix(0.006, 0.011, nwHash11(fi * 9.13));
        float g  = exp(-(d * d) / (2.0 * r * r));

        // Fireflies blink; daytime pollen just glints.
        float blink = mix(1.0, 0.5 + 0.5 * sin(iTime * 2.3 * sp + fi * 1.7), night);

        vec3 fireCol   = vec3(1.00, 0.86, 0.32);
        vec3 pollenCol = vec3(1.00, 0.97, 0.80);
        vec3 col = mix(pollenCol * 0.55, fireCol, night);

        add += col * g * edge * blink * mix(0.35, 1.0, night);
    }

    fragColor = vec4(max(prev, add), 1.0);
}

// ================================== Image ====================================

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  uv     = fragCoord / iResolution.xy;
    float fieldW = iResolution.x / iResolution.y;
    vec2  p      = vec2(uv.x * fieldW, uv.y);

    float day    = dayNight();
    float night  = 1.0 - day;
    float light  = lightLevel();
    float heat   = clamp(iCpuTemp, 0.0, 1.0);      // drought
    float lush   = clamp(iRam, 0.0, 1.0);          // foliage density

    // ------------------------------- sky --------------------------------
    float sy = clamp((uv.y - GROUND_Y) / max(1.0 - GROUND_Y, 0.001), 0.0, 1.0);

    vec3 zenithDay  = vec3(0.16, 0.42, 0.78);
    vec3 horizonDay = vec3(0.72, 0.82, 0.90);
    vec3 zenithNite = vec3(0.02, 0.03, 0.09);
    vec3 horizNite  = vec3(0.09, 0.08, 0.18);

    vec3 sky = mix(mix(horizNite, zenithNite, sy),
                   mix(horizonDay, zenithDay, sy), day);

    // Dawn / dusk warmth: strongest when the sun is near the horizon.
    float golden = pulse(1.0 - abs(iSun - 0.22) * 4.0);
    sky = mix(sky, vec3(0.95, 0.52, 0.28), golden * 0.45 * (1.0 - sy * 0.7));
    sky *= mix(1.0, 1.06, heat);   // hazy white sky when the box runs hot

    // stars
    if (night > 0.05) {
        vec2 sc = floor(p * 90.0);
        float st = nwHash21(sc);
        float twinkle = 0.5 + 0.5 * sin(iTime * 1.7 + st * 40.0);
        float star = smoothstep(0.9965, 1.0, st) * twinkle;
        sky += vec3(0.85, 0.88, 1.0) * star * night * sy;
    }

    // ------------------------- sun / moon on an arc ----------------------
    float arc  = clamp((iTimeOfDay - 0.25) * 2.0, -0.2, 1.2);   // 06:00 -> 18:00
    vec2  body = vec2(arc * fieldW, GROUND_Y + sin(arc * NW_PI) * 0.60);
    float bd   = length(p - body);

    vec3 sunCol  = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.96, 0.82), pulse(iSun));
    sky += sunCol * smoothstep(0.055, 0.030, bd) * day;
    sky += sunCol * exp(-bd * 6.0) * 0.28 * day;

    vec2  moon = vec2(fract(arc + 0.5) * fieldW, GROUND_Y + sin(fract(arc + 0.5) * NW_PI) * 0.55);
    float md   = length(p - moon);
    // crescent: subtract an offset disc
    float disc = smoothstep(0.042, 0.036, md);
    float bite = smoothstep(0.040, 0.034, length(p - moon - vec2(0.016, 0.008)));
    sky += vec3(0.92, 0.93, 1.0) * max(disc - bite * 0.85, 0.0) * night;
    sky += vec3(0.55, 0.60, 0.85) * exp(-md * 9.0) * 0.16 * night;

    vec3 col = sky;

    // ------------------------------ soil ---------------------------------
    if (uv.y < GROUND_Y + 0.004) {
        float depth = (GROUND_Y - uv.y) / GROUND_Y;
        float grain = nwFbm(p * vec2(9.0, 22.0)) * 0.14;
        vec3 earth  = mix(vec3(0.20, 0.13, 0.09), vec3(0.07, 0.05, 0.04), depth);
        earth += grain * (1.0 - depth);
        earth = mix(earth, earth * vec3(1.10, 0.98, 0.82), heat);   // dry, dusty
        col = earth * mix(0.35, 1.0, light);
    }

    // ------------------------------ grass --------------------------------
    {
        float bladeIdx = floor(p.x * 220.0);
        float bh   = nwHash11(bladeIdx * 0.37);
        float top  = GROUND_Y + 0.012 + bh * 0.038 * (0.7 + 0.5 * lush);
        float lean = windOffset(1.0, bh) * 0.55;
        float bx   = (fract(p.x * 220.0) - 0.5) / 220.0;
        float t    = clamp((p.y - GROUND_Y) / max(top - GROUND_Y, 0.001), 0.0, 1.0);
        float dx   = abs(bx - lean * t * t);
        float wdt  = 0.0022 * (1.0 - t) + 0.0008;
        float mask = smoothstep(wdt, wdt * 0.4, dx) * step(p.y, top) * step(GROUND_Y - 0.01, p.y);

        vec3 green = mix(vec3(0.16, 0.34, 0.12), vec3(0.30, 0.52, 0.18), bh);
        green = mix(green, vec3(0.48, 0.42, 0.16), heat * 0.75);       // browns off
        col = mix(col, green * mix(0.30, 1.0, light), mask);
    }

    // ----------------------------- plants --------------------------------
    for (int i = 0; i < NPLANTS; i++) {
        Plant pl = getPlant(i, fieldW);
        if (pl.height < 0.005) continue;
        if (abs(p.x - pl.x) > 0.30) continue;     // cheap horizontal cull

        // stem
        float d = 1e9;
        vec2 prev = stemPoint(pl, 0.0);
        for (int k = 1; k <= SEGS; k++) {
            vec2 cur = stemPoint(pl, float(k) / float(SEGS));
            d = min(d, sdSegment(p, prev, cur));
            prev = cur;
        }
        vec2 head = prev;

        float wilt   = smoothstep(0.82, 1.0, pl.age);
        float thick  = 0.0016 + 0.0022 * smoothstep(0.0, 0.5, pl.age);
        float stemM  = smoothstep(thick, thick * 0.35, d);

        vec3 stemCol = mix(vec3(0.20, 0.44, 0.16), vec3(0.40, 0.34, 0.12), max(wilt, heat * 0.5));
        col = mix(col, stemCol * mix(0.25, 1.0, light), stemM);

        // leaves — more of them when there is memory pressure (lush growth)
        int leaves = 2 + int(floor(lush * 2.0 + 0.5));
        for (int L = 0; L < 4; L++) {
            if (L >= leaves) break;
            float lt = 0.28 + float(L) * 0.17;
            if (lt > 0.92) break;
            vec2  lp   = stemPoint(pl, lt);
            float side = (mod(float(L), 2.0) < 0.5) ? 1.0 : -1.0;
            float lsz  = (0.030 + 0.016 * nwHash11(pl.seed * 13.0 + float(L))) * pl.height * 2.2;

            vec2 q = p - lp;
            q = nwRot(side * (0.55 + 0.25 * sin(iTime * 0.8 + float(L)))) * q;
            q.x -= side * lsz * 0.55;
            q.x *= 0.55;                                    // squash into a blade
            float ld = length(q) - lsz * 0.5;
            float lm = smoothstep(0.0015, -0.0015, ld) * smoothstep(0.05, 0.30, pl.age);

            vec3 leafCol = mix(vec3(0.18, 0.40, 0.14), vec3(0.42, 0.36, 0.13), max(wilt, heat * 0.6));
            col = mix(col, leafCol * mix(0.25, 1.0, light), lm);
        }

        // flower — opens with the sun, closes at night, pulses on the beat
        if (pl.bloom > 0.01) {
            float open = pl.bloom * mix(0.30, 1.0, pulse(iSun));
            float rad  = 0.032 * open * (1.0 + 0.12 * iAudioBeat);

            vec2  q = p - head;
            float a = atan(q.y, q.x);
            float r = length(q);

            float petals = 5.0 + floor(nwHash11(pl.seed * 21.0) * 3.0);
            float shape  = rad * (0.55 + 0.45 * abs(cos(petals * 0.5 * a)));
            float pm     = smoothstep(shape, shape - 0.0035, r);

            vec3 petal  = nwHsv2rgb(vec3(pl.hue, mix(0.75, 0.45, heat), 1.0));
            petal = mix(petal, vec3(0.55, 0.45, 0.25), wilt * 0.8);
            vec3 centre = vec3(1.0, 0.85, 0.35);

            float cm = smoothstep(rad * 0.30, rad * 0.16, r);
            vec3  fc = mix(petal, centre, cm);

            col = mix(col, fc * mix(0.35, 1.0, light), pm);

            // soft bloom halo at night so the garden stays readable
            col += fc * exp(-r * 55.0) * 0.16 * open * night;
        }
    }

    // ------------------- pollen / fireflies (the mote buffer) ------------
    // NOTE: do not write the literal pass name here -- multipass_parse.c scans
    // forward from a comment to EOF when detecting pass markers, so mentioning
    // it below this point would misclassify this pass.
    vec3 motes = texture(iChannel1, uv).rgb;
    col += motes * mix(0.55, 1.15, night);

    // ------------------------------ finish -------------------------------
    // heat haze near the ground when the CPU is cooking
    col *= 1.0 - 0.12 * heat * (1.0 - smoothstep(GROUND_Y, GROUND_Y + 0.25, uv.y));

    // vignette
    vec2 vg = uv - 0.5;
    col *= 1.0 - dot(vg, vg) * 0.45;

    col = nwTonemap(col * 1.15);
    fragColor = vec4(col, 1.0);
}
