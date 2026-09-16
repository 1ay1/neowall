/* GLSL standard library + reactive uniform block injected into every neowall
 * shader's wrapper prefix.
 *
 * This is what makes authoring "mind-blowing" shaders a 20-line job: the noise,
 * SDF, color and reactive helpers below are always available — no #include, no
 * copy-paste. It also declares the reactive uniforms (iCpu, iAudioLevel, ...)
 * and the audio channel helpers (audioBand, beat, ...).
 *
 * Everything here is namespaced under nw* so it cannot clash with a user
 * shader that defines its own noise()/fbm()/hsv2rgb(). The nw-prefixed names
 * are the public API; there are deliberately NO unprefixed aliases — GLSL has
 * no way to detect a user function definition from the preprocessor, so a
 * `#define hsv2rgb nwHsv2rgb` would rewrite the user's own `vec3 hsv2rgb(...)`
 * into a duplicate of nwHsv2rgb and fail to compile.
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
    "uniform float iCpuCores[64];   // per-core load 0..1\n"
    "uniform int   iCpuCoreCount;\n"
    "uniform float iCpuMax;         // hottest single core 0..1\n"
    "uniform float iCpuSpread;      // core-load imbalance 0..1\n"
    "uniform float iRam;            // memory used 0..1\n"
    "uniform float iRamGB;          // memory used in GiB (absolute)\n"
    "uniform float iRamTotalGB;     // total memory in GiB\n"
    "uniform float iSwap;           // swap used 0..1\n"
    "uniform float iNetDown;        // download activity 0..1\n"
    "uniform float iNetUp;          // upload activity 0..1\n"
    "uniform float iNetDownRaw;     // download rate in MB/s (absolute)\n"
    "uniform float iNetUpRaw;       // upload rate in MB/s (absolute)\n"
    "uniform float iDiskRead;       // disk read activity 0..1\n"
    "uniform float iDiskWrite;      // disk write activity 0..1\n"
    "uniform float iLoad;           // 1-min load avg / cores, 0..1\n"
    "uniform float iLoadRaw;        // raw 1-min load average\n"
    "uniform float iCpuTemp;        // CPU temp 0..1 over 30..95C\n"
    "uniform float iCpuTempC;       // CPU temp in degrees C\n"
    "uniform float iGpu;            // GPU utilisation 0..1\n"
    "uniform float iGpuTemp;        // GPU temp 0..1 over 30..95C\n"
    "uniform float iGpuTempC;       // GPU temp in degrees C\n"
    "uniform float iNvGpu;          // NVIDIA GPU util 0..1 (nvidia-smi)\n"
    "uniform float iNvVram;         // NVIDIA VRAM used 0..1\n"
    "uniform float iNvGpuTempC;     // NVIDIA GPU temp in degrees C\n"
    "uniform float iNvPower;        // NVIDIA power draw / limit 0..1\n"
    "uniform float iNvActive;       // 1.0 if nvidia-smi capture is live\n"
    "uniform float iThermal;        // hottest of CPU/GPU 0..1 (30..95C)\n"
    "uniform float iActivity;       // fused machine busyness 0..1\n"
    "uniform float iPulse;          // stress heartbeat 0..1 (rate rises w/ load)\n"
    "uniform float iUptimeHours;    // system uptime in hours\n"
    "uniform float iProcs;          // process activity proxy 0..1\n"
    "uniform int   iProcCount;      // total process/thread count\n"
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
    "// Your windows, in PIXELS relative to this output's top-left, y DOWN.\n"
    "// xy = position, zw = size. Only windows on the visible workspace appear,\n"
    "// and only on the monitor showing them.\n"
    "//\n"
    "// iWindowCount is 0 when the compositor does not expose geometry (today\n"
    "// only Hyprland does), so guard on it and a shader stays correct\n"
    "// everywhere -- it simply sees an empty desktop.\n"
    "#define NW_MAX_WINDOWS 16\n"
    "uniform vec4  iWindows[NW_MAX_WINDOWS];\n"
    "uniform int   iWindowCount;    // how many entries of iWindows are valid\n"
    "uniform vec4  iFocusedWindow;  // the fullscreen/active one; zero if none\n"
    "\n"
    "// Persistent state: survives restarts and wallpaper switches, unlike a\n"
    "// feedback buffer which is lost the moment the process exits. Read it to\n"
    "// resume where you left off; iStateAge is how many REAL seconds passed\n"
    "// while you were gone (0 on the very first run).\n"
    "uniform vec4  iState[4];       // 16 floats you choose the meaning of\n"
    "uniform float iStateAge;       // seconds since iState was written\n"
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
    "float nwAudioBand(float lo, float hi) {\n"
    "    lo = clamp(lo, 0.0, 1.0); hi = clamp(hi, 0.0, 1.0);\n"
    "    float acc = 0.0; float n = 0.0;\n"
    "    const int STEPS = 16;\n"
    "    for (int i = 0; i < STEPS; i++) {\n"
    "        float t = lo + (hi - lo) * (float(i) + 0.5) / float(STEPS);\n"
    "        acc += texture(iAudio, vec2(t, 0.25)).r; n += 1.0;\n"
    "    }\n"
    "    return n > 0.0 ? acc / n : 0.0;\n"
    "}\n"
    "float nwSpectrum(float x) { return texture(iAudio, vec2(clamp(x,0.0,1.0), 0.25)).r; }\n"
    "float nwWaveform(float x) { return texture(iAudio, vec2(clamp(x,0.0,1.0), 0.75)).r; }\n"
    "float nwBeat() { return iAudioBeat; }\n"
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
    "float nwSdCircle(vec2 p,float r){ return length(p)-r; }\n"
    "float nwSdBox(vec2 p,vec2 b){ vec2 d=abs(p)-b; return length(max(d,0.0))+min(max(d.x,d.y),0.0); }\n"
    "float nwSdSegment(vec2 p,vec2 a,vec2 b){ vec2 pa=p-a,ba=b-a; float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h); }\n"
    "float nwSdHex(vec2 p,float r){ const vec3 k=vec3(-0.866025,0.5,0.577350); p=abs(p); p-=2.0*min(dot(k.xy,p),0.0)*k.xy; p-=vec2(clamp(p.x,-k.z*r,k.z*r),r); return length(p)*sign(p.y); }\n"
    "float nwOpSmoothUnion(float a,float b,float k){ float h=clamp(0.5+0.5*(b-a)/k,0.0,1.0); return mix(b,a,h)-k*h*(1.0-h); }\n"
    "float nwOpSmoothSub(float a,float b,float k){ float h=clamp(0.5-0.5*(b+a)/k,0.0,1.0); return mix(b,-a,h)+k*h*(1.0-h); }\n"
    "\n"
    "// ---- 3D SDFs (for raymarchers) ----\n"
    "float nwSdSphere(vec3 p,float r){ return length(p)-r; }\n"
    "float nwSdBox(vec3 p,vec3 b){ vec3 d=abs(p)-b; return length(max(d,0.0))+min(max(d.x,max(d.y,d.z)),0.0); }\n"
    "float nwSdTorus(vec3 p,vec2 t){ vec2 q=vec2(length(p.xz)-t.x,p.y); return length(q)-t.y; }\n"
    "mat2 nwRot(float a){ float c=cos(a),s=sin(a); return mat2(c,-s,s,c); }\n"
    "\n"
    "// ---- handy reactive shaping ----\n"
    "// pulse(x): smooth 0..1 emphasis curve, good for load->intensity mapping.\n"
    "float nwPulse(float x){ x=clamp(x,0.0,1.0); return x*x*(3.0-2.0*x); }\n"
    "// dayNightMix: 0 at night, 1 at noon, using iSun.\n"
    "float nwDayNight(){ return nwPulse(iSun); }\n"
    "\n"
    "// ---- window awareness ----\n"
    "// All take fragCoord in pixels. They convert to the compositor's y-down\n"
    "// convention internally, so you never have to think about the flip.\n"
    "//\n"
    "// Every one returns a neutral value when iWindowCount is 0, so a shader\n"
    "// using them looks sane on compositors that expose no geometry.\n"
    "\n"
    "// Signed distance in pixels from p to a window rect: negative inside.\n"
    "float nwWinDist(vec2 frag, vec4 win){\n"
    "    vec2 p = vec2(frag.x, iResolution.y - frag.y);\n"
    "    vec2 c = win.xy + win.zw*0.5;\n"
    "    vec2 d = abs(p - c) - win.zw*0.5;\n"
    "    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
    "}\n"
    "\n"
    "// Distance in pixels to the NEAREST window edge. Large when the desktop\n"
    "// is empty, so `exp(-nwWinNearest(fc)*0.01)` fades in as windows approach.\n"
    "float nwWinNearest(vec2 frag){\n"
    "    float d = 1e6;\n"
    "    for (int i = 0; i < NW_MAX_WINDOWS; i++){\n"
    "        if (i >= iWindowCount) break;\n"
    "        d = min(d, nwWinDist(frag, iWindows[i]));\n"
    "    }\n"
    "    return d;\n"
    "}\n"
    "\n"
    "// 1.0 where a window covers this pixel, 0.0 on bare wallpaper.\n"
    "float nwWinCovered(vec2 frag){\n"
    "    return nwWinNearest(frag) < 0.0 ? 1.0 : 0.0;\n"
    "}\n"
    "\n"
    "// Soft glow that leaks out from behind every window, falling off over\n"
    "// `radius` pixels. This is the money function: multiply it into a colour\n"
    "// and the wallpaper appears lit by the windows sitting on top of it.\n"
    "float nwWinGlow(vec2 frag, float radius){\n"
    "    float d = nwWinNearest(frag);\n"
    "    if (d < 0.0) return 0.0;              // under a window: invisible anyway\n"
    "    return exp(-d/max(radius, 1.0));\n"
    "}\n"
    "\n"
    "// How much of the screen your windows occupy, 0..1. Cheap proxy for\n"
    "// \"how busy am I\" -- dim the wallpaper as the desktop fills up.\n"
    "float nwWinBusy(){\n"
    "    float area = 0.0;\n"
    "    for (int i = 0; i < NW_MAX_WINDOWS; i++){\n"
    "        if (i >= iWindowCount) break;\n"
    "        area += iWindows[i].z * iWindows[i].w;\n"
    "    }\n"
    "    return clamp(area/max(iResolution.x*iResolution.y, 1.0), 0.0, 1.0);\n"
    "}\n"
    "// warmCool: a color temperature shift driven by time of day.\n"
    "vec3 nwTimeOfDayTint(){ return mix(vec3(0.35,0.45,0.85), vec3(1.05,0.95,0.7), nwDayNight()); }\n"
    "\n"
    "// Friendly unprefixed aliases (sdBox, pulse, beat, ...) are NOT emitted\n"
    "// here. They are appended by the host at injection time, and only for\n"
    "// names the shader has not defined itself -- see neowall_stdlib_aliases\n"
    "// in shader_stdlib.h and glsl_shadow.h. That is why everything above is\n"
    "// nw*-prefixed and calls only other nw* names: the canonical library can\n"
    "// never be disturbed by whatever the user chooses to define.\n"
    "// ============================================================\n"
    "\n";

/* Font-atlas helpers (split again for the C99 string-literal limit).
 * Requires a channel bound to the "font" texture (128x72, 16x6 grid of
 * 8x12 ASCII cells, codes 32..127), sampled NEAREST. Pass the sampler in.
 *
 *   nwGlyph(fontTex, ascii, p) : p in [0,1)^2 across one glyph cell,
 *                                returns 1.0 on ink, 0.0 on paper.
 *   nwHexDigit(v) : 0..15 -> ascii of '0'..'9','A'..'F'.
 * Typical use, drawing char C at pixel `pos` with cell size `s`:
 *   vec2 gp=(fragCoord-pos)/vec2(s*0.6,s); ink += nwGlyph(iChannelN, C, gp);
 */
