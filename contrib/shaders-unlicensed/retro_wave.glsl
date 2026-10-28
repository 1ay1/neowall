#define ZERO min(iFrame, 0)
#define MIN_FLOAT 1e-6
#define MAX_FLOAT 1e6
#define EPSILON 1e-3
#define saturate(x) clamp(x, 0., 1.)
#define UP vec3(0., 1., 0.)
const float PI = acos(-1.);
float time = 0.;

struct Ray{ vec3 origin, dir; };
struct Sphere{vec3 origin; float rad;};

vec3 rayDirection(float fieldOfView, vec2 size, vec2 fragCoord) {
    vec2 xy = fragCoord - size / 2.;
    float z = size.y / tan(radians(fieldOfView) / 2.);
    return normalize(vec3(xy, -z));
}

mat4 viewMatrix(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye),
         s = normalize(cross(f, up)),
         u = cross(s, f);
    return mat4(vec4(s, 0.), vec4(u, 0.), vec4(-f, 0.), vec4(vec3(0.), 1.));
}

Ray makeViewRay(vec2 coord, vec2 res, float a){
    //vec3 lookAt = vec3(0., 0., 0.);
    //vec3 origin = vec3(15. * cos(a), 1., 15. * sin(a));

    vec3 lookAt = vec3(0., 1.5 ,0.);
    vec3 origin = vec3(0., 2., 18.);
    vec3 viewDir = rayDirection(60., res, coord);
    mat4 viewToWorld = viewMatrix(origin, lookAt, vec3(0., 1., 0.));
    vec3 rd = (viewToWorld * vec4(viewDir, 1.0)).xyz;

    return Ray(origin, rd);
}

struct Box{ vec3 origin; vec3 size; };
#define MIN x
#define MAX y
bool box_hit(const in Box inbox, const in Ray inray){
    vec2 tx, ty, tz;
    vec3 maxbounds = inbox.origin + vec3(inbox.size);
    vec3 minbounds = inbox.origin + vec3(-inbox.size);
    tx = ((inray.dir.x >= 0.?vec2(minbounds.x, maxbounds.x):vec2(maxbounds.x, minbounds.x)) - inray.origin.x) / inray.dir.x;
	ty = ((inray.dir.y >= 0.?vec2(minbounds.y, maxbounds.y):vec2(maxbounds.y, minbounds.y)) - inray.origin.y) / inray.dir.y;
    if ((tx.MIN > ty.MAX) || (ty.MIN > tx.MAX))
        return false;
    tx = vec2(max(tx.MIN, ty.MIN), min(tx.MAX, ty.MAX));
	tz = ((inray.dir.z >= 0.?vec2(minbounds.z, maxbounds.z):vec2(maxbounds.z, minbounds.z)) - inray.origin.z) / inray.dir.z;
    if ((tx.MIN > tz.MAX) || (tz.MIN > tx.MAX))
        return false;
    tx = vec2(max(tx.MIN, tz.MIN), min(tx.MAX, tz.MAX));

    if(tx.MIN >= 0.){
    	return true;
    }

    return false;
}

bool sphere_hit(const in Sphere sphere, const in Ray inray) {
    vec3 oc = inray.origin - sphere.origin;
    float a = dot(inray.dir, inray.dir);
    float b = dot(oc, inray.dir);
    float c = dot(oc, oc) - sphere.rad*sphere.rad;
    float discriminant = b*b - a*c;
    if (discriminant > 0.) {
        return true;
        //return (-b - sqrt(discriminant))/a;
    }
    return false;
}

float Hash21(vec2 uv){
    float f = uv.x + uv.y * 37.0;
    return fract(sin(f)*104003.9);
}

vec2 Hash22(vec2 uv){
    float f = uv.x + uv.y * 37.0;
    return fract(cos(f)*vec2(10003.579, 37049.7));
}

float sdBox(vec3 p, vec3 radius){
  vec3 dist = abs(p) - radius;
  return min(max(dist.x, max(dist.y, dist.z)), 0.0) + length(max(dist, 0.0));
}

// capped cylinder distance field
float cylCap(vec3 p, float r, float lenRad){
    float a = length(p.xy) - r;
    a = max(a, abs(p.z) - lenRad);
    return a;
}

