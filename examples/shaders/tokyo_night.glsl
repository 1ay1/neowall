// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: NeoWall contributors
// tokyo_night.glsl — NEO-TOKYO: a rainy cyberpunk night in the Tokyo Night palette.
// The default wallpaper.
//
//   Clouds    clouds + two fog layers at quarter resolution (soft by nature)
//   Buffer A  the city: sky, lightning, shooting stars, searchlights, synthwave
//             moon, blimp, Tokyo Tower, three parallax skyline layers with neon
//             glyph signs and holographic billboards, elevated train, flying
//             cars (and the odd cop). Pixel (0,0) keeps the smoothed mouse.
//   Buffer B  the water: A reflected, rippled, smeared like a wet street,
//             with rain rings on the surface
//   Buffer C  bloom, horizontal (bright pass), quarter resolution
//   Buffer D  bloom, vertical, quarter resolution; alpha = anamorphic streak
//   Halo      the wide glow, eighth resolution
//   Image     composite: bloom + halo + streak, rain lit by the neon it falls
//             past, chromatic fringe, filmic grade, grain, vignette
//
// Alive: follows the real clock (dusk glow in the evening, the city switching
// off after midnight, dawn early morning), parallaxes with the mouse, pulses to
// music, lights the gaps between your windows and calms when the screen fills.
// ~1.2 ms of GPU per frame at 3440x1440 on a mid-range GPU (`neowall render
// tokyo_night --bench`).
//
// Edit live:  neowall watch tokyo_night

// ═══════════════════════════════ COMMON ═══════════════════════════════════

const vec3 CRUST  = vec3(0.059, 0.059, 0.078);   // #0f0f14
const vec3 BASE   = vec3(0.102, 0.106, 0.149);   // #1a1b26
const vec3 STORM  = vec3(0.141, 0.157, 0.231);   // #24283b
const vec3 SURF   = vec3(0.161, 0.180, 0.259);   // #292e42
const vec3 BLUE   = vec3(0.478, 0.635, 0.969);   // #7aa2f7
const vec3 CYAN   = vec3(0.490, 0.812, 1.000);   // #7dcfff
const vec3 MAG    = vec3(0.733, 0.604, 0.969);   // #bb9af7
const vec3 PURPLE = vec3(0.616, 0.486, 0.847);   // #9d7cd8
const vec3 RED    = vec3(0.969, 0.463, 0.557);   // #f7768e
const vec3 ORANGE = vec3(1.000, 0.620, 0.392);   // #ff9e64
const vec3 YELLOW = vec3(0.878, 0.686, 0.408);   // #e0af68
const vec3 TEAL   = vec3(0.451, 0.855, 0.792);   // #73daca
const vec3 FG     = vec3(0.753, 0.792, 0.961);   // #c0caf5

const float HORIZON = 0.30;

float PX;              // ~1.5 px in screen units
float BEAT;            // audio beat 0..1
float DUSK, LATE, DAWN; // time-of-day weights

float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void timeOfDay() {
    float hr = iTimeOfDay * 24.0;
    DUSK = smoothstep(16.5, 18.5, hr) * (1.0 - smoothstep(20.5, 22.5, hr));
    LATE = smoothstep(0.5, 2.5, hr)   * (1.0 - smoothstep(4.5, 6.0, hr));
    DAWN = smoothstep(4.5, 5.8, hr)   * (1.0 - smoothstep(7.0, 8.5, hr));
}

vec3 neonColor(float h) {
    if (h < 0.22) return BLUE;
    if (h < 0.44) return MAG;
    if (h < 0.62) return CYAN;
    if (h < 0.80) return RED;
    return TEAL;
}

// Procedural "kanji": up to 5 strokes on a 3×3 node grid. uv in 0..1.
float glyph(vec2 uv, float seed) {
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.0;
    float d = 1.0;
    for (int k = 0; k < 5; k++) {
        float fk = float(k);
        float h1 = nwHash11(seed * 13.7 + fk * 3.1);
        float h2 = nwHash11(seed * 7.3  + fk * 5.9);
        vec2  a  = vec2(floor(h1 * 3.0), floor(h2 * 3.0));
        vec2  b  = min(a + (fract(h1 * 7.0) > 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0)), vec2(2.0));
        d = min(d, nwSdSegment(uv, 0.18 + a * 0.32, 0.18 + b * 0.32));
    }
    return 1.0 - smoothstep(0.05, 0.10, d);
}

float beam(vec2 p, vec2 o, float ang) {
    vec2  d = vec2(sin(ang), cos(ang));
    vec2  v = p - o;
    float along = dot(v, d);
    if (along < 0.0) return 0.0;
    float perp  = abs(v.x * d.y - v.y * d.x);
    float w     = 0.006 + along * 0.07;
    return exp(-(perp * perp) / (w * w)) * exp(-along * 1.4);
}

