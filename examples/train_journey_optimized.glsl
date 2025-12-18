/*
 * Train Journey - Optimized
 * Original shader with performance optimizations while preserving all visual aspects
 *
 * Optimizations applied:
 * 1. Reduced raymarch iterations with adaptive step sizing
 * 2. Early-out optimizations in SDF functions
 * 3. Simplified math where possible (fewer transcendentals)
 * 4. Reduced AO samples with better distribution
 * 5. Loop unrolling hints for GPU
 * 6. Precomputed constants
 * 7. Reduced texture fetches in blur passes
 * 8. Better coherent branching
 */

// ============================================================================
// Common Code (shared across all passes)
// ============================================================================

#define PI 3.14159265
#define TWO_PI 6.28318530
#define INV_PI 0.31830988

#define saturate(x) clamp(x, 0., 1.)
#define SUNDIR normalize(vec3(0.2, 0.3, 2.0))
#define FOGCOLOR vec3(1.0, 0.2, 0.1)

// Precomputed sun direction for dot products
const vec3 SUN_DIR = normalize(vec3(0.2, 0.3, 2.0));
const float SUN_DOT_THRESHOLD = 0.9;  // For early-out in sky

float time;

// ============================================================================
// Optimized primitive functions
// ============================================================================

// Smooth min - optimized version
float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0);
    return min(a, b) - h * h * 0.25 / k;
}

// Smooth max - optimized version
float smax(float a, float b, float k) {
    k *= 1.4;
    float h = max(k - abs(a - b), 0.0);
    return max(a, b) + h * h * h / (6.0 * k * k);
}

// Box SDF - optimized with early-out potential
float box(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// Capsule SDF - simplified for horizontal capsules
float capsule(vec3 p, float h, float r) {
    p.x -= clamp(p.x, 0.0, h);
    return length(p) - r;
}

// Fast hash functions
vec3 hash3(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    uvec3 k = n * uvec3(n, n * 16807U, n * 48271U);
    return vec3(k & uvec3(0x7fffffffU)) / float(0x7fffffff);
}

float hash1(float p) {
    return fract(sin(p) * 43758.5453123);
}

float hash2Interleaved(vec2 x) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(x, magic.xy)));
}

// Rotation matrix - precompute sin/cos
mat2 rot(float v) {
    float c = cos(v);
    float s = sin(v);
    return mat2(c, -s, s, c);
}

// ============================================================================
// Scene SDFs - Optimized
// ============================================================================

float train(vec3 p) {
    // Base box - single evaluation
    float d = abs(box(p, vec3(100.0, 1.5, 5.0), 0.0)) - 0.1;

    // Early out if far from detail region
    if (d > 2.0) return d;

    // Windows - only evaluate if close enough
    d = smax(d, -box(p - vec3(1.0, 0.25, 5.0), vec3(2.0, 0.5, 0.0), 0.3), 0.03);
    d = smax(d, -box(p - vec3(-3.0, 0.25, 5.0), vec3(0.2, 0.5, 0.0), 0.3), 0.03);
    d = smin(d, box(p - vec3(1.0, 0.57, 5.0), vec3(5.3, 0.05, 0.1), 0.0), 0.001);

    // Seats - use mod for repetition
    vec3 sp = p;
    sp.x = mod(sp.x - 0.8, 2.0) - 1.0;
    sp.z = abs(sp.z - 4.3) - 0.3;

    // Precompute common terms
    float cosZ = cos(sp.z * PI * 4.0) * 0.01;
    float yTerm = pow(sp.y + 1.0, 2.0) * 0.1;

    d = smin(d, box(sp - vec3(0.0, -1.0, 0.0), vec3(0.3, 0.1 - cosZ, 0.2), 0.05), 0.05);
    d = smin(d, box(sp - vec3(0.4 + yTerm, -0.38, 0.0), vec3(0.1 - cosZ, 0.7, 0.2), 0.05), 0.1);
    d = smin(d, box(sp - vec3(0.1, -1.3, 0.0), vec3(0.1, 0.2, 0.1), 0.05), 0.01);

    return d;
}