vec3 hsv2rgb(vec3 c) {
  const vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float sun(vec2 p) {
  vec2 sp = p + vec2(0., .55);
  float clr = length(sp) - 1.;
  return clr * .5;
}

vec3 sunEffect(vec2 p) {
  vec3 res = vec3(0.1);
  vec3 skyCol1 = hsv2rgb(vec3(283.0/360.0, 0.83, 0.16));
  vec3 skyCol2 = hsv2rgb(vec3(297.0/360.0, 0.79, 0.43));
  res = mix(skyCol1, skyCol2, pow(clamp(0.5*(1.0+p.y+0.1*sin(4.0*p.x)), 0.0, 1.0), 4.0));

  p.y -= .375;
  float ds = sun(p);
  vec3 sunCol = mix(vec3(1.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), clamp(0.5 - .5 * p.y, 0.0, 1.0));
  vec3 glareCol = sqrt(sunCol);

  res += glareCol*(exp(-30.0*ds))*step(0.0, ds);
  res = mix(res, sunCol, smoothstep(-.01, 0., -ds));

  return res;
}

float sdEllipsoid(vec3 p, vec3 r){
  float k0 = length(p/r);
  float k1 = length(p/(r*r));
  return k0*(k0-1.0)/k1;
}

float sdRoundBox(vec3 p, vec3 b, float r){
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;
}

float sdPlane(vec3 p, vec3 n, float h ) {
    return dot(p,n) + h;
}

vec2 opMin(vec2 a, vec2 b){
    if(a.x <= b.x) return a; else return b;
}

//by iq

vec2 hash( in vec2 x )  // replace this by something better
{
    const vec2 k = vec2( 0.3183099, 0.3678794 );
    x = x*k + k.yx;
    return -1.0 + 2.0*fract( 16.0 * k*fract( x.x*x.y*(x.x+x.y)) );
}


// return gradient noise (in x) and its derivatives (in yz)
vec3 noised( in vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );

#if 1
    // quintic interpolation
    vec2 u = f*f*f*(f*(f*6.0-15.0)+10.0);
    vec2 du = 30.0*f*f*(f*(f-2.0)+1.0);
#else
    // cubic interpolation
    vec2 u = f*f*(3.0-2.0*f);
    vec2 du = 6.0*f*(1.0-f);
#endif

    vec2 ga = hash( i + vec2(0.0,0.0) );
    vec2 gb = hash( i + vec2(1.0,0.0) );
    vec2 gc = hash( i + vec2(0.0,1.0) );
    vec2 gd = hash( i + vec2(1.0,1.0) );

    float va = dot( ga, f - vec2(0.0,0.0) );
    float vb = dot( gb, f - vec2(1.0,0.0) );
    float vc = dot( gc, f - vec2(0.0,1.0) );
    float vd = dot( gd, f - vec2(1.0,1.0) );

    return vec3( va + u.x*(vb-va) + u.y*(vc-va) + u.x*u.y*(va-vb-vc+vd),   // value
                 ga + u.x*(gb-ga) + u.y*(gc-ga) + u.x*u.y*(ga-gb-gc+gd) +  // derivatives
                 du * (u.yx*(va-vb-vc+vd) + vec2(vb,vc) - va));
}

float trail(vec2 uv){
    return smoothstep(distance(uv.y, 9. + noised(uv + vec2(time)).x), .5 - uv.x * .5 - .5, - uv.x * .5 - .5);
}

vec3 sea(vec2 uv){

    uv *= vec2(1., 3.);

    const vec2 size = vec2(10., 0.);
    const vec3 off = vec3(-20.,0, 50.);

    float s11 = noised(uv).x;
    float s01 = noised(uv + off.xy).x;
    float s21 = noised(uv + off.zy).x;
    float s10 = noised(uv + off.yx).x;
    float s12 = noised(uv + off.yz).x;
    vec3 va = normalize(vec3(size.xy, s21-s01));
    vec3 vb = normalize(vec3(size.yx, s12-s10));
    vec4 bump = vec4( cross(va,vb), s11 );
    return bump.xzy;
}

#define MOD_POS(poz) poz.x += iTime * 4. + mod(floor(poz.z), 2.) * .35;
//#define MOD_POS(poz) poz.x += mod(floor(poz.z), 2.) * .35;

