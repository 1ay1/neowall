// mouse_ripples.glsl — water ripples that emanate from your cursor.
// Interactive: move the mouse to draw trails on the water; the surface ripples
// and settles. A wave-equation simulation in Buffer A (self-feedback), shaded
// in the Image pass. Demonstrates multipass + iMouse + the .neowall manifest.
//
// Sidecar: mouse_ripples.neowall binds Buffer A iChannel0 = self.

// ===== Buffer A =====
// Classic 2D wave equation on a height field stored in R = current, G = prev.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy;

    // Read neighbours from our own previous frame (iChannel0 = self).
    float c  = texture(iChannel0, uv).r;
    float p  = texture(iChannel0, uv).g;
    float l  = texture(iChannel0, uv - vec2(px.x, 0.0)).r;
    float r  = texture(iChannel0, uv + vec2(px.x, 0.0)).r;
    float u  = texture(iChannel0, uv + vec2(0.0, px.y)).r;
    float d  = texture(iChannel0, uv - vec2(0.0, px.y)).r;

    // Wave step with damping.
    float next = (l + r + u + d) * 0.5 - p;
    next *= 0.985;   // damping → ripples fade

    // Inject from the mouse (iMouse.xy in pixels). A moving cursor draws waves.
    if (iMouse.x > 1.0 || iMouse.y > 1.0) {
        vec2 m = iMouse.xy / iResolution.xy;
        float d2 = distance(uv, m);
        next += smoothstep(0.03, 0.0, d2) * 0.6;
    }

    // A gentle ambient driver so the pool is never totally dead, and a bass
    // thump that drops a big droplet on the beat.
    next += beat() * smoothstep(0.08, 0.0, distance(uv, vec2(0.5))) * 0.5;

    // Seed on first frame.
    if (iFrame < 2) next = 0.0;

    fragColor = vec4(next, c, 0.0, 1.0);
}

// ===== Image =====
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy;

    // Height + gradient → surface normal for refraction-ish shading.
    float h  = texture(iChannel0, uv).r;
    float hx = texture(iChannel0, uv + vec2(px.x, 0.0)).r - h;
    float hy = texture(iChannel0, uv + vec2(0.0, px.y)).r - h;
    vec3 n = normalize(vec3(-hx, -hy, 0.06));

    // Base water color, shifting with time of day.
    vec3 deep = mix(vec3(0.02, 0.06, 0.12), vec3(0.05, 0.12, 0.2), dayNight());
    vec3 col = deep;

    // Fake sky reflection along the normal.
    vec3 light = normalize(vec3(0.4, 0.6, 0.7));
    float spec = pow(max(dot(n, light), 0.0), 32.0);
    float fres = pow(1.0 - n.z, 3.0);
    col += spec * vec3(1.0, 0.95, 0.85) * 1.5;
    col += fres * mix(vec3(0.1, 0.3, 0.5), vec3(0.6, 0.8, 1.0), dayNight());

    // Caustic shimmer from the height field.
    col += abs(h) * vec3(0.2, 0.5, 0.8) * 2.0;

    // Cursor glow so you can always find your pointer.
    if (iMouse.x > 1.0) {
        float m = distance(uv, iMouse.xy / iResolution.xy);
        col += smoothstep(0.04, 0.0, m) * vec3(0.3, 0.6, 1.0);
    }

    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