static const char *neowall_glsl_stdlib3 =
    "// ---- bitmap font atlas (bind a channel to the \"font\" texture) ----\n"
    "// atlas: 16 cols x 6 rows of 8x12 cells; ascii 32..127.\n"
    "float nwGlyph(sampler2D fontTex, float ascii, vec2 p){\n"
    "    if (p.x < 0.0 || p.x >= 1.0 || p.y < 0.0 || p.y >= 1.0) return 0.0;\n"
    "    float idx = clamp(floor(ascii) - 32.0, 0.0, 95.0);\n"
    "    float col = mod(idx, 16.0);\n"
    "    float row = floor(idx / 16.0);\n"
    "    // Caller passes p with x left->right, y top->down across the cell.\n"
    "    // Atlas is stored top-row-first; GL v=0 is the bottom, so flip p.y.\n"
    "    vec2 cell = vec2(p.x, 1.0 - p.y);\n"
    "    vec2 uv = (vec2(col, row) + cell) / vec2(16.0, 6.0);\n"
    "    // LINEAR-filtered atlas gives a ramp at ink edges; smoothstep it against\n"
    "    // the 0.5 iso-line with a fwidth-wide band for a clean anti-aliased edge.\n"
    "    float s = texture(fontTex, uv).r;\n"
    "    float w = max(fwidth(s), 0.001);\n"
    "    return smoothstep(0.5 - w, 0.5 + w, s);\n"
    "}\n"
    "// convenience: draw glyph at pixel `pos`, cell height `s` (width ~0.6*s).\n"
    "float nwChar(sampler2D fontTex, float ascii, vec2 fragPx, vec2 pos, float s){\n"
    "    vec2 p = (fragPx - pos) / vec2(s * 0.6, s);\n"
    "    return nwGlyph(fontTex, ascii, p);\n"
    "}\n"
    "// 0..15 -> ascii of hex digit 0-9 A-F.\n"
    "float nwHexDigit(float v){ v = clamp(floor(v + 0.5), 0.0, 15.0); return v < 10.0 ? 48.0 + v : 55.0 + v; }\n"
    "// 0..9 -> ascii of that decimal digit.\n"
    "float nwDigit(float v){ return 48.0 + clamp(floor(v + 0.5), 0.0, 9.0); }\n"
    "\n";

