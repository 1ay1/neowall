/**
 * Ping 2.0 14/06/2019
 *
 * TODO : 	Cloud rocking
**/
const vec3 c0 = vec3(.042,0.530,0.159);
const vec3 c1 = vec3(.142,0.630,0.259);
const vec3 c2 = vec3(0.242,0.730, 0.359);
const vec3 c3 = vec3(0.342,0.830,0.459);
const vec3 c4 = vec3(0.442,0.930,0.559);

const vec3 c5 =vec3(1);
const vec3 c6 = vec3(0.95, 0.95 ,1.0);
const vec3 c7 = vec3(0.9, 0.9,1.0);
//const vec3 c8 = vec3(0.85, 0.85 ,1.0);
//const vec3 c9 = vec3(0.8,0.85, 0.95);

// min dist 2 circles (or ellipsis)
#define GRND1 min(length(fract(op)*vec2(1, 3) - vec2(0.5,0.18)) - 0.3,     length(fract(op+vec2(0.5, 0))*vec2(1, 2) - vec2(0.5,0.09)) - 0.35)
#define GRND2 min(length(fract(op)*vec2(1.2, 2.5) - vec2(0.5,0.45)) - 0.4, length(fract(op+vec2(0.65, 0))*vec2(1, 1.4) - vec2(0.5,0.25)) - 0.35)
#define GRND3 min(length(fract(op)-vec2(0.5,0.3))-0.35, length(fract(op+vec2(0.5, 0))-vec2(0.5,0.25))-0.3)
#define GRND4 min(length(fract(op)-vec2(0.5,0.1))-0.3, length(fract(op+vec2(0.5, 0))-vec2(0.5,0.1))-0.4)
#define GRND5 min(length(fract(op)-vec2(0.5,0.2))-0.5, length(fract(op+vec2(0.5, 0))-vec2(0.5,0.2))-0.5)

#define txc(c, n, f) c*n + (1.0125-n)*texture(iChannel0, op*f).r

vec3 ground(in vec2 u, in vec3 c, in float shadow_pos)
{
	if(u.y<0.4)
	{
		const float b = 0.005; //blur
		vec2 op = u*2.0;
		op.x += iTime*0.05;
		c=mix(c, txc(c0, 0.98, vec2(2.5,2.5)), smoothstep(b*5.0, -b*5.0, GRND5));

		op = vec2(u.x*3.0 + iTime*0.1 - shadow_pos, u.y*3.0-0.5);
		c=mix(c, c*0.75, smoothstep(b*30.0, -b*30.0, GRND4));
		op.x += shadow_pos;
		c=mix(c, txc(c1, 0.98, vec2(1.33,1.33)), smoothstep(b*3.0, -b*3.0, GRND4));

		op = vec2(u.x*4.0 + iTime*0.2 - shadow_pos, u.y*3.0-0.2);
		c=mix(c, c*0.9, smoothstep(b*10.0, -b*10.0, GRND3));
		op.x += shadow_pos;
		c=mix(c, txc(c2, 0.98, vec2(0.75, 1.0)), smoothstep(b*0.5, -b*0.5, GRND3));

		op = vec2(u.x*5.0 + iTime*0.4 - shadow_pos, u.y*2.0);
		c=mix(c, c*0.82, smoothstep(b*20.0, -b*20.0, GRND2));
		op.x += shadow_pos;
		c=mix(c, txc(c3, 0.98, vec2(0.4,1.0)), smoothstep(b*3.0, -b*3.0, GRND2));

		op = vec2(u.x*8.0 + iTime -shadow_pos, u.y*2.0+0.02);
		c=mix(c, c*0.75, smoothstep(b*30.0, -b*30.0, GRND1));
		op += vec2(shadow_pos, -0.02);
        c=mix(c, txc(c4, 0.96, vec2(0.5, 1.0)), smoothstep(b*5.0, -b*5.0, GRND1));
	}
	return c;
}

// https://iquilezles.org/articles/distfunctions2d
float sdLine( in vec2 p, in vec2 a, in vec2 b )
{
    vec2 pa = p-a, ba = b-a;
    float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 );
    return length( pa - ba*h );
}