// ── Tokyo Tower ───────────────────────────────────────────────────────────
void tower(vec2 p, vec2 base, float H, inout vec3 col) {
    vec2 q = p - base;
    float gd = length(vec2(q.x * 3.0, q.y - H * 0.45));
    col += ORANGE * 0.04 * exp(-gd * 6.0);
    if (q.y < 0.0 || q.y > H * 1.02 || abs(q.x) > 0.11) return;

    float t  = q.y / H;
    float hw = 0.003 + 0.075 * pow(max(1.0 - t / 0.82, 0.0), 2.0);
    float u  = q.x / max(hw, 1e-4);
    float inside = smoothstep(1.0 + PX / hw, 1.0 - PX / hw, abs(u));
    float arch = step(t, 0.10) * smoothstep(0.55, 0.50, abs(u) * (1.0 - t * 4.0) + t * 3.0);
    inside *= 1.0 - arch;

    if (inside > 0.0) {
        vec3  c  = mix(CRUST, SURF, 0.4);
        float rows = 46.0;
        float l1 = abs(fract(t * rows + u * 0.9) - 0.5);
        float l2 = abs(fract(t * rows - u * 0.9) - 0.5);
        float lat  = max(smoothstep(0.12, 0.0, l1 - 0.36), smoothstep(0.12, 0.0, l2 - 0.36));
        float edge = smoothstep(0.80, 0.98, abs(u));
        vec3  lc   = mix(ORANGE, RED, smoothstep(0.2, 0.8, t));
        float shimmer = 0.8 + 0.2 * sin(iTime * 1.5 - t * 12.0);
        // a light pulse climbs the tower every few seconds
        float climb = exp(-abs(t - fract(iTime * 0.12)) * 30.0);
        c += lc * (lat * 0.95 + edge * 1.1) * shimmer + mix(ORANGE, FG, 0.5) * climb * (lat + edge) * 1.2;
        col = mix(col, c, inside);
    }
    for (int k = 0; k < 2; k++) {
        float dy  = (k == 0) ? 0.45 : 0.72;
        float dh  = (k == 0) ? 0.030 : 0.020;
        float dw  = (0.003 + 0.075 * pow(max(1.0 - dy / 0.82, 0.0), 2.0)) * 1.5 + 0.004;
        float dd  = nwSdBox(vec2(q.x, q.y - dy * H), vec2(dw, dh * H * 0.5));
        float win = step(0.5, fract(q.x / 0.004)) * step(abs(q.y - dy * H), dh * H * 0.22);
        vec3  deck = mix(SURF, CRUST, 0.3) + CYAN * win * 1.2 + YELLOW * (1.0 - win) * 0.15;
        col = mix(col, deck, smoothstep(PX, -PX, dd));
        col += CYAN * 0.15 * exp(-max(dd, 0.0) * 300.0);
    }
    vec2  bp = vec2(0.0, H * 1.01);
    float bd = length(q - bp);
    col += RED * smoothstep(0.0, 0.2, sin(iTime * 2.0)) * (smoothstep(0.005, 0.0, bd) * 2.0 + 0.4 * exp(-bd * 70.0));
}

