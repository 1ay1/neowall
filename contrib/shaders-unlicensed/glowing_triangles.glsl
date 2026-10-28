/*

	Glowing Offset Triangles
	------------------------

    Using an offset triangle grid to produce some randomly glowing markings
    on some irregular triangles.

    I coded this some time ago, and decided to tidy it up and post it after
    seeing Yusef28's cool static triangle grid efforts. This particular one
    was inspired by those phone backgrounds that you may have seen around.

    The code was written off the top of my head just to get the job done, so
    I'd ignore most of it. The offset triangle routine is probably worth
    looking at, if you're interested in this kind of thing.

    In case it isn't obvious, this is a simple 2D example with some fake 3D
    elements thrown in. Since there's emitting light involved, it would have
    been nice to path trace it, but I'm guessing not a lot of people here are
    viewing Shadertoy on a supercomputer, so perhaps some other time. :) A
    standard realtime 3D version should be possible though.



	Related examples:


	// My example is loosely enspired by this.
	Simplex Experiment #3b  - Yusef28
	https://www.shadertoy.com/view/Nl33WM

*/

// Display the animated glowing cracks. One could argue that it defeats the
// purpose of the example, but you may wish to see the pattern without the glow. :)
#define GLOW

// Light decorations on the triangle faces.
//#define FACE_DECO

// Light decorations on the outer triangle faces.
#define OUTER_FACE_DECO

// Offsetting the triangle coordinates. The look is a lot cleaner without it.
#define OFFSET_TRIS

// Textured.
//#define TEXTURE

// Animate the triangles -- Technically, a metallic surface animating in such a
// way isn't really realistic, so you may wish to keep the arrangement static.
#define ANIMATE

// Light color - Reddish Pink: 0, Greenish Blue: 1.
#define COLOR 0


// Standard 2D rotation formula.
mat2 rot2(in float a){ float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }


// IQ's vec2 to float hash.
float hash21(vec2 p){  return fract(sin(dot(p, vec2(27.619, 57.583)))*43758.5453); }


// vec2 to vec2 hash.
vec2 hash22B(vec2 p) {

    // Faster, but doesn't disperse things quite as nicely. However, when framerate
    // is an issue, and it often is, this is a good one to use. Basically, it's a tweaked
    // amalgamation I put together, based on a couple of other random algorithms I've
    // seen around... so use it with caution, because I make a tonne of mistakes. :)
    float n = sin(dot(p, vec2(1, 113)));
    p = fract(vec2(262144, 32768)*n)*2. - 1.;
    #ifdef ANIMATE
    return sin(p*6.2831853 + iTime/2.);
    #else
    return p;
    #endif

}

// vec2 to vec2 hash.
vec2 hash22C(vec2 p) {

    // Faster, but doesn't disperse things quite as nicely. However, when framerate
    // is an issue, and it often is, this is a good one to use. Basically, it's a tweaked
    // amalgamation I put together, based on a couple of other random algorithms I've
    // seen around... so use it with caution, because I make a tonne of mistakes. :)
    float n = sin(dot(p, vec2(289, 41)));
    return fract(vec2(262144, 32768)*n)*2. - 1.;

    // Animated.
    //p = fract(vec2(262144, 32768)*n)*2. - 1.;
    //return sin(p*6.2831853 + iTime/2.);
}


// Based on IQ's gradient noise formula.
float n2D3G( in vec2 p ){

    // Cell ID and local coordinates.
    vec2 i = floor(p); p -= i;

    // Four corner samples.
    vec4 v;
    v.x = dot(hash22C(i), p);
    v.y = dot(hash22C(i + vec2(1, 0)), p - vec2(1, 0));
    v.z = dot(hash22C(i + vec2(0, 1)), p - vec2(0, 1));
    v.w = dot(hash22C(i + 1.), p - 1.);

    // Cubic interpolation.
    p = p*p*(3. - 2.*p);

    // Bilinear interpolation -- Along X, along Y, then mix.
    return mix(mix(v.x, v.y, p.x), mix(v.z, v.w, p.x), p.y);

}

// Two layers of noise.
float fBm(vec2 p){ return n2D3G(p)*.57 + n2D3G(p*2.)*.28 + n2D3G(p*4.)*.15; }



