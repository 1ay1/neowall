// Decodering
// Leon Denise 2023-10-29
// inspired by Ryoiji Ikeda and Evangelion

// 2023-10-31: updated code from Fabrice Neyret suggestions

// Blackle Mori
// https://suricrasia.online/blog/shader-functions/
#define erot(p,A,a) mix(dot(A, p)*A, p, cos(a)) + cross(A,p)*sin(a)

// Dave Hoskins
// https://www.shadertoy.com/view/4djSRW
float hash11(float p)
{
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}
vec2 hash22(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

void mainImage(out vec4 color, in vec2 pixel)
{
    color = vec4(0, 0, 0, 1);

    // coordinates
    vec2 p = (pixel - iResolution.xy / 2.0) / iResolution.y;

    // perspective
    vec3 q = vec3(p, 1.0);
    q = erot(q, vec3(1, 1, 1), -0.5);
    p = q.xy / q.z;

    // scroll
    p.y += iTime * 0.1;

    // grid
    float grid = 16.0;
    vec2 cell = floor(p * grid);

    // column animation
    float mask = floor(iTime * hash11(cell.x));
    mask = step(0.5, sin(mask));
    float speed = 40.0 * hash11(cell.x + 75.0);
    cell.y += floor(iTime * speed) * mask;

    // random per cell
    vec2 rng = hash22(cell);

    // character selection
    int ch = int(iTime * rng.x);
    int grd = int(grid);
    ch = (ch % 50) + 145;
    vec2 offset = vec2(float(ch % grd), float(ch / grd));

    // atlas coordinates
    p = mod(p, 1.0 / grid);
    p += offset / grid;
    vec4 map = textureLod(iChannel0, p, 1.5 / q.z);

    // color
    bool colorful = sin(rng.y * 6.0 + iTime) > 0.5;
    if (colorful)
    {
        color.rgb = 0.5 + 0.5 * cos(vec3(0, 2, 4) + floor(cell.y * 0.1));
        color.rgb *= 1.0 - map.r;
    }
    else
    {
        color.rgb = vec3(map.r);
    }
}
