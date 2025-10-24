// Common: Modeling
// Image: Rendering


// Constants/Uniforms
int ZERO;
float uTime;


/* SHELL */

// Functional shell manually exported from this "graphing calculator":
// https://harry7557558.github.io/tools/raymarching-implicit/index.html
#define _uv float u, float v
#define _xyz float x, float y, float z
#define PI 3.1415926
const float a_o = 0.16*PI; // half of opening angle
const float b = 0.6; // r=e^bt
float s_min(float a, float b, float k) { return -1./k*log(exp(-k*a)+exp(-k*b)); } // smoothed minimum

// Cross section
float C_m(_uv) { return 1.-(1.-0.01*exp(sin(12.*PI*(u+2.*v))))*exp(-5.*v*5.*v); } // mid rod
float C_s(_uv) { // basic cross section
    float _x = u-exp(-16.*v);
    float _y = v*(1.-0.2*exp(-4.*sqrt(u*u+.1*.1)))-0.5+0.5*exp(-v)*sin(4.*u)+.2*cos(2.*u)*exp(-v);
    return (sqrt(_x*_x+_y*_y)-0.55)*tanh(5.*sqrt(2.*u*u+(v-1.2)*(v-1.2)))+.01*sin(40.*u)*sin(40.*v)*exp(-(u*u+v*v));
}
float C_0(_uv) { return abs(C_s(u,v))*C_m(u,v); } // single layer
float n_1(_uv) { return log(sqrt(u*u+v*v))/b+2.; } // index of layer
float a_1(_uv) { return atan(v,u)/a_o; } // opening angle, 0-1
float d_1(_uv, float s_d) { // map to layer
    float n = n_1(u,v);
    return 0.5*sqrt(u*u+v*v)*C_0(n>0.?n-s_d:fract(n)-s_d,a_1(u,v));
}
float C(_uv) { return min(d_1(u,v,0.5),d_1(u,v,1.5)); } // result cross section

// Spiral
float l_p(float x, float y) { return exp(b*atan(y,x)/(2.*PI)); } // a multiplying factor
float U(_xyz) { return exp(log(-z)+b*atan(y,x)/(2.*PI)); } // xyz to cross section u
float V(_xyz) { return sqrt(x*x+y*y)*l_p(x,y); } // xyz to cross section v
float S_s(_xyz) { return C(U(x,y,z),V(x,y,z))/l_p(x,y); } // body
float S_o(_xyz) { return sqrt(pow(C(exp(log(-z)-b/2.),-x*exp(-b/2.))*exp(b/2.),2.)+y*y); } // opening
float S_t(_xyz) { return d_1(-z,sqrt(x*x+y*y),0.5); } // tip
float S_a(_xyz) { return -z>0.?min(S_s(x,y,z),S_o(x,y,z)):S_t(x,y,z); } // body+tip
float S_0(_xyz) { return S_a(x,y,z)-0.01-0.01*pow(x*x+y*y+z*z,0.4)
    -0.02*sqrt(x*x+y*y)*exp(cos(8.*atan(y,x)))
    -0.007*(0.5-0.5*tanh(10.*(z+1.+8.*sqrt(3.*x*x+y*y)))); } // subtract thickness
float S_r(_xyz) { return -s_min(-S_0(x,y,z),z+1.7,10.); } // clip bottom
float r_a(_xyz) { return -0.1*sin(3.*z)*tanh(2.*(x*x+y*y-z-1.5)); } // thicken the bottom "rod"
float S(_xyz) { return S_r(x-r_a(x,y,z)*y,y+r_a(x,y,z)*x,z-0.8); }


// Rotation matrices
mat3 rotx(float a) { return mat3(1, 0, 0, 0, cos(a), sin(a), 0, -sin(a), cos(a)); }
mat3 rotz(float a) { return mat3(cos(a), sin(a), 0, -sin(a), cos(a), 0, 0, 0, 1); }