// ── skyline layer ─────────────────────────────────────────────────────────
void skyline(vec2 p, float scale, float speed, float hMin, float hMax,
             vec3 body, float detail, float seed, bool hero, inout vec3 col) {
    float y   = p.y - HORIZON;
    // nothing of this layer exists above its tallest tower (+ tier, mast, glow)
    if (y > hMax + 0.12 || y < -0.002) return;
    float x   = p.x * scale + iTime * speed + seed * 17.0;
    float id  = floor(x);
    float fx  = fract(x);

    float r   = nwHash11(id + seed * 31.7);
    float h   = mix(hMin, hMax, r * r);
    float w0  = 0.06 + 0.14 * nwHash11(id * 1.7 + seed);
    float w1  = 0.94 - 0.14 * nwHash11(id * 2.3 + seed);
    float tierOn = step(0.55, nwHash11(id * 3.1 + seed));
    float tw0 = mix(w0, w1, 0.22), tw1 = mix(w0, w1, 0.78);
    float inTier = step(tw0, fx) * step(fx, tw1);
    float top = h + tierOn * inTier * (0.035 + 0.05 * nwHash11(id * 4.7 + seed));

    float inX   = step(w0, fx) * step(fx, w1);
    float cover = inX * smoothstep(top + PX, top - PX, y) * step(-0.001, y);
    float lx    = (fx - 0.5 * (w0 + w1)) / scale;
    float bw    = (w1 - w0) / scale;
    float hs    = nwHash11(id * 5.3 + seed);

    if (cover > 0.0) {
        vec3 c = body;
        c += PURPLE * 0.10 * (1.0 - smoothstep(0.0, 0.25, y)) * (1.0 - detail * 0.5);
        c += STORM * 0.25 * detail * step(0.92, fract((fx - w0) * scale * 24.0));
        // rim light from the moon side
        c += MAG * 0.10 * detail * smoothstep(0.8, 1.0, (fx - w0) / max(w1 - w0, 1e-3));
        if (detail > 0.0) {
            float u    = (fx - w0) / max(w1 - w0, 1e-3);
            float rows = 20.0 * scale;
            float cols = max(2.0, floor(bw * rows / 1.15));
            vec2  cell = vec2(floor(u * cols), floor(y * rows));
            vec2  f    = vec2(fract(u * cols), fract(y * rows));
            float win  = step(0.24, f.x) * step(f.x, 0.76) * step(0.22, f.y) * step(f.y, 0.80);
            float wh   = nwHash21(cell + vec2(id * 7.13, seed * 3.7));
            // fewer lights on late at night
            float lit  = step(0.66 + 0.22 * LATE, fract(wh + floor(iTime * 0.04 + wh * 9.0) * 0.618));
            float margin = step(0.02, y) * step(y, top - 0.015);
            vec3  wc   = wh > 0.86 ? CYAN : (wh > 0.74 ? MAG : YELLOW);
            // a few windows flicker like a TV is on
            float tv   = step(0.97, wh) * (0.6 + 0.4 * sin(iTime * 9.0 + wh * 70.0) * sin(iTime * 3.1));
            c = mix(c, wc * (0.6 + 0.5 * wh + tv), win * lit * margin * detail * 0.9);
        }
        c += mix(BLUE, MAG, hs) * 0.7 * detail * smoothstep(2.0 * PX, 0.0, abs(y - top)) * inX;
        col = mix(col, c, cover);
    }
    if (detail <= 0.0) return;

    // vertical neon sign with glyph text
    if (hs > 0.5) {
        float side = (nwHash11(id * 8.9 + seed) - 0.5) * bw * 0.5;
        vec2  sc   = vec2(side, h * (0.45 + 0.22 * nwHash11(id * 9.7 + seed)));
        vec2  hb   = vec2(0.0055 + 0.0085 * detail, h * 0.17);
        vec2  s    = vec2(lx, y) - sc;
        float d    = nwSdBox(s, hb);
        vec3  nc   = neonColor(nwHash11(id * 11.3 + seed));
        float on   = (0.85 + 0.15 * sin(iTime * 2.0 + hs * 40.0)) * (1.0 + 0.7 * BEAT);
        if (hs > 0.9) on *= step(0.25, fract(sin(floor(iTime * 9.0) + hs * 91.0) * 43758.5));
        if (d < 0.0) {
            float cellH = hb.x * 2.2;
            float ci    = floor((s.y + hb.y) / cellH);
            vec2  gu    = vec2((s.x + hb.x) / (2.0 * hb.x), fract((s.y + hb.y) / cellH));
            gu = (gu - 0.5) * 1.25 + 0.5;
            float g = glyph(vec2(gu.x, 1.0 - gu.y), ci + id * 3.0 + seed);
            col = mix(col, CRUST * 1.4, 0.85);
            col += nc * on * (g * 1.8 + 0.10);
        }
        col += nc * on * (smoothstep(PX * 2.0, 0.0, abs(d)) * 1.4 + exp(-max(d, 0.0) * 140.0) * 0.20);
    }

    // holographic billboard
    if (hero && hs > 0.18 && hs < 0.46 && bw > 0.09) {
        vec2  bc = vec2(0.0, h * 0.70);
        vec2  bh = vec2(bw * 0.34, min(h * 0.11, bw * 0.22));
        vec2  s  = vec2(lx, y) - bc;
        float d  = nwSdBox(s, bh);
        float t  = iTime * 0.5 + hs * 10.0;
        float flick = 0.85 + 0.15 * step(0.1, fract(sin(floor(iTime * 13.0) + hs * 50.0) * 9871.3));
        vec3  ac = mix(MAG, CYAN, 0.5 + 0.5 * sin(t * 0.7));
        if (d < 0.0) {
            vec2  uv = (s + bh) / (2.0 * bh);
            vec3  panel = mix(STORM, CRUST, uv.y) * 0.8;
            float row  = floor(uv.y * 2.0);
            float gx   = uv.x * 6.0 + t * (row > 0.5 ? 1.2 : -0.9);
            float g    = glyph(vec2(fract(gx), fract(uv.y * 2.0)) * 1.25 - 0.125, floor(gx) + row * 31.0 + hs * 7.0);
            vec3  gc   = mix(ac, row > 0.5 ? RED : TEAL, 0.35);
            panel += gc * g * 1.6;
            panel += ac * 0.30 * exp(-abs(uv.x - fract(t * 0.25)) * 18.0);
            panel *= 0.82 + 0.18 * step(0.5, fract(uv.y * 40.0));
            col = mix(col, panel * flick * (1.0 + 0.6 * BEAT), 0.95);
        }
        col += ac * flick * (smoothstep(PX * 2.5, 0.0, abs(d)) * 1.3 + exp(-max(d, 0.0) * 60.0) * 0.15);
    }

    if (r > 0.78) {
        vec2  bp    = vec2(0.0, top + 0.012);
        float mast  = smoothstep(PX, 0.0, abs(lx)) * step(top, y) * step(y, bp.y);
        float blink = smoothstep(0.0, 0.1, sin(iTime * 2.4 + hs * 30.0));
        float bd    = length(vec2(lx, y) - bp);
        col  = mix(col, SURF, mast * 0.8);
        col += RED * blink * (smoothstep(0.004, 0.0, bd) * 2.0 + 0.25 * exp(-bd * 90.0));
    }
}