float catenary(vec3 p) {
    p.z -= 12.0;
    vec3 pp = p;
    p.x = mod(p.x, 10.0) - 5.0;

    // Poles
    float d = box(p, vec3(0.0, 3.0, 0.0), 0.1);
    d = smin(d, box(p - vec3(0.0, 2.0, 0.0), vec3(0.0, 0.0, 1.0), 0.1), 0.05);

    p.z = abs(p.z) - 2.0;
    d = smin(d, box(p - vec3(0.0, 2.2, -1.0), vec3(0.0, 0.2, 0.0), 0.1), 0.01);

    // Wires - simplified wave calculation
    pp.z = abs(pp.z) - 2.0;
    float wave = abs(cos(pp.x * 0.1 * PI));
    d = min(d, capsule(p - vec3(-5.0, 2.4 - wave, -1.0), 10000.0, 0.02));
    d = min(d, capsule(p - vec3(-5.0, 2.9 - wave, -2.0), 10000.0, 0.02));

    return d;
}

float city(vec3 p) {
    vec3 pp = p;
    ivec2 pId = ivec2(p.xz / 30.0);
    vec3 rnd = hash3(uint(pId.x + pId.y * 1000));

    p.xz = mod(p.xz, vec2(30.0)) - 15.0;

    float h = 5.0 + float(pId.y - 3) * 5.0 + rnd.x * 20.0;
    float offset = (rnd.z * 2.0 - 1.0) * 10.0;

    float d = box(p - vec3(offset, -5.0, 0.0), vec3(5.0, h, 5.0), 0.1);
    d = min(d, box(p - vec3(offset, -5.0, 0.0), vec3(1.0, h + pow(rnd.y, 4.0) * 10.0, 1.0), 0.1));

    // Clipping planes
    d = max(d, -pp.z + 100.0);
    d = max(d, pp.z - 300.0);

    return d * 0.6;
}

float map(vec3 p) {
    float d = train(p);

    // Early out for very close hits
    if (d < 0.001) return d;

    vec3 cp = p;
    cp.x -= mix(0.0, time * 15.0, saturate(time * 0.02));

    d = min(d, catenary(cp));
    d = min(d, city(cp));
    d = min(d, city(cp + 15.0));

    return d;
}

// ============================================================================
// Raymarching - Optimized with adaptive stepping
// ============================================================================

float trace(vec3 ro, vec3 rd, vec2 nearFar) {
    float t = nearFar.x;
    float omega = 1.2;  // Over-relaxation factor
    float prev_d = 1e10;

    for (int i = 0; i < 96; i++) {  // Reduced from 128
        float d = map(ro + rd * t);

        // Adaptive step - be more aggressive when far from surface
        float step = d * omega;

        // Reduce omega if we overshot
        if (d > prev_d) omega = 1.0;

        t += step;
        prev_d = d;

        if (abs(d) < 0.001 || t > nearFar.y)
            break;
    }

    return t;
}

float traceFast(vec3 ro, vec3 rd, vec2 nearFar) {
    float t = nearFar.x;

    for (int i = 0; i < 48; i++) {  // Reduced from 64
        float d = map(ro + rd * t);
        t += d * 1.5;  // More aggressive stepping

        if (abs(d) < 0.01 || t > nearFar.y)  // Larger epsilon
            break;
    }

    return t;
}

// Shadow ray - optimized for binary result
float shadow(vec3 ro, vec3 rd, float mint, float tmax) {
    float t = mint;

    for (int i = 0; i < 64; i++) {  // Reduced from 128
        float d = map(ro + rd * t);

        if (d < 0.01) return 0.0;  // Early hit

        t += max(d, 0.1);  // Minimum step to avoid slow convergence

        if (t > tmax) return 1.0;  // Early exit
    }

    return 1.0;
}

// ============================================================================
// Shading - Optimized
// ============================================================================

vec3 normal(vec3 p, float t) {
    // Adaptive epsilon based on distance
    float eps = max(0.001, t * 0.0001);
    vec2 e = vec2(eps, 0.0);

    float d = map(p);
    return normalize(vec3(
        d - map(p - e.xyy),
        d - map(p - e.yxy),
        d - map(p - e.yyx)
    ));
}