// Returns the SDF of the shell
float mapShell(vec3 p) {
    // position and orientation
    vec3 q = rotz(0.125*PI)*rotx(0.38*PI)*(0.7*p-vec3(0,0,0.26));
    // a relatively cheap bounding box to speed up rendering and reduce discontinuities
    float bound = length(vec3(vec2(1.2,1.4)*exp(q.z*q.z),1.)*q)/exp(q.z*q.z)-1.0;
    bound = max(bound, length(vec3(1.2,1.4,1)*(q+vec3(0,0.1,0)))-1.);
    float boundw = 0.2;  // padding of the bounding box for continuous transition
    if (bound > 0.0) return bound+boundw;  // outside bound
    else {
        float v = S(q.x,q.y,q.z); // sample raw SDF
        // do some hacking to reduce the high gradient and discontinuities
        // Adjusted with the help of this SDF visualizer:
        // - https://www.shadertoy.com/view/ssKGWR
        // - https://github.com/harry7557558/Shadertoy/blob/master/spiral/functional_conch.glsl
        float k = 1.0-0.9/length(vec3(4.*q.xy,1.0*abs(q.z+0.7)+1.));  // reduce gradient at the bottom
        k = 0.7*mix(k, 1.0, clamp(10.*max(-q.x,q.z-.7*q.x+0.5), 0., 1.));  // reduce a discontinuity
        v = k*v/0.7;  // dividing by 0.7 is due to scaling
        // continuous transition between bound and SDF
        v = mix(v, bound+boundw, smoothstep(0.,1.,(bound+boundw)/boundw));
        //return v; return min(v,1.0);  // this two are broken in Firefox for me
        return min(v,0.1);
    }
}

// Numerical gradient of the shell SDF
vec3 gradShell(vec3 p) {
    // https://iquilezles.org/articles/normalsSDF/
    const float h = 0.001;
    vec3 n = vec3(0.0);
    for(int i=ZERO; i<4; i++) {
        vec3 e = 2.0*vec3((((i+3)>>1)&1),((i>>1)&1),(i&1))-1.0;
        n += e*mapShell(p+e*h);
    }
    return n*(.25/h);
}

// Color calculated from position and gradient
// Interpolating two colors based on gradient magnitude
vec3 albedoShell(vec3 p, vec3 g) {
    // gradient magnitude => interpolation parameter
    float t = 0.5-0.5*cos(2.0*log(0.6*length(g)));
    t += 0.05*sin(40.*p.x)*sin(40.*p.y)*sin(20.*p.z); // some noise
    vec3 col = mix(vec3(0.9,0.9,0.85), vec3(0.75,0.55,0.3), t); // interpolation
    col = min(1.2*col, vec3(1.0)); // adjustments
    return col;
}


/* NOISE */

// Hash function by David Hoskins, https://www.shadertoy.com/view/4djSRW, MIT license
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}
// Gradient noise
float GradientNoise2D(vec2 xy) {
    float i0 = floor(xy.x), i1 = i0 + 1.0;
    float j0 = floor(xy.y), j1 = j0 + 1.0;
    float v00 = dot(2.0 * hash22(vec2(i0, j0)) - 1.0, xy - vec2(i0, j0));
    float v01 = dot(2.0 * hash22(vec2(i0, j1)) - 1.0, xy - vec2(i0, j1));
    float v10 = dot(2.0 * hash22(vec2(i1, j0)) - 1.0, xy - vec2(i1, j0));
    float v11 = dot(2.0 * hash22(vec2(i1, j1)) - 1.0, xy - vec2(i1, j1));
    float xf = xy.x - i0; xf = xf * xf * xf * (10.0 + xf * (-15.0 + xf * 6.0));
    float yf = xy.y - j0; yf = yf * yf * yf * (10.0 + yf * (-15.0 + yf * 6.0));
    return v00 + (v10 - v00)*xf + (v01 - v00)*yf + (v00 + v11 - v01 - v10) * xf*yf;
}


