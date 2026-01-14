// Common: Modeling
// Image: Rendering
// Quality restored version


// Constants/Uniforms
int ZERO;
float uTime;


/* SHELL - Optimized */

#define PI 3.1415926
#define TWO_PI 6.2831853
#define INV_TWO_PI 0.15915494
const float a_o = 0.16*PI; // half of opening angle
const float b = 0.6; // r=e^bt
const float b_inv_twopi = 0.6 * INV_TWO_PI; // precomputed b/(2*PI)

// Smoothed minimum
float s_min(float a, float b, float k) {
    float invk = -1.0/k;
    return invk*log(exp(invk*a)+exp(invk*b));
}

// Optimized cross section - takes precomputed values
float C_m(float u, float v) {
    return 1.0-(1.0-0.01*exp(sin(12.0*PI*(u+2.0*v))))*exp(-25.0*v*v);
}

float C_s(float u, float v) {
    float exp_neg16v = exp(-16.0*v);
    float _x = u - exp_neg16v;
    float u2 = u*u;
    float exp_negv = exp(-v);
    float _y = v*(1.0-0.2*exp(-4.0*sqrt(u2+0.01)))-0.5+0.5*exp_negv*sin(4.0*u)+0.2*cos(2.0*u)*exp_negv;
    float v_off = v - 1.2;
    return (sqrt(_x*_x+_y*_y)-0.55)*tanh(5.0*sqrt(2.0*u2+v_off*v_off))+0.01*sin(40.0*u)*sin(40.0*v)*exp(-(u2+v*v));
}

float C_0(float u, float v) { return abs(C_s(u,v))*C_m(u,v); }

// Optimized d_1 - single layer map
float d_1(float u, float v, float s_d, float r) {
    float n = log(r)/b + 2.0;
    float a = atan(v,u)/a_o;
    float n_adj = n > 0.0 ? n - s_d : fract(n) - s_d;
    return 0.5*r*C_0(n_adj, a);
}

// Result cross section
float C(float u, float v) {
    float r = sqrt(u*u+v*v);
    return min(d_1(u,v,0.5,r), d_1(u,v,1.5,r));
}

// Optimized spiral shell SDF - cache common calculations
float S_optimized(float x, float y, float z) {
    float x2 = x*x;
    float y2 = y*y;
    float r_xy = sqrt(x2+y2);
    float atan_yx = atan(y,x);
    float l_p = exp(b_inv_twopi * atan_yx); // l_p(x,y)

    float neg_z = -z;
    float log_negz = log(neg_z);

    // U and V for body
    float U_val = exp(log_negz + b_inv_twopi * atan_yx);
    float V_val = r_xy * l_p;

    // Body SDF
    float S_s = C(U_val, V_val) / l_p;

    // Opening SDF
    float exp_negb2 = exp(-b*0.5);
    float exp_b2 = exp(b*0.5);
    float C_open = C(exp(log_negz - b*0.5), -x*exp_negb2);
    float S_o = sqrt(C_open*C_open*exp_b2*exp_b2 + y2);

    // Tip SDF
    float S_t = d_1(neg_z, r_xy, 0.5, sqrt(neg_z*neg_z + r_xy*r_xy));

    // Combine body+tip
    float S_a = neg_z > 0.0 ? min(S_s, S_o) : S_t;

    // Subtract thickness
    float r2 = x2 + y2 + z*z;
    float S_0 = S_a - 0.01 - 0.01*pow(r2, 0.4)
        - 0.02*r_xy*exp(cos(8.0*atan_yx))
        - 0.007*(0.5-0.5*tanh(10.0*(z+1.0+8.0*sqrt(3.0*x2+y2))));

    // Clip bottom
    float S_r = -s_min(-S_0, z+1.7, 10.0);

    // Rod thickening
    float r_a = -0.1*sin(3.0*z)*tanh(2.0*(x2+y2-z-1.5));

    // Final transform - inline the offset
    float fx = x - r_a*y;
    float fy = y + r_a*x;
    float fz = z - 0.8;

    // Recompute for transformed coords
    float fx2 = fx*fx;
    float fy2 = fy*fy;
    float fr_xy = sqrt(fx2+fy2);
    float fatan_yx = atan(fy,fx);
    float fl_p = exp(b_inv_twopi * fatan_yx);

    float fneg_z = -fz;

    if (fneg_z <= 0.0) {
        // Tip
        return d_1(fneg_z, fr_xy, 0.5, sqrt(fneg_z*fneg_z + fr_xy*fr_xy));
    }

    float flog_negz = log(fneg_z);
    float fU_val = exp(flog_negz + b_inv_twopi * fatan_yx);
    float fV_val = fr_xy * fl_p;
    float fS_s = C(fU_val, fV_val) / fl_p;

    float fexp_negb2 = exp(-b*0.5);
    float fexp_b2 = exp(b*0.5);
    float fC_open = C(exp(flog_negz - b*0.5), -fx*fexp_negb2);
    float fS_o = sqrt(fC_open*fC_open*fexp_b2*fexp_b2 + fy2);

    float fS_a = min(fS_s, fS_o);

    float fr2 = fx2 + fy2 + fz*fz;
    float fS_0 = fS_a - 0.01 - 0.01*pow(fr2, 0.4)
        - 0.02*fr_xy*exp(cos(8.0*fatan_yx))
        - 0.007*(0.5-0.5*tanh(10.0*(fz+1.0+8.0*sqrt(3.0*fx2+fy2))));

    return -s_min(-fS_0, fz+1.7, 10.0);
}


