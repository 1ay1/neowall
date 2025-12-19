// Train Journey - Optimized Single Pass Version
// Original by @leon - https://www.shadertoy.com/view/wdyfWy
// Converted to single-pass for NeoWall

#define PI 3.14159265
#define saturate(x) clamp(x,0.,1.)
#define SUNDIR normalize(vec3(0.2,.3,2.))
#define FOGCOLOR vec3(1.,.2,.1)

float time;

// ============== Hash Functions ==============

float hash(float p) {
    return fract(sin(p)*43758.5453123);
}

vec3 hash3(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    uvec3 k = n * uvec3(n, n*16807U, n*48271U);
    return vec3(k & uvec3(0x7fffffffU)) / float(0x7fffffff);
}

// ============== Utility Functions ==============

mat2 rot(float v) {
    float a = cos(v);
    float b = sin(v);
    return mat2(a,-b,b,a);
}

float smin(float a, float b, float k) {
    float h = max(k-abs(a-b),0.0);
    return min(a, b) - h*h*0.25/k;
}

float smax(float a, float b, float k) {
    k *= 1.4;
    float h = max(k-abs(a-b),0.0);
    return max(a, b) + h*h*h/(6.0*k*k);
}

float box(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b;
    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;
}

float capsule(vec3 p, float h, float r) {
    p.x -= clamp(p.x, 0.0, h);
    return length(p) - r;
}

// ============== Scene Objects ==============

float train(vec3 p) {
    // base
    float d = abs(box(p-vec3(0., 0., 0.), vec3(100.,1.5,5.), 0.))-.1;

    // windows
    d = smax(d, -box(p-vec3(1.,0.25,5.), vec3(2.,.5,0.0), .3), 0.03);
    d = smax(d, -box(p-vec3(-3.,0.25,5.), vec3(.2,.5,0.0), .3), 0.03);
    d = smin(d,  box(p-vec3(1.,0.57,5.), vec3(5.3,.05,0.1), .0), 0.001);

    // seats (simplified)
    p.x = mod(p.x-.8,2.)-1.;
    p.z = abs(p.z-4.3)-.3;
    d = smin(d, box(p-vec3(0.,-1., 0.), vec3(.3,.1,.2),.05), 0.05);
    d = smin(d, box(p-vec3(0.4,-0.38, 0.), vec3(.1,.7,.2),.05), 0.1);

    return d;
}

float catenary(vec3 p) {
    p.z -= 12.;
    vec3 pp = p;
    p.x = mod(p.x,10.)-5.;

    // base poles with cross arm
    float d = box(p-vec3(0.,0.,0.), vec3(.0,3.,.0), .1);
    d = smin(d, box(p-vec3(0.,2.,0.), vec3(.0,0.,1.), .1), 0.05);

    p.z = abs(p.z-0.)-2.;
    d = smin(d, box(p-vec3(0.,2.2,-1.), vec3(.0,0.2,0.), .1), 0.01);

    // power lines with catenary sag
    pp.z = abs(pp.z-0.)-2.;
    float sag = abs(cos(pp.x*.1*PI));

    // main contact wire + messenger wire
    d = min(d, capsule(p-vec3(-5.,2.4-sag,-1.),10000.,.022));
    d = min(d, capsule(p-vec3(-5.,2.85-sag*0.6,-1.),10000.,.015));
    d = min(d, capsule(p-vec3(-5.,2.5-sag,-2.),10000.,.02));

    return d;
}

float city(vec3 p) {
    vec3 pp = p;
    ivec2 pId = ivec2((p.xz)/30.);
    vec3 rnd = hash3(uint(pId.x + pId.y*1000));
    p.xz = mod(p.xz, vec2(30.))-15.;
    float h = 5.+float(pId.y-3)*5.+rnd.x*20.;
    float offset = (rnd.z*2.-1.)*10.;
    float d = box(p-vec3(offset,-5.,0.), vec3(5.,h,5.), 0.1);
    d = min(d, box(p-vec3(offset,-5.,0.), vec3(1.,h+pow(rnd.y,4.)*10.,1.), 0.1));
    d = max(d,-pp.z+100.);
    d = max(d,pp.z-300.);

    return d*.6;
}

float map(vec3 p) {
    float d = train(p);
    p.x -= mix(0.,time*15., saturate(time*.02));
    d = min(d, catenary(p));
    d = min(d, city(p));
    d = min(d, city(p+15.));
    return d;
}

// ============== Raymarching ==============

float trace(vec3 ro, vec3 rd, vec2 nearFar) {
    float t = nearFar.x;
    for(int i=0; i<64; i++) {
        float d = map(ro+rd*t);
        t += d;
        if(abs(d) < 0.005 || t > nearFar.y)
            break;
    }
    return t;
}