/* Continuation (split for the C99 4095-char string-literal limit). */
static const char *neowall_glsl_stdlib4 =
    "// ---- live terminal (bind a channel to \"terminal\") --------------------\n"
    "// nwTermCell(cell, frac, cw, ch) composites ONE terminal cell: it resolves\n"
    "// the glyph rect + colours packed by term_render.c, samples the coverage\n"
    "// atlas iTermAtlas, and returns fg-over-bg (incl. underline/strike/cursor).\n"
    "// nwTerm(uv) tiles it across the unit square; nwTermFX(uv) adds bloom, a\n"
    "// phosphor scanline and a gentle CRT curve on top.\n"
    "vec3 nwTermCell(ivec2 cell, vec2 frac, float cw, float ch, bool drawCursor){\n"
    "    uvec4 rec = texelFetch(iTermCells, cell, 0);\n"
    "    // decode fg (rec.b) and bg (rec.a low byte carries attrs)\n"
    "    vec3 fg = vec3(float((rec.b>>24)&0xFFu), float((rec.b>>16)&0xFFu), float((rec.b>>8)&0xFFu))/255.0;\n"
    "    vec3 bg = vec3(float((rec.a>>24)&0xFFu), float((rec.a>>16)&0xFFu), float((rec.a>>8)&0xFFu))/255.0;\n"
    "    uint attr = rec.a & 0xFFu;   // BOLD1 FAINT2 ITALIC4 UNDER8 BLINK16 REV32 INV64 STRIKE128\n"
    "    // FAINT/dim: pull fg halfway to bg.\n"
    "    if ((attr & 2u) != 0u) fg = mix(bg, fg, 0.55);\n"
    "    // BLINK: fade the glyph fg on a ~1.2s cycle (reverse/bg untouched).\n"
    "    float blinkA = 1.0;\n"
    "    if ((attr & 16u) != 0u) blinkA = 0.35 + 0.65 * step(0.5, fract(iTime * 0.83));\n"
    "    vec3 col = bg;\n"
    "    // glyph present?  rec.r bit0\n"
    "    if ((rec.r & 1u) != 0u) {\n"
    "        float ax = float((rec.r>>20)&0xFFFu);\n"
    "        float ay = float((rec.r>>8)&0xFFFu);\n"
    "        float gw = float((rec.g>>24)&0xFFu);\n"
    "        float gh = float((rec.g>>16)&0xFFu);\n"
    "        float ox = float((rec.g>>8)&0xFFu) - 128.0;\n"
    "        float oy = float(rec.g&0xFFu) - 128.0;\n"
    "        // pixel position within the cell (top-left origin, y down)\n"
    "        vec2 px = vec2(frac.x * cw, frac.y * ch);\n"
    "        vec2 gp = px - vec2(ox, oy);           // into glyph-local px\n"
    "        if (gp.x >= 0.0 && gp.x < gw && gp.y >= 0.0 && gp.y < gh) {\n"
    "            // rec.r bit1 = COLOR emoji: sample the RGBA atlas directly and\n"
    "            // alpha-composite over bg (no fg tint, no blink/underline).\n"
    "            if ((rec.r & 2u) != 0u) {\n"
    "                vec2 gpc2 = clamp(gp, vec2(0.5), vec2(gw, gh) - 0.5);\n"
    "                vec2 cuv = (vec2(ax, ay) + gpc2) / iTermAtlasSize;\n"
    "                vec4 e = texture(iTermColorAtlas, cuv);\n"
    "                col = mix(bg, e.rgb, e.a);\n"
    "                if (drawCursor && iTermCursor.z > 0.5 && cell.x == int(iTermCursor.x) && cell.y == int(iTermCursor.y))\n"
    "                    col = vec3(1.0) - col;\n"
    "                return col;\n"
    "            }\n"
    "            // Clamp to the glyph's own texel centres so the LINEAR fetch\n"
    "            // never reaches the 1px atlas gutter (=0) at cell edges; that\n"
    "            // bleed is a thin dark seam per cell that breaks full-cell\n"
    "            // braille/block graph runs into a dotted trail.\n"
    "            vec2 gpc = clamp(gp, vec2(0.5), vec2(gw, gh) - 0.5);\n"
    "            // SMOOTH AA: 5 bilinear taps in a half-texel quincunx (centre +\n"
    "            // 4 corners). Each tap is already box-filtered by the LINEAR 4x\n"
    "            // atlas; averaging the quincunx removes the residual stair-step\n"
    "            // a single tap leaves on diagonals/curves.\n"
    "            vec2 tpx = vec2(0.5) / iTermAtlasSize;\n"
    "            vec2 b0 = (vec2(ax, ay) + gpc) / iTermAtlasSize;\n"
    "            float cov = texture(iTermAtlas, b0).r * 0.5\n"
    "                      + texture(iTermAtlas, b0 + vec2( tpx.x,  tpx.y)).r * 0.125\n"
    "                      + texture(iTermAtlas, b0 + vec2(-tpx.x,  tpx.y)).r * 0.125\n"
    "                      + texture(iTermAtlas, b0 + vec2( tpx.x, -tpx.y)).r * 0.125\n"
    "                      + texture(iTermAtlas, b0 + vec2(-tpx.x, -tpx.y)).r * 0.125;\n"
    "            // Gamma-correct the coverage so the edge ramp is perceptually\n"
    "            // even (a raw sRGB mix makes dark-on-light too thin and mid-gray\n"
    "            // edges muddy). 0.714 ~= 1/1.4 lifts the mid coverage.\n"
    "            cov = pow(clamp(cov, 0.0, 1.0), 0.714) * blinkA;\n"
    "            col = mix(bg, fg, cov);\n"
    "        }\n"
    "    }\n";