// Rotation matrices - use precomputed sin/cos
const float cos_rz = 0.92387953; // cos(0.125*PI)
const float sin_rz = 0.38268343; // sin(0.125*PI)
const float cos_rx = 0.79015501; // cos(0.38*PI)
const float sin_rx = 0.61283112; // sin(0.38*PI)

vec3 transformShell(vec3 p) {
    vec3 q = 0.7*p - vec3(0.0, 0.0, 0.26);
    // rotz
    float tx = cos_rz*q.x + sin_rz*q.y;
    float ty = -sin_rz*q.x + cos_rz*q.y;
    // rotx
    float tz = cos_rx*q.z + sin_rx*ty;
    ty = -sin_rx*q.z + cos_rx*ty;
    return vec3(tx, ty, tz);
}

// Returns the SDF of the shell
float mapShell(vec3 p) {
    vec3 q = transformShell(p);

    // Bounding box check
    float qz2 = q.z*q.z;
    float exp_qz2 = exp(qz2);
    float inv_exp_qz2 = 1.0/exp_qz2;
    vec3 scaled1 = vec3(1.2*inv_exp_qz2, 1.4*inv_exp_qz2, 1.0);
    float bound = length(scaled1*q)*inv_exp_qz2 - 1.0;
    bound = max(bound, length(vec3(1.2,1.4,1.0)*(q+vec3(0.0,0.1,0.0)))-1.0);

    float boundw = 0.2;
    if (bound > 0.0) return bound+boundw;

    float v = S_optimized(q.x, q.y, q.z);

    // Gradient/discontinuity fixes
    float len_xy = length(vec2(4.0*q.x, 4.0*q.y));
    float k = 1.0 - 0.9/(len_xy*0.25 + abs(q.z+0.7) + 1.0);
    k = 0.7*mix(k, 1.0, clamp(10.0*max(-q.x, q.z-0.7*q.x+0.5), 0.0, 1.0));
    v = k*v/0.7;

    v = mix(v, bound+boundw, smoothstep(0.0, 1.0, (bound+boundw)/boundw));
    return min(v, 0.1);
}

