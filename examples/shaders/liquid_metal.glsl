#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

// Smooth noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Fractional Brownian Motion
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < 6; i++) {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// Calculate normal for lighting
vec3 calculateNormal(vec2 uv, float t) {
    float eps = 0.01;

    float h = fbm(uv * 3.0 + vec2(t * 0.2, 0.0));
    float hx = fbm((uv + vec2(eps, 0.0)) * 3.0 + vec2(t * 0.2, 0.0));
    float hy = fbm((uv + vec2(0.0, eps)) * 3.0 + vec2(t * 0.2, 0.0));

    vec3 normal = normalize(vec3(
        (h - hx) / eps,
        (h - hy) / eps,
        1.0
    ));

    return normal;
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    vec2 centeredUv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    float t = time * 0.3;

    // Create flowing liquid metal surface
    vec2 flow = vec2(
        fbm(centeredUv * 2.0 + vec2(t, 0.0)),
        fbm(centeredUv * 2.0 + vec2(0.0, t * 0.7))
    );

    vec2 distorted = centeredUv + flow * 0.1;

    // Multiple layers of liquid movement
    float liquid1 = fbm(distorted * 3.0 + vec2(t * 0.5, t * 0.3));
    float liquid2 = fbm(distorted * 5.0 - vec2(t * 0.3, t * 0.6));
    float liquid3 = fbm(distorted * 8.0 + vec2(t * 0.7, -t * 0.4));

    float surface = liquid1 * 0.5 + liquid2 * 0.3 + liquid3 * 0.2;

    // Calculate surface normal for realistic metallic reflection
    vec3 normal = calculateNormal(distorted, t);

    // Light direction (moving)
    vec3 lightDir = normalize(vec3(
        cos(t * 0.5) * 2.0,
        sin(t * 0.3) * 2.0,
        3.0
    ));

    // Specular reflection
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specular = pow(max(dot(normal, halfDir), 0.0), 32.0);

    // Diffuse lighting
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.5 + 0.5;

    // Metallic base color - silver/mercury
    vec3 metalColor = vec3(0.7, 0.75, 0.8);

    // Add color variation based on surface height
    float colorVar = surface * 0.5 + 0.5;
    metalColor = mix(
        vec3(0.5, 0.55, 0.6),  // Darker metal
        vec3(0.9, 0.95, 1.0),  // Brighter metal
        colorVar
    );

    // Apply lighting
    vec3 color = metalColor * diffuse;

    // Add bright specular highlights
    color += vec3(1.0, 1.0, 1.0) * specular * 0.8;

    // Add subsurface scattering effect
    float scatter = pow(1.0 - abs(dot(normal, viewDir)), 3.0);
    color += vec3(0.6, 0.7, 0.9) * scatter * 0.2;

    // Add ripples
    float rippleTime = t * 2.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);

        // Ripple source
        vec2 rippleCenter = vec2(
            cos(rippleTime * 0.3 + fi * 2.0) * 0.4,
            sin(rippleTime * 0.5 + fi * 1.5) * 0.4
        );

        float dist = length(centeredUv - rippleCenter);

        // Expanding ripple
        float ripple = sin(dist * 20.0 - rippleTime * 3.0 - fi * 2.0) * 0.5 + 0.5;
        ripple *= exp(-dist * 2.0);  // Fade with distance

        color += vec3(0.8, 0.9, 1.0) * ripple * 0.15;
    }

    // Add flowing highlights
    float flowHighlight = sin(distorted.x * 10.0 + distorted.y * 8.0 - t * 4.0) * 0.5 + 0.5;
    flowHighlight = pow(flowHighlight, 4.0);
    color += vec3(1.0, 1.0, 1.0) * flowHighlight * 0.3;

    // Ambient occlusion in valleys
    float ao = smoothstep(0.3, 0.7, surface);
    color *= ao * 0.4 + 0.6;

    // Add edge highlights for liquid metal droplets
    float edge = 1.0 - smoothstep(0.0, 0.1, abs(surface - 0.5));
    color += vec3(0.9, 0.95, 1.0) * edge * 0.2;

    // Chromatic aberration effect for extra realism
    float chromatic = sin(distorted.x * 5.0 + t) * 0.02;
    color.r += chromatic * 0.5;
    color.b -= chromatic * 0.5;

    // Subtle vignette
    float vignette = 1.0 - length(centeredUv * 0.5);
    vignette = smoothstep(0.3, 1.0, vignette);
    color *= vignette * 0.6 + 0.4;

    // Boost contrast for metallic look
    color = pow(color, vec3(0.9));

    gl_FragColor = vec4(color, 1.0);
}
