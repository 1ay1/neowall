//Ultra-optimized ocean shader - minimal iterations, aggressive simplifications
//Set to 2.0 for AA (not recommended for ultra mode)
#define AA 1.0

#define STEPS 30.0
#define MDIST 25.0
#define pi 3.14159265
#define rot(a) mat2(cos(a),sin(a),-sin(a),cos(a))
#define sat(a) clamp(a,0.0,1.0)

#define ITERS_TRACE 4
#define ITERS_NORM 8

#define HOR_SCALE 1.1
#define OCC_SPEED 1.0
#define DX_DET 0.65

#define FREQ 0.6
#define HEIGHT_DIV 2.5
#define WEIGHT_SCL 0.8
#define FREQ_SCL 1.2
#define TIME_SCL 1.095
#define WAV_ROT 1.21
#define DRAG 0.9
#define SCRL_SPEED 1.0
vec2 scrollDir = vec2(1,1);

//Pre-compute rotation matrices
mat2 wavRotMat;
mat2 sunRotY;
mat2 sunRotX;

void initRotations(){
    wavRotMat = rot(WAV_ROT);
    sunRotY = rot(-0.25);
    sunRotX = rot(-0.3);
}

vec2 wavedx(vec2 wavPos, int iters, float t){
    vec2 dx = vec2(0);
    vec2 wavDir = vec2(1,0);
    float wavWeight = 1.0;
    wavPos = (wavPos + t*SCRL_SPEED*scrollDir) * HOR_SCALE;
    float wavFreq = FREQ;
    float wavTime = OCC_SPEED*t;
    float invWeight = 1.0;

    for(int i=0;i<iters;i++){
        wavDir *= wavRotMat;
        float x = dot(wavDir,wavPos)*wavFreq+wavTime;
        float sinx = sin(x);
        float result = exp(sinx-1.)*cos(x)*wavWeight;
        dx += result*wavDir*invWeight;
        wavFreq *= FREQ_SCL;
        wavTime *= TIME_SCL;
        wavPos -= wavDir*result*DRAG;
        wavWeight *= WEIGHT_SCL;
        invWeight /= pow(WEIGHT_SCL, DX_DET);
    }
    float wavSum = -(pow(WEIGHT_SCL,float(iters))-1.)*HEIGHT_DIV;
    return dx/pow(wavSum,1.-DX_DET);
}

float wave(vec2 wavPos, int iters, float t){
    float wav = 0.0;
    vec2 wavDir = vec2(1,0);
    float wavWeight = 1.0;
    wavPos = (wavPos + t*SCRL_SPEED*scrollDir) * HOR_SCALE;
    float wavFreq = FREQ;
    float wavTime = OCC_SPEED*t;

    for(int i=0;i<iters;i++){
        wavDir *= wavRotMat;
        float x = dot(wavDir,wavPos)*wavFreq+wavTime;
        float w = exp(sin(x)-1.0)*wavWeight;
        wav += w;
        wavFreq *= FREQ_SCL;
        wavTime *= TIME_SCL;
        wavPos -= wavDir*w*DRAG*cos(x);
        wavWeight *= WEIGHT_SCL;
    }
    float wavSum = -(pow(WEIGHT_SCL,float(iters))-1.)*HEIGHT_DIV;
    return wav/wavSum;
}

vec3 norm(vec3 p){
    vec2 wav = -wavedx(p.xz, ITERS_NORM, iTime);
    return normalize(vec3(wav.x,1.0,wav.y));
}

float map(vec3 p){
    return p.y - wave(p.xz,ITERS_TRACE,iTime);
}

vec3 pal(float t, vec3 a, vec3 b, vec3 c, vec3 d){
    return a+b*cos(6.28318*(c*t+d));
}

vec3 spc(float n,float bright){
    return pal(n,vec3(bright),vec3(0.5),vec3(1.0),vec3(0.0,0.33,0.67));
}

float spec = 0.13;
vec3 specColor1;
vec3 specColor2;