// ── flying cars ───────────────────────────────────────────────────────────
void traffic(vec2 p, float ar, float lane0, float lanes, float size, float bright, bool cops, inout vec3 col) {
    float margin = cops ? 0.03 : 0.012;              // trail height / cop glow reach
    if (p.y < HORIZON + lane0 - margin || p.y > HORIZON + lane0 + lanes + margin) return;
    for (int i = 0; i < 6; i++) {
        float fi  = float(i);
        float h   = nwHash11(fi * 4.13 + lane0 * 17.0);
        float dir = h > 0.5 ? 1.0 : -1.0;
        float y   = HORIZON + lane0 + lanes * fract(h * 7.31) + 0.004 * sin(iTime * 1.3 + fi * 2.0);
        float sp  = 0.05 + 0.08 * fract(h * 13.7);
        float span = ar + 0.8;
        float x   = mod(h * span + iTime * sp * dir, span) - 0.4;
        vec2  d   = p - vec2(x, y);
        float back  = -d.x * dir;
        float trail = step(0.0, back) * exp(-back * 9.0) * exp(-abs(d.y) / (size * 0.35));
        float head  = exp(-dot(d, d) / (size * size));
        vec3  lc    = dir > 0.0 ? RED : mix(CYAN, FG, 0.5);
        if (cops && i == 0) {                     // the cop car: red/blue light bar
            float ph = step(0.5, fract(iTime * 3.0));
            lc = mix(RED, BLUE, ph) * 1.4;
            col += lc * exp(-dot(d, d) / (size * size * 60.0)) * 0.25;
        }
        col += lc * bright * (trail * 0.6 + head * 2.2);
    }
}

// ── blimp with a video screen ─────────────────────────────────────────────
void blimp(vec2 p, float ar, inout vec3 col) {
    float span = ar + 0.6;
    vec2  c  = vec2(mod(iTime * 0.010, span) - 0.3, 0.80 + 0.008 * sin(iTime * 0.21));
    vec2  q  = p - c;
    if (abs(q.x) > 0.13 || abs(q.y) > 0.05) return;
    float body = length(q / vec2(0.085, 0.020)) - 1.0;
    float fin  = nwSdBox(q - vec2(-0.082, 0.0), vec2(0.010, 0.016 - abs(q.x + 0.082) * 0.4));
    float d    = min(body * 0.02, fin);
    vec3  bc   = mix(CRUST * 1.5, STORM, smoothstep(-0.02, 0.02, q.y));
    bc += MAG * 0.18 * smoothstep(0.012, 0.02, q.y) * step(body, 0.0);     // moonlit top
    col = mix(col, bc, smoothstep(PX, -PX, d));
    // side screen
    vec2  sq = q - vec2(0.008, -0.002);
    float sd = nwSdBox(sq, vec2(0.040, 0.0075));
    if (sd < 0.0) {
        vec2  uv = (sq + vec2(0.040, 0.0075)) / vec2(0.080, 0.015);
        float gx = uv.x * 7.0 - iTime * 0.8;
        float g  = glyph(vec2(fract(gx), uv.y) * 1.2 - 0.1, floor(gx) + 77.0);
        vec3  ac = mix(CYAN, MAG, 0.5 + 0.5 * sin(iTime * 0.4));
        col = mix(col, STORM * 0.6 + ac * g * 1.6 + ac * 0.15, 0.95) * (0.85 + 0.15 * step(0.5, fract(uv.y * 12.0)));
    }
    col += mix(CYAN, MAG, 0.5) * 0.15 * exp(-max(sd, 0.0) * 120.0) * step(0.0, sd);
    // nav lights
    float bl = step(0.5, fract(iTime * 0.9));
    col += RED   * bl * smoothstep(0.004, 0.0, length(q - vec2(-0.085, 0.0))) * 2.0;
    col += TEAL  * (1.0 - bl) * smoothstep(0.004, 0.0, length(q - vec2(0.085, 0.0))) * 2.0;
    // searchlight cone beneath (only below the hull — the width term goes negative above it)
    if (q.y < -0.02) {
        float below = -q.y - 0.02;
        float cone  = exp(-abs(q.x) / (0.004 + below * 0.25)) * exp(-below * 30.0);
        col += mix(CYAN, FG, 0.4) * cone * 0.08;
    }
}

