// 'The Exorcist' - Optimized Version
// Original by dean_the_coder (Twitter: @deanthecoder)
// https://www.shadertoy.com/view/ftGSDV
// Optimized for lower GPU usage - 2025

#define R iResolution
#define Z0 min(iTime, 0.)
#define sat(x) clamp(x, 0., 1.)
#define S(a, b, c) smoothstep(a, b, c)
#define S01(a) S(0., 1., a)

float t, fade = 1.;
struct Hit {
	float d;
	int id;
	vec3 p;
};

// Simplified hash functions
float h31(vec3 p3) {
	p3 = fract(p3 * .1031);
	p3 += dot(p3, p3.yzx + 3.3456);
	return fract((p3.x + p3.y) * p3.z);
}

float h21(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5); }

// Reduced quality noise (2 octaves instead of 4)
float n31(vec3 p) {
	vec3 ip = floor(p);
	p = fract(p);
	p = p * p * (3. - 2. * p);
	vec4 h = vec4(0, 157, 113, 270) + dot(ip, vec3(7, 157, 113));
	h = mix(fract(sin(h) * 43758.5), fract(sin(h + 7.) * 43758.5), p.x);
	h.xy = mix(h.xz, h.yw, p.y);
	return mix(h.x, h.y, p.z);
}

float n21(vec2 p) { return n31(vec3(p, 1)); }

// Reduced FBM octaves: 2 instead of 4
float fbm(vec3 p) {
	float a = n31(p) * .5;
	p *= 2.;
	a += n31(p) * .25;
	return a;
}

float smin(float a, float b, float k) {
	float h = sat(.5 + .5 * (b - a) / k);
	return mix(b, a, h) - k * h * (1. - h);
}

float max2(vec2 v) { return max(v.x, v.y); }
float max3(vec3 v) { return max(v.x, max(v.y, v.z)); }

float box2d(vec2 p, vec2 b) {
	vec2 q = abs(p) - b;
	return length(max(q, 0.)) + min(max2(q), 0.);
}

float box(vec3 p, vec3 b) {
	vec3 q = abs(p) - b;
	return length(max(q, 0.)) + min(max3(q), 0.);
}

float cyl(vec3 p) {
	vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(.45, .2);
	return min(max(d.x, d.y), 0.) + length(max(d, 0.));
}

float cap(vec3 p) {
	p.x -= clamp(p.x, 0., 10.);
	return length(p) - .1;
}

float tor(vec3 p) {
	vec2 q = vec2(length(p.xz) - .24, p.y);
	return length(q) - .05;
}

vec3 rayDir(vec2 uv) {
	vec3 r = normalize(cross(vec3(0, 1, 0), vec3(0, 0, 1)));
	return normalize(vec3(0, 0, 1.5) + r * uv.x + cross(vec3(0, 0, 1), r) * uv.y);
}

vec3 toWorld(vec3 p) {
	p.yz *= mat2(.99875, .04998, -.04998, .99875);
	p.y += 1.2;
	p.xz -= vec2(2.1875, 17);
	p.xz *= mat2(.96891, .2474, -.2474, .96891);
	return p;
}

float beam(vec3 p) {
	p -= vec3(-15, 16.4, 42);
	p.xy += p.z * vec2(.49, -.44);
	return min(p.z + 62.4, box(p, vec3(2, 3.5, 50)));
}