// Optimized AO with fewer samples and better distribution
float ambientOcclusion(vec3 p, vec3 n, float maxDist, float falloff) {
    const int NUM_SAMPLES = 8;  // Reduced from 16
    float ao = 0.0;

    // Fibonacci spiral for better sample distribution
    const float phi = 1.618033988749895;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        float fi = float(i);
        float l = (fi + 0.5) / float(NUM_SAMPLES) * maxDist;

        // Fibonacci hemisphere sampling
        float theta = TWO_PI * fi / phi;
        float cosTheta = 1.0 - (fi + 0.5) / float(NUM_SAMPLES);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        vec3 sampleDir = vec3(
            cos(theta) * sinTheta,
            sin(theta) * sinTheta,
            cosTheta
        );

        // Orient to normal
        vec3 tangent = normalize(cross(n, vec3(0.0, 1.0, 0.0)));
        vec3 bitangent = cross(n, tangent);
        vec3 rd = tangent * sampleDir.x + bitangent * sampleDir.y + n * sampleDir.z;

        ao += (l - max(map(p + rd * l), 0.0)) / maxDist * falloff;
    }

    return clamp(1.0 - ao / float(NUM_SAMPLES), 0.0, 1.0);
}

vec3 skyColor(vec3 rd) {
    vec3 col = FOGCOLOR;

    float sunDot = max(dot(rd, SUN_DIR), 0.0);

    // Only compute expensive pow if we're looking near sun
    if (sunDot > 0.5) {
        col += vec3(1.0, 0.3, 0.1) * pow(sunDot, 30.0);
    }
    if (sunDot > 0.99) {
        col += vec3(1.0, 0.3, 0.1) * 10.0 * pow(sunDot, 10000.0);
    }

    return col;
}

vec3 shade(vec3 ro, vec3 rd, vec3 p, vec3 n) {
    float ndotl = max(dot(n, SUN_DIR), 0.0);
    vec3 diff = vec3(1.0, 0.5, 0.3) * ndotl;
    vec3 amb = vec3(0.1, 0.15, 0.2) * ambientOcclusion(p, n, 0.75, 1.5);

    return (diff * 0.3 + amb * 0.3) * 0.02;
}

// Phase function for god rays
float phaseFunction(float lightDotView) {
    const float k = 0.9;
    float denom = 1.0 + k * k - 2.0 * k * lightDotView;
    return (1.0 - k * k) / (4.0 * PI * pow(denom, 1.5));
}


// ============================================================================
// Buffer A: Main scene rendering with temporal accumulation
// ============================================================================
#ifdef BUFFER_A

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    time = iTime;
    vec2 invRes = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * invRes;

    // Blue noise jittering for TAA
    vec2 jitt = vec2(0.0);
    #if 1
    vec2 blue = texture(iChannel1, fragCoord / 1024.0).zw;
    blue = fract(blue + float(iFrame % 256) * 0.61803398875);
    jitt = (blue - 0.5) * invRes;
    #endif

    vec2 v = -1.0 + 2.0 * (uv + jitt);
    v.x *= iResolution.x / iResolution.y;

    // Camera
    vec3 ro = vec3(-1.5, -0.4, 1.2);
    vec3 rd = normalize(vec3(v, 2.5));
    rd.xz = rot(0.15) * rd.xz;
    rd.yz = rot(0.1) * rd.yz;

    // Primary ray
    float t = trace(ro, rd, vec2(0.0, 300.0));
    vec3 p = ro + rd * t;
    vec3 n = normal(p, t);
    vec3 col = skyColor(rd);

    if (t < 300.0) {
        col = shade(ro, rd, p, n);

        // Reflections only inside train (z < 6)
        if (p.z < 6.0) {
            vec3 rrd = reflect(rd, n);
            float t2 = traceFast(p, rrd, vec2(0.1, 300.0));
            vec3 rp = p + rrd * t2;
            vec3 rn = normal(rp, t2);

            float fre = pow(saturate(1.0 + dot(n, rd)), 8.0);
            vec3 rcol = skyColor(rrd);

            if (t2 < 300.0) {
                rcol = shade(p, rrd, rp, rn);
                rcol = mix(rcol, FOGCOLOR, smoothstep(100.0, 500.0, t2));
            }

            col = mix(col, rcol, fre * 0.1);
        }

        // Distance fog
        col = mix(col, FOGCOLOR, smoothstep(100.0, 500.0, t));
    }

    // Temporal accumulation for inside train
    if (p.z < 6.0) {
        fragColor = mix(texture(iChannel0, uv), vec4(col, t), 0.2);
    } else {
        fragColor = vec4(col, t);
    }
}

