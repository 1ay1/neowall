// Author: Rigel rui@gil.com
// licence: https://creativecommons.org/licenses/by/4.0/
// link: https://www.shadertoy.com/view/lljfRD


/*
This was a study on circles, inspired by this artwork
http://www.dailymail.co.uk/news/article-1236380/Worlds-largest-artwork-etched-desert-sand.html

and implemented with the help of this article
http://www.ams.org/samplings/feature-column/fcarc-kissing

The structure is called an apollonian packing (or gasket)
https://en.m.wikipedia.org/wiki/Apollonian_gasket

There is a lot of apollonians in shadertoy, but not many quite like the image above.
This one by klems is really cool. He uses a technique called a soddy circle.
https://www.shadertoy.com/view/4s2czK

This shader uses another technique called a Descartes Configuration.
The only thing that makes this technique interesting is that it can be generalized to higher dimensions.
*/


// a few utility functions
// a signed distance function for a rectangle
float sdfRect(vec2 uv, vec2 s) {vec2 auv = abs(uv); return max(auv.x-s.x,auv.y-s.y); }
// a signed distance function for a circle
float sdfCircle(vec2 uv, vec2 c, float r) { return length(uv-c)-r; }
// fills an sdf in 2d
float fill(float d, float s, float i) { return abs(smoothstep(0.,s,d) - i); }
// makes a stroke of an sdf at the zero boundary
float stroke(float d, float w, float s, float i) { return abs(smoothstep(0.,s,abs(d)-(w*.5)) - i); }
// a simple palette
vec3 pal(float d) { return .5*(cos(6.283*d*vec3(2.,2.,1.)+vec3(.0,1.4,.0))+1.);}
// 2d rotation matrix
mat2 uvRotate(float a) { return mat2(cos(a),sin(a),-sin(a),cos(a)); }
// circle inversion
vec2 inversion(vec2 uv, float r) { return (r*r*uv)/vec2(dot(uv,uv)); }
// seeded random number
float hash(vec2 s) { return fract(sin(dot(s,vec2(12.9898,78.2333)))*43758.5453123); }

// this is an algorithm to construct an apollonian packing with a descartes configuration
// remaps the plane to a circle at the origin and a specific radius. vec3(x,y,radius)
vec3 apollonian(vec2 uv) {
    // the algorithm is recursive and must start with a initial descartes configuration
    // each vec3 represents a circle with the form vec3(centerx, centery, 1./radius)
    // the signed inverse radius is also called the bend (refer to the article above)
    vec3 dec[4];
    // a DEC is a configuration of 4 circles tangent to each other
    // the easiest way to build the initial one it to construct a symetric Steiner Chain.
    // http://mathworld.wolfram.com/SteinerChain.html
	float a = 6.283/3.;
	float ra = 1.+sin(a*.5);
	float rb = 1.-sin(a*.5);
	dec[0] = vec3(0.,0.,-1./ra);
    float radius = .5*(ra-rb);
	float bend = 1./radius;
    for (int i=1; i<4; i++) {
        dec[i] = vec3(cos(float(i)*a),sin(float(i)*a),bend);
        // if the point is in one of the starting circles we have already found our solution
        if (length(uv-dec[i].xy) < radius) return vec3(uv-dec[i].xy,radius);
    }

    // Now that we have a starting DEC we are going to try to
    // find the solution for the current point
    for(int i=0; i<7; i++) {
        // find the circle that is further away from the point uv, using euclidean distance
        int fi = 0;
        float d = distance(uv,dec[0].xy)-abs(1./dec[0].z);
        // for some reason, the euclidean distance doesn't work for the circle with negative bend
        // can anyone with proper math skills, explain me why?
        d *= dec[0].z < 0. ? -.5 : 1.; // just scale it to make it work...
        for(int i=1; i<4; i++) {
            float fd = distance(uv,dec[i].xy)-abs(1./dec[i].z);
            fd *= dec[i].z < 0. ? -.5: 1.;
            if (fd>d) {fi = i;d=fd;}
        }
        // put the cicle found in the last slot, to generate a solution
        // in the "direction" of the point
        vec3 c = dec[3];
        dec[3] = dec[fi];
        dec[fi] = c;
        // generate a new solution
        float bend = (2.*(dec[0].z+dec[1].z+dec[2].z))-dec[3].z;
        vec2 center = vec2((2.*(dec[0].z*dec[0].xy
                               +dec[1].z*dec[1].xy
                               +dec[2].z*dec[2].xy)
                               -dec[3].z*dec[3].xy)/bend);

		vec3 solution = vec3(center,bend);
		// is the solution radius is to small, quit
		if (abs(1./bend) < 0.01) break;
		// if the solution contains the point return the circle
    	if (length(uv-solution.xy) < 1./bend) return vec3(uv-solution.xy,1./bend);
    	// else update the descartes configuration,
    	dec[3] = solution;
    	// and repeat...
	}
	// if nothing is found we return by default the inner circle of the Steiner chain
	return vec3(uv,rb);
}