vec3 cloud(in vec2 u, in vec2 p, in float iscale,in vec3 c, const in vec3 cloud_color, in vec2 shadow_pos, in float shadow_factor, in float blur, in float shadow_blur)
{
	u *= iscale;
	p *= iscale;

    // thread shadow
	c=mix(c, c*.95, smoothstep(shadow_blur*0.2, -shadow_blur*0.2, sdLine(u, p+vec2(shadow_pos.x,0.07), vec2(p.x + shadow_pos.x, iscale))));

    // cloud shadow
	vec2 st = u - p -shadow_pos ;
	float d = length(st) - 0.07;
	d = min(d, length((st  -vec2(0.06, 0))) - 0.055);
	d = min(d, length((st  +vec2(0.06, 0))) - 0.055);
	c=mix(c, c*shadow_factor, smoothstep(shadow_blur, -shadow_blur, d));

    // cloud
	st += shadow_pos;
	d = length(st) - 0.07;
	d = min(d, length((st  -vec2(0.06, 0))) - 0.055);
	d = min(d, length((st  +vec2(0.06, 0))) - 0.055);
    vec2 op = st;
	c=mix(c, cloud_color*0.98 + (1.0-0.98)*texture(iChannel0, st*2.5).r, smoothstep(blur, -blur, d));

    // thread
	c=mix(c, cloud_color*0.65, smoothstep(blur / (iscale*iscale), -(blur*0.5)/(iscale*iscale), sdLine(u, p + vec2(0,0.065), vec2(p.x, iscale))));
	return c;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord/iResolution.xy;
    uv.x *= 4.0/3.0; // dev in 4/3 screen

    // beautiful sky
	float d = length(uv-vec2(0.25,0.5)); // -0.5;
   	vec3 c = mix(vec3(.4,0.4,.8), vec3(0.55,0.8,0.8), smoothstep(1.7, 0., d));

    // gorgeous ground
	float shadow_pos =  - smoothstep(1.0, 0.0, uv.x)*0.06 - 0.1 ;
	c = ground(uv, c, shadow_pos);

    // wonderful clouds
	vec2 np = vec2(1.4-fract((iTime+50.0)*0.005) *1.5 , 0.8);
	c = cloud(uv, np, 2.0, c, c7, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.01, 0.03);

	np = vec2(1.4-fract((iTime)*0.0055) *1.5 , 0.75+ sin(iTime*0.1)*0.01); // x : -1 1
	c = cloud(uv, np, 2.0, c, c7, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.01, 0.03);

    np = vec2(1.4-fract((iTime + 100.0)*0.0045) *1.5 , 0.8+ sin(0.5+iTime*0.01)*0.02); // x : -1 1
    c = cloud(uv, np, 2.0, c, c7, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.01, 0.03);

     np = vec2(1.4-fract((iTime + 0.75)*0.0045) *1.5 , 0.88+ sin(0.75+iTime*0.01)*0.03); // x : -1 1
    c = cloud(uv, np, 2.0, c, c7, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.01, 0.03);


	np = vec2(1.41-fract((iTime+75.0)*0.007) *1.5 , 0.88+ sin(iTime*0.05)*0.01); // x : -1 1
	c = cloud(uv, np, 1.5, c, c6, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.005, 0.04);

   	np = vec2(1.41-fract((iTime+50.0)*0.0071) *1.5 , 0.85+ sin(0.5+iTime*0.042)*0.0095); // x : -1 1
	c = cloud(uv, np, 1.5, c, c6, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.005, 0.04);

   	np = vec2(1.41-fract((iTime+35.0)*0.0067) *1.5 , 0.82+ sin(0.9+iTime*0.035)*0.012); // x : -1 1
	c = cloud(uv, np, 1.5, c, c6, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.005, 0.04);


	np = vec2(1.50-fract(iTime*0.011) *1.75 , 0.85 + sin(iTime*0.2)*0.025); // x : -1 1
	c = cloud(uv , np, 1.0, c, c5, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.002, 0.04);

   	np = vec2(1.50-fract((iTime+50.0)*0.01) *1.75 , 0.85 + sin(1.5+iTime*0.08)*0.0125); // x : -1 1
	c = cloud(uv , np, 1.0, c, c5, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.002, 0.04);

   	np = vec2(1.50-fract((iTime+35.0)*0.009) *1.75 , 0.8 + sin(0.5+iTime*0.05)*0.025); // x : -1 1
	c = cloud(uv , np, 1.0, c, c5, vec2(shadow_pos, -0.1)*0.2, 0.8,  0.002, 0.04);


    // Output to screen
    fragColor = vec4(c,1.0);
}