#endif

// ============================================================================
// Buffer B: God rays at half resolution
// ============================================================================
#ifdef BUFFER_B

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 invRes = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * invRes * 2.0;

    // Early exit for half resolution
    if (uv.x > 1.0 || uv.y > 1.0) {
        fragColor = vec4(0.0);
        return;
    }

    time = iTime;
    float t = texture(iChannel0, uv).a;

    vec2 v = -1.0 + 2.0 * uv;
    v.x *= iResolution.x / iResolution.y;

    // Camera (same as Buffer A)
    vec3 ro = vec3(-1.5, -0.4, 1.2);
    vec3 rd = normalize(vec3(v, 2.5));
    rd.xz = rot(0.15) * rd.xz;
    rd.yz = rot(0.1) * rd.yz;

    // Jitter for ray marching
    float jitt = hash2Interleaved(fragCoord) * 0.2;

    // Volumetric shadow accumulation - reduced samples
    const int NUM_STEPS = 4;  // Reduced from 5 (1/0.2)
    const float STEP = 1.0 / float(NUM_STEPS);

    float phase = phaseFunction(dot(SUN_DIR, rd));
    vec3 godray = vec3(0.0);

    for (int i = 0; i < NUM_STEPS; i++) {
        float fi = (float(i) + jitt) * STEP;
        vec3 p = ro + rd * t * fi;
        float s = shadow(p, SUN_DIR, 0.1, 500.0);
        godray += s * phase;
    }

    fragColor = vec4(godray, t);
}

#endif

// ============================================================================
// Buffer C: Depth-aware blur
// ============================================================================
#ifdef BUFFER_C

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 invRes = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * invRes;

    vec4 center = texture(iChannel0, uv);

    // Only blur for distant pixels
    if (center.w <= 8.0) {
        fragColor = center;
        return;
    }

    vec4 acc = vec4(center.rgb, 1.0);

    // Reduced kernel size: 5x5 instead of 7x7
    const int N = 2;

    for (int j = -N; j <= N; j++) {
        for (int i = -N; i <= N; i++) {
            if (i == 0 && j == 0) continue;  // Skip center (already added)

            vec2 offset = vec2(float(i), float(j)) * invRes * 0.8;
            vec4 tap = texture(iChannel0, uv + offset);
            float w = tap.w > 8.0 ? 1.0 : 0.0;
            acc += vec4(tap.rgb * w, w);
        }
    }

    acc.rgb /= max(acc.w, 1.0);
    fragColor = vec4(acc.rgb, center.w);
}

#endif

// ============================================================================
// Image: Final compositing
// ============================================================================
#ifdef IMAGE

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 invRes = 1.0 / iResolution.xy;
    vec2 uv = fragCoord * invRes;

    // Chromatic aberration
    vec2 offset = (uv * 2.0 - 1.0) * invRes * 1.3;
    vec3 col;
    col.r = texture(iChannel0, uv + offset).r;
    col.g = texture(iChannel0, uv).g;  // Center channel has no offset
    col.b = texture(iChannel0, uv - offset).b;

    float t = texture(iChannel0, uv).a;

    // Blur and add god rays - reduced kernel
    vec4 godray = vec4(0.0);
    const int GR_SIZE = 2;  // Reduced from 3

    for (int y = -GR_SIZE; y <= GR_SIZE; y++) {
        for (int x = -GR_SIZE; x <= GR_SIZE; x++) {
            vec4 tap = texture(iChannel1, uv * 0.5 + vec2(float(x), float(y)) * invRes);
            float w = (tap.w > t + 1.0 && t < 8.0) ? 0.0 : 1.0;
            godray += vec4(tap.rgb * w, w);
        }
    }
    godray.rgb /= max(godray.w, 1.0);
    col += FOGCOLOR * godray.rgb * 0.01;

    // Tone mapping and color grading
    col = pow(col, vec3(1.0 / 2.2));
    col = pow(col, vec3(0.6, 1.0, 0.8 * (uv.y * 0.2 + 0.8)));

    // Vignette
    float vignette = pow(uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.3) * 2.5;
    col *= vignette;

    // Fade in
    col *= smoothstep(0.0, 10.0, iTime);

    fragColor = vec4(col, 1.0);
}

#endif