Hit map(vec3 p) {
	p = toWorld(p);

	// Lamp
	vec3 q = p + vec3(3.9, 1.9, 2);
	float l = length(q.xz);
	float d = l - .2 + .01 * p.y;
	d = smin(d, cyl(q) - .05, .5);
	q.y = abs(q.y - .8) - .25;
	d = min(d, tor(q));
	Hit h = Hit(d, 8, q);

	// Pavement
	d = min(max(p.y + 2.1, p.z), box(p + vec3(0, 2, 1.8), vec3(99, .1, 1.8)));
	if (d < h.d) h = Hit(d, 1, p);

	// Fence walls
	q = p;
	q.x = abs(q.x + .8) - 11.;
	d = box(q - vec3(0, -.5, 0), vec3(8, .15, .4));
	if (d < h.d) h = Hit(d, 4, q);
	d = cap(q + vec3(7.5, -.5, 0));
	if (d < h.d) h = Hit(d, 9, q);
	d = box(q - vec3(0, -1.4, 0), vec3(8, .75, .2));

	// Pillars
	q.x = abs(p.x) - 4.;
	if (p.x > 0.) q.x = abs(q.x) - 1.4;
	d = min(d, box(q, vec3(.5, 2, .5)));
	d = min(d, box(q - vec3(0, 1.6, 0), vec3(.55, .1, .55)));
	if (d < h.d) h = Hit(d, 3, q);

	// Pillar tops
	if (p.x < 4.) {
		q.y -= 2.6;
		l = length(q);
		float f = max(l - .5, q.y);
		d = f < d ? smin(d, f, .25) : f;
		d = smin(d, l - .35, .1);
		if (d < h.d) h = Hit(d, 4, q);
	}

	// House
	d = min(box(p - vec3(12.5, 0, 24), vec3(5, 25, 18)), 42.1 - p.z);

	// Windows
	q = p - vec3(7.5, 0, 42);
	if (q.x > q.z) {
		q.xz = q.zx;
		q.x += 3.;
	}

	q.xy += vec2(15.75, -9.3);
	q.xy = abs(abs(q.xy) - 7.25);
	d = max(d, -box(q, vec3(2.7, 5.2, 2)));
	if (d < h.d) h = Hit(d, 3, q);
	q.z -= .7;
	d = box(q, vec3(2.7, 5.2, .2));
	if (d + .1 < h.d) h = Hit(d + .1, 6, q);
	if (box(q - vec3(4, 0, 0), vec3(1.25, 5.2, 1)) < h.d) h = Hit(box(q - vec3(4, 0, 0), vec3(1.25, 5.2, 1)), 7, q);
	q.x = abs(q.x - 1.25) - .6;
	q.y = abs(abs(q.y) - 2.5) - 1.2;
	d = max(d, -box(q, vec3(.45, 1, 1.2)));
	if (d < h.d) h = Hit(d, 5, q);

	return h;
}

// Simplified normal calculation (tetrahedron method, but fewer samples)
vec3 N(vec3 p, float t) {
	float h = t * .1;
	vec2 e = vec2(.005, 0);
	return normalize(vec3(
		map(p + e.xyy * h).d - map(p - e.xyy * h).d,
		map(p + e.yxy * h).d - map(p - e.yxy * h).d,
		map(p + e.yyx * h).d - map(p - e.yyx * h).d
	));
}

// Simplified shadow (fewer steps)
float shadow(vec3 p) {
	float d, s = 1., t = .1;
	float mxt = length(p - vec3(-7.5, 6.5, 25));
	vec3 ld = normalize(vec3(-7.5, 6.5, 25) - p);

	for (float i = 0.; i < 20.; i++) { // Reduced from 30
		d = map(t * ld + p).d;
		s = min(s, 15. * d / t);
		t += max(.15, d); // Larger steps
		if (mxt - t < .5 || s < .001) break;
	}

	return S01(s);
}

// Simplified AO
float aof(vec3 p, vec3 n, float h) {
	return sat(map(h * n + p).d / h);
}

bool puddle(vec3 p) {
	return p.z > -3.4 && step(.45, n21(p.xz * 2.) + .13) < .5;
}

vec3 lights(vec3 p, vec3 rd, vec3 n, Hit h) {
	float c;
	vec3 ld = normalize(vec3(-7.5, 6.5, 25) - p);
	vec2 spe = vec2(10, 1);

	if (h.id == 3) {
		h.p.x += .1;
		c = .02 + max2(S(.04, 0., abs(fract(vec2(h.p.y * 2., h.p.x + .5 * step(.5, fract(h.p.y - .25)))) - .5))) * .06;
	}
	else if (h.id == 1) {
		float f = step(h.p.z, -3.61);
		c = mix(.1, .01, f) + .1 * step(h.p.z + 3.2, 0.) * S01(abs(fract(h.p.x * .3) - .5) * 60. + f);
		if (f > .5) {
			f = step(.66, abs(fbm(h.p * 20.)));
			spe.y = 5.5 * f;
			c += .3 * f;
		}
	}
	else if (h.id == 7) {
		c = .15;
		c *= .2 + .8 * S01(4. * abs(fract(h.p.y * 3.15) - .5));
	}
	else if (h.id == 6) c = mix(.008, S(25., 0., length(p.xy - vec2(-20, 10))), step(p.x, -3.));
	else if (h.id == 4) c = .25;
	else if (h.id == 8) {
		c = .6;
		spe = vec2(50, 30);
	}
	else c = .15;

	// Simplified lighting calculation
	float diff = sat(.1 + .9 * dot(ld, n));
	float spec = pow(sat(dot(rd, reflect(ld, n))), spe.x) * spe.y;
	float ao = aof(p, n, .5); // Single AO sample instead of two

	return mix(
		(diff + spec) * (.1 + .9 * shadow(p)) * ao * c,
		1.,
		S(.7, 1., 1. + dot(rd, n)) * .01
	) * vec3(.4, .32, .28);
}