// Numerical gradient of the shell SDF
vec3 gradShell(vec3 p) {
    const float h = 0.0005;
    // Tetrahedron gradient
    vec3 e0 = vec3(1,1,1);
    vec3 e1 = vec3(1,-1,-1);
    vec3 e2 = vec3(-1,1,-1);
    vec3 e3 = vec3(-1,-1,1);
    return (e0*mapShell(p+e0*h) + e1*mapShell(p+e1*h) +
            e2*mapShell(p+e2*h) + e3*mapShell(p+e3*h)) * (0.25/h);
}

// Color calculated from position and gradient
vec3 albedoShell(vec3 p, vec3 g) {
    float glen = length(g);
    float t = 0.5-0.5*cos(2.0*log(0.6*glen));
    t += 0.05*sin(40.0*p.x)*sin(40.0*p.y)*sin(20.0*p.z);
    vec3 col = mix(vec3(0.9,0.9,0.85), vec3(0.75,0.55,0.3), t);
    return min(1.2*col, vec3(1.0));
}


/* NOISE - Optimized */

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

float GradientNoise2D(vec2 xy) {
    vec2 i = floor(xy);
    vec2 f = xy - i;

    // Quintic interpolation
    vec2 f3 = f*f*f;
    vec2 f2 = f*f;
    vec2 w = f3 * (10.0 + f * (-15.0 + f * 6.0));

    vec2 i1 = i + vec2(1.0, 0.0);
    vec2 i2 = i + vec2(0.0, 1.0);
    vec2 i3 = i + vec2(1.0, 1.0);

    float v00 = dot(2.0 * hash22(i) - 1.0, f);
    float v10 = dot(2.0 * hash22(i1) - 1.0, f - vec2(1.0, 0.0));
    float v01 = dot(2.0 * hash22(i2) - 1.0, f - vec2(0.0, 1.0));
    float v11 = dot(2.0 * hash22(i3) - 1.0, f - vec2(1.0, 1.0));

    return v00 + (v10-v00)*w.x + (v01-v00)*w.y + (v00+v11-v01-v10)*w.x*w.y;
}


/* SEA + BEACH - Optimized */

vec4 smin(vec4 a, vec4 b, float k) {
    float h = clamp(0.5 + 0.5 * (b.x - a.x) / k, 0.0, 1.0);
    float d = mix(b.x, a.x, h) - k * h * (1.0 - h);
    return vec4(d, mix(b.yzw, a.yzw, h));
}

vec4 mapGround(vec3 p) {
    float time = 0.25*PI*uTime;
    float sinTime = sin(time);
    float cosTime = cos(time);

    float beach = 0.4*tanh(0.2*p.y) - 0.2*GradientNoise2D(0.5*p.xy);

    // Shell pit
    float pitDist = length(vec2(1.4,1.0)*p.xy - vec2(-0.2,-0.2)) - 0.5;
    beach *= smoothstep(0.0, 1.0, 0.5*(1.0+exp(0.3*p.x)) * pitDist);

    float sea = -0.2 + 0.1*exp(sinTime);

    float beachSeaDiff = beach - sea;
    float seaBeachDiff = sea - beach;

    // Sea wave
    float waveFade = 0.005*tanh(2.0*max(seaBeachDiff, 0.0));
    sea += waveFade * sin(10.0*(p.x-uTime-sin(p.y))) * sin(10.0*(p.y+uTime-sin(p.x)));

    // Sand grains
    float grainFade = 0.005*tanh(5.0*max(beachSeaDiff, 0.0));
    beach += grainFade * GradientNoise2D(50.0*p.xy);

    // Recalculate after modifications
    beachSeaDiff = beach - sea;
    seaBeachDiff = sea - beach;

    // Sea color
    vec3 seacol = mix(vec3(0.65,0.85,0.8), vec3(0.2,0.55,0.45), smoothstep(0.0, 1.0, -0.1*p.y));
    seacol = mix(vec3(1.0), seacol, clamp(4.0*seaBeachDiff, 0.0, 1.0));
    seacol = mix(vec3(1.1), seacol, clamp(20.0*seaBeachDiff, 0.0, 1.0));

    // Beach color
    vec3 beachcol = mix(vec3(0.7,0.7,0.6), vec3(0.9,0.85,0.8), clamp(5.0*beachSeaDiff, 0.0, 1.0));

    vec4 ground = smin(vec4(-sea, seacol), vec4(-beach, beachcol), 0.01-0.005*cosTime);
    return vec4(p.z+ground.x, min(ground.yzw, 1.0));
}

