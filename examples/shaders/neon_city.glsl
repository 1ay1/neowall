// neon_city.glsl — chrome, neon and rain, in about 60 lines.
//
// Everything expensive is done for you by neowall's scene kit: the march,
// normals, penumbra shadows, ambient occlusion, sky reflections, fog and
// tonemapping. You describe the world and the surfaces.
//
// This one leans on the material hooks the kit exposes:
//   nwMaterial  -> base colour
//   nwGloss     -> roughness + metalness (drives reflections and highlights)
//   nwEmissive  -> glow, added after lighting, so neon actually looks lit
//
// Reactive: the city breathes with CPU load, neon pulses to the audio beat,
// and the whole scene is lit by the real time of day.

// --- the world -------------------------------------------------------------

// One tower, repeated. Height varies per cell so the skyline is not uniform.
float tower(vec3 p, vec2 id) {
    float h = 1.2 + 3.4 * nwHash21(id);
    // CPU load makes the towers rise and settle, slightly out of phase.
    h += 0.5 * iCpu * sin(iTime * 0.6 + nwHash21(id + 3.0) * NW_TAU);
    float body = nwSdRoundBox(p - vec3(0.0, h * 0.5, 0.0),
                              vec3(0.42, h * 0.5, 0.42), 0.06);
    // A thin spire on the taller ones.
    float spire = nwSdCapsule(p, vec3(0.0, h, 0.0), vec3(0.0, h + 0.9, 0.0), 0.035);
    return min(body, mix(1e4, spire, step(3.0, h)));
}

float nwMap(vec3 p) {
    float ground = nwGround(p, 0.0);

    // Infinite city on a 1.6-unit grid.
    vec3 q = p;
    q.xz = nwRepeat2(q.xz, vec2(1.6));
    vec2 id = nwCellId(p.xz, vec2(1.6));
    float city = tower(q, id);

    // A slowly turning chrome monument at the centre, built from an
    // octahedron carved by a sphere — and hollowed into a shell.
    vec3 m = p - vec3(0.0, 2.2, 0.0);
    m.xz = nwRot(iTime * 0.25) * m.xz;
    float shell = nwOpOnion(nwSdOctahedron(m, 1.25), 0.06);
    float monument = nwOpSmoothInter(shell, nwSdSphere(m, 1.1), 0.05);

    // Keep the city clear of the monument.
    city = mix(1e4, city, smoothstep(1.6, 3.2, length(p.xz)));

    return min(min(ground, city), monument);
}

// --- surfaces --------------------------------------------------------------

vec3 nwMaterial(vec3 p, vec3 n) {
    if (p.y < 0.02) return vec3(0.04, 0.045, 0.06);        // wet asphalt
    if (length(p.xz) < 1.7) return vec3(0.92, 0.94, 1.00);  // chrome monument
    return vec3(0.06, 0.07, 0.10);                          // dark glass towers
}

vec2 nwGloss(vec3 p, vec3 n) {
    if (p.y < 0.02) return vec2(0.12, 0.25);   // rain-slicked, mirror-ish
    if (length(p.xz) < 1.7) return vec2(0.05, 1.00);  // polished metal
    return vec2(0.25, 0.60);                    // glass-and-steel
}

vec3 nwEmissive(vec3 p, vec3 n) {
    if (p.y < 0.02 || length(p.xz) < 1.7) return vec3(0.0);
    // Window bands up each tower; colour varies per building.
    vec2 id = nwCellId(p.xz, vec2(1.6));
    float band = smoothstep(0.62, 0.78, fract(p.y * 3.1));
    float lit  = step(0.35, nwHash21(id + floor(p.y * 3.1) * 11.0));
    vec3 hue   = nwPalette(nwHash21(id), vec3(0.5), vec3(0.5),
                           vec3(1.0), vec3(0.00, 0.33, 0.67));
    return hue * band * lit * (0.9 + 0.8 * iAudioBeat) * 1.6;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float yaw = iTime * 0.07 + (iMouse.x / iResolution.x - 0.5) * 2.5;
    fragColor = vec4(nwRender(nwCameraOrbit(fragCoord, 9.5, yaw, 0.30)), 1.0);
}
