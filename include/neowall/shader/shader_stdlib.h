/* GLSL standard library + reactive uniform block injected into every neowall
 * shader's wrapper prefix.
 *
 * This is what makes authoring "mind-blowing" shaders a 20-line job: the noise,
 * SDF, color and reactive helpers below are always available — no #include, no
 * copy-paste. It also declares the reactive uniforms (iCpu, iAudioLevel, ...)
 * and the audio channel helpers (audioBand, beat, ...).
 *
 * Everything here is namespaced under nw* / declared as overloadable helpers so
 * it cannot clash with a user shader that defines its own noise(): GLSL allows
 * shadowing of these library functions by user definitions of the same name and
 * signature only if we DON'T define them — so the helpers use nw-prefixed names
 * and we expose a few un-prefixed aliases (palette, fbm) guarded by #ifndef.
 *
 * Kept as a single C string so it concatenates straight into the wrapper. */

#ifndef NEOWALL_SHADER_STDLIB_H
#define NEOWALL_SHADER_STDLIB_H

/* The reactive uniform declarations. These are filled every frame by
 * multipass_set_uniforms() from the reactive snapshot. A shader that never uses
 * them pays nothing (the GLSL compiler strips unused uniforms). */
static const char *neowall_reactive_uniforms =
    "// --- neowall reactive uniforms (live system + audio) ---\n"
    "uniform float iCpu;            // total CPU load 0..1\n"
    "uniform float iCpuCores[8];    // per-core load 0..1\n"
    "uniform int   iCpuCoreCount;\n"
    "uniform float iRam;            // memory used 0..1\n"
    "uniform float iNetDown;        // download activity 0..1\n"
    "uniform float iNetUp;          // upload activity 0..1\n"
    "uniform float iBattery;        // charge 0..1\n"
    "uniform float iCharging;       // 1.0 if charging/AC\n"
    "uniform float iTimeOfDay;      // 0..1 across the local day\n"
    "uniform float iSun;            // sun elevation proxy 0..1\n"
    "uniform float iDayFraction;    // 0..1 across the year\n"
    "uniform float iKeyEnergy;      // recent keyboard activity 0..1\n"
    "uniform float iMouseEnergy;    // recent mouse motion 0..1\n"
    "uniform float iAudioLevel;     // overall loudness 0..1\n"
    "uniform float iAudioBass;      // low band 0..1\n"
    "uniform float iAudioMid;       // mid band 0..1\n"
    "uniform float iAudioTreble;    // high band 0..1\n"
    "uniform float iAudioBeat;      // beat pulse 0..1 (decays)\n"
    "uniform float iAudioActive;    // 1.0 if audio capture is live\n"
    "uniform sampler2D iAudio;      // row0 = spectrum, row1 = waveform (512 wide)\n"
    "\n"
    "// User uniforms (manifest-driven) live here; declared dynamically.\n";