vec3 gradGround(vec3 p) {
    const float h = 0.002;
    vec3 e0 = vec3(1,1,1);
    vec3 e1 = vec3(1,-1,-1);
    vec3 e2 = vec3(-1,1,-1);
    vec3 e3 = vec3(-1,-1,1);
    return (e0*mapGround(p+e0*h).x + e1*mapGround(p+e1*h).x +
            e2*mapGround(p+e2*h).x + e3*mapGround(p+e3*h).x) * (0.25/h);
}


/* CONCH INTERSECTION - Optimized */

bool boxIntersection(float offset, vec3 ro, vec3 rd, out float tn, out float tf) {
    vec3 ro_t = ro - vec3(-0.1, 0.1, 0.6);
    vec3 inv_rd = 1.0 / rd;
    vec3 n = inv_rd * ro_t;
    vec3 k = abs(inv_rd) * (vec3(0.9, 1.3, 0.7) + offset);
    vec3 t1 = -n - k;
    vec3 t2 = -n + k;
    tn = max(max(t1.x, t1.y), t1.z);
    tf = min(min(t2.x, t2.y), t2.z);
    return tn <= tf;
}

bool intersectConch(vec3 ro, vec3 rd, inout float t, float tf, float eps) {
    float t0, t1;
    if (!boxIntersection(0.0, ro, rd, t0, t1)) return false;
    t1 = min(t1, tf);
    if (t1 < t0) return false;
    t = t0;

    float v0 = 0.0, v, dt;
    for (int i=ZERO; i<120; i++) {
        v = mapShell(ro+rd*t);
        if (v*v0 < 0.0) {
            t -= dt * v/(v-v0);
            return true;
        }
        dt = max(abs(v), eps*0.5);
        t += dt;
        if (t > t1) return false;
        v0 = v;
    }
    return true;
}

float calcShadow(vec3 ro, vec3 rd) {
    float t0, t1;
    if (!boxIntersection(0.2, ro, rd, t0, t1)) return 1.0;

    float sh = 1.0;
    float t = max(t0, 0.01) + 0.01*hash22(rd.xy).x;

    for (int i=ZERO; i<64; i++) {
        float h = 0.8*mapShell(ro + rd*t);
        sh = min(sh, smoothstep(0.0, 1.0, 32.0*h/t));
        t += clamp(h, 0.01, 0.3);
        if (h < 0.0) return 0.0;
        if (t > t1) break;
    }
    return max(sh, 0.0);
}


/* BEACH INTERSECTION */

bool intersectBeach(vec3 ro, vec3 rd, out float t, float tf) {
    t = 0.01;
    float v0 = 0.0, v, dt;
    for (int i = ZERO; i < 80; i++) {
        if (t > tf) return false;
        v = mapGround(ro+rd*t).x;
        if (v*v0 < 0.0) break;
        dt = (i == ZERO) ? v : dt*v/abs(v-v0);
        dt = sign(dt)*clamp(abs(dt), 0.01, 0.8);
        t += dt;
        v0 = v;
    }
    t -= dt * clamp(v/(v-v0), 0.0, 1.0);
    return true;
}


/* SKY */

vec3 sundir = normalize(vec3(0.3, 0.3, 1.0));