// IQ's signed distance to a 2D triangle.
float sdTri(in vec2 p, in vec2 p0, in vec2 p1, in vec2 p2){

    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;

	vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;

	vec2 pq0 = v0 - e0*clamp( dot(v0, e0)/dot(e0, e0), 0., 1.);
	vec2 pq1 = v1 - e1*clamp( dot(v1, e1)/dot(e1, e1), 0., 1.);
	vec2 pq2 = v2 - e2*clamp( dot(v2, e2)/dot(e2, e2), 0., 1.);

    float s = sign( e0.x*e2.y - e0.y*e2.x);
    vec2 d = min( min( vec2(dot(pq0, pq0), s*(v0.x*e0.y - v0.y*e0.x)),
                       vec2(dot(pq1, pq1), s*(v1.x*e1.y - v1.y*e1.x))),
                       vec2(dot(pq2, pq2), s*(v2.x*e2.y - v2.y*e2.x)));

	return -sqrt(d.x)*sign(d.y);
}



// Triangle's incenter and radius.
vec3 inCentRad(vec2 p0, vec2 p1, vec2 p2){

    // Side lengths.
    float bc = length(p1 - p2), ac = length(p0 - p2), ab = length(p0 - p1);
    vec2 inCir = (bc*p0 + ac*p1 + ab*p2)/(bc + ac + ab);

    // Area.
    float p = (bc + ac + ab)/2.;
    float area = sqrt(p*(p - bc)*(p - ac)*(p - ab));

    return vec3(inCir, area/p);
}

// Skewing coordinates. "s" contains the X and Y skew factors.
vec2 skewXY(vec2 p, vec2 s){ return mat2(1, -s.yx, 1)*p; }


// Unskewing coordinates. "s" contains the X and Y skew factors.
vec2 unskewXY(vec2 p, vec2 s){ return inverse(mat2(1, -s.yx, 1))*p; }


/*

// Rounded triangle routine. Not used here, but handy.
float sdTriR(vec2 p, vec2 v0, vec2 v1, vec2 v2){

    vec3 inC = inCentRad(v0, v1, v2);
    float ndg = .0002/inC.z;
    return sdTri(p, v0 - (v0 - inC.xy)*ndg,  v1 - (v1 - inC.xy)*ndg,  v2 - (v2 - inC.xy)*ndg) - .0002;

}

*/
// Global vertices, local coordinates, etc, of the triangle cell.
struct triS{

    vec2[3] v; // Outer vertices.
    vec2 p; // Local coordinate.
    vec2 id; // Position based ID.
    float dist; // Distance field value.
    float triID; // Triangle ID.
};

const float tf = 2./sqrt(3.);
// Scale.
const vec2 scale = vec2(tf, 1)*vec2(1./3.);

// Brick dimension: Length to height ratio with additional scaling.
const vec2 dim = vec2(scale);
// A helper vector, but basically, it's the size of the repeat cell.
const vec2 s = dim*2.;

// Skewing half way along X, and not skewing in the Y direction.
const vec2 sk = vec2(tf/2., 0);

// A regular extruded block grid.
//
// The idea is very simple: Produce a normal grid full of packed square pylons.
// That is, use the grid cell's center pixel to obtain a height value (read in
// from a height map), then render a pylon at that height.

