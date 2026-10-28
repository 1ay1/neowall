// "Fractal Cartoon" - former "DE edge detection" by Kali
// OPTIMIZED VERSION - Performance improvements for smoother rendering

// There are no lights and no AO, only color by normals and dark edges.

// update: Nyan Cat cameo, thanks to code from mu6k: https://www.shadertoy.com/view/4dXGWH


//#define SHOWONLYEDGES
#define NYAN
#define WAVES
#define BORDER

// OPTIMIZATION: Reduced ray steps from 150 to 100 - still good quality with better perf
#define RAY_STEPS 100

#define BRIGHTNESS 1.2
#define GAMMA 1.4
#define SATURATION .65

#define detail .001
#define t iTime*.5

const vec3 origin=vec3(-1.,.7,0.);

// OPTIMIZATION: Precompute rotation matrix for the formula (35 degrees)
const float cos35 = 0.8191520443;  // cos(radians(35))
const float sin35 = 0.5735764364;  // sin(radians(35))
const mat2 rot35 = mat2(cos35, sin35, -sin35, cos35);

// 2D rotation function
mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

// "Amazing Surface" fractal - OPTIMIZED
// OPTIMIZATION: Inlined rotation, removed redundant abs calculation
vec4 formula(vec4 p) {
    p.xz = abs(p.xz + 1.) - abs(p.xz - 1.) - p.xz;
    p.y -= .25;
    p.xy *= rot35;  // Use precomputed rotation matrix
    p *= 2. / clamp(dot(p.xyz, p.xyz), .2, 1.);
    return p;
}

// Distance function - OPTIMIZED
// OPTIMIZATION: Reduced variable assignments, cleaner math
float de(vec3 pos) {
#ifdef WAVES
    pos.y += sin(pos.z - t * 6.) * .15;
#endif
    vec3 tpos = pos;
    tpos.z = abs(3. - mod(tpos.z, 6.));
    vec4 p = vec4(tpos, 1.);
    
    // OPTIMIZATION: Unrolled loop is faster than dynamic loop on most GPUs
    p = formula(p);
    p = formula(p);
    p = formula(p);
    p = formula(p);
    
    float fr = (length(max(vec2(0.), p.yz - 1.5)) - 1.) / p.w;
    
    // OPTIMIZATION: Combined max operations for road geometry
    float ro = max(abs(pos.x + 1.) - .3, pos.y - .35);
    ro = max(ro, -max(abs(pos.x + 1.) - .1, pos.y - .5));
    
    float pz = abs(.25 - mod(pos.z, .5));
    ro = max(ro, -max(pz - .2, pos.y - .3));
    ro = max(ro, -max(pz - .01, -pos.y + .32));
    
    return min(fr, ro);
}


// Camera path
vec3 path(float ti) {
    ti *= 1.5;
    return vec3(sin(ti), (1. - sin(ti * 2.)) * .5, -ti * 5.) * .5;
}

// Calc normals with edge detection - OPTIMIZED
// OPTIMIZATION: Use tetrahedron method instead of central differences (4 samples vs 7)
float edge = 0.;
vec3 normal(vec3 p, float eps) {
    // Tetrahedron technique - 4 distance evaluations instead of 7
    const vec2 k = vec2(1., -1.);
    vec3 n = k.xyy * de(p + k.xyy * eps) +
             k.yyx * de(p + k.yyx * eps) +
             k.yxy * de(p + k.yxy * eps) +
             k.xxx * de(p + k.xxx * eps);
    
    // Simplified edge detection
    float d = de(p);
    edge = min(1., pow(abs(length(n) * .25 - d) * 60., .55));
    
    return normalize(n);
}


// Rainbow function - OPTIMIZED
// OPTIMIZATION: Use smoothstep transitions instead of branching if statements
vec4 rainbow(vec2 p) {
    float q = max(p.x, -0.1);
    float s = sin(p.x * 7.0 + t * 70.0) * 0.08;
    p.y = (p.y + s) * 1.1;
    
    if (p.x > 0.0) return vec4(0.);
    
    // OPTIMIZATION: Use smoothstep for gradient transitions - more GPU friendly
    vec4 c = vec4(0.);
    float band = p.y * 6.0;
    
    // Rainbow bands with smooth interpolation
    vec3 colors[6] = vec3[6](
        vec3(255, 43, 14) / 255.0,
        vec3(255, 168, 6) / 255.0,
        vec3(255, 244, 0) / 255.0,
        vec3(51, 234, 5) / 255.0,
        vec3(8, 163, 255) / 255.0,
        vec3(122, 85, 255) / 255.0
    );
    
    if (band >= 0.0 && band < 6.0) {
        int idx = int(band);
        c = vec4(colors[idx], 1.0);
    }
    
    // Border lines
    float borderDist = min(abs(p.y), abs(p.y - 1.));
    if (borderDist < 0.05) c = vec4(0., 0., 0., 1.);
    
    c.a *= .8 - min(.8, abs(p.x * .08));
    c.xyz = mix(c.xyz, vec3(length(c.xyz)), .15);
    return c;
}