/* Continuation (split again for the C99 4095-char string-literal limit). */
static const char *neowall_glsl_stdlib5 =
    "    // UNDERLINE / STRIKETHROUGH: draw a fg-coloured bar across the cell.\n"
    "    // frac.y is 0 at the cell top, 1 at the bottom. Bar thickness scales\n"
    "    // with the cell so it stays ~1-2px. Uses smoothstep for a soft edge.\n"
    "    if ((attr & (8u|128u)) != 0u && (attr & 64u) == 0u) {\n"
    "        float th = max(1.0 / ch, 0.6 / ch);\n"
    "        float t = 1.5 / ch;                    // half-thickness in frac units\n"
    "        if ((attr & 8u) != 0u) {               // underline near the baseline\n"
    "            float d = abs(frac.y - 0.90);\n"
    "            col = mix(col, fg, (1.0 - smoothstep(t, t + th, d)) * blinkA);\n"
    "        }\n"
    "        if ((attr & 128u) != 0u) {             // strikethrough mid-cell\n"
    "            float d = abs(frac.y - 0.50);\n"
    "            col = mix(col, fg, (1.0 - smoothstep(t, t + th, d)) * blinkA);\n"
    "        }\n"
    "    }\n"
    "    // block cursor: invert the cell it sits on\n"
    "    if (drawCursor && iTermCursor.z > 0.5 && cell.x == int(iTermCursor.x) && cell.y == int(iTermCursor.y))\n"
    "        col = vec3(1.0) - col;\n"
    "    return col;\n"
    "}\n"
    "\n"
    "// nwTerm(uv): the crisp grid across the unit square. uv.x left->right,\n"
    "// uv.y BOTTOM->top (GL). Returns linear RGB, no post effects. This is the\n"
    "// stable entry point user styling shaders sample.\n"
    "vec3 nwTerm(vec2 uv){\n"
    "    if (iTermInfo.x < 1.0) return vec3(0.0);\n"
    "    float cols = iTermInfo.x, rows = iTermInfo.y;\n"
    "    float cw = iTermInfo.z, ch = iTermInfo.w;\n"
    "    vec2 grid = vec2(uv.x * cols, (1.0 - uv.y) * rows);\n"
    "    if (grid.x < 0.0 || grid.x >= cols || grid.y < 0.0 || grid.y >= rows)\n"
    "        return vec3(0.0);\n"
    "    ivec2 cell = ivec2(floor(grid));\n"
    "    vec2  frac = grid - vec2(cell);\n"
    "    return nwTermCell(cell, frac, cw, ch, true);\n"
    "}\n"
    "\n";

/* Continuation: the enhanced terminal path. Split for the 4095-char limit. */
static const char *neowall_glsl_stdlib6 =
    "// iTermFX packs the post-effect intensities the engine feeds the built-in\n"
    "// terminal pass: x=bloom, y=scanline, z=CRT curve, w=chromatic aberration.\n"
    "// All 0 => nwTermFX is byte-identical to nwTerm. Absent (user styling\n"
    "// shader) => treated as 0 via the default below.\n"
    "#ifndef NW_HAS_ITERMFX\n"
    "const vec4 iTermFX = vec4(0.0);\n"
    "#endif\n"
    "\n"
    "// nwTermSampleGrid: like nwTerm at an arbitrary uv, but WITHOUT the built-in\n"
    "// block cursor (nwTermFX draws its own sliding cursor on top) and clamped so\n"
    "// CRT/bloom taps that stray off the unit square read black (the bezel).\n"
    "vec3 nwTermSampleGrid(vec2 uv){\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);\n"
    "    float cols = iTermInfo.x, rows = iTermInfo.y;\n"
    "    float cw = iTermInfo.z, ch = iTermInfo.w;\n"
    "    vec2 grid = vec2(uv.x * cols, (1.0 - uv.y) * rows);\n"
    "    if (grid.x < 0.0 || grid.x >= cols || grid.y < 0.0 || grid.y >= rows)\n"
    "        return vec3(0.0);\n"
    "    ivec2 cell = ivec2(floor(grid));\n"
    "    vec2  frac = grid - vec2(cell);\n"
    "    return nwTermCell(cell, frac, cw, ch, false);\n"
    "}\n"
    "\n"
    "// nwTermFX(uv): the built-in enhanced terminal look. A real terminal blits\n"
    "// cells; because we render the grid procedurally we can add, for free:\n"
    "//   - a subtle CRT barrel warp + vignette (iTermFX.z)\n"
    "//   - per-channel chromatic aberration at the edges (iTermFX.w)\n"
    "//   - brightness-keyed bloom: only bright/bold cells bleed light (iTermFX.x)\n"
    "//   - a scanline that tracks the cell grid, not the pixel grid (iTermFX.y)\n"
    "vec3 nwTermFX(vec2 uv){\n"
    "    if (iTermInfo.x < 1.0) return vec3(0.0);\n"
    "    vec2 warped = uv;\n"
    "    // CRT barrel: push uv outward from centre by a small r^2 term.\n"
    "    if (iTermFX.z > 0.001) {\n"
    "        vec2 c = uv * 2.0 - 1.0;                 // -1..1\n"
    "        float r2 = dot(c, c);\n"
    "        c *= 1.0 + iTermFX.z * 0.12 * r2;        // gentle bulge\n"
    "        warped = c * 0.5 + 0.5;\n"
    "    }\n"
    "    // Chromatic aberration: split the RGB sample points radially.\n"
    "    vec3 base;\n"
    "    if (iTermFX.w > 0.001) {\n"
    "        vec2 dir = (warped - 0.5);\n"
    "        float amt = iTermFX.w * 0.0025 * dot(dir, dir) * 4.0;\n"
    "        base = vec3(nwTermSampleGrid(warped + dir * amt).r,\n"
    "                    nwTermSampleGrid(warped).g,\n"
    "                    nwTermSampleGrid(warped - dir * amt).b);\n"
    "    } else {\n"
    "        base = nwTermSampleGrid(warped);\n"
    "    }\n"
    "    vec3 col = base;\n"
    "    // Change-driven fade: cells whose record just changed briefly pulse\n"
    "    // brighter, then ease back — so graph bars and updating numbers glide\n"
    "    // into place instead of hard-snapping. A real terminal has no history\n"
    "    // to do this; we do, via the R32UI per-cell last-change stamps.\n"
    "    if (iTermFade.x > 0.001) {\n"
    "        float cols = iTermInfo.x, rows = iTermInfo.y;\n"
    "        ivec2 fc = ivec2(int(warped.x * cols), int((1.0 - warped.y) * rows));\n"
    "        fc = clamp(fc, ivec2(0), ivec2(int(cols)-1, int(rows)-1));\n"
    "        uint chg = texelFetch(iTermChange, fc, 0).r;\n"
    "        float age = (iTermFade.y - float(chg)) / 1000.0;   // seconds since change\n"
    "        if (chg > 0u && age >= 0.0) {\n"
    "            // pulse: 0 at t=0, peak ~40ms, gone by ~260ms.\n"
    "            float p = age * 8.0 * exp(-age / 0.09);\n"
    "            col += col * clamp(p, 0.0, 1.0) * iTermFade.x * 0.6;\n"
    "        }\n"
    "    }\n"
    "    // Bloom: gather a few taps, keep only the energy above a threshold so\n"
    "    // dim text stays crisp and bright/bold cells glow. Taps are in cell\n"
    "    // units so the halo is font-scale-independent.\n"
    "    if (iTermFX.x > 0.001) {\n"
    "        vec2 cs = vec2(iTermInfo.z / max(iResolution.x,1.0),\n"
    "                       iTermInfo.w / max(iResolution.y,1.0));\n"
    "        vec3 acc = vec3(0.0);\n"
    "        acc += nwTermSampleGrid(warped + vec2( cs.x, 0.0));\n"
    "        acc += nwTermSampleGrid(warped + vec2(-cs.x, 0.0));\n"
    "        acc += nwTermSampleGrid(warped + vec2(0.0,  cs.y));\n"
    "        acc += nwTermSampleGrid(warped + vec2(0.0, -cs.y));\n"
    "        acc += nwTermSampleGrid(warped + cs) + nwTermSampleGrid(warped - cs);\n"
    "        acc /= 6.0;\n"
    "        float e = max(max(acc.r, acc.g), acc.b);\n"
    "        vec3 bright = acc * smoothstep(0.45, 0.9, e);   // threshold\n"
    "        col += bright * iTermFX.x * 0.6;\n"
    "    }\n";