vec3 scene(vec2 uv, vec4 ms) {

	vec2 ci = vec2(.0);

    // drag your mouse to apply circle inversion
    if (ms.y != -2. && ms.z > -2.) {
        uv = inversion(uv,cos(radians(60.)));
        ci = ms.xy;
    }

    // remap uv to appolonian packing
    vec3 uvApo = apollonian(uv-ci);

    float d = 6.2830/360.;
    float a = atan(uvApo.y,uvApo.x);
    float r = length(uvApo.xy);

    float circle = sdfCircle(uv,uv-uvApo.xy,uvApo.z);

    // background
	vec3 c = length(uv)*pal(.7)*.2;

    // drawing the clocks
    if (uvApo.z > .3) {
    	c = mix(c,pal(.75-r*.1)*.8,fill(circle+.02,.01,1.)); // clock
    	c = mix(c,pal(.4+r*.1),stroke(circle+(uvApo.z*.03),uvApo.z*.01,.005,1.));// dial

        float h = stroke(mod(a+d*15.,d*30.)-d*15.,.02,0.01,1.);
    	c = mix(c,pal(.4+r*.1),h*stroke(circle+(uvApo.z*.16),uvApo.z*.25,.005,1.0));// hours

        float m = stroke(mod(a+d*15.,d*6.)-d*3.,.005,0.01,1.);
    	c = mix(c,pal(.45+r*.1),(1.-h)*m*stroke(circle+(uvApo.z*.15),uvApo.z*.1,.005,1.0));// minutes,

    	// needles rotation
    	vec2 uvrh = uvApo.xy*uvRotate(sign(cos(hash(vec2(uvApo.z))*d*180.))*d*iTime*(1./uvApo.z*10.)-d*90.);
    	vec2 uvrm = uvApo.xy*uvRotate(sign(cos(hash(vec2(uvApo.z)*4.)*d*180.))*d*iTime*(1./uvApo.z*120.)-d*90.);
    	// draw needles
    	c = mix(c,pal(.85),stroke(sdfRect(uvrh+vec2(uvApo.z-(uvApo.z*.8),.0),uvApo.z*vec2(.4,.03)),uvApo.z*.01,0.005,1.));
    	c = mix(c,pal(.9),fill(sdfRect(uvrm+vec2(uvApo.z-(uvApo.z*.65),.0),uvApo.z*vec2(.5,.002)),0.005,1.));
    	c = mix(c,pal(.5+r*10.),fill(circle+uvApo.z-.02,0.005,1.)); // center
    // drawing the gears
    } else if (uvApo.z > .05) {
    	vec2 uvrg = uvApo.xy*uvRotate(sign(cos(hash(vec2(uvApo.z+2.))*d*180.))*d*iTime*(1./uvApo.z*20.));
        float g = stroke(mod(atan(uvrg.y,uvrg.x)+d*22.5,d*45.)-d*22.5,.3,.05,1.0);
        vec2 size = uvApo.z*vec2(.45,.08);
        c = mix(c,pal(.55-r*.6),fill(circle+g*(uvApo.z*.2)+.01,.001,1.)*fill(circle+(uvApo.z*.6),.005,.0));
        c = mix(c,pal(.55-r*.6),fill(min(sdfRect(uvrg,size.xy),sdfRect(uvrg,size.yx)),.005,1.));
    // drawing the screws
    } else {
 	    vec2 size = uvApo.z * vec2(.5,.1);
 	    c = mix(c, pal(.85-(uvApo.z*2.)), fill(circle + 0.01,.007,1.));
 	    c = mix(c, pal(.8-(uvApo.z*3.)), fill(min(sdfRect(uvApo.xy,size.xy),sdfRect(uvApo.xy,size.yx)), .002, 1.));
    }
	return c;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
	vec2 uv = (fragCoord.xy - iResolution.xy * .5) / iResolution.y;
	vec4 ms = (iMouse - iResolution.xyxy * .5 ) / iResolution.y ;
	fragColor = vec4(scene(uv*4., ms*4.),1.0);
}