vec3 getSkyCol(vec3 rd) {
    vec3 rd_n = normalize(vec3(rd.xy, max(rd.z, 0.0)));
    vec3 sky = mix(vec3(0.8,0.9,1.0), vec3(0.3,0.6,0.9), rd_n.z);
    float sunDot = max(dot(rd_n, sundir), 0.0);
    vec3 sun = 1.5*vec3(0.95,0.9,0.5)*pow(sunDot, 8.0);
    return sky + sun;
}


/* MAIN */

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ZERO = min(iFrame, 0);
    uTime = iTime;

    // Camera setup - precompute trig
    float rx, rz;
    if (iMouse.z != 0.0) {
        rx = 1.65*(iMouse.y/iResolution.y) - 0.05;
        rz = -iMouse.x/iResolution.x * 4.0 * PI;
    } else {
        rx = 0.12;
        rz = 0.5;
    }

    float cos_rx = cos(rx), sin_rx = sin(rx);
    float cos_rz = cos(rz), sin_rz = sin(rz);

    vec3 w = vec3(cos_rx*cos_rz, cos_rx*sin_rz, sin_rx);
    vec3 u = vec3(-sin_rz, cos_rz, 0.0);
    vec3 v = cross(w, u);
    vec3 ro = vec3(0.0, 0.0, 0.5) + 6.0*w - 0.5*u + 0.2*v;

    vec2 uv = 2.0*fragCoord.xy/iResolution.xy - vec2(1.0);
    float focal = 2.0*length(iResolution.xy);
    vec3 rd = normalize(mat3(u, v, -w) * vec3(uv*iResolution.xy, focal));

    // Ray intersection
    float t, t1 = 40.0;
    int intersect_id = -1;
    if (intersectBeach(ro, rd, t, t1)) { intersect_id = 0; t1 = t; }
    if (intersectConch(ro, rd, t, t1, 0.005)) { intersect_id = 1; t1 = t; }
    t = t1;

    // Shading
    vec3 p = ro + rd*t;
    vec3 col;
    float shadow = calcShadow(p, sundir);

    if (intersect_id == -1) {
        col = vec3(1.0);
    }
    else if (intersect_id == 0) {
        vec3 n = normalize(gradGround(p));
        vec3 albedo = mapGround(p).yzw;
        vec3 amb = 0.2*albedo;
        float ndotl = max(dot(n, sundir), 0.0);
        vec3 dif = 0.6*(0.3+0.7*shadow) * ndotl * albedo;

        vec3 refl = reflect(rd, n);
        float t_refl = 2.0;
        vec3 spc = intersectConch(p, refl, t_refl, 2.0, 0.01)
            ? vec3(0.05, 0.045, 0.04)
            : vec3(0.2-0.1*tanh(0.5*p.y)) * getSkyCol(refl);
        col = amb + dif + spc;
    }
    else {
        vec3 n0 = gradShell(p);
        vec3 n = normalize(n0);
        vec3 albedo = albedoShell(p, n0);
        float ndotr = dot(rd, n);
        vec3 amb = (0.4-0.1*ndotr)*albedo;
        float ndotl = max(dot(n, sundir), 0.0);
        vec3 dif = albedo*(vec3(0.45,0.4,0.35)*ndotl*shadow + vec3(0.2,0.3,0.4)*max(n.z, 0.0));
        vec3 refl = reflect(rd, n);
        float fresnel = pow(1.0 - abs(ndotr), 3.0);
        vec3 spc = 0.15 * fresnel * getSkyCol(refl);
        col = pow(amb+dif+spc, vec3(0.8));
    }

    // Fog and sky blend
    col = mix(getSkyCol(rd), col, exp(-0.04*max(t-5.0, 0.0)));
    float sunHaze = pow(max(dot(rd, sundir), 0.0), 1.5);
    col += 0.5*vec3(0.8, 0.5, 0.6)*sunHaze;
    col = pow(0.95*col, vec3(1.25));

    fragColor = vec4(col, 1.0);
}
