// Man has demonstrated that he is master of everything except his own nature
//---------------------------------------------------------------------------

// One morning we woke up when the sky was still dark.
// We walked half an hour through the forest,
// to reach the other side of the island,
// where the beach is facing the rising sun.
// The sun was already there, one half over the horizon.
// The sky was on fire.
// We swum in the sea, staring at the rising sun.

// visual parameters -------------------
const vec3 sunColor = vec3(1.5,.9,.7);
const vec3 lightColor = vec3(1.,.8,.7);
const vec3 darkColor = vec3(.2,.2,.3);
const vec3 baseSkyColor = vec3(.6,.7,.8);
const vec3 seaColor = vec3(.1,.3,.5);
const vec3 seaLight = vec3(.1,.45,.55);
//---------------------------------------

vec3 gamma( vec3 col, float g){
    return pow(col,vec3(g));
}


// clouds layered noise
float noiseLayer(vec2 uv){
    float t = (iTime+iMouse.x)/5.;
    uv.y -= t/60.; // clouds pass by
    float e = 0.;
    for(float j=1.; j<9.; j++){
        // shift each layer in different directions
        float timeOffset = t*mod(j,2.989)*.02 - t*.015;
        e += 1.-texture(iChannel0, uv * (j*1.789) + j*159.45 + timeOffset).r / j ;
    }
    e /= 3.5;
    return e;
}

// waves layered noise
float waterHeight(vec2 uv){
    float t = (iTime+iMouse.x);
    float e = 0.;
    for(float j=1.; j<6.; j++){
        // shift each layer in different directions
        float timeOffset = t*mod(j,.789)*.1 - t*.05;
        e += texture(iChannel1, uv * (j*1.789) + j*159.45 + timeOffset).r / j ;
    }
    e /= 6.;
    return e;
}

vec3 waterNormals(vec2 uv){
    uv.x *= .25;
    float eps = 0.008;
    vec3 n=vec3( waterHeight(uv) - waterHeight(uv+vec2(eps,0.)),
                 1.,
                 waterHeight(uv) - waterHeight(uv+vec2(0.,eps)));
   return normalize(n);
}


vec3 drawSky( vec2 uv, vec2 uvInit){

	float clouds = noiseLayer(uv);

    // clouds normals
    float eps = 0.1;
    vec3 n = vec3(	clouds - noiseLayer(uv+vec2(eps,0.)),
            		-.3,
             		clouds - noiseLayer(uv+vec2(0.,eps)));
    n = normalize(n);

    // fake lighting
    float l = dot(n, normalize(vec3(uv.x,-.2,uv.y+.5)));
    l = clamp(l,0.,1.);

    // clouds color	(color gradient from light)
    vec3 cloudColor = mix(baseSkyColor, darkColor, length(uvInit)*1.7);
    cloudColor = mix( cloudColor,sunColor, l );

    // sky color (color gradient on Y)
    vec3 skyColor = mix(lightColor , baseSkyColor, clamp(uvInit.y*2.,0.,1.) );
    skyColor = mix ( skyColor, darkColor, clamp(uvInit.y*3.-.8,0.,1.) );
    skyColor = mix ( skyColor, sunColor, clamp(-uvInit.y*10.+1.1,0.,1.) );

	// draw sun
    if(length(uvInit-vec2(0.,.04) )<.03){
     	skyColor += vec3(2.,1.,.8);
    }

   	// mix clouds and sky
    float cloudMix = clamp(0.,1.,clouds*4.-8.);
    vec3 color = mix( cloudColor, skyColor, clamp(cloudMix,0.,1.) );

    // draw islands on horizon
    uvInit.y = abs(uvInit.y);
    float islandHeight = texture(iChannel1, uvInit.xx/2.+.67).r/15. - uvInit.y + .978;
    islandHeight += texture(iChannel1, uvInit.xx*2.).r/60.;
    islandHeight = clamp(floor(islandHeight),0.,1.);
    vec3 landColor = mix(baseSkyColor, darkColor, length(uvInit)*1.5);
    color = mix(color, landColor, islandHeight);

    return color;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // center uv around horizon and manage ratio
	vec2 uvInit = fragCoord.xy / iResolution.xy;
    uvInit.x -= .5;
    uvInit.x *= iResolution.x/iResolution.y;
    uvInit.y -= 0.35;

    // perspective deform
    vec2 uv = uvInit;
    uv.y -=.01;
	uv.y = abs(uv.y);
    uv.y = log(uv.y)/2.;
    uv.x *= 1.-uv.y;
    uv *= .2;

    vec3 col = vec3(1.,1.,1.);

    // draw water
    if(uvInit.y < 0.){

        vec3 n = waterNormals(uv);

        // draw reflection of sky into water
        vec3 waterReflections = drawSky(uv+n.xz, uvInit+n.xz);

        // mask for fore-ground green light effect in water
        float transparency = dot(n, vec3(0.,.2,1.5));
        transparency -= length ( (uvInit - vec2(0.,-.35)) * vec2(.2,1.) );
		transparency = (transparency*12.+1.5);

        // add foreground water effect
        waterReflections = mix( waterReflections, seaColor, clamp(transparency,0.,1.) );
        waterReflections = mix( waterReflections, seaLight, max(0.,transparency-1.5) );

       	col = waterReflections;

        // darken sea near horizon
       	col = mix(col, col*vec3(.6,.8,1.), -uv.y);

        //sun specular
        col += max(0.,.02-abs(uv.x+n.x))* 8000. * vec3(1.,.7,.3) * -uv.y * max(0.,-n.z);

    }else{

        // sky
        col = drawSky(uv, uvInit);
    }

    // sun flare & vignette
    col += vec3(1.,.8,.6) * (0.55-length(uvInit)) ;

    // "exposure" adjust
    col *= .75;
    col = gamma(col,1.3);

    fragColor = vec4(col,1.);
}

//--------------------------------------------------------------------
//There is no salvation in becoming adapted to a world which is crazy.