// ── the whole city (above the water) ──────────────────────────────────────
vec3 scene(vec2 p, float ar, vec2 par) {
    float y = p.y;

    // sky + time-of-day light at the horizon
    vec3 col = mix(STORM, CRUST, smoothstep(HORIZON, 1.0, y));
    col *= 1.0 - 0.25 * LATE;
    col += mix(PURPLE, MAG, 0.4) * 0.30 * exp(-(y - HORIZON) * 7.0);
    col += BLUE * 0.06 * exp(-(y - HORIZON) * 2.5);
    col += mix(ORANGE, RED, 0.45) * 0.75 * DUSK * exp(-(y - HORIZON) * 4.0);
    col += mix(MAG, RED, 0.5) * 0.20 * DUSK * exp(-(y - HORIZON) * 1.5);
    col += mix(TEAL, ORANGE, smoothstep(HORIZON, HORIZON + 0.25, y)) * 0.60 * DAWN * exp(-(y - HORIZON) * 3.0);
    col += BLUE * 0.15 * DAWN * smoothstep(HORIZON, 1.0, y);

    // lightning
    float lt   = floor(iTime / 19.0);
    float lloc = iTime - lt * 19.0;
    float lon  = step(0.4, nwHash11(lt * 3.7));
    float flash = lon * (exp(-lloc * 9.0) + 0.7 * step(0.16, lloc) * exp(-max(lloc - 0.16, 0.0) * 12.0));

    // clouds (parallax 0.006) — only where they can appear
    vec2  pc = p + par * 0.006;
    float cmask = 0.0;
    vec3  skyN  = texture(iChannel1, vec2(pc.x / ar, pc.y)).rgb;   // quarter-res Clouds pass
    if (y > HORIZON + 0.07) cmask = smoothstep(0.48, 0.78, skyN.r) * smoothstep(HORIZON + 0.08, 0.75, y);
    vec3  ccol  = mix(STORM * 1.2, mix(PURPLE, MAG, 0.4) * 0.6, exp(-(y - HORIZON) * 3.0));
    ccol += mix(ORANGE, RED, 0.4) * 0.55 * DUSK * exp(-(y - HORIZON) * 1.6);
    ccol += mix(ORANGE, YELLOW, 0.5) * 0.40 * DAWN * exp(-(y - HORIZON) * 2.0);
    col = mix(col, ccol + mix(BLUE, FG, 0.5) * flash * 1.4, cmask * 0.75);
    col += mix(BLUE, FG, 0.4) * flash * 0.12;
    if (lon > 0.0 && flash > 0.02) {
        float bx   = ar * (0.2 + 0.6 * nwHash11(lt * 9.1));
        float path = bx + (nwFbm(vec2(y * 9.0, lt), 4) - 0.5) * 0.10;
        float bd   = abs(pc.x - path);
        col += mix(FG, CYAN, 0.3) * flash * (exp(-bd * 1400.0) * 3.0 + 0.3 * exp(-bd * 90.0))
             * step(HORIZON + 0.12, y) * step(y, 0.86);
    }

    // stars (parallax 0.004)
    vec2  ps  = p + par * 0.004;
    vec2  sg  = ps * 85.0;
    vec2  sid = floor(sg);
    float sh  = nwHash21(sid);
    vec2  so  = nwHash22(sid) - 0.5;
    float star = step(0.982, sh) * smoothstep(0.09, 0.0, length(fract(sg) - 0.5 - so * 0.6));
    star *= 0.55 + 0.45 * sin(iTime * (0.7 + sh * 2.5) + sh * 60.0);
    col += star * mix(BLUE, vec3(1.0), 0.55) * smoothstep(HORIZON + 0.15, 0.85, y) * (1.0 - cmask) * (1.0 + 0.5 * LATE);

    // shooting star
    float st = floor(iTime / 9.0);
    float sl = (iTime - st * 9.0) / 0.9;
    if (sl < 1.0 && nwHash11(st * 5.3) > 0.45) {
        vec2  s0  = vec2(ar * (0.15 + 0.7 * nwHash11(st * 2.1)), 0.80 + 0.15 * nwHash11(st * 7.7));
        vec2  dir = normalize(vec2(nwHash11(st * 3.3) > 0.5 ? 1.0 : -1.0, -0.45));
        vec2  v   = ps - (s0 + dir * sl * 0.35);
        float along = -dot(v, dir);
        float perp  = abs(v.x * dir.y - v.y * dir.x);
        col += mix(CYAN, FG, 0.5) * step(0.0, along) * exp(-along * 18.0) * exp(-perp * 2500.0) * sin(sl * 3.14159) * 2.5;
    }

    // searchlights
    float sx = par.x * 0.02;
    float b1 = beam(p, vec2(ar * 0.56 - sx, HORIZON), 0.40 * sin(iTime * 0.11) - 0.15);
    float b2 = beam(p, vec2(ar * 0.88 - sx, HORIZON), 0.35 * sin(iTime * 0.08 + 2.0) + 0.10);
    col += mix(BLUE, CYAN, 0.4) * (b1 + b2) * 0.16 * (1.0 + cmask * 1.5);

    // synthwave moon (parallax 0.010) — sits lower and warmer at dusk
    vec2  pm = p + vec2(par.x * 0.010, par.y * 0.006);
    vec2  mc = vec2(ar * 0.70, 0.62 - 0.06 * DUSK);
    float mr = 0.19;
    float d  = length(pm - mc);
    float t  = clamp((pm.y - (mc.y - mr)) / (2.0 * mr), 0.0, 1.0);
    vec3  mcol = mix(mix(RED, ORANGE, DUSK * 0.6), MAG, smoothstep(0.0, 0.55, t));
    mcol = mix(mcol, CYAN, smoothstep(0.55, 1.05, t));
    float below = (mc.y - pm.y) / mr;
    float cuts = 1.0;
    if (below > 0.0) cuts = step(0.10 + 0.40 * below, fract(below * 6.0 - iTime * 0.12));
    float disc = smoothstep(mr, mr - 0.0025, d);
    col += mix(MAG, BLUE, 0.4) * (0.22 + 0.15 * BEAT) * exp(-max(d - mr, 0.0) * 7.0);
    col += RED  * 0.40 * smoothstep(0.004, 0.0, abs(length(pm - mc - vec2(0.003, 0.0)) - mr));
    col += CYAN * 0.40 * smoothstep(0.004, 0.0, abs(length(pm - mc + vec2(0.003, 0.0)) - mr));
    col  = mix(col, mcol * 1.15, disc * cuts * (1.0 - cmask * 0.6));

    // blimp (parallax 0.012)
    blimp(p + par * 0.012, ar, col);

    // far skyline + fog
    skyline(p + vec2(par.x * 0.014, 0.0), 11.0, 0.010, 0.04, 0.20, mix(STORM, PURPLE, 0.22), 0.0, 1.0, false, col);
    float fog1 = skyN.g;
    col += mix(PURPLE, MAG, 0.5) * (0.06 + 0.10 * fog1) * exp(-(y - HORIZON) * 14.0);

    // Tokyo Tower
    tower(p, vec2(ar * 0.22 - par.x * 0.018, HORIZON), 0.66, col);
    col += mix(PURPLE, MAG, 0.5) * 0.05 * exp(-(y - HORIZON) * 10.0);

    // mid skyline + fog
    skyline(p + vec2(par.x * 0.022, 0.0), 6.5, 0.020, 0.05, 0.30, mix(SURF, STORM, 0.5) * 0.85, 0.6, 2.0, false, col);
    float fog2 = skyN.b;
    col += mix(BLUE, PURPLE, 0.6) * (0.03 + 0.07 * fog2) * exp(-(y - HORIZON) * 22.0);

    // elevated train
    vec2  pt = p + vec2(par.x * 0.024, 0.0);
    float ty = HORIZON + 0.078;
    float track = smoothstep(PX * 1.5, 0.0, abs(y - ty + 0.004))
                + step(abs(y - ty + 0.012), 0.008) * step(0.94, fract(pt.x * 30.0)) * step(y, ty);
    col = mix(col, CRUST * 1.3, clamp(track, 0.0, 1.0) * 0.9);
    float tspan = ar * 3.2;
    float tx = mod(iTime * 0.16, tspan) - 0.7;
    float tl = 0.62;
    if (pt.x > tx && pt.x < tx + tl && abs(y - ty - 0.004) < 0.006) {
        float u    = (pt.x - tx) / tl;
        float car  = step(0.012, fract(u * 6.0)) * step(fract(u * 6.0), 0.985);
        float wins = step(0.35, fract(u * 6.0 * 9.0)) * step(abs(y - ty - 0.005), 0.0022);
        vec3  tc   = mix(SURF, STORM, 0.5) + mix(YELLOW, CYAN, step(0.5, fract(u * 3.0))) * wins * 1.6;
        tc += MAG * 1.0 * smoothstep(PX * 1.5, 0.0, abs(y - ty - 0.0095));
        col = mix(col, tc, car);
    }
    if (abs(y - ty) < 0.03) col += mix(CYAN, MAG, 0.5) * 0.12 * exp(-abs(y - ty) * 120.0)
                                 * smoothstep(0.0, 0.05, pt.x - tx) * smoothstep(0.0, 0.05, tx + tl - pt.x);

    // traffic between layers
    traffic(p + vec2(par.x * 0.026, 0.0), ar, 0.10, 0.12, 0.0016, 0.8, false, col);

    // near skyline with billboards
    skyline(p + vec2(par.x * 0.036, 0.0), 3.6, 0.034, 0.04, 0.42, CRUST * 1.15, 1.0, 3.0, true, col);

    // foreground traffic (with the occasional cop)
    traffic(p + vec2(par.x * 0.042, 0.0), ar, 0.14, 0.16, 0.0026, 1.0, true, col);

    return col;
}