/* The GLSL helper library. Pure functions, no state. */
static const char *neowall_glsl_stdlib =
    "// ============================================================\n"
    "//  neowall GLSL std-lib  (auto-injected; see shader_stdlib.h)\n"
    "// ============================================================\n"
    "\n"
    "#ifndef NW_PI\n"
    "#define NW_PI 3.14159265359\n"
    "#define NW_TAU 6.28318530718\n"
    "#endif\n"
    "\n"
    "// ---- audio sampling helpers ----\n"
    "// Energy in a normalised frequency band [lo,hi] in 0..1 of the spectrum.\n"
    "float audioBand(float lo, float hi) {\n"
    "    lo = clamp(lo, 0.0, 1.0); hi = clamp(hi, 0.0, 1.0);\n"
    "    float acc = 0.0; float n = 0.0;\n"
    "    const int STEPS = 16;\n"
    "    for (int i = 0; i < STEPS; i++) {\n"
    "        float t = lo + (hi - lo) * (float(i) + 0.5) / float(STEPS);\n"
    "        acc += texture(iAudio, vec2(t, 0.25)).r; n += 1.0;\n"
    "    }\n"
    "    return n > 0.0 ? acc / n : 0.0;\n"
    "}\n"
    "float spectrum(float x) { return texture(iAudio, vec2(clamp(x,0.0,1.0), 0.25)).r; }\n"
    "float waveform(float x) { return texture(iAudio, vec2(clamp(x,0.0,1.0), 0.75)).r; }\n"
    "float beat() { return iAudioBeat; }\n"
    "\n"
    "// ---- hashing ----\n"
    "float nwHash11(float p){ p=fract(p*0.1031); p*=p+33.33; p*=p+p; return fract(p); }\n"
    "float nwHash21(vec2 p){ vec3 p3=fract(vec3(p.xyx)*0.1031); p3+=dot(p3,p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }\n"
    "vec2  nwHash22(vec2 p){ vec3 p3=fract(vec3(p.xyx)*vec3(0.1031,0.1030,0.0973)); p3+=dot(p3,p3.yzx+33.33); return fract((p3.xx+p3.yz)*p3.zy); }\n"
    "vec3  nwHash33(vec3 p){ p=fract(p*vec3(0.1031,0.1030,0.0973)); p+=dot(p,p.yxz+33.33); return fract((p.xxy+p.yxx)*p.zyx); }\n"
    "\n"
    "// ---- value noise + fbm ----\n"
    "float nwValueNoise(vec2 p){\n"
    "    vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);\n"
    "    return mix(mix(nwHash21(i+vec2(0,0)),nwHash21(i+vec2(1,0)),u.x),\n"
    "               mix(nwHash21(i+vec2(0,1)),nwHash21(i+vec2(1,1)),u.x),u.y);\n"
    "}\n"
    "float nwFbm(vec2 p){\n"
    "    float v=0.0, a=0.5; mat2 m=mat2(1.6,1.2,-1.2,1.6);\n"
    "    for(int i=0;i<6;i++){ v+=a*nwValueNoise(p); p=m*p; a*=0.5; } return v;\n"
    "}\n"
    "float nwFbm(vec2 p, int oct){\n"
    "    float v=0.0, a=0.5; mat2 m=mat2(1.6,1.2,-1.2,1.6);\n"
    "    for(int i=0;i<8;i++){ if(i>=oct) break; v+=a*nwValueNoise(p); p=m*p; a*=0.5; } return v;\n"
    "}\n"
    "\n"
    "// ---- gradient (simplex-ish) noise, -1..1 ----\n"
    "float nwGradNoise(vec2 p){\n"
    "    vec2 i=floor(p), f=fract(p);\n"
    "    vec2 u=f*f*f*(f*(f*6.0-15.0)+10.0);\n"
    "    float a=dot(nwHash22(i+vec2(0,0))*2.0-1.0, f-vec2(0,0));\n"
    "    float b=dot(nwHash22(i+vec2(1,0))*2.0-1.0, f-vec2(1,0));\n"
    "    float c=dot(nwHash22(i+vec2(0,1))*2.0-1.0, f-vec2(0,1));\n"
    "    float d=dot(nwHash22(i+vec2(1,1))*2.0-1.0, f-vec2(1,1));\n"
    "    return mix(mix(a,b,u.x),mix(c,d,u.x),u.y);\n"
    "}\n"
    "\n"
    "// ---- worley / cellular, returns nearest distance ----\n"
    "float nwWorley(vec2 p){\n"
    "    vec2 i=floor(p), f=fract(p); float d=1.0;\n"
    "    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){\n"
    "        vec2 g=vec2(float(x),float(y)); vec2 o=nwHash22(i+g);\n"
    "        d=min(d, length(g+o-f));\n"
    "    } return d;\n"
    "}\n"
    "\n"
    "// ---- curl of fbm (great for flow/smoke) ----\n"
    "vec2 nwCurl(vec2 p){\n"
    "    float e=0.01;\n"
    "    float n1=nwFbm(p+vec2(0.0,e)); float n2=nwFbm(p-vec2(0.0,e));\n"
    "    float n3=nwFbm(p+vec2(e,0.0)); float n4=nwFbm(p-vec2(e,0.0));\n"
    "    return vec2(n1-n2, -(n3-n4))/(2.0*e);\n"
    "}\n"
    "\n"
    "// ---- color ----\n"
    "// IQ cosine palette. Pick a,b,c,d to taste; t in 0..1.\n"
    "vec3 nwPalette(float t, vec3 a, vec3 b, vec3 c, vec3 d){ return a + b*cos(NW_TAU*(c*t+d)); }\n"
    "// A pleasing default neon palette.\n"
    "vec3 nwPalette(float t){ return nwPalette(t, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.0,0.33,0.67)); }\n"
    "vec3 nwHsv2rgb(vec3 c){ vec4 K=vec4(1.0,2.0/3.0,1.0/3.0,3.0); vec3 p=abs(fract(c.xxx+K.xyz)*6.0-K.www); return c.z*mix(K.xxx,clamp(p-K.xxx,0.0,1.0),c.y); }\n"
    "// OKLab-ish quick saturation/contrast aids\n"
    "vec3 nwSaturate(vec3 c, float s){ float l=dot(c,vec3(0.2126,0.7152,0.0722)); return mix(vec3(l),c,s); }\n"
    "// ACES filmic tonemap\n"
    "vec3 nwTonemap(vec3 x){ x*=0.6; float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14; return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0); }\n"
    "// sRGB out\n"
    "vec3 nwGamma(vec3 c){ return pow(clamp(c,0.0,1.0), vec3(0.4545)); }\n"
    "\n";