triS blocks(vec2 q){




    // Distance.
    float d = 1e5;
    // Cell center, local coordinates and overall cell ID.
    vec2 p, ip;

    // Individual block ID and block center.
    vec2 id = vec2(0), cntr;

    // For block corner postions.
    const vec2[4] ps4 = vec2[4](vec2(-.5, .5), vec2(.5), vec2(.5, -.5), vec2(-.5));

    float triID = 0.; // Triangle ID. Not used in this example, but helpful.


    // Height scale.
    const float hs = .5;


    // Initializing the global vertices and local coordinates of the triangle cell.
    triS gT, tri1, tri2;

    for(int i = min(0, iFrame); i<4; i++){

        // Block center.
        cntr = ps4[i]/2.;// -  ps4[0];
        // Skewed local coordinates.
        p = skewXY(q.xy, sk);// - cntr*s;
        ip = floor(p/s - cntr) + .5; // Local tile ID.
        p -= (ip + cntr)*s; // New local position.
        // Unskew the local coordinates.
        p = unskewXY(p, sk);


        // Correct positional individual tile ID.
        vec2 idi = ip + cntr;

        // Skewed rectangle vertices.
        vec2[4] vert = ps4;

        #ifdef OFFSET_TRIS
        // Offsetting the vertices.
        vert[0] += hash22B((idi + vert[0]/2.))*.2;
   		vert[1] += hash22B((idi + vert[1]/2.))*.2;
        vert[2] += hash22B((idi + vert[2]/2.))*.2;
        vert[3] += hash22B((idi + vert[3]/2.))*.2;
        #endif


        // Unskewing to enable rendering back in normal space.
        vert[0] = unskewXY(vert[0]*dim, sk);
        vert[1] = unskewXY(vert[1]*dim, sk);
        vert[2] = unskewXY(vert[2]*dim, sk);
        vert[3] = unskewXY(vert[3]*dim, sk);


        // Unskewing the rectangular cell ID.
		idi = unskewXY(idi*s, sk);


        // Partioning the rectangle into two triangles.

        // Triangle one.
        tri1.v = vec2[3](vert[0], vert[1], vert[2]);
        tri1.id = idi + inCentRad(tri1.v[0], tri1.v[1], tri1.v[2]).xy; // Position Id.
        tri1.triID = float(i); // Triangle ID. Not used here.
        tri1.dist = sdTri(p, tri1.v[0], tri1.v[1], tri1.v[2]); // Field distance.
        tri1.p = p; // 2D coordinates.

        // Triangle two.
        tri2.v = vec2[3](vert[0], vert[2], vert[3]);
        tri2.id = idi + inCentRad(tri2.v[0], tri2.v[1], tri2.v[2]).xy; // Position Id.
        tri1.triID = float(i + 4); // Triangle ID. Not used here.
        tri2.dist = sdTri(p, tri2.v[0], tri2.v[1], tri2.v[2]); // Field distance.
        tri2.p = p; // 2D coordinates.

        // Doesn't work, unfortunately, so I need to write an ugly "if" statement.
        //triS gTi = tri1.dist<tri2.dist? tri1 : tri2;
        triS gTi;
        // Obtain the closest triangle information.
        if(tri1.dist<tri2.dist) gTi = tri1;
        else gTi = tri2;


        // If applicable, update the overall minimum distance value,
        // then return the correct triangle information.
        if(gTi.dist<d){
            d = gTi.dist;
            gT = gTi;
            gT.id = idi;//(idi + inCentRad(gT.v[0], gT.v[1], gT.v[2]).xy)*s;
        }

        //if(d>1e8) break; // Fake break to get compile times down.

    }

    // Return the distance, position-based ID and triangle ID.
    return gT;
}