float rain(vec2 p, float density, float speed, float seed) {
    p.x += p.y * 0.16;
    vec2  g  = vec2(p.x * density, p.y * 2.6 + iTime * speed + seed);
    vec2  id = floor(g);
    vec2  f  = fract(g);
    float r  = nwHash21(id + seed);
    float len = 0.25 + 0.5 * fract(r * 13.0);
    return step(0.94, r) * smoothstep(0.0, 0.08, f.y) * smoothstep(len, len * 0.4, f.y)
         * (1.0 - smoothstep(0.0, 0.09, abs(f.x - 0.5)));
}

// bloom tap offsets (px) and weights: wide, smooth falloff with 9 taps per side
const float BO[5] = float[5](0.0, 5.0, 14.0, 27.0, 44.0);   // full-res px; blurred mip in between

float bw(float o) { return exp(-(o * o) / (2.0 * 17.0 * 17.0)); }

// must match `scale` of bufferC/bufferD in tokyo_night.neowall
const float BLOOM_SCALE = 0.25;

// ═══════════════════════════════ Clouds (quarter res) ════════════════════════
// Soft things only: the cloud field and the two fog layers. They are blurry by
// nature, so they render at scale 0.25 (1/16 of the pixels) and Buffer A reads
// them with bilinear filtering: r = clouds (raw fbm), g = far fog, b = mid fog.
// Pass: Clouds

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  p = fragCoord / iResolution.y;          // same units as the city
    float y = p.y;
    float cl   = y > HORIZON + 0.05 ? nwFbm(vec2(p.x * 1.1 + iTime * 0.008, y * 3.0 - iTime * 0.004), 4) : 0.0;
    float fog1 = y < HORIZON + 0.32 ? nwFbm(vec2(p.x * 2.2 - iTime * 0.012, y * 9.0), 3) : 0.0;
    float fog2 = y < HORIZON + 0.22 ? nwFbm(vec2(p.x * 3.0 + iTime * 0.015, y * 12.0 + 5.0), 3) : 0.0;
    fragColor = vec4(cl, fog1, fog2, 1.0);
}