/* Continuation of nwTermFX (same function body, split for the 4095-char
 * limit — the strings are concatenated at injection). */
static const char *neowall_glsl_stdlib7 =
    "    // Phosphor scanline locked to the cell rows: a faint dark band across\n"
    "    // the lower part of every glyph row. Cell-locked, not pixel-locked, so\n"
    "    // it never moires with the framebuffer.\n"
    "    if (iTermFX.y > 0.001) {\n"
    "        float row = (1.0 - warped.y) * iTermInfo.y;\n"
    "        float s = 0.5 + 0.5 * cos(fract(row) * 6.2831853);\n"
    "        col *= 1.0 - iTermFX.y * 0.12 * (1.0 - s);\n"
    "    }\n"
    "    // Vignette rides with the CRT curve.\n"
    "    if (iTermFX.z > 0.001) {\n"
    "        vec2 c = warped * 2.0 - 1.0;\n"
    "        float v = 1.0 - iTermFX.z * 0.18 * dot(c, c);\n"
    "        col *= clamp(v, 0.0, 1.0);\n"
    "    }\n"
    "    // Physical cursor: slide a soft glowing box from the previous cell to\n"
    "    // the current one over ~110ms with a little overshoot, instead of the\n"
    "    // hard per-cell invert. A real terminal can only teleport the cursor.\n"
    "    if (iTermCursor.z > 0.5) {\n"
    "        float dt = max(iTime - iTermCursorPrev.z, 0.0);\n"
    "        float t = clamp(dt / 0.11, 0.0, 1.0);\n"
    "        // ease-out-back: quick start, tiny overshoot, settle.\n"
    "        float e = 1.0 + 2.70158 * pow(t-1.0,3.0) + 1.70158 * pow(t-1.0,2.0);\n"
    "        vec2 curCell = mix(iTermCursorPrev.xy, iTermCursor.xy, e);\n"
    "        // cell-space position of this fragment (top-left origin)\n"
    "        vec2 gpos = vec2(warped.x * iTermInfo.x, (1.0 - warped.y) * iTermInfo.y);\n"
    "        vec2 d = abs(gpos - (curCell + 0.5));\n"
    "        // soft box the size of one cell, feathered at the edge.\n"
    "        float box = (1.0 - smoothstep(0.35, 0.55, d.x)) *\n"
    "                    (1.0 - smoothstep(0.40, 0.60, d.y));\n"
    "        // subtle motion trail: a fading streak along the travel direction.\n"
    "        vec2 mv = iTermCursor.xy - iTermCursorPrev.xy;\n"
    "        float trail = 0.0;\n"
    "        if (dot(mv,mv) > 0.001 && t < 1.0) {\n"
    "            vec2 nd = normalize(mv);\n"
    "            float along = dot(gpos - (curCell+0.5), -nd);\n"
    "            float perp  = length((gpos-(curCell+0.5)) - (-nd)*along);\n"
    "            trail = clamp(along, 0.0, 2.0) * 0.5 *\n"
    "                    (1.0 - smoothstep(0.0, 0.5, perp)) * (1.0 - t);\n"
    "        }\n"
    "        float glow = clamp(box + trail, 0.0, 1.0);\n"
    "        col = mix(col, vec3(1.0) - col, box);          // invert under the box\n"
    "        col += vec3(0.25, 0.55, 0.9) * glow * 0.35;    // cool phosphor halo\n"
    "    }\n"
    "    return col;\n"
    "}\n"
    "\n";

/* Chunk 8: the scene kit.
 *
 * Everything above is a toolbox of primitives; this is the assembled machine.
 * The bundled shaders show why it is needed -- twelve of them hand-roll the
 * same raymarch loop and fourteen redefine the same 2x2 rotation, and a
 * good-looking 3D scene costs 200-500 lines before it looks like anything.
 * That is the real barrier to "drop in a file and get something impressive".
 *
 * So this chunk provides the loop, the normals, the lighting and the camera,
 * and asks the shader for exactly one thing: a distance function. A complete
 * lit, shadowed, fogged, tonemapped scene becomes:
 *
 *     float nwMap(vec3 p){ return nwSdSphere(p, 1.0); }
 *     void mainImage(out vec4 o, vec2 u){
 *         o = vec4(nwRender(nwCameraOrbit(u, 4.0, iTime*0.2, 0.3)), 1.0);
 *     }
 *
 * nwMap is a FORWARD DECLARATION: the shader defines it, the kit calls it.
 * GLSL resolves that at link time within the translation unit, which is why
 * the whole kit can live in the injected prelude.
 */
