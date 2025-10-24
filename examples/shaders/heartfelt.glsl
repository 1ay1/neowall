// Heartfelt - by Martijn Steinrucken aka BigWings - 2017
// Email:countfrolic@gmail.com Twitter:@The_ArtOfCode
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
//
// Adapted for staticwall with procedural background

#define S(a, b, t) smoothstep(a, b, t)
#define USE_POST_PROCESSING

vec3 N13(float p) {
    //  from DAVE HOSKINS
   vec3 p3 = fract(vec3(p) * vec3(.1031,.11369,.13787));
   p3 += dot(p3, p3.yzx + 19.19);
   return fract(vec3((p3.x + p3.y)*p3.z, (p3.x+p3.z)*p3.y, (p3.y+p3.z)*p3.x));
}

vec4 N14(float t) {
	return fract(sin(t*vec4(123., 1024., 1456., 264.))*vec4(6547., 345., 8799., 1564.));
}

float N(float t) {
    return fract(sin(t*12345.564)*7658.76);
}

float Saw(float b, float t) {
	return S(0., b, t)*S(1., b, t);
}

// Procedural background - beautiful bokeh city lights
vec3 Background(vec2 uv, float blur) {
    // Base gradient
    vec3 col = mix(vec3(0.05, 0.1, 0.15), vec3(0.15, 0.2, 0.3), uv.y * 0.5 + 0.5);

    // Add bokeh lights
    for(float i = 0.; i < 20.; i++) {
        vec2 n = N13(i).xy;
        vec2 pos = vec2(n.x * 2. - 1., n.y * 1.5 - 0.5);

        float size = mix(0.01, 0.05, fract(n.x * 13.7));
        float dist = length(uv - pos);

        // Blur affects bokeh size
        float bokehSize = size * (1. + blur * 0.3);
        float light = exp(-dist * dist / (bokehSize * bokehSize)) * 0.5;

        // Colored lights
        vec3 lightCol = vec3(1.0, 0.9, 0.7);
        if(fract(n.y * 7.3) > 0.5) lightCol = vec3(0.9, 0.7, 1.0);
        if(fract(n.x * 5.1) > 0.7) lightCol = vec3(1.0, 0.6, 0.5);

        col += lightCol * light;
    }

    // Add some subtle building shapes
    float buildings = 0.;
    for(float i = 0.; i < 8.; i++) {
        float x = i / 8. * 2. - 1.;
        float height = fract(sin(i * 43.7) * 234.5) * 0.4 + 0.2;
        float width = 0.12;

        if(abs(uv.x - x) < width && uv.y < -0.3 + height) {
            buildings = 1.;
        }
    }

    col = mix(col, vec3(0.02, 0.03, 0.05), buildings * 0.8);

    // Add atmospheric glow
    float glow = exp(-length(uv) * 0.5) * 0.2;
    col += vec3(0.1, 0.15, 0.2) * glow;

    return col;
}

vec2 DropLayer2(vec2 uv, float t) {
    vec2 UV = uv;

    uv.y += t*0.75;
    vec2 a = vec2(6., 1.);
    vec2 grid = a*2.;
    vec2 id = floor(uv*grid);

    float colShift = N(id.x);
    uv.y += colShift;

    id = floor(uv*grid);
    vec3 n = N13(id.x*35.2+id.y*2376.1);
    vec2 st = fract(uv*grid)-vec2(.5, 0);

    float x = n.x-.5;

    float y = UV.y*20.;
    float wiggle = sin(y+sin(y));
    x += wiggle*(.5-abs(x))*(n.z-.5);
    x *= .7;
    float ti = fract(t+n.z);
    y = (Saw(.85, ti)-.5)*.9+.5;
    vec2 p = vec2(x, y);

    float d = length((st-p)*a.yx);

    float mainDrop = S(.4, .0, d);

    float r = sqrt(S(1., y, st.y));
    float cd = abs(st.x-x);
    float trail = S(.23*r, .15*r*r, cd);
    float trailFront = S(-.02, .02, st.y-y);
    trail *= trailFront*r*r;

    y = UV.y;
    float trail2 = S(.2*r, .0, cd);
    float droplets = max(0., (sin(y*(1.-y)*120.)-st.y))*trail2*trailFront*n.z;
    y = fract(y*10.)+(st.y-.5);
    float dd = length(st-vec2(x, y));
    droplets = S(.3, 0., dd);
    float m = mainDrop+droplets*r*trailFront;

    return vec2(m, trail);
}

float StaticDrops(vec2 uv, float t) {
	uv *= 40.;

    vec2 id = floor(uv);
    uv = fract(uv)-.5;
    vec3 n = N13(id.x*107.45+id.y*3543.654);
    vec2 p = (n.xy-.5)*.7;
    float d = length(uv-p);

    float fade = Saw(.025, fract(t+n.z));
    float c = S(.3, 0., d)*fract(n.z*10.)*fade;
    return c;
}

vec2 Drops(vec2 uv, float t, float l0, float l1, float l2) {
    float s = StaticDrops(uv, t)*l0;
    vec2 m1 = DropLayer2(uv, t)*l1;
    vec2 m2 = DropLayer2(uv*1.85, t)*l2;

    float c = s+m1.x+m2.x;
    c = S(.3, 1., c);

    return vec2(c, max(m1.y*l0, m2.y*l1));
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = (fragCoord.xy-.5*iResolution.xy) / iResolution.y;
    vec2 UV = fragCoord.xy/iResolution.xy;
    vec3 M = iMouse.xyz/iResolution.xyz;
    float T = iTime;

    float t = T*.2;

    // Animated rain amount
    float rainAmount = sin(T*.05)*.3+.7;

    float maxBlur = mix(3., 6., rainAmount);
    float minBlur = 2.;

    float zoom = -cos(T*.2);
    uv *= .7+zoom*.3;

    UV = (UV-.5)*(.9+zoom*.1)+.5;

    float staticDrops = S(-.5, 1., rainAmount)*2.;
    float layer1 = S(.25, .75, rainAmount);
    float layer2 = S(.0, .5, rainAmount);

    vec2 c = Drops(uv, t, staticDrops, layer1, layer2);

    // Calculate normals (expensive but better quality)
    vec2 e = vec2(.001, 0.);
    float cx = Drops(uv+e, t, staticDrops, layer1, layer2).x;
    float cy = Drops(uv+e.yx, t, staticDrops, layer1, layer2).x;
    vec2 n = vec2(cx-c.x, cy-c.x);

    float focus = mix(maxBlur-c.y, minBlur, S(.1, .2, c.x));

    // Use procedural background instead of texture
    vec3 col = Background(UV*2.-1. + n*0.1, focus * 0.3);

    #ifdef USE_POST_PROCESSING
    t = (T+3.)*.5;
    float colFade = sin(t*.2)*.5+.5;
    col *= mix(vec3(1.), vec3(.8, .9, 1.3), colFade);

    float fade = S(0., 10., T);
    float lightning = sin(t*sin(t*10.));
    lightning *= pow(max(0., sin(t+sin(t))), 10.);
    col *= 1.+lightning*fade*mix(1., .1, 0.);
    col *= 1.-dot(UV-=.5, UV);

    col *= fade;
    #endif

    fragColor = vec4(col, 1.);
}
