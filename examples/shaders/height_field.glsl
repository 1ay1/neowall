mat3 rotX(float a)
{
    return mat3(1., 0., 0.,
                0., cos(a), sin(a),
                0., -sin(a), cos(a));
}

mat3 rotY(float a)
{
    return mat3(cos(a), 0., sin(a),
                0., 1., 0.,
                -sin(a), 0., cos(a));
}

mat3 rotZ(float a)
{
    return mat3(cos(a), sin(a), 0.,
                -sin(a), cos(a), 0.,
                0., 0., 1.);
}

float area(vec3 a,vec3 b,vec3 c)
{
   b-=a;
   c-=a;
   return length(cross(b,c))/2.;
}
//
// The shaders of this series:
//
//   Triangulated Heightfield Trick 1 - https://www.shadertoy.com/view/XlcBRX (Rigid, right-triangle)
//   Triangulated Heightfield Trick 2 - https://www.shadertoy.com/view/tlXSzB (Rigid, equilateral)
//   Triangulated Heightfield Trick 3 - https://www.shadertoy.com/view/ttsSzX (Deforming, equilateral)
//   Tetrahedral Voxel Traversal      - https://www.shadertoy.com/view/wtfXWB (Rigid, tetrahedron)
//

// Constants
const float pi = 3.14159265358979323;
const float th = pi * 2. / 3.;

// Equilateral triangle edge length
const float edgeLength = 2. / tan(th);

// Transformations for deforming the equilateral triangle grid into
// a square grid with diagonal lines
const float th2 = -pi * .25;
const float e = edgeLength * 1.5;
const mat2 rm = mat2(cos(th2), sin(th2), -sin(th2), cos(th2));
const mat2 m = rm * mat2(1. / 2., 0., 0., e / 2.) * sqrt(2.);
const mat2 mt = (1. / sqrt(2.)) * mat2(2., 0., 0., 2. / e) * transpose(rm);

// Normals of the 3 line directions in the equilateral triangle grid
const vec2 ns[3] = vec2[3](vec2(1, 0), vec2(cos(th), sin(th)), vec2(cos(th * 2.), sin(th * 2.)));

const float maxHeight = 6.;

// Heightfield
float sampleHeightfield(vec2 p)
{
    float h = 	textureLod(iChannel0, p / 40. + iTime / 400., 2.).b *
    			textureLod(iChannel1, p / 8., 2.).r * 1.6;

    return clamp(h, 0., 1. - 1e-4) * maxHeight;
}