static const char *neowall_glsl_stdlib8 =
    "// ============================================================\n"
    "// Scene kit: a raymarcher you drive with one distance function.\n"
    "//\n"
    "// Define nwMap(vec3) and call nwRender(ray). Everything else -- marching,\n"
    "// normals, soft shadows, ambient occlusion, sky, fog, tonemapping -- is\n"
    "// handled here. Override any piece by defining your own before use.\n"
    "// ============================================================\n"
    "\n"
    "// The shader supplies this. Signed distance to the nearest surface.\n"
    "float nwMap(vec3 p);\n"
    "// Surface albedo. Declared here so nwRender can call it whether it comes\n"
    "// from the kit's default below or from the shader's own definition, which\n"
    "// is appended after this whole prelude.\n"
    "vec3 nwMaterial(vec3 p, vec3 n);\n"
    "// Optional surface response: x=roughness (0 mirror .. 1 chalk), y=metalness.\n"
    "// Metals tint their reflection by the albedo and lose diffuse.\n"
    "vec2 nwGloss(vec3 p, vec3 n);\n"
    "// Optional emission, added after lighting. Use for neon, lava, screens.\n"
    "vec3 nwEmissive(vec3 p, vec3 n);\n"
    "\n"
    "// A camera ray: where it starts and which way it points.\n"
    "struct nwRay { vec3 ro; vec3 rd; };\n"
    "\n"
    "// Pixel -> ray, looking at the origin from an orbiting position.\n"
    "// uv is raw fragCoord; dist is orbit radius; yaw/pitch in radians.\n"
    "nwRay nwCameraOrbit(vec2 uv, float dist, float yaw, float pitch){\n"
    "    vec2 p = (uv - 0.5*iResolution.xy) / iResolution.y;\n"
    "    float cp = cos(pitch);\n"
    "    vec3 ro = dist * vec3(cp*sin(yaw), sin(pitch), cp*cos(yaw));\n"
    "    vec3 fw = normalize(-ro);\n"
    "    vec3 rt = normalize(cross(vec3(0.0,1.0,0.0), fw));\n"
    "    vec3 up = cross(fw, rt);\n"
    "    nwRay r; r.ro = ro; r.rd = normalize(p.x*rt + p.y*up + 1.4*fw);\n"
    "    return r;\n"
    "}\n"
    "\n"
    "// Free camera: explicit eye and target.\n"
    "nwRay nwCameraLookAt(vec2 uv, vec3 eye, vec3 target, float zoom){\n"
    "    vec2 p = (uv - 0.5*iResolution.xy) / iResolution.y;\n"
    "    vec3 fw = normalize(target - eye);\n"
    "    vec3 rt = normalize(cross(vec3(0.0,1.0,0.0), fw));\n"
    "    vec3 up = cross(fw, rt);\n"
    "    nwRay r; r.ro = eye; r.rd = normalize(p.x*rt + p.y*up + zoom*fw);\n"
    "    return r;\n"
    "}\n"
    "\n"
    "// March until we hit something. Returns distance travelled, or -1.0.\n"
    "// Step scaling below 1.0 keeps thin//warped fields from overshooting.\n"
    "float nwMarch(vec3 ro, vec3 rd, float tmax){\n"
    "    float t = 0.0;\n"
    "    for (int i = 0; i < 128; i++){\n"
    "        vec3 p = ro + rd*t;\n"
    "        float d = nwMap(p);\n"
    "        if (d < 0.0005*t) return t;\n"
    "        t += d*0.9;\n"
    "        if (t > tmax) break;\n"
    "    }\n"
    "    return -1.0;\n"
    "}\n"
    "\n"
    "// Gradient of the distance field = surface normal. Tetrahedron sampling:\n"
    "// four taps instead of six, same quality.\n"
    "vec3 nwNormal(vec3 p){\n"
    "    vec2 e = vec2(1.0,-1.0)*0.0008;\n"
    "    return normalize(e.xyy*nwMap(p+e.xyy) + e.yyx*nwMap(p+e.yyx) +\n"
    "                     e.yxy*nwMap(p+e.yxy) + e.xxx*nwMap(p+e.xxx));\n"
    "}\n"
    "\n"
    "\n";

/* Chunk 8a2: shadowing, occlusion and sky. Split purely for the C99 4095-char
 * literal limit. */
static const char *neowall_glsl_stdlib8a2 =
    "// Soft shadow with penumbra, using Sebastian Aaltonen's improvement on the\n"
    "// classic h/t estimator (GDC 2016): instead of sampling the ratio only at\n"
    "// the marched points, triangulate the closest approach BETWEEN two steps.\n"
    "// The naive version misses the darkest penumbra whenever it falls between\n"
    "// samples, which shows up as banding along sharp shadow-caster corners.\n"
    "// k is inverse light size: larger k = smaller light = sharper shadow.\n"
    "float nwShadow(vec3 p, vec3 ldir, float k){\n"
    "    float res = 1.0, t = 0.02, ph = 1e20;\n"
    "    for (int i = 0; i < 48; i++){\n"
    "        float h = nwMap(p + ldir*t);\n"
    "        if (h < 0.0008) return 0.0;\n"
    "        float y = h*h/(2.0*ph);\n"
    "        float d = sqrt(max(h*h - y*y, 0.0));\n"
    "        res = min(res, d/(max(0.0, t - y)/k));\n"
    "        ph = h;\n"
    "        t += clamp(h, 0.01, 0.3);\n"
    "        if (res < 0.004 || t > 14.0) break;\n"
    "    }\n"
    "    return clamp(res, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "// Ambient occlusion: how enclosed is this point.\n"
    "float nwAO(vec3 p, vec3 n){\n"
    "    float occ = 0.0, sca = 1.0;\n"
    "    for (int i = 0; i < 5; i++){\n"
    "        float h = 0.01 + 0.12*float(i)/4.0;\n"
    "        occ += (h - nwMap(p + n*h))*sca;\n"
    "        sca *= 0.95;\n"
    "    }\n"
    "    return clamp(1.0 - 3.0*occ, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "// Default sky. Time-of-day aware, so a scene drifts with the real sun\n"
    "// without the shader asking for it.\n"
    "vec3 nwSky(vec3 rd){\n"
    "    vec3 day   = mix(vec3(0.52,0.70,0.95), vec3(0.11,0.28,0.62), clamp(rd.y*1.3,0.0,1.0));\n"
    "    vec3 night = mix(vec3(0.05,0.07,0.14), vec3(0.01,0.02,0.05), clamp(rd.y*1.3,0.0,1.0));\n"
    "    vec3 col = mix(night, day, nwDayNight());\n"
    "    float sun = pow(clamp(dot(rd, normalize(vec3(0.5,0.42,0.3))),0.0,1.0), 32.0);\n"
    "    return col + vec3(1.0,0.82,0.55)*sun*nwDayNight();\n"
    "}\n"
    "\n";