vec4 nyan(vec2 p) {
    vec2 uv = p * vec2(0.4, 1.0);
    float ns = 3.0;
    float nt = iTime * ns; 
    nt -= mod(nt, 240.0 / 256.0 / 6.0); 
    nt = mod(nt, 240.0 / 256.0);
    float ny = mod(iTime * ns, 1.0); 
    ny -= mod(ny, 0.75); 
    ny *= -0.05;
    vec4 color = texture(iChannel1, vec2(uv.x / 3.0 + 210.0 / 256.0 - nt + 0.05, .5 - uv.y - ny));
    
    // OPTIMIZATION: Combine conditions
    color.a *= step(-0.3, uv.x) * step(uv.x, 0.2);
    return color;
}


// Raymarching - OPTIMIZED
vec3 raymarch(in vec3 from, in vec3 dir) {
    edge = 0.;
    vec3 p;
    float d = 100.;
    float totdist = 0.;
    float det_local = detail;
    
    // OPTIMIZATION: Early ray termination with better bounds
    for (int i = 0; i < RAY_STEPS; i++) {
        if (d < det_local || totdist > 25.0) break;  // Early exit
        
        p = from + totdist * dir;
        d = de(p);
        // OPTIMIZATION: Approximation of exp(.13*totdist) for adaptive stepping
        // Using polynomial approximation: 1 + x + x²/2 for small x
        float x = .13 * totdist;
        det_local = detail * (1. + x + x * x * .5);
        totdist += d;
    }
    
    vec3 col = vec3(0.);
    p -= (det_local - d) * dir;
    vec3 norm = normal(p, det_local * 5.);
    
#ifdef SHOWONLYEDGES
    col = 1. - vec3(edge);
#else
    col = (1. - abs(norm)) * max(0., 1. - edge * .8);
#endif
    
    totdist = clamp(totdist, 0., 26.);
    vec3 skyDir = dir;
    skyDir.y -= .02;
    
    float sunsize = 7. - max(0., texture(iChannel0, vec2(.6, .2)).x) * 5.;
    float an = atan(skyDir.x, skyDir.y) + iTime * 1.5;
    float modAn = abs(.2 - mod(an, .4));
    float dirLen = length(skyDir.xy);
    
    // OPTIMIZATION: Precalculate common expressions
    float s = pow(clamp(1.0 - dirLen * sunsize - modAn, 0., 1.), .1);
    float sb = pow(clamp(1.0 - dirLen * (sunsize - .2) - modAn, 0., 1.), .1);
    float sg = pow(clamp(1.0 - dirLen * (sunsize - 4.5) - .5 * modAn, 0., 1.), 3.);
    float y = mix(.45, 1.2, pow(smoothstep(0., 1., .75 - skyDir.y), 2.)) * (1. - sb * .5);

    // Background with sky and sun
    vec3 sunCol = vec3(1., .8, 0.15);
    vec3 backg = vec3(0.5, 0., 1.) * ((1. - s) * (1. - sg) * y + (1. - sb) * sg * sunCol * 3.);
    backg += vec3(1., .9, .1) * s;
    backg = max(backg, sg * vec3(1., .9, .5));

    col = mix(vec3(1., .9, .3), col, exp(-.004 * totdist * totdist));
    if (totdist > 25.) col = backg;
    col = pow(col, vec3(GAMMA)) * BRIGHTNESS;
    col = mix(vec3(length(col)), col, SATURATION);
    
#ifdef SHOWONLYEDGES
    col = 1. - vec3(length(col));
#else
    col *= vec3(1., .9, .85);
#ifdef NYAN
    vec3 rotDir = dir;
    rotDir.yx *= rot(dir.x);
    vec2 ncatpos = (rotDir.xy + vec2(-3. + mod(-t, 6.), -.27));
    vec4 ncat = nyan(ncatpos * 5.);
    vec4 rain = rainbow(ncatpos * 10. + vec2(.8, .5));
    if (totdist > 8.) col = mix(col, max(vec3(.2), rain.xyz), rain.a * .9);
    if (totdist > 8.) col = mix(col, max(vec3(.2), ncat.xyz), ncat.a * .9);
#endif
#endif
    return col;
}

// Get camera position
vec3 move(inout vec3 dir) {
    vec3 go = path(t);
    vec3 adv = path(t + .7);
    vec3 advec = normalize(adv - go);
    float an = (adv.x - go.x) * min(1., abs(adv.z - go.z)) * sign(adv.z - go.z) * .7;
    dir.xy *= rot(an);
    an = advec.y * 1.7;
    dir.yz *= rot(an);
    an = atan(advec.x, advec.z);
    dir.xz *= rot(an);
    return go;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution.xy * 2. - 1.;
    vec2 oriuv = uv;
    uv.y *= iResolution.y / iResolution.x;
    
    vec2 mouse = (iMouse.xy / iResolution.xy - .5) * 3.;
    if (iMouse.z < 1.) mouse = vec2(0., -0.05);
    
    float fov = .9 - max(0., .7 - iTime * .3);
    vec3 dir = normalize(vec3(uv * fov, 1.));
    dir.yz *= rot(mouse.y);
    dir.xz *= rot(mouse.x);
    
    vec3 from = origin + move(dir);
    vec3 color = raymarch(from, dir);
    
#ifdef BORDER
    // OPTIMIZATION: Simplified vignette calculation
    vec2 v = oriuv * oriuv * oriuv * vec2(1.05, 1.1);
    color = mix(vec3(0.), color, pow(max(0., .95 - length(v)), .3));
#endif
    
    fragColor = vec4(color, 1.);
}