vec3 scene(vec3 rd, vec2 uv) {
	t = mod(iTime, 30.);
	fade = min(1., abs(t));

	// Reduced march steps: 60 instead of 80
	float i, od, g, f, man, d = 0.;
	vec3 op, n, col, p = vec3(0);
	Hit h;

	for (i = 0.; i < 60.; i++) {
		h = map(p);
		if (abs(h.d) < .002) break; // Slightly less precise
		d += h.d;
		p = d * rd;
	}

	od = d;
	op = p;
	n = N(p, d);
	col = lights(p, rd, n, h);

	// Simplified puddle reflections (fewer march steps)
	if (h.id == 1 && puddle(h.p)) {
		rd = reflect(rd, n);
		p += n * .01;
		d = 0.;

		for (i = 0.; i < 30.; i++) { // Reduced from 50
			h = map(p);
			if (abs(h.d) < .002) break;
			d += h.d;
			p += h.d * rd;
		}

		col += .005 + .9 * lights(p, rd, N(p, d), h);
	}

	// Simplified spotlight (fewer steps)
	g = 0.;
	d = 0.;
	p = vec3(0);

	for (i = 0.; i < 50.; i++) { // Reduced from 80
		vec3 q = toWorld(p);
		float sdf = beam(q) + (h31(q) - .5) * .3;
		g += .5 / (.01 + pow(abs(sdf), 1. + 4. * S(-80., 125., q.z)));
		d += sdf;
		if (d >= od) break;
		p = d * rd;
	}

	// Fog
	f = fbm(rd * 8. + t * vec3(.05, -.07, .2));
	col = mix(col, vec3(4, 3.2, 2.8), sat(g) * f * .3 * S(26.5, 60., op.z));

	// Man silhouette (unchanged - already efficient)
	uv -= vec2(.175, .059);
	uv.x += (uv.y + .5) * (uv.y + .5) * sin(t) * .01;
	vec2 tuv = uv;
	uv *= mat2(.90045, -.43497, .43497, .90045);
	man = step(.1, step(0., length(uv + vec2(0, .1122)) - .12) + step(0., length(uv - vec2(0, .1122)) - .12));

	f = step(0., box2d(uv, vec2(.014 - .2 * uv.y, .024 + .01 * sin(abs(uv.x) * 8.))) - .01) + step(uv.y, 0.);
	man *= step(.1, f);

	uv *= mat2(.995, .09983, -.09983, .995);
	f = step(0., box2d(uv, vec2(.019 + .3 * uv.y, .032)) - .007) + step(0., uv.y);
	man *= step(.1, f);

	tuv += vec2(.004, .03);
	man *= step(0., box2d(tuv, vec2(.02 - .3 * tuv.y, .01)));

	tuv += vec2(.005, .104);
	tuv *= mat2(.995, -.09983, .09983, .995);
	man *= step(0., box2d(tuv, vec2(.07 - .2 * tuv.y + sin(tuv.y * 18.) * .01, .099 - .3 * abs(tuv.x))) - .005);

	tuv.x = abs(tuv.x);
	tuv *= mat2(.96891, .2474, -.2474, .96891);
	tuv += vec2(-.033, .095);
	man *= step(0., box2d(tuv, vec2(.01 + .05 * tuv.y, .01)) - .008);

	tuv = uv + vec2(-.01, .293);
	tuv *= mat2(.95534, .29552, -.29552, .95534);
	man *= step(0., box2d(tuv, vec2(.034 - .05 * tuv.y, .04 + .05 * tuv.x)) - .005);
	man *= step(0., box2d(tuv - vec2(.014, .054), vec2(.01, .005)) - .003);
	man *= step(0., box2d(tuv - vec2(.074, .02), vec2(.05, .046)) - .003);

	tuv -= vec2(.053, -.08);
	tuv *= mat2(.99875, .04998, -.04998, .99875);
	man *= step(0., box2d(tuv, vec2(.016 + (sin(tuv.y * 80. - 2.25) - tuv.y * 40.) * mix(5e-4, .0015, step(0., tuv.x)), .06)) - .004);
	tuv -= vec2(.055, .01);
	tuv *= mat2(.99875, .04998, -.04998, .99875);
	man *= step(0., box2d(tuv, vec2(.016 + (sin(tuv.y * 60.) - tuv.y * 40.) * mix(5e-4, .0015, step(0., tuv.x)), .065)) - .004);
	if (tuv.y > -.06) col *= man;

	return col;
}

void mainImage(out vec4 fragColor, vec2 fc) {
	vec2 uv = (fc - .5 * R.xy) / R.y;
	vec2 v = fc.xy / R.xy;

	vec3 col = scene(rayDir(uv), uv);

	// Removed AA for performance

	col += .003 * h21(fc);
	col *= .5 + .5 * pow(16. * v.x * v.y * (1. - v.x) * (1. - v.y), .4);
	fragColor = vec4(pow(max(vec3(0), col), vec3(.45)) * fade, 1.);
}