vec2 CityBlock(vec3 p, vec2 pint){
    // Get random numbers for this block by hashing the city block variable
    vec4 rand;
    rand.xy = Hash22(pint);
    rand.zw = Hash22(rand.xy);
    vec2 rand2 = Hash22(rand.zw);

    // Radius of the building
    float baseRad = 0.2 + (rand.x) * 0.1;
    baseRad = floor(baseRad * 20.0+0.5)/20.0;   // try to snap this for window texture

    // make position relative to the middle of the block
    vec3 baseCenter = p - vec3(0.5, 0.0, 0.5);
    float height = .75 * rand.w*rand.z + 0.3; // height of first building block
    // Make the city skyline higher in the middle of the city.
    height *= 1.5+(baseRad-0.15)*20.0;
    height += 0.1;  // minimum building height
    //height += sin(iTime + pint.x);    // animate the building heights if you're feeling silly
    height = floor(height*20.0)*0.05;   // height is in floor units - each floor is 0.05 high.
    float d = sdBox(baseCenter, vec3(baseRad, height, baseRad)); // large building piece

    // road
    d = min(d, p.y);

    //if (length(pint.xy) > 8.0) return vec2(d, mat);   // Hack to LOD in the distance

    // height of second building section
    float height2 = max(0.0, rand.y * 2.0 - 1.0);
    height2 = floor(height2*20.0)*0.05; // floor units
    rand2 = floor(rand2*20.0)*0.05; // floor units
    // size pieces of building
    d = min(d, sdBox(baseCenter - vec3(0.0, height, 0.0), vec3(baseRad, height2 - rand2.y, baseRad*0.4)));
    d = min(d, sdBox(baseCenter - vec3(0.0, height, 0.0), vec3(baseRad*0.4, height2 - rand2.x, baseRad)));
    // second building section
    if (rand2.y > 0.25)
    {
        d = min(d, sdBox(baseCenter - vec3(0.0, height, 0.0), vec3(baseRad*0.8, height2, baseRad*0.8)));
        // subtract off piece from top so it looks like there's a wall around the roof.
        float topWidth = baseRad;
        if (height2 > 0.0) topWidth = baseRad * 0.8;
        d = max(d, -sdBox(baseCenter - vec3(0.0, height+height2, 0.0), vec3(topWidth-0.0125, 0.015, topWidth-0.0125)));
    }
    else
    {
        // Cylinder top section of building
        if (height2 > 0.0) d = min(d, cylCap((baseCenter - vec3(0.0, height, 0.0)).xzy, baseRad*0.8, height2));
    }
    // mini elevator shaft boxes on top of building
    d = min(d, sdBox(baseCenter - vec3((rand.x-0.5)*baseRad, height+height2, (rand.y-0.5)*baseRad),
                     vec3(baseRad*0.3*rand.z, 0.1*rand2.y, baseRad*0.3*rand2.x+0.025)));
    // mirror another box (and scale it) so we get 2 boxes for the price of 1.
    vec3 boxPos = baseCenter - vec3((rand2.x-0.5)*baseRad, height+height2, (rand2.y-0.5)*baseRad);
    float big = sign(boxPos.x);
    boxPos.x = abs(boxPos.x)-0.02 - baseRad*0.3*rand.w;
    d = min(d, sdBox(boxPos,
    vec3(baseRad*0.3*rand.w, 0.07*rand.y, baseRad*0.2*rand.x + big*0.025)));

    // Put domes on some building tops for variety
    if (rand.y < 0.04)
    {
        d = min(d, length(baseCenter - vec3(0.0, height, 0.0)) - baseRad*0.8);
    }

    return vec2(d, 0.0);
}

float city(vec3 p){
    MOD_POS(p)
    vec3 rep = p;
    rep.xz = fract(p.xz);
    return CityBlock(rep, floor(p.xz)).x;
}

vec3 estimateCityNormal(vec3 p) {
    return normalize(vec3(
        city(vec3(p.x + EPSILON, p.y, p.z)) - city(vec3(p.x - EPSILON, p.y, p.z)),
        city(vec3(p.x, p.y + EPSILON, p.z)) - city(vec3(p.x, p.y - EPSILON, p.z)),
        city(vec3(p.x, p.y, p.z  + EPSILON)) - city(vec3(p.x, p.y, p.z - EPSILON))
    ));
}