void mainImage(out vec4 fragColor, in vec2 fragCoord){

    // Resolution and aspect correct screen coordinates.
    float iRes = min(iResolution.y, 800.);
    vec2 uv = (fragCoord - iResolution.xy*.5)/iRes;

    // Scene rotation.
    uv.xy *= rot2(-3.14159/9.);


    // Unit direction vector and camera origin.
    // Used for some mock lighting.
    vec3 rd = normalize(vec3(uv, 1));
    vec3 ro = vec3(0);


    // Scaling and translation.
    const float gSc = 1.;
    vec2 p = uv*gSc - vec2(0, iTime/32.).yx;


    // Resolution and scale based smoothing factor.
    float sf = gSc/iResolution.y;


    // Take an offset triangle function sample.
    triS gT = blocks(p);


    // Triangle vertices, local coordinates and position-based ID.
    // With these three things, you can render anything you want.
    vec2[3] v = gT.v;
    vec2 rp = gT.p;
    vec2 svID = gT.id;


    // Initializing the scene color to black.
    vec3 col = vec3(0);

    // Triangle color.
    vec3 tCol = vec3(0);


    // Triangle cell center.
    vec3 cntr = inCentRad(v[0], v[1], v[2]);


    // Light position, light direction and light distance.
    vec3 lp = vec3(-.5, 1, -1);
    vec3 ld = lp - vec3(uv, 0);
    float lDist = length(ld);
    ld /= lDist;

    // Light attenuation.
    float atten = 2./(1. + lDist*lDist*.5);

    // Triangle cell.
    float triCell = sdTri(rp, v[0], v[1], v[2]);
    //float triCell2 = sdTri(rp, v[0] - ld.xy*.01, v[1] - ld.xy*.01, v[2] - ld.xy*.01);


    // Circle center and opening.
    float cir = length(rp - cntr.xy);
    // The opening is created by using the edge triangles to chip away
    // from the original cell space.
    float opening = triCell;


    // Glow color.
    vec3 glCol = vec3(1, .35, .2);
    #if COLOR == 1
    glCol = mix(glCol.zyx, glCol.zxy, clamp(-uv.y*1.25 + .5, 0., 1.));
    #endif
    glCol = mix(glCol, glCol.xzy, dot(sin(p*2. - cos(p.yx*4.)*3.), vec2(.125)) + .25);
    //tx = glCol;
    glCol *= (fBm((rp -svID*s)*128.)*.25 + .75)*1.25; // Adding noise.



    // Adding some glow to the triangle cell. The subdivided triangle will be drawn
    // over the top, thus giving the impression of light eminating through cracks.
    col += glCol*max(.2 - triCell/scale.x*6., 0.);

    // Edge and opening distance fields.
    float edge = 1e5, openMax = 0.;

    // Subdividing the triangle cell and producing three triangle wedges -- one
    // for each side.
    for(int j = min(0, iFrame); j<3; j++){


        // Random numbers based on the triangle cell ID, and the individual
        // subdivided triangle ID.
        float rnd = hash21(svID + .08);
        float rndJ = hash21(svID + float(j)/9. + .13);

        // Open the triangle sides at random blinking intervals.
        float open = smoothstep(.9, .966, sin(rnd*6.2831 + rndJ/6. + iTime/1.)*.5 + .5);
        //if(hash21(svID +.34)<.5) open = 0.;
        //if(gT.triID<.5) open = 0.;
        // If not showing the glowing light through the cracks, overide
        // the open variable.
        #ifndef GLOW
        open = 0.;
        #endif

        // Subdivided triangle vertices.
        vec3 p0 = vec3(v[j], 0);
        vec3 p1 = vec3(v[(j + 1)%3], 0);
        vec3 p2 = vec3(cntr.xy, -.2);
        // Moving the central vertex toward the opposing edge to
        // simulate the triangle opening like a flower.
        //p2.xy -= (vec3(v[(j + 2)%3], 0) - p2).xy*.065*open;//(rndJ*.5 + .5)*.07;
        p2.xy -= normalize(vec3(v[(j + 2)%3], 0) - p2).xy*.065*scale.x*open;

        float triJ = sdTri(rp, v[j], v[(j + 1)%3], p2.xy);

        // Z value, for some faux texture depth.
        float z = 1./(1. - p2.z);

        // Creating a second shifted triangle give the open flap some fake depth.
        p2.xy += normalize(v[(j + 2)%3] - cntr.xy)*.008*open;

        // Produce the triangle for this edge.
        float triEdge = sdTri(rp, v[j], v[(j + 1)%3], p2.xy);


        // Normal -- Based on slightly incorrect hit information, but
        // it's near enough.
        vec3 nJ = normalize(cross(p1 - p0, p2 - p0));


        // Diffuse lighting.
        float diff = max(dot(ld, nJ), 0.);
        diff = pow(diff, 4.)*2.;
        // Specular lighting.
        float spec = pow(max(dot(reflect(ld, nJ), rd ), 0.), 8.);



        // Triangle color.
        #ifdef TEXTURE
        // Texture color.
        vec3 tx2 = texture(iChannel0, (rp - svID*s)*z).xyz; tx2 *= tx2;
        //vec3 tx2 = texture(iChannel0, svID*s).xyz; tx2 *= tx2;

        vec3 tCol = smoothstep(-.5, 1., tx2)*.1;
        //tCol = sqrt(tx2)*.1;
        #else
        vec3 tCol = vec3(.035);
        #endif

        // Concentric triangular face pattern.
        //float pat = (abs(fract(triPat[j]*84. - .5/84.) - .5)*2. - .5)/84.;
        //tCol = mix(tCol*1.3, tCol*.65, 1. - smoothstep(0., sf, pat));


        // Applying the diffuse and specular to the triangle.
        tCol = tCol*(diff + .25 + spec*4.);

        // Easier way to add diffuse light, but not as good.
        //float b = max(triCell2 - triCell, 0.)/.01;
        //tCol *= (b + .25);


        // Lightening the edges.
        //col = mix(col, col*vec3(12, 6, 2), (1. - smoothstep(0., sf*4., triPat[j] - .001))*.25);

        if(open>1e-5){
            col = mix(col, vec3(0), open*(1. - smoothstep(0., sf*4., triEdge - .00)));
            col = mix(col, mix(tCol*vec3(1.5, 1.25, 1), vec3(0), open), (1. - smoothstep(0., sf*2., triEdge)));// + .005/3.
            col = mix(col, mix(tCol, glCol, .5)*(open), (1. - smoothstep(0., sf, triEdge + .005)));// + .005/3.
        }
        //else col = mix(col, vec3(0), open*(1. - smoothstep(0., sf*4., triPat[j] - .00)));// + .005/3.

        col = mix(col, tCol*vec3(1.5, 1.25, 1)*(1. - open), 1. - smoothstep(0., sf*2., triJ));// + .005/3.
        col = mix(col, tCol, 1. - smoothstep(0., sf*2., triJ + .005));// .005*2./3.

        #ifdef OUTER_FACE_DECO
        // Outer face decoration.
        col = mix(col, mix(tCol, glCol*1., open), 1. - smoothstep(0., sf*4., abs(triJ + .035) - .002));// .005*2./3.
        col = mix(col, mix(tCol, tCol/3., open), 1. - smoothstep(0., sf, abs(abs(triJ + .035) - .006) - .00125));// .005*2./3.
        #endif

        #ifdef FACE_DECO
        // Face decoration.
        col = mix(col, mix(tCol, (diff + .25 + spec*4.)*glCol, open), (1. - smoothstep(0., sf*3., triJ + .035))*.2);
        col = mix(col, mix(tCol, vec3(0), open), 1. - smoothstep(0., sf*2., triJ + .035));
        col = mix(col, mix(tCol, (diff + .25 + spec*4.)*glCol*2., open), 1. - smoothstep(0., sf, triJ + .035 + .005));
        #endif

        edge = min(edge, abs(triJ));

        p0.xy -= (p2.xy - p0.xy)*1.;//*length(p2.xy - p0.xy)*2.;
        p1.xy -= (p2.xy - p1.xy)*1.;//*length(p2.xy - p1.xy)*2.;
        float eTri = sdTri(rp, p0.xy*8., p1.xy*8., p2.xy);

        // Glow mask.
        openMax = max(openMax, open);
        opening = max(opening, -eTri);


    }

    // Applying the glow mask.
    cir = mix(cir, opening, .65);
    col = mix(col, col + col*glCol*(openMax*2.5 + .5), (1. - smoothstep(0., sf*24., cir)));// + .005/3.

    // Darkening and shading the edges.
    //col = mix(col, vec3(0), (1. - smoothstep(0., sf, edge - .001))*.5);
    //col = mix(col, vec3(0), (1. - smoothstep(0., sf*32., abs(triCell)))*.35);
    col *= clamp(.5 - triCell/scale.x*4., 0., 1.);


    // Outer layer noise. This is applied to the border cords and points.
    float ns = fBm((rp - cntr.xy)*128.)*.5 + .5;
    col *= .5 + ns*.75;

    // Light attenuation.
    col *= atten;


/*
    // Vertices.
    vec3 cir3 = vec3(length(svP - svV[0]), length(svP - svV[1]), length(svP - svV[2]));
    float verts = min(min(cir3.x, cir3.y), cir3.z);
    verts -= .016;

    vec3 vCol = lCol;//*.7;
    col = mix(col, vec3(0), (1. - smoothstep(0., sf*5.*iRes/450., verts))*.35);
    col = mix(col, vec3(0), (1. - smoothstep(0., .005, verts)));
    col = mix(col, vCol, (1. - smoothstep(0., sf, verts + .0035)));
    col = mix(col, vec3(0), (1. - smoothstep(0., sf, verts + .011))); // Pin staple hole.
*/

    // Subtle vignette.
    //uv = fragCoord/iResolution.xy;
    //col *= pow(16.*uv.x*uv.y*(1. - uv.x)*(1. - uv.y) , .0625)*1.05;
    // Colored variation.
    //col = mix(col*vec3(.25, .5, 1)/8., col, pow(16.*uv.x*uv.y*(1. - uv.x)*(1. - uv.y) , .125));

    // Rough gamma correction.
	fragColor = vec4(sqrt(max(col, 0.)), 1);
}