/* Continuation of the std-lib (split to satisfy the C99 4095-char string
 * literal limit; the two are concatenated at injection time). */
static const char *neowall_glsl_stdlib2 =
    "// ---- 2D SDFs + ops ----\n"
    "float sdCircle(vec2 p,float r){ return length(p)-r; }\n"
    "float sdBox(vec2 p,vec2 b){ vec2 d=abs(p)-b; return length(max(d,0.0))+min(max(d.x,d.y),0.0); }\n"
    "float sdSegment(vec2 p,vec2 a,vec2 b){ vec2 pa=p-a,ba=b-a; float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h); }\n"
    "float sdHex(vec2 p,float r){ const vec3 k=vec3(-0.866025,0.5,0.577350); p=abs(p); p-=2.0*min(dot(k.xy,p),0.0)*k.xy; p-=vec2(clamp(p.x,-k.z*r,k.z*r),r); return length(p)*sign(p.y); }\n"
    "float opSmoothUnion(float a,float b,float k){ float h=clamp(0.5+0.5*(b-a)/k,0.0,1.0); return mix(b,a,h)-k*h*(1.0-h); }\n"
    "float opSmoothSub(float a,float b,float k){ float h=clamp(0.5-0.5*(b+a)/k,0.0,1.0); return mix(b,-a,h)+k*h*(1.0-h); }\n"
    "\n"
    "// ---- 3D SDFs (for raymarchers) ----\n"
    "float sdSphere(vec3 p,float r){ return length(p)-r; }\n"
    "float sdBox(vec3 p,vec3 b){ vec3 d=abs(p)-b; return length(max(d,0.0))+min(max(d.x,max(d.y,d.z)),0.0); }\n"
    "float sdTorus(vec3 p,vec2 t){ vec2 q=vec2(length(p.xz)-t.x,p.y); return length(q)-t.y; }\n"
    "mat2 nwRot(float a){ float c=cos(a),s=sin(a); return mat2(c,-s,s,c); }\n"
    "\n"
    "// ---- handy reactive shaping ----\n"
    "// pulse(x): smooth 0..1 emphasis curve, good for load->intensity mapping.\n"
    "float pulse(float x){ x=clamp(x,0.0,1.0); return x*x*(3.0-2.0*x); }\n"
    "// dayNightMix: 0 at night, 1 at noon, using iSun.\n"
    "float dayNight(){ return pulse(iSun); }\n"
    "// warmCool: a color temperature shift driven by time of day.\n"
    "vec3 timeOfDayTint(){ return mix(vec3(0.35,0.45,0.85), vec3(1.05,0.95,0.7), dayNight()); }\n"
    "\n"
    "// Aliases (only if the user didn't define their own).\n"
    "#ifndef NW_NO_ALIASES\n"
    "  #define fbm nwFbm\n"
    "  #define palette nwPalette\n"
    "  #define hsv2rgb nwHsv2rgb\n"
    "  #define rot2d nwRot\n"
    "#endif\n"
    "// ============================================================\n"
    "\n";

#endif /* NEOWALL_SHADER_STDLIB_H */