/* SEA + BEACH */
vec4 smin(vec4 a, vec4 b, float k) {
    // smoothed blending with color
    float h = clamp(0.5 + 0.5 * (b.x - a.x) / k, 0., 1.);
    float d = mix(b.x, a.x, h) - k * h * (1.0 - h);
    return vec4(d, mix(b.yzw, a.yzw, h));
}
vec4 mapGround(vec3 p) {
    // returns drgb
    float time = 0.25*PI*uTime; // animation time
    float beach = 0.4*tanh(0.2*p.y)-0.2*GradientNoise2D(0.5*p.xy); // height
    beach *= smoothstep(0.,1., 0.5*(1.+exp(0.3*p.x))
        * (length(vec2(1.4,1.0)*p.xy-vec2(-0.2,-0.2))-0.5)); // shell "pit"
    float sea = -0.2+0.1*exp(sin(time)); // animated sea level
    if (abs(p.z-sea)<0.1)  // sea wave
        sea += 0.005*tanh(2.*max(sea-beach,0.)) * // fade when close to beach
            sin(10.*(p.x-uTime-sin(p.y)))*sin(10.*(p.y+uTime-sin(p.x)));
    if (abs(p.z-beach)<0.1)  // sand grains
        beach += 0.005*tanh(5.*max(beach-sea,0.)) // fade when close to sea
            * GradientNoise2D(50.0*p.xy);
    vec3 seacol = mix(vec3(0.65,0.85,0.8),vec3(0.2,0.55,0.45),
        smoothstep(0.,1.,-0.1*p.y)); // sea color, deeper when further
    seacol = mix(vec3(1.), seacol, clamp(4.*(sea-beach),0.,1.)); // white foam
    seacol = mix(vec3(1.1), seacol, clamp(20.*(sea-beach),0.,1.)); // whiter foam
    vec3 beachcol = mix(vec3(0.7,0.7,0.6),vec3(0.9,0.85,0.8),
        clamp(5.*(beach-sea),0.,1.)); // beach color, darker when wetter
    vec4 ground = smin(vec4(-sea,seacol), vec4(-beach,beachcol), // water-sand transition
        0.01-0.005*cos(time)); // sharper when rising, smoother when falling
    return vec4(p.z+ground.x, min(ground.yzw,1.));
}
vec3 gradGround(vec3 p) {
    // https://iquilezles.org/articles/normalsSDF/
    const float h = 0.01;
    vec3 n = vec3(0.0);
    for(int i=ZERO; i<4; i++) {
        vec3 e = 2.0*vec3((((i+3)>>1)&1),((i>>1)&1),(i&1))-1.0;
        n += e*mapGround(p+e*h).x;
    }
    return n*(.25/h);
}
// Common: Modeling
// Image: Rendering

// The conch was modeled in a "graphing calculator" I made.
// (see Common for link and details)

// I was inspired by:
//  - my childhood fascination with the spiral shells of mollusks
//  - my fascination with the sea that lasts to this day
//  - a certain Disney movie
//  - some thoughts when leaving high school for university

// Mouse-able.

// Also check my "Nautilus Shell" shader (modeled in GLSL):
// https://www.shadertoy.com/view/sdVGWh


/* CONCH INTERSECTION */

// Intersect with the bounding box, used to speed up rendering
bool boxIntersection(float offset, vec3 ro, vec3 rd, out float tn, out float tf) {
    ro -= vec3(-0.1,0.1,0.6); // translation
    vec3 inv_rd = 1.0 / rd;
    vec3 n = inv_rd*(ro);
    vec3 k = abs(inv_rd)*(vec3(0.9,1.3,0.7)+offset); // offset is positive for shadow
    vec3 t1 = -n - k, t2 = -n + k;
    tn = max(max(t1.x, t1.y), t1.z);
    tf = min(min(t2.x, t2.y), t2.z);
    if (tn > tf) return false;
    return true;
}
bool intersectConch(vec3 ro, vec3 rd, inout float t, float tf, float eps) {
    // intersect bounding box
    float t0, t1;
    if (!boxIntersection(0.0, ro, rd, t0, t1)) return false;
    t1 = min(t1, tf);
    if (t1 < t0) return false;
    t = t0;
    // raymarching, eps is the minimum step
    float v0=0.0, v, dt;
    for (int i=ZERO; i<80; i++) {
        v = mapShell(ro+rd*t);
        if (v*v0 < 0.0) { // intersect
            t -= dt * v/(v-v0); // linear interpolation
            return true;
        }
        dt = max(abs(v), eps);
        t += dt;
        if (t > t1) return false; // too far
        v0 = v;
    }
    return true;
    //return false;
}

// Soft shadow
float calcShadow(vec3 ro, vec3 rd) {
    // check bounding box
    float t0, t1;
    if (!boxIntersection(0.2, ro, rd, t0, t1)) return 1.0;
    // https://iquilezles.org/articles/rmshadows
    float sh = 1.;
    float t = max(t0, 0.01) + 0.02*hash22(rd.xy).x;
    for (int i=ZERO; i<40; i++) {
        float h = 0.8*mapShell(ro + rd*t);
        sh = min(sh, smoothstep(0., 1., 20.0*h/t));
        t += clamp(h, 0.02, 0.5);
        if (h<0.) return 0.0;
        if (t>t1) break;
    }
    return max(sh, 0.);
}


/* BEACH INTERSECTION */