// The raytracing function.
float trace(vec3 ro, vec3 rd, out vec3 triNorm, out vec3 bary)
{
    vec3 oro = ro;

    float mint = (maxHeight * step(rd.y, 0.) - ro.y) / rd.y;

    // Move ray start to bounding slab of heightfield.
    ro += rd * max(0., mint);

    // Determine the starting triangle by transforming the XZ
    // plane triangular grid to a square grid with diagonal cuts at each square,
    // then transforming the closet corners in that square grid back again.

    vec2 u = m * ro.xz;

    vec2 cu = floor(u), fu = u - cu;

    vec2 tri0, tri1, tri2;

    tri0 = mt * cu;

    if(fu.x > fu.y)
    {
        tri1 = mt * (cu + vec2(1, 1));
        tri2 = mt * (cu + vec2(1, 0));
    }
    else
    {
        tri1 = mt * (cu + vec2(1, 1));
        tri2 = mt * (cu + vec2(0, 1));
    }

    // Ray geometry
    vec3 rod = vec3(ro.x, dot(ro.xz, ns[1]), dot(ro.xz, ns[2]));
    vec3 rdd = vec3(rd.x, dot(rd.xz, ns[1]), dot(rd.xz, ns[2]));

    vec3 inv = vec3(1) / rdd;

    // Intersection distances to each of the three lines on the
    // equilateral triangle grid, from the ray starting point.
    vec3 is = (floor(rod) + step(0., rdd) - rod) * inv;

    inv = abs(inv);

    vec3 triangle[3];

    // Sort the triangle corners so that the corner at index N is opposite
    // to the triangle edge coincident to grid line normal at index N.
    for(int j = 0; j < 3; ++j)
    {
        float d0 = abs(dot(tri1 - tri0, ns[j]));
        float d1 = abs(dot(tri2 - tri1, ns[j]));
        float d2 = abs(dot(tri0 - tri2, ns[j]));

        triangle[j].xz = tri1;

        if(d0 < d1)
        {
            if(d0 < d2)
                triangle[j].xz = tri2;
        }
        else if(d1 < d2)
            triangle[j].xz = tri0;

        // Also get the height values of the starting triangle corners here.
        triangle[j].y = sampleHeightfield(triangle[j].xz);
    }

    // The step vectors which are used for mirroring the triangle across
    // one of it's edges.
    vec2 triSteps[3] = vec2[3](ns[0] * 2. * sign(rdd.x),
                               ns[1] * 2. * sign(rdd.y),
                               ns[2] * 2. * sign(rdd.z));

    float t0 = 0., t1, t = -1.;

    float maxt = (maxHeight * step(0., rd.y) - ro.y) / rd.y;

    triNorm = vec3(0);

    // The ray stepping loop
    // "min(iFrame, 0)" is used here to prevent complete unrolling of the loop (which
    // causes the compiler to take forever on OpenGL).
    for(int i = min(iFrame, 0); i < 200; ++i)
    {
        // Determine which grid line has the next closest intersection, and get the index
        // of the triangle corner which is opposite to the edge coincident with that line.

        int idx = 2;
        t1 = is.z;

        if(is.x < is.y)
        {
            if(is.x < is.z)
            {
                idx = 0;
                t1 = is.x;
            }
        }
        else if(is.y < is.z)
        {
        	idx = 1;
            t1 = is.y;
        }

        // Intersect ray with triangle. Actually this is just a ray-versus-plane
        // intersection, because the intersection point is already bounded by t0 and t1.
        triNorm = cross(triangle[2] - triangle[0], triangle[1] - triangle[0]);
        t = dot(triangle[0] - ro, triNorm) / dot(rd, triNorm);

        if(t > t0 && t < t1)
            break;

		if(t1 > maxt)
            return 1e5;

        int idx1 = (idx + 1) % 3, idx2 = (idx + 2) % 3;

        // Step the ray to the next grid line intersection point.
        is[idx] += inv[idx];

        // Mirror the triangle aross this grid line (which is coincident with
        // the edge opposite the triangle corner being moved here). This reverses
        // the winding.
        triangle[idx].xz += triSteps[idx];

        // Take a single sample of the heightfield.
        triangle[idx].y = sampleHeightfield(triangle[idx].xz);

        // Swap the other two corners, to maintain correspondence between triangle
        // corners and opposite edge lines. This also has the effect of reversing the winding
        // a second time, so all of the constructed triangles in fact have the same winding order.
        vec3 temp = triangle[idx1];
        triangle[idx1] = triangle[idx2];
        triangle[idx2] = temp;

        t0 = t1;
    }

    // Return the final intersection information.

    triNorm = normalize(triNorm);

    vec3 rp = ro + rd * t;

    // Get the barycentric coordinates.

    float alpha = area(triangle[0], triangle[1], rp);
    float beta = area(triangle[1], triangle[2], rp);
    float gamma = area(triangle[2], triangle[0], rp);

    float area = alpha + beta + gamma;

    bary = vec3(alpha, beta, gamma) / area;

    return distance(oro, rp);
}

// Ray direction function
vec3 rfunc(vec2 uv)
{
    vec3 r = normalize(vec3(uv.xy, -1.5));
    mat3 m = rotX(-.75) * rotZ(sin(iTime / 4.) * .1);
    return m * r;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 col = vec3(0);

    vec2 uv = fragCoord / iResolution.xy * 2. - 1.;
    uv.x *= iResolution.x / iResolution.y;

    // Setup primary ray.
    vec3 o = vec3(cos(iTime / 4.) * 4., 10., -iTime), r = rfunc(uv);

    vec3 triNorm, bary;
    float t = trace(o, r, triNorm, bary);

    vec3 n = triNorm;

    vec3 rp = o + r * t;
    vec3 ld = normalize(vec3(10, 6, 3));

    // Directional light
	col = vec3(max(0., dot(triNorm, ld))) * .8;

    // Shadow
    float st = trace(rp + ld * 1e-2, ld, triNorm, triNorm);
    if(st > 1e-2 && st < 1e3)
		col *= .1;

    // Ambient light
    col += max(0., n.y) * vec3(.3);

    col *= cos((rp.y + 6.5) * vec3(1.5, 2, .5) / 3.) * .5 + .5;
    float w = t / 800. + pow(max(0., 1. - dot(-r, n)), 4.) * .2;
    col *= mix(1.4, 1., smoothstep(.02 - w, .02 + w, min(bary.x, min(bary.y, bary.z))));

    // Fog
    col = mix(vec3(.5, .5, 1.), col, exp2(-t / 400.));

    // Clamp and gamma-correct
    fragColor = vec4(pow(clamp(col, 0., 1.), vec3(1. / 2.2)), 1.0);
}
