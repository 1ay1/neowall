// ULTRA OPTIMIZED windows shader - target <30% GPU
#define PI     3.14159265
#define REP    6
#define WBCOL  vec3(0.5, 0.7, 1.7)
#define WBCOL2 vec3(0.15, 0.8, 1.7)
#define ZERO   min(iFrame, 0)

// Pre-computed rotation constants for d2r(70) and d2r(90)
const float COS70 = 0.342020143;
const float SIN70 = 0.939692621;

// Optimized hash using cheaper operations
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 458.325421) * 2.0 - 1.0;
}

// Low quality noise (faster)
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Reduced FBM iterations (base + 1 detail)
float fbm(vec2 p) {
    float v = 0.5 * noise(p);
    p *= 2.03;
    v += 0.25 * noise(p);
    return v;
}

// Optimized map1 using space folding (1 check instead of 4)
// Maps 4 symmetric boxes by folding space to first quadrant
float map1(vec3 p, float F) {
    // Fold to first quadrant (abs(p.xy)) then shift origin to (0.5, 0.5)
    vec2 d = abs(abs(p.xy) - 0.5) - F;
    return length(max(d, 0.0));
}

float map2(vec3 p) {
    float t = map1(p, 0.45);
    vec3 d = abs(p) - vec3(1.0, 1.0, 0.02);
    return max(t, length(max(d, 0.0)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord.xy / iResolution.xy) * 2.0 - 1.0;
    uv *= 1.4;

    float aspect = iResolution.x / iResolution.y;
    // Increased wobble amplitude (0.01 -> 0.025)
    vec3 dir = normalize(vec3(uv.x * aspect, uv.y, 1.0 + sin(iTime) * 0.025));
    
    // Apply rotations
    dir.xz = vec2(dir.x * COS70 - dir.z * SIN70, dir.x * SIN70 + dir.z * COS70);
    dir.xy = vec2(-dir.y, dir.x); 
    
    // Increased movement speed and amplitude
    float time03 = iTime * 0.35;
    float time04 = iTime * 0.45;
    
    vec3 pos = vec3(-0.1 + sin(time03) * 0.25, 2.0 + cos(time04) * 0.25, -3.5);
    
    vec3 col = vec3(0.0);
    float t = 0.0;
    float bsh = 0.01;
    float dens = 0.0;
    
    // Slightly faster fog scroll
    float fogTimeOffset = iTime * 0.08; 
    vec2 fogScroll = vec2(0.0, iTime * 0.035);

    // Main raymarching loop
    for (int i = ZERO; i < 48; i++) {
        vec3 p = pos + dir * t;
        
        // Early exit if totally opaque
        if (dens > 1.0) break;
        
        // Inline map check
        vec2 d = abs(abs(p.xy) - 0.5) - 0.3;
        if (length(max(d, 0.0)) < 0.2) {
            col += WBCOL * (0.005 * dens);
        }

        vec2 fogCoord = p.xz * 0.25 + fogTimeOffset + fogScroll;
        // Simplified fog calculation (direct call, removed extra math)
        float fogLayer = fbm(fogCoord);
        float dynamicFog = smoothstep(0.2, 0.8, fogLayer) * min(t * 0.1, 1.0);
        
        col += vec3(0.15, 0.22, 0.35) * (1.2 * dynamicFog * 0.1); 

        // Faster step growth to cover distance with fewer iterations
        t += bsh * 1.004;
        bsh *= 1.004;
        dens += 0.025;
    }

    // Windows pass
    t = 0.0;
    for (int i = ZERO; i < REP; i++) {
        float temp = map2(pos + dir * t);
        if (temp < 0.025) {
            col += WBCOL2 * 0.5;
        }
        t += max(temp, 0.01);
    }

    col += (2.0 + uv.x) * WBCOL2;
    // Reduced final noise detail
    col += noise(dir.xz * 5.0 + iTime) * 0.25;
    col *= (1.0 - uv.y * 0.5) * 0.1;

    fragColor = vec4(pow(col, vec3(0.717)), 1.0);
}
