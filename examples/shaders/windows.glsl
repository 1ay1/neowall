#define PI     3.1415926535897921284
#define REP    8
#define d2r(x) (x * PI / 180.0)
#define WBCOL  (vec3(0.5, 0.7, 1.7))
#define WBCOL2 (vec3(0.15, 0.8, 1.7))
#define ZERO   (min(iFrame, 0))

float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 458.325421) * 2.0 - 1.0;
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x),
        f.y
    );
}

// Fractal brownian motion for mist texture
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.55;
    }
    return v;
}

vec2 rot(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float nac(vec3 p, vec2 F, vec3 o) {
    p += o;
    return length(max(abs(p.xy) - F, 0.0)) - 0.0001;
}

float recta(vec3 p, vec3 F, vec3 o) {
    p += o;
    return length(max(abs(p) - F, 0.0)) - 0.0001;
}

float map1(vec3 p, float scale) {
    float G = 0.50;
    float F = 0.50 * scale;
    float t = nac(p, vec2(F), vec3(G, G, 0.0));
    t = min(t, nac(p, vec2(F), vec3(G, -G, 0.0)));
    t = min(t, nac(p, vec2(F), vec3(-G, G, 0.0)));
    t = min(t, nac(p, vec2(F), vec3(-G, -G, 0.0)));
    return t;
}

float map2(vec3 p) {
    float t = map1(p, 0.9);
    return max(t, recta(p, vec3(1.0, 1.0, 0.02), vec3(0.0)));
}

float gennoise(vec2 p) {
    return 0.5 * noise(p * 5.0 + iTime) + 0.25 * noise(p * 8.0 + iTime);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = -1.0 + 2.0 * (fragCoord.xy / iResolution.xy);
    uv *= 1.4;

    vec3 dir = normalize(vec3(uv * vec2(iResolution.x / iResolution.y, 1.0), 1.0 + sin(iTime) * 0.01));
    dir.xz = rot(dir.xz, d2r(70.0));
    dir.xy = rot(dir.xy, d2r(90.0));

    vec3 pos = vec3(-0.1 + sin(iTime * 0.3) * 0.1, 2.0 + cos(iTime * 0.4) * 0.1, -3.5);
    vec3 col = vec3(0.0);
    float t = 0.0;
    float M = 1.002;
    float bsh = 0.01;
    float dens = 0.0;

    for (int i = ZERO; i < REP * 12; i++) {
        vec3 p = pos + dir * t;
        float temp = map1(p, 0.6);
        if (temp < 0.2) {
            col += WBCOL * 0.005 * dens;
        }

        // --- Thick mist layer ---
        float fogLayer = fbm(p.xz * 0.25 + iTime * 0.05 + vec2(0.0, iTime * 0.02));
        float fogDepth = smoothstep(0.2, 0.8, fogLayer);
        float fogDistance = clamp(t * 0.08, 0.0, 1.0);
        float dynamicFog = fogDepth * fogDistance;
        vec3 fogColor = vec3(0.15, 0.22, 0.35) * (1.2 + 0.5 * sin(iTime * 0.3 + p.x * 0.7));
        col += fogColor * dynamicFog * 0.1;
        // ------------------------

        t += bsh * M;
        bsh *= M;
        dens += 0.025;
    }

    // Windows pass
    t = 0.0;
    float y = 0.0;
    for (int i = ZERO; i < REP; i++) {
        float temp = map2(pos + dir * t);
        if (temp < 0.025) {
            col += WBCOL2 * 0.5;
        }
        t += max(temp, 0.01);
        y++;
    }

    col += ((2.0 + uv.x) * WBCOL2) + (y / 625.0);
    col += gennoise(dir.xz) * 0.5;
    col *= 1.0 - uv.y * 0.5;

    // Lift brightness for fog visibility
    col *= 0.1;

    // Soft gamma
    col = pow(col, vec3(0.717));

    fragColor = vec4(col, 1.0);
}