vec3 sky(vec3 rd){
    float px = 1.5/min(iResolution.x,iResolution.y);
    vec3 col = vec3(0);

    //Sun
    vec3 rd2 = rd;
    rd2.yz *= sunRotY;
    rd2.xz *= sunRotX;
    float zFade = rd2.z*0.5+0.5;

    float a = length(rd2.xy);
    float rad = 0.075;
    float sFade = 2.5/min(iResolution.x,iResolution.y);
    vec3 sun = smoothstep(a-px-sFade,a+px+sFade,rad)*specColor1*zFade*2.;
    col += sun;
    col += rad/(rad+pow(a,1.7))*specColor1*zFade;
    col = col + mix(col,specColor2,sat(1.0-length(col)))*0.2;

    //Simplified clouds - fewer iterations
    float e = 0.;
    vec3 p = rd;
    p.xz *= 0.4;
    p.x += iTime*0.007;
    float t2 = iTime*0.02;

    //Unrolled cloud loop for performance
    p.xz *= rot(200.);
    p += 200.;
    e += abs(dot(sin(p*200.+t2)/200.,vec3(1.65)));

    p.xz *= rot(150.);
    p += 150.;
    e += abs(dot(sin(p*150.+t2)/150.,vec3(1.65)));

    p.xz *= rot(112.5);
    p += 112.5;
    e += abs(dot(sin(p*112.5+t2)/112.5,vec3(1.65)));

    e *= smoothstep(0.5,0.4,e-0.095);
    col += e*smoothstep(-0.02,0.3,rd.y)*0.8*(1.0-sun*3.75)*mix(specColor1,vec3(1),0.4);

    return col;
}

void render( out vec4 fragColor, in vec2 fragCoord ){
    vec2 uv = (fragCoord-0.5*iResolution.xy)/min(iResolution.y,iResolution.x);
    vec3 col = vec3(0);
    vec3 ro = vec3(0,2.475,-3.3);
    bool click = iMouse.z>0.;

    if(click){
        ro.yz *= rot(2.0*min(iMouse.y/iResolution.y-0.5,0.15));
        ro.zx *= rot(-7.0*(iMouse.x/iResolution.x-0.5));
    }

    vec3 lk = vec3(0,2,0);
    vec3 f = normalize(lk-ro);
    vec3 r = normalize(cross(vec3(0,1,0),f));
    vec3 rd = normalize(f*0.9+uv.x*r+uv.y*cross(f,r));

    float dO = 0.;
    bool hit = false;
    vec3 p;

    float tPln = -(ro.y-1.86)/rd.y;
    if(tPln>0. || click){
        if(!click) dO = tPln;

        for(float i = 0.; i<STEPS; i++){
            p = ro+rd*dO;
            float d = map(p);
            dO += d;
            if(abs(d)<0.015 || dO>MDIST){
                hit = abs(d)<0.015;
                break;
            }
        }
    }

    vec3 skyrd = sky(rd);

    if(hit){
        vec3 n = norm(p);
        vec3 rfl = reflect(rd,n);
        rfl.y = abs(rfl.y);

        float ndot = max(0.0, dot(-n, rd));
        float fres = pow(1. - ndot, 5.0);

        col += sky(rfl)*fres*0.9;

        //Simplified subsurface
        vec3 rf = refract(rd,n,0.7518796992);
        vec3 sunDir = vec3(0,0.15,1.0);
        sunDir.xz *= rot(0.3);
        float subRefract = pow(max(0.0, dot(rf,sunDir)),35.0);
        col += pow(spc(spec-0.1,0.5),vec3(2.2))*subRefract*2.5;

        //Water color
        vec3 rd2 = rd;
        rd2.xz *= sunRotX;
        float skyLen = length(skyrd);
        vec3 waterCol = sat(spc(spec-0.1,0.4))*(0.4*pow(min(p.y*0.7+0.9,1.8),4.)*skyLen*(rd2.z*0.15+0.85));
        col += waterCol*0.17;

        col = mix(col,skyrd,dO/MDIST);
    }
    else{
        col = skyrd;
    }

    col = sat(col);
    col = pow(col,vec3(0.87));
    col *= 1.0-0.8*pow(length(uv*vec2(0.8,1.)),2.7);
    fragColor = vec4(col,1.0);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    //Pre-compute colors and rotations once per frame
    initRotations();
    specColor1 = spc(spec-0.1,0.6)*0.85;
    specColor2 = spc(spec+0.1,0.8);

    if(AA == 1.0) {
        render(fragColor, fragCoord);
        return;
    }

    //AA path (not recommended for ultra mode)
    vec4 col = vec4(0);
    float px = 1.0/AA;
    for(float i = 0.; i < AA; i++){
        for(float j = 0.; j < AA; j++){
            vec4 col2;
            render(col2, fragCoord + vec2(px*i, px*j));
            col.rgb += col2.rgb;
        }
    }
    col /= AA*AA;
    fragColor = col;
}