const float voxelPad = .2;
float marchCity(vec3 eye, vec3 dir, float start, float end) {
    float t = start;
    vec3 pos = vec3(0.0);
    for (int i = ZERO; i < 128; i++) {
        pos = eye + dir * t;
        float dist = city(pos);
        if(abs(dist) < EPSILON){
            return t;
        }

        float walk = dist;
        float dx = -fract(pos.x);
        float dz = -fract(pos.z);
        float nearestVoxel = min(fract(dx/dir.x), fract(dz/dir.z)) + voxelPad;
        nearestVoxel = max(voxelPad, nearestVoxel);
        walk = min(walk, nearestVoxel);


        t += walk;
        if ((t > end)) break;
    }
    return -1.;
}

vec3 cityColor(vec3 pos, vec3 view){
    vec3 normal = estimateCityNormal(pos);
    MOD_POS(pos);
    float winid = Hash21(floor(pos.xy * vec2(2., 6.)));
    float winStencil = step(winid, Hash21(floor(pos.xz)) * .75);
    float win = step(distance(.125, abs(fract(pos.x) - .5)), .075)
              * step(distance(fract(pos.y * 6.), .5), .4);
    return hsv2rgb(vec3(.525 + (winid * .2 - .1), 1., 1.)) * winStencil * win * step(.000001, dot(normal, vec3(0., 0., 1.)))
         + vec3(1.0, 1.0, 0.0) * abs(dot(normal, vec3(1., 0., 0.))) * abs(dot(view, normal)) * .2;
}

const vec3 boat_center = vec3(0., -.3, -9.);
vec2 boat(vec3 pos){
    float scale = .65;
    float ang = noised(vec2(iTime)).x * .2 - .05;
    pos.xy *= mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
    pos += boat_center + vec3(0., ang, 0.);

    vec2 res = vec2(0.);

    float c = length(pos + vec3(0., 7., 0.) * scale) - 7. * scale;
    res.x = max(res.x, c);
    float d = length(pos + vec3(-.75, 7., 0.) * scale) - 7. * scale;
    res.x = max(res.x, d);

    {
        float a = sdRoundBox(pos + vec3(.65, .35, 0.) * scale, vec3(.9, .25, .65 + pos.x * .2) * scale, .02 * scale);
        res.x = min(res.x, a);
    }

    {
        float a = sdEllipsoid(pos + vec3(1.8, .38, 0.) * scale, vec3(3., .75, .86) * scale);
        a = max(a, sdPlane(pos, normalize(vec3(-1., 1., 0.)), -.05 * scale));
        a = max(a, sdPlane(pos, normalize(vec3(0., -1., 0.)), -0.1));
        res = opMin(res, vec2(a, 1.));
    }

    {
        vec3 mpos = pos + vec3(0., .2 - pos.x * .1, 0.) * scale;
        float a = sdEllipsoid(mpos + vec3(0., 0., .25) * scale, vec3(2., .5, 1.) * scale);
        float b = sdEllipsoid(mpos - vec3(0., 0., .25) * scale, vec3(2., .5, 1.) * scale);
        res.x = max(max(a, b), res.x);
    }

    {
        float a = sdRoundBox(pos + vec3(.3, 0.2, 0.) * scale, vec3(.9, .25 + pos.y * .1, .42 + pos.x * .2) * scale, .25 * scale);
        float b = sdRoundBox(pos + vec3(.3, 0.2, 0.) * scale, vec3(.85, .24, .4 + pos.x * .2) * scale, .25 * scale);
        float comb = max(a, -b);
        comb = max(comb, sdPlane(pos, normalize(vec3(1., -.2, 0.)), .4 * scale));
        comb = max(comb, -sdPlane(pos, normalize(vec3(1., -.8, 0.)), .6 * scale));
        comb = max(comb, -pos.y - .2 * scale);
        res.x = min(res.x, comb);
    }

    float e = sdPlane(pos, -normalize(vec3(-1., .5, 0.) * scale), 1.2 * scale);
    res.x = max(res.x, -e);

    return res;
}

vec3 boatNormals(vec3 pos){
    vec2 eps = vec2(0.0, EPSILON);
    vec3 n = normalize(vec3(
        boat(pos + eps.yxx).x - boat(pos - eps.yxx).x,
        boat(pos + eps.xyx).x - boat(pos - eps.xyx).x,
        boat(pos + eps.xxy).x - boat(pos - eps.xxy).x));
    return n;
}

