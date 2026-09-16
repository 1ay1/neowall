// scene_demo.glsl — a complete lit 3D scene, reactive to the machine.
//
// This is the whole shader. No raymarch loop, no normal estimation, no shadow
// function, no tonemapping: neowall's scene kit supplies all of that. You
// describe the WORLD with a distance function and pick a camera.
//
// Compare with fractal_land.glsl (267 lines) or retro_wave.glsl (486) for the
// same class of result.
//
// Reactive bits, because a wallpaper should know what the machine is doing:
//   - CPU load makes the pillars breathe
//   - audio bass pumps the sphere
//   - time of day drives the sky and light (handled by the kit)

// Colour the surface. Defining nwMaterial withholds the kit's default.
vec3 nwMaterial(vec3 p, vec3 n) {
    // Checkerboard on the ground, warm metal on everything else.
    if (n.y > 0.7 && p.y < -0.49) {
        float c = mod(floor(p.x) + floor(p.z), 2.0);
        return mix(vec3(0.16, 0.17, 0.20), vec3(0.34, 0.36, 0.40), c);
    }
    return mix(vec3(0.85, 0.42, 0.22), vec3(0.25, 0.65, 0.85),
               clamp(0.5 + 0.5 * sin(p.y * 1.6), 0.0, 1.0));
}

// The world. Everything the renderer knows about the scene is this function.
float nwMap(vec3 p) {
    float ground = nwGround(p, -0.5);

    // A pulsing sphere in the middle, driven by audio bass.
    float r = 0.8 + 0.18 * iAudioBass + 0.05 * sin(iTime * 1.3);
    float orb = nwSdSphere(p - vec3(0.0, 0.35 + 0.1 * sin(iTime), 0.0), r);

    // An infinite field of pillars, each a different height. nwRepeat tiles
    // space so one box becomes a forest; nwCellId gives per-pillar variation.
    vec3 q = p;
    q.xz = nwRepeat2(q.xz, vec2(3.0));
    vec2 id = nwCellId(p.xz, vec2(3.0));
    float h = 0.6 + 1.4 * nwHash21(id) + 0.5 * iCpu * nwHash21(id + 7.0);
    // Keep the pillars clear of the centre so the orb stays visible.
    float keepOut = smoothstep(2.0, 5.0, length(p.xz));
    float pillar = nwSdBox(q - vec3(0.0, -0.5 + h * 0.5, 0.0), vec3(0.34, h * 0.5, 0.34));
    pillar = mix(1e4, pillar, keepOut);

    // Union everything; smooth-blend the orb into the ground for a soft base.
    return min(nwOpSmoothUnion(ground, orb, 0.35), pillar);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Slow orbit. Mouse drags the camera when you move it.
    float yaw = iTime * 0.12 + (iMouse.x / iResolution.x - 0.5) * 3.0;
    float pitch = 0.22 + (iMouse.y / iResolution.y - 0.5) * 0.6;

    fragColor = vec4(nwRender(nwCameraOrbit(fragCoord, 7.0, yaw, pitch)), 1.0);
}