/* Chunk 8b: shading and composition. Split from 8a only because ISO C99
 * guarantees just 4095 characters per string literal, the same reason the
 * chunks above are split; the two are concatenated back-to-back at injection
 * and are a single unit semantically. */
/* Per-hook defaults for the scene kit. Kept as separate strings, not one
 * chunk, so a shader overriding nwMaterial still gets the stock nwGloss and
 * nwEmissive. Index order must match `scene_hooks` in shader_multipass.c. */
static const char *const neowall_scene_hook_defaults[] = {
    /* nwMaterial */
    "vec3 nwMaterial(vec3 p, vec3 n){ return vec3(0.62); }\n",
    /* nwGloss: x = roughness, y = metalness. */
    "vec2 nwGloss(vec3 p, vec3 n){ return vec2(0.55, 0.0); }\n",
    /* nwEmissive */
    "vec3 nwEmissive(vec3 p, vec3 n){ return vec3(0.0); }\n",
};

/* Chunk 8c: the renderer proper. Separated from the default material so the
 * material can be withheld independently when the shader defines its own --
 * a preprocessor guard cannot do this, because the user's #define is appended
 * AFTER the prelude and so is not yet visible here. */
static const char *neowall_glsl_stdlib8c =
    "// The whole pipeline: march, light, shadow, occlude, reflect, fog, tonemap.\n"
    "// Lighting is a three-source rig -- key sun, sky dome, bounce -- which is\n"
    "// what makes an SDF scene read as lit rather than flat-shaded.\n"
    "vec3 nwRender(nwRay r){\n"
    "    float t = nwMarch(r.ro, r.rd, 40.0);\n"
    "    if (t < 0.0) return nwSky(r.rd);\n"
    "\n"
    "    vec3 p = r.ro + r.rd*t;\n"
    "    vec3 n = nwNormal(p);\n"
    "    vec3 l = normalize(vec3(0.5, 0.42, 0.3));\n"
    "    vec3 v = -r.rd;\n"
    "\n"
    "    vec3  alb   = nwMaterial(p, n);\n"
    "    vec2  gl    = nwGloss(p, n);\n"
    "    float rough = clamp(gl.x, 0.03, 1.0);\n"
    "    float metal = clamp(gl.y, 0.0, 1.0);\n"
    "\n"
    "    float sha = nwShadow(p, l, 16.0);\n"
    "    float dif = clamp(dot(n, l),0.0,1.0) * sha;\n"
    "    float sky = clamp(0.5 + 0.5*n.y, 0.0, 1.0);\n"
    "    float ao  = nwAO(p, n);\n"
    "    // Bounce light from the ground, the cheap trick that stops undersides\n"
    "    // from going flat black.\n"
    "    float bnc = clamp(0.5 - 0.5*n.y, 0.0, 1.0)*ao;\n"
    "\n"
    "    // Blinn-Phong specular with roughness mapped to an exponent, plus a\n"
    "    // Schlick fresnel so grazing angles brighten like real surfaces.\n"
    "    vec3  h    = normalize(l + v);\n"
    "    float shin = mix(256.0, 8.0, rough);\n"
    "    float spe  = pow(clamp(dot(n, h),0.0,1.0), shin) * dif;\n"
    "    float fres = pow(1.0 - clamp(dot(n, v),0.0,1.0), 5.0);\n"
    "\n"
    "    vec3 lin = vec3(1.10,0.95,0.80)*dif*1.6\n"
    "             + vec3(0.28,0.36,0.52)*sky*ao*0.9\n"
    "             + vec3(0.22,0.18,0.14)*bnc*0.5\n"
    "             + vec3(0.10)*ao;\n"
    "\n"
    "    // Metals have no diffuse and tint their highlight by the albedo.\n"
    "    vec3 col = alb*lin*(1.0 - metal);\n"
    "    vec3 specTint = mix(vec3(1.0), alb, metal);\n"
    "    col += specTint*spe*mix(0.6, 2.2, metal);\n"
    "\n"
    "    // One reflection bounce off the sky, weighted by fresnel and smoothness.\n"
    "    // Cheap (no second march) but it is what sells metal and wet surfaces.\n"
    "    float refl = mix(0.04, 1.0, metal) * mix(fres, 1.0, metal) * (1.0 - rough);\n"
    "    col = mix(col, nwSky(reflect(r.rd, n))*mix(vec3(1.0), alb, metal), refl*ao);\n"
    "\n"
    "    col += nwEmissive(p, n);\n"
    "    col = mix(col, nwSky(r.rd), 1.0 - exp(-0.0016*t*t));  // distance fog\n"
    "    return nwGamma(nwTonemap(col));\n"
    "}\n"
    "\n"
    "// ---- composition helpers ----\n"
    "// Repeat space on a grid: one primitive becomes an infinite field.\n"
    "vec3 nwRepeat(vec3 p, vec3 period){ return mod(p + 0.5*period, period) - 0.5*period; }\n"
    "vec2 nwRepeat2(vec2 p, vec2 period){ return mod(p + 0.5*period, period) - 0.5*period; }\n"
    "// Which cell are we in -- feed to nwHash21 for per-cell variation.\n"
    "vec2 nwCellId(vec2 p, vec2 period){ return floor((p + 0.5*period)/period); }\n"
    "// Repeat a LIMITED number of times, so the field stays finite. Without the\n"
    "// clamp, mod() tiles to infinity and every shadow ray marches forever.\n"
    "vec3 nwRepeatLim(vec3 p, float period, vec3 limit){\n"
    "    return p - period*clamp(round(p/period), -limit, limit);\n"
    "}\n"
    "// Mirror space about an axis: model half, get both halves.\n"
    "vec3 nwMirrorX(vec3 p){ p.x = abs(p.x); return p; }\n"
    "// Polar repetition: n copies around the Y axis. Good for wheels, flowers.\n"
    "vec3 nwPolarRepeat(vec3 p, float n){\n"
    "    float a = atan(p.z, p.x);\n"
    "    float seg = NW_TAU/n;\n"
    "    a = mod(a + 0.5*seg, seg) - 0.5*seg;\n"
    "    float r = length(p.xz);\n"
    "    return vec3(r*cos(a), p.y, r*sin(a));\n"
    "}\n"
    "// Bend and twist space around Y.\n"
    "vec3 nwTwist(vec3 p, float amount){ p.xz = nwRot(p.y*amount)*p.xz; return p; }\n"
    "vec3 nwBend(vec3 p, float amount){ p.xy = nwRot(p.x*amount)*p.xy; return p; }\n"
    "// Ground plane at height h, so scenes have something to cast onto.\n"
    "float nwGround(vec3 p, float h){ return p.y - h; }\n"
    "\n"
    "\n";