vec3 normal(vec3 p) {
    vec2 eps = vec2(0.02, 0.);
    float d = map(p);
    vec3 n;
    n.x = d - map(p-eps.xyy);
    n.y = d - map(p-eps.yxy);
    n.z = d - map(p-eps.yyx);
    return normalize(n);
}

// ============== Lighting ==============

float ambientOcclusion(vec3 p, vec3 n) {
    float ao = 0.0;
    float scale = 1.0;
    for(int i=1; i<=4; i++) {
        float dist = 0.15 * float(i);
        float d = map(p + n * dist);
        ao += (dist - d) * scale;
        scale *= 0.5;
    }
    return clamp(1.0 - ao * 2.0, 0.0, 1.0);
}

vec3 skyColor(vec3 rd) {
    vec3 col = FOGCOLOR;
    col += vec3(1.,.3,.1)*1. * pow(max(dot(rd,SUNDIR),0.),30.);
    col += vec3(1.,.3,.1)*10. * pow(max(dot(rd,SUNDIR),0.),10000.);
    return col;
}

vec3 shade(vec3 p, vec3 n) {
    vec3 diff = vec3(1.,.5,.3) * max(dot(n,SUNDIR),0.);
    vec3 amb = vec3(0.1,.15,.2) * ambientOcclusion(p, n);
    vec3 col = (diff*0.3 + amb*.3)*.025;
    return col;
}

// ============== God Rays (Simplified) ==============

float computeGodRays(vec3 ro, vec3 rd, float maxDist) {
    const float k = .9;
    float phase = (1.0 - k * k) / (4.0 * PI * pow(1.0 + k * k - 2.0 * k * dot(SUNDIR, rd), 1.5));
    float godray = 0.0;

    // Simple 3-step volumetric approximation
    for(float i = 0.25; i < 1.0; i += 0.25) {
        vec3 p = ro + rd * maxDist * i;
        float d = map(p + SUNDIR * 3.0);
        godray += step(1.0, d) * phase;
    }
    return godray * 0.33;
}

// ============== Main ==============

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    time = iTime;
    vec2 invRes = vec2(1.) / iResolution.xy;
    vec2 uv = fragCoord * invRes;

    vec2 v = -1.0 + 2.0 * uv;
    v.x *= iResolution.x / iResolution.y;

    // Camera
    vec3 ro = vec3(-1.5, -.4, 1.2);

    // Train shake - combined for efficiency
    float shakeY = sin(time * 15.7) * 0.002 + sin(time * 8.3) * 0.0015;
    float shakeX = sin(time * 4.1) * 0.0012;

    // Random track joints (simplified)
    float jointBump = pow(max(0., sin(fract(time * 0.4) * 6.28)), 16.0) * step(0.65, fract(sin(floor(time * 0.4) * 127.1) * 43758.5453)) * 0.006;

    ro.y += shakeY + jointBump;
    ro.x += shakeX;

    vec3 rd = normalize(vec3(v, 2.5));
    rd.xz = rot(.15 + sin(time * 6.5) * 0.001) * rd.xz;
    rd.yz = rot(.1) * rd.yz;

    // Trace main ray
    float t = trace(ro, rd, vec2(0., 300.));
    vec3 p = ro + rd * t;
    vec3 n = normal(p);
    vec3 col = skyColor(rd);

    if (t < 300.) {
        col = shade(p, n);

        // Reflections for train interior (window glass)
        if (p.z < 6.) {
            vec3 rrd = reflect(rd, n);
            float t2 = trace(p + rrd * 0.1, rrd, vec2(0., 150.));
            float fre = pow(saturate(1.0 + dot(n, rd)), 8.0);
            vec3 rcol = skyColor(rrd);

            if (t2 < 150.) {
                vec3 rp = p + rrd * t2;
                vec3 rn = normal(rp);
                rcol = shade(rp, rn);
                rcol = mix(rcol, FOGCOLOR, smoothstep(50., 150., t2));
            }
            col = mix(col, rcol, fre * .1);
        }

        // Distance fog
        col = mix(col, FOGCOLOR, smoothstep(100., 500., t));
    }

    // God rays
    float godray = computeGodRays(ro, rd, min(t, 80.));
    col += FOGCOLOR * godray * 0.02;

    // ============== Post Processing ==============

    // Tone mapping
    col = pow(col, vec3(1./2.2));

    // Color grading - warm sunset feel
    col = pow(col, vec3(.6, 1., .8 * (uv.y * .2 + .8)));

    // Chromatic aberration
    float caStrength = length(uv - 0.5) * 0.015;
    col.r *= (1.0 + caStrength);
    col.b *= (1.0 - caStrength);

    // Vignetting
    float vignetting = pow(uv.x * uv.y * (1. - uv.x) * (1. - uv.y), .3) * 2.5;
    col *= vignetting;

    // Fade in at start
    col *= smoothstep(0., 10., iTime);

    fragColor = vec4(col, 1.0);
}