// ═══════════════════════════════ Buffer A ═════════════════════════════════
// The city. Pixel (0,0) stores the smoothed mouse position for parallax.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    if (fragCoord.x < 1.0 && fragCoord.y < 1.0) {
        vec2 prev = texelFetch(iChannel0, ivec2(0, 0), 0).xy;
        if (iFrame < 3 || (prev.x == 0.0 && prev.y == 0.0)) prev = vec2(0.5);
        vec2 target = (iMouse.x > 0.0 || iMouse.y > 0.0) ? iMouse.xy / iResolution.xy : prev;
        fragColor = vec4(mix(prev, clamp(target, 0.0, 1.0), 0.035), 0.0, 1.0);
        return;
    }
    float ar = iResolution.x / iResolution.y;
    vec2  p  = fragCoord / iResolution.y;
    if (p.y < HORIZON - 0.004) { fragColor = vec4(0.0); return; }     // water: done in B

    PX   = 1.5 / iResolution.y;
    BEAT = clamp(iAudioBeat, 0.0, 1.0) * iAudioActive;
    timeOfDay();

    vec2 m   = texelFetch(iChannel0, ivec2(0, 0), 0).xy;
    vec2 par = (m - 0.5) + vec2(sin(iTime * 0.031), cos(iTime * 0.023)) * 0.18;  // + slow camera drift
    par.x *= 1.8;

    vec3 c = scene(p, ar, par);
    if (any(isnan(c)) || any(isinf(c))) c = BASE;          // never let a bad pixel reach the bloom
    fragColor = vec4(max(c, 0.0), 1.0);
}

// ═══════════════════════════════ Buffer B ═════════════════════════════════
// Water: reflection of A, rippled + vertically smeared, rain rings.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float ar = iResolution.x / iResolution.y;
    vec2  p  = fragCoord / iResolution.y;
    vec3  col;
    if (p.y >= HORIZON) {
        col = texelFetch(iChannel0, ivec2(fragCoord), 0).rgb;
    } else {
        float depth = HORIZON - p.y;
        vec2  q = p;
        q.x += sin(depth * 140.0 - iTime * 1.6) * 0.0025 * (0.3 + depth * 6.0)
             + (nwValueNoise(vec2(p.x * 8.0, depth * 60.0 - iTime * 0.5)) - 0.5) * 0.007;
        q.y = HORIZON + depth * 1.15 + 0.002;
        // smear: taps reach further up the city the deeper we look into the water
        vec3  refl = vec3(0.0);
        float wsum = 0.0;
        for (int k = 0; k < 6; k++) {
            float fk = float(k);
            float off = fk * (0.003 + depth * 0.045);
            float w   = exp(-fk * 0.45);
            vec2  uv  = vec2((q.x + (nwHash11(fk + floor(iTime * 30.0)) - 0.5) * 0.0015) / ar, min(q.y + off, 0.999));
            refl += texture(iChannel0, uv).rgb * w;
            wsum += w;
        }
        refl /= wsum;
        float band = nwValueNoise(vec2(p.x * 2.5, depth * 140.0 - iTime * 0.9));
        refl *= 0.50 + 0.50 * band;
        col  = mix(BASE * 0.85, refl * vec3(0.60, 0.66, 0.92), exp(-depth * 2.6));
        col += BLUE * 0.04 * (1.0 - depth * 2.0);

        // rain rings
        vec2  rg  = vec2(p.x * 9.0, depth * 9.0 / (0.25 + depth * 2.5));
        vec2  rid = floor(rg);
        vec2  rf  = fract(rg) - 0.5;
        float rh  = nwHash21(rid);
        float ph  = fract(iTime * (0.5 + rh * 0.5) + rh * 7.0);
        vec2  ro  = (nwHash22(rid + 2.0) - 0.5) * 0.4;
        float rr  = length((rf - ro) * vec2(1.0, 2.2)) - ph * 0.42;
        col += mix(CYAN, MAG, rh) * 0.22 * exp(-abs(rr) * 70.0) * (1.0 - ph) * step(0.45, rh) * exp(-depth * 2.5);
    }
    // glowing waterline
    col += mix(MAG, BLUE, 0.5) * 0.45 * exp(-abs(p.y - HORIZON) * 260.0);
    fragColor = vec4(col, 1.0);
}