/* Chunk 8d: extra primitives and operators. Split for the C99 literal limit. */
static const char *neowall_glsl_stdlib8d =
    "// ---- more primitives (exact SDFs, after Inigo Quilez) ----\n"
    "float nwSdRoundBox(vec3 p, vec3 b, float r){\n"
    "    vec3 q = abs(p) - b + r;\n"
    "    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;\n"
    "}\n"
    "float nwSdCapsule(vec3 p, vec3 a, vec3 b, float r){\n"
    "    vec3 pa = p-a, ba = b-a;\n"
    "    float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);\n"
    "    return length(pa - ba*h) - r;\n"
    "}\n"
    "float nwSdCylinder(vec3 p, float r, float h){\n"
    "    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r,h);\n"
    "    return min(max(d.x,d.y),0.0) + length(max(d,0.0));\n"
    "}\n"
    "float nwSdCone(vec3 p, float h, float r){\n"
    "    vec2 q = vec2(length(p.xz), p.y);\n"
    "    vec2 tip = q - vec2(0.0, h);\n"
    "    vec2 mantleDir = normalize(vec2(h, r));\n"
    "    float mantle = dot(tip, mantleDir);\n"
    "    float d = max(mantle, -q.y);\n"
    "    float projected = dot(tip, vec2(mantleDir.y, -mantleDir.x));\n"
    "    if (q.y > h && projected < 0.0) d = max(d, length(tip));\n"
    "    if (q.x > r && projected > length(vec2(h,r))) d = max(d, length(q - vec2(r,0.0)));\n"
    "    return d;\n"
    "}\n"
    "float nwSdOctahedron(vec3 p, float s){\n"
    "    p = abs(p); return (p.x+p.y+p.z-s)*0.57735027;\n"
    "}\n"
    "// Hollow out any shape: turn a solid into a shell of given thickness.\n"
    "float nwOpOnion(float d, float thickness){ return abs(d) - thickness; }\n"
    "// Carve b out of a, and keep only the overlap, both smoothed.\n"
    "float nwOpSmoothInter(float a, float b, float k){\n"
    "    float h = clamp(0.5 - 0.5*(b-a)/k, 0.0, 1.0);\n"
    "    return mix(b, a, h) + k*h*(1.0-h);\n"
    "}\n"
    "\n";

/* ---------------------------------------------------------------- *
 * Friendly aliases
 *
 * Unmodified Shadertoy shaders expect short, unprefixed helper names. We want
 * to offer them, but a name like `sdBox` is also exactly what a raymarching
 * shader is most likely to define for itself -- and in GLSL a duplicate
 * definition is a hard compile error (issue #82).
 *
 * So the aliases are DATA, not baked into the library text above. At injection
 * time the host scans the user's source (glsl_shadow.h) and emits only the
 * aliases whose names the shader left free. If you define `sdBox`, you get
 * your `sdBox`, with every neowall overload of that name withheld; the
 * canonical `nwSdBox` remains available either way.
 *
 * Each alias is emitted as a thin forwarding function rather than a `#define`
 * on purpose: a macro would rewrite the *user's own* later definition of that
 * token and reintroduce the very redefinition error we are avoiding.
 *
 * INVARIANT, enforced by tests/test_glsl_shadow.c: every unprefixed function
 * defined anywhere in this header must appear in this table. Add a helper
 * without adding its alias here and the test fails -- which is what keeps a
 * future edit from silently re-opening #82.
 */
typedef struct {
    const char *name; /* the unprefixed name the shader may also define */
    const char *decl; /* GLSL emitted when the name is free */
} neowall_stdlib_alias;

static const neowall_stdlib_alias neowall_stdlib_aliases[] = {
    /* audio */
    {"audioBand",
     "float audioBand(float lo,float hi){ return nwAudioBand(lo,hi); }\n"},
    {"spectrum", "float spectrum(float x){ return nwSpectrum(x); }\n"},
    {"waveform", "float waveform(float x){ return nwWaveform(x); }\n"},
    {"beat", "float beat(){ return nwBeat(); }\n"},

    /* 2D SDFs + ops */
    {"sdCircle", "float sdCircle(vec2 p,float r){ return nwSdCircle(p,r); }\n"},
    {"sdSegment",
     "float sdSegment(vec2 p,vec2 a,vec2 b){ return nwSdSegment(p,a,b); }\n"},
    {"sdHex", "float sdHex(vec2 p,float r){ return nwSdHex(p,r); }\n"},
    {"opSmoothUnion",
     "float opSmoothUnion(float a,float b,float k){ return nwOpSmoothUnion(a,b,k); }\n"},
    {"opSmoothSub",
     "float opSmoothSub(float a,float b,float k){ return nwOpSmoothSub(a,b,k); }\n"},

    /* 3D SDFs */
    {"sdSphere", "float sdSphere(vec3 p,float r){ return nwSdSphere(p,r); }\n"},
    {"sdTorus", "float sdTorus(vec3 p,vec2 t){ return nwSdTorus(p,t); }\n"},

    /* sdBox is overloaded; a shader defining either signature takes both. */
    {"sdBox",
     "float sdBox(vec2 p,vec2 b){ return nwSdBox(p,b); }\n"
     "float sdBox(vec3 p,vec3 b){ return nwSdBox(p,b); }\n"},

    /* reactive shaping */
    {"pulse", "float pulse(float x){ return nwPulse(x); }\n"},
    {"dayNight", "float dayNight(){ return nwDayNight(); }\n"},
    {"timeOfDayTint", "vec3 timeOfDayTint(){ return nwTimeOfDayTint(); }\n"},
};

#define NEOWALL_STDLIB_ALIAS_COUNT \
    (sizeof(neowall_stdlib_aliases) / sizeof(neowall_stdlib_aliases[0]))


#endif /* NEOWALL_SHADER_STDLIB_H */