bool intersectBeach(vec3 ro, vec3 rd, out float t, float tf) {
    //t = -ro.z/rd.z; if (t < 0.0) return false;
    t = 0.01;
    float v0 = 0.0, v, dt;
    for (int i = int(ZERO); i < 50; i++) {  // raymarching
        if (t>tf) return false;
        v = mapGround(ro+rd*t).x;
        if (v*v0 < 0.0) break;
        dt = i==int(ZERO)?v:dt*v/abs(v-v0); // divide by line derivative
        dt = sign(dt)*clamp(abs(dt), 0.02, 1.0);
        t += dt;
        v0 = v;
    }
    t -= dt * clamp(v/(v-v0), 0., 1.); // linear interpolation
    return true;
}


/* SKY */

vec3 sundir = normalize(vec3(0.3,0.3,1.0));

vec3 getSkyCol(vec3 rd) {
    rd = normalize(vec3(rd.xy,max(rd.z,0.))); // prevent below horizon
    vec3 sky = mix(vec3(0.8,0.9,1.0), vec3(0.3,0.6,0.9), rd.z); // higher => darker
    vec3 sun = 1.5*vec3(0.95,0.9,0.5)*pow(max(dot(rd,sundir),0.), 8.); // warm color
    return sky + sun;
}


/* MAIN */

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    // pass uniforms to Common
    ZERO = min(iFrame, 0);
    uTime = iTime;

    // set camera
    float rx = iMouse.z!=0.0 ? 1.65*(iMouse.y/iResolution.y)-0.05 : 0.12; // azimuthal angle
    float rz = iMouse.z!=0.0 ? -iMouse.x/iResolution.x*4.0*3.14 : 0.5; // polar angle
    vec3 w = vec3(cos(rx)*vec2(cos(rz),sin(rz)), sin(rx));  // far to near
    vec3 u = vec3(-sin(rz),cos(rz),0);  // left to right
    vec3 v = cross(w,u);  // down to up
    vec3 ro = vec3(0,0,0.5)+6.0*w-0.5*u+0.2*v;  // ray origin
    vec2 uv = 2.0*fragCoord.xy/iResolution.xy - vec2(1.0);
    vec3 rd = mat3(u,v,-w)*vec3(uv*iResolution.xy, 2.0*length(iResolution.xy));
    rd = normalize(rd);  // ray direction

    // ray intersection
    float t, t1=40.;
    int intersect_id = -1;
    if (intersectBeach(ro, rd, t, t1)) intersect_id=0, t1=t;
    if (intersectConch(ro, rd, t, t1, 0.02)) intersect_id=1, t1=t;
    t = t1;

    // shading
    vec3 p = ro+rd*t;
    vec3 col; // final color
    float shadow = calcShadow(p, sundir);
    if (intersect_id == -1) { // background
        col = vec3(1.0); // this will be blended to sky color later
    }
    if (intersect_id == 0) { // beach/sea
        vec3 n = normalize(gradGround(p));
        //n *= -sign(dot(n,rd)); // faceforward
        vec3 albedo = mapGround(p).yzw; // raw color
        vec3 amb = 0.2*albedo; // ambient
        vec3 dif = 0.6*(0.3+0.7*shadow) * max(dot(n,sundir),0.0) * albedo; // diffuse
        vec3 spc = intersectConch(p,reflect(rd,n),t1,2.,0.05) // reflection
            ? vec3(0.05,0.045,0.04) // occluded, conch color
            : vec3(0.2-0.1*tanh(0.5*p.y)) * getSkyCol(reflect(rd,n)); // sky color, wetter reflects more
        col = amb+dif+spc;
    }
    if (intersect_id == 1) { // shell
        vec3 n0 = gradShell(p); // raw gradient
        //n0 *= -sign(dot(n0,rd)); // faceforward
        vec3 n = normalize(n0); // normal
        vec3 albedo = albedoShell(p, n0); // color based on gradient
        vec3 amb = (0.4-0.1*dot(rd,n))*albedo; // ambient light
        vec3 dif = albedo*(
            vec3(0.45,0.4,0.35)*max(dot(n,sundir),0.0)+ // sunlight, warm
            vec3(0.2,0.3,0.4)*max(n.z,0.)); // skylight, blueish
        col = pow(amb+dif, vec3(0.8));
    }
    col = mix(getSkyCol(rd), col, exp(-0.04*max(t-5.,0.))); // sky blending/fog
    col += 0.5*vec3(0.8,0.5,0.6)*pow(max(dot(rd,sundir),0.),1.5);  // sun haze
    col = pow(0.95*col, vec3(1.25)); // adjustment
    fragColor = vec4(col,1.0);
}