// ═══════════════════════════════ Buffer C ═════════════════════════════════
// Bloom: bright pass + horizontal blur at a fraction of the output (`scale` in
// tokyo_night.neowall). Offsets are in full-res pixels and scaled to this
// buffer, and B is read at the matching mip, so the look is the same at any scale.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  px  = 1.0 / iResolution.xy;
    vec2  uv  = fragCoord * px;
    float k   = iResolution.x / iChannelResolution[0].x;   // 0.25 → quarter res
    float lod = log2(1.0 / k);
    vec3 acc = vec3(0.0);
    float ws = 0.0;
    for (int i = -4; i <= 4; i++) {
        float o = BO[abs(i)] * sign(float(i));
        float w = bw(o);
        vec3  c = textureLod(iChannel0, clamp(uv + vec2(o * k * px.x, 0.0), px, 1.0 - px), lod).rgb;
        c = clamp(c, 0.0, 4.0);                                // NaN/inf-safe bright pass
        acc += c * smoothstep(0.42, 1.25, lum(c)) * w;
        ws  += w;
    }
    fragColor = vec4(acc / ws, 1.0);
}

// ═══════════════════════════════ Buffer D ═════════════════════════════════
// Bloom: vertical blur, same fraction.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  px = 1.0 / iResolution.xy;
    vec2  uv = fragCoord * px;
    vec3 acc = vec3(0.0);
    float ws = 0.0;
    float sk = BLOOM_SCALE;
    for (int i = -4; i <= 4; i++) {
        float o = BO[abs(i)] * sign(float(i));
        float w = bw(o);
        acc += texture(iChannel0, clamp(uv + vec2(0.0, o * sk * px.y), px, 1.0 - px)).rgb * w;
        ws  += w;
    }
    // anamorphic streak: the horizontal bloom far to either side (±95/190/285
    // full-res px), as luminance in alpha, so the Image pass reads it for free
    float streak = 0.0;
    for (int i = 1; i <= 3; i++) {
        float o = float(i) * 95.0 * sk * px.x;
        streak += lum(texture(iChannel0, clamp(uv + vec2(o, 0.0), px, 1.0 - px)).rgb);
        streak += lum(texture(iChannel0, clamp(uv - vec2(o, 0.0), px, 1.0 - px)).rgb);
    }
    fragColor = vec4(acc / ws, streak);
}

// ═══════════════════════════════ Halo (eighth res) ════════════════════════
// The wide soft halo around bright lights: bloom sampled 70 full-res px away
// on four diagonals. Very blurry, so 1/64 of the pixels is plenty.
// Pass: Halo

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv  = fragCoord / iResolution.xy;
    vec2 off = 70.0 * BLOOM_SCALE / iChannelResolution[0].xy;   // 70 full-res px in uv
    vec3 h   = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float a = float(i) * 1.5708 + 0.785;
        h += texture(iChannel0, clamp(uv + vec2(cos(a), sin(a)) * off, vec2(0.001), vec2(0.999))).rgb;
    }
    fragColor = vec4(h * 0.25, 1.0);
}

// ═══════════════════════════════ Image ════════════════════════════════════
// iChannel0 = B (scene+water), iChannel1 = D (bloom + streak), iChannel2 = Halo

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  res = iResolution.xy;
    vec2  uv  = fragCoord / res;
    vec2  px  = 1.0 / res;
    vec2  p   = fragCoord / res.y;
    vec2  v   = uv - 0.5;

    // chromatic fringe toward the edges
    vec2  ca  = v * dot(v, v) * 0.010;
    vec3  col;
    col.r = texture(iChannel0, uv + ca).r;
    col.g = texture(iChannel0, uv).g;
    col.b = texture(iChannel0, uv - ca).b;

    // bloom, the wide halo and the anamorphic streak, all from low-res passes
    vec4 bl    = texture(iChannel1, uv);       // rgb bloom, a = streak luminance
    vec3 bloom = bl.rgb;
    vec3 halo  = texture(iChannel2, uv).rgb;
    col += bloom * 1.25 + halo * 0.70;
    col += bl.a * mix(BLUE, CYAN, 0.5) * 0.10;

    // rain, lit by the neon it falls past
    float r1 = rain(p, 70.0, 2.2, 0.0);
    float r2 = rain(p * 1.7, 110.0, 1.6, 7.0);
    vec3  lit = bloom * 3.5 + halo * 2.0;
    col += (mix(BLUE, CYAN, 0.5) * 0.06 + lit) * r1;
    col += (mix(BLUE, MAG, 0.3)  * 0.04 + lit * 0.6) * r2;

    // the desktop lights the city
    col += mix(BLUE, MAG, 0.35) * 0.30 * nwWinGlow(fragCoord, 70.0);
    col *= 1.0 - 0.20 * nwWinBusy();

    // grade: soft filmic shoulder, indigo shadows, vignette, grain
    col = mix(col, 1.0 - exp(-col * 1.15), 0.55);
    col += STORM * 0.10 * (1.0 - smoothstep(0.0, 0.25, lum(col)));
    col *= 1.0 - 0.45 * dot(v * vec2(0.9, 1.3), v * vec2(0.9, 1.3));
    col += (nwHash21(fragCoord + fract(iTime) * 100.0) - 0.5) * 0.014;

    if (fragCoord.x < 1.0 && fragCoord.y < 1.0) col = CRUST;    // mouse-state pixel
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