vec2 marchBoat(in Ray r){
    float t = .01;
    for(int i = ZERO; i <= 64; i++){
        vec3 p = r.origin + r.dir * t;
        vec2 dst = boat(p);
        if(dst.x < .01)
            return vec2(t, dst.y);
        t += dst.x;
    }
    return vec2(-1.);
}

vec3 boatColor(vec3 p, float matid){
    vec3 bn = boatNormals(p);
    vec3 albedo = mix(vec3(1.), vec3(1., 0., 0.), step(abs(-.4 - dot(bn, vec3(0., 1., 0.))), .2));

    vec3 sunPos = vec3(0., 3., -5.);
    vec3 sun = max(dot(bn, normalize(sunPos - p)), .15) * mix(vec3(1.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), .4);

    if(matid == 0.)
        return albedo * sun + smoothstep(.15, 1., dot(bn, normalize(vec3(1., -1., 0.)))) * vec3(1.0, .0, 1.0) * (noised(p.xz * 3. + vec2(iTime * 4., 0.)).x * .5 + .5) * .25;
    else if(matid == 1.)
        return vec3(0.);
    else
        return vec3(0.);
}

vec3 geometry(Ray r){
    vec3 color = vec3(0.);


    float start = (-r.origin.z)/r.dir.z;
    float end = (-3.-r.origin.z)/r.dir.z;
    float cityDist = -1.;
    if(box_hit(Box(vec3(0., 2.25, -2.), vec3(10., 2.25, 2.)), r)){
        cityDist = marchCity(r.origin, r.dir, start, end);
    }
    if (cityDist >= 0.) {
        color = cityColor(r.origin + cityDist * r.dir, r.dir);
    }else{
        vec3 backPlane = r.origin + end * r.dir;
        color = sunEffect(backPlane.xy * .125);
    }

    if(box_hit(Box(vec3(0., .4, 9.), vec3(1.2, .35, .5)), r)){
        vec2 boatDist = marchBoat(r);
        vec3 boatPoint = r.origin + boatDist.x * r.dir;
        if (boatDist.x >= 0. && boatPoint.y >= 0.) {
            color = boatColor(boatPoint, boatDist.y);
        }
    }

    return color;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (2. * fragCoord.xy - iResolution.xy)/iResolution.y;
    time = iTime;
    Ray originalRay = makeViewRay(fragCoord, iResolution.xy, iMouse.x/iResolution.x * 5.);
    Ray r = originalRay;

    vec3 color = vec3(0.);

    float groundDst = (-r.origin.y)/r.dir.y;
    vec3 groundPos = r.origin + groundDst * r.dir;
    vec3 nor = vec3(0., 1., 0.);
    if(groundPos.z > 0. && r.dir.y < 0.) {
        vec3 nor;
        {
            vec2 uv = mod(groundPos.xz + vec2(iTime * 2., 0.), vec2(100.)) * vec2(5., 10.);

            const vec2 size = vec2(10., 0.);
            const vec3 off = vec3(-20.,0, 50.);

            float s11 = noised(uv).x + trail(groundPos.xz);
            float s01 = noised(uv + off.xy).x + trail(groundPos.xz + vec2(.4, 0.));
            float s21 = noised(uv + off.zy).x + trail(groundPos.xz + vec2(-.4, 0.));
            float s10 = noised(uv + off.yx).x + trail(groundPos.xz + vec2(.0, .4));
            float s12 = noised(uv + off.yz).x + trail(groundPos.xz + vec2(.0, -.4));
            vec3 va = normalize(vec3(size.xy, s21-s01));
            vec3 vb = normalize(vec3(size.yx, s12-s10));
            vec4 bump = vec4( cross(va,vb), s11 );
            nor =  bump.xzy;
        }

        vec3 reflected = reflect(r.dir, nor);
        r = Ray(groundPos, reflected);
    }
    color = geometry(r);

    if(box_hit(Box(vec3(0., .3, 9.), vec3(1.2, .3, .5)), originalRay)){
        vec2 boatDist = marchBoat(originalRay);
        if (boatDist.x >= 0. && boatDist.x < groundDst) {
            vec3 boatPoint = originalRay.origin + boatDist.x * originalRay.dir;
            color = boatColor(boatPoint, boatDist.y);
        }
    }

    fragColor = vec4(color, 1.);
}
