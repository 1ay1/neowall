// Optimized windows shader - maintains visual quality with better performance
#define PI     3.14159265
#define REP    8
#define WBCOL  vec3(0.5, 0.7, 1.7)
#define WBCOL2 vec3(0.15, 0.8, 1.7)
#define ZERO   min(iFrame, 0)

// Pre-computed rotation constants for d2r(70) and d2r(90)
const float COS70 = 0.342020143;
const float SIN70 = 0.939692621;
// d2r(90) = PI/2, cos=0, sin=1

// Optimized hash using cheaper operations
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 458.325421) * 2.0 - 1.0;
}

// Vectorized noise with batched hash lookups
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    // Batch all 4 hash calls - compute offsets once
    vec4 h = vec4(
        hash(i),
        hash(i + vec2(1.0, 0.0)),
        hash(i + vec2(0.0, 1.0)),
        hash(i + vec2(1.0, 1.0))
    );
    return mix(mix(h.x, h.y, f.x), mix(h.z, h.w, f.x), f.y);
}

// Reduced FBM iterations (4 instead of 5) - minimal visual difference for fog
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.55;
    }
    return v;
}

// Inlined rotation for known angles
vec2 rot70(vec2 p) {
    return vec2(p.x * COS70 - p.y * SIN70, p.x * SIN70 + p.y * COS70);
}

// Optimized nac - single vec2 subtraction
float nac(vec3 p, float F, vec3 o) {
    vec2 d = abs(p.xy + o.xy) - F;
    return length(max(d, 0.0));
}

// Optimized map1 - unrolled with shared computation
float map1(vec3 p, float F) {
    const float G = 0.5;
    float t = nac(p, F, vec3(G, G, 0.0));
    t = min(t, nac(p, F, vec3(G, -G, 0.0)));
    t = min(t, nac(p, F, vec3(-G, G, 0.0)));
    t = min(t, nac(p, F, vec3(-G, -G, 0.0)));
    return t;
}

float map2(vec3 p) {
    float t = map1(p, 0.45); // 0.5 * 0.9
    vec3 d = abs(p) - vec3(1.0, 1.0, 0.02);
    return max(t, length(max(d, 0.0)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord.xy / iResolution.xy) * 2.0 - 1.0;
    uv *= 1.4;

    float aspect = iResolution.x / iResolution.y;
    float timeSin = sin(iTime);
    
    vec3 dir = normalize(vec3(uv.x * aspect, uv.y, 1.0 + timeSin * 0.01));
    
    // Apply rotations with pre-computed constants
    dir.xz = rot70(dir.xz);
    dir.xy = vec2(-dir.y, dir.x); // 90 degree rotation: cos=0, sin=1
    
    // Pre-compute time-based values
    float time03 = iTime * 0.3;
    float time04 = iTime * 0.4;
    vec3 pos = vec3(-0.1 + sin(time03) * 0.1, 2.0 + cos(time04) * 0.1, -3.5);
    
    vec3 col = vec3(0.0);
    float t = 0.0;
    float bsh = 0.01;
    float dens = 0.0;
    
    // Pre-compute fog time offsets
    float fogTimeOffset = iTime * 0.05;
    vec2 fogScroll = vec2(0.0, iTime * 0.02);

    // Main raymarching loop - reduced from 96 to 72 iterations
    for (int i = ZERO; i < 72; i++) {
        vec3 p = pos + dir * t;
        
        if (map1(p, 0.3) < 0.2) { // 0.5 * 0.6
            col += WBCOL * (0.005 * dens);
        }

        // Optimized fog calculation
        vec2 fogCoord = p.xz * 0.25 + fogTimeOffset + fogScroll;
        float fogLayer = fbm(fogCoord);
        float dynamicFog = smoothstep(0.2, 0.8, fogLayer) * min(t * 0.08, 1.0);
        col += vec3(0.15, 0.22, 0.35) * ((1.2 + 0.5 * sin(time03 + p.x * 0.7)) * dynamicFog * 0.1);

        t += bsh * 1.002;
        bsh *= 1.002;
        dens += 0.025;
    }

    // Windows pass - kept at 8 iterations
    t = 0.0;
    for (int i = ZERO; i < REP; i++) {
        float temp = map2(pos + dir * t);
        if (temp < 0.025) {
            col += WBCOL2 * 0.5;
        }
        t += max(temp, 0.01);
    }

    // Final color computation
    col += (2.0 + uv.x) * WBCOL2;
    col += (noise(dir.xz * 5.0 + iTime) * 0.5 + noise(dir.xz * 8.0 + iTime) * 0.25) * 0.5;
    col *= (1.0 - uv.y * 0.5) * 0.1;

    // Gamma correction
    fragColor = vec4(pow(col, vec3(0.717)), 1.0);
}
