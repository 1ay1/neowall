#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash functions
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

vec2 hash2(vec2 p) {
    return vec2(
        hash(p),
        hash(p + vec2(43.45, 71.23))
    );
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

// Quantum probability wave
float quantumWave(vec2 p, float t) {
    float wave = 0.0;

    // Multiple interfering waves
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        vec2 offset = vec2(cos(fi * 2.4), sin(fi * 1.8)) * 0.5;
        vec2 center = offset + vec2(cos(t * 0.5 + fi), sin(t * 0.3 + fi * 0.7)) * 0.3;

        float dist = length(p - center);
        float freq = 10.0 + fi * 3.0;

        // Quantum wave packet
        float amplitude = exp(-dist * dist * 2.0);
        wave += sin(dist * freq - t * 3.0) * amplitude;
    }

    return wave;
}

// Particle with quantum uncertainty
float drawQuantumParticle(vec2 uv, vec2 basePos, float t, float phase) {
    // Uncertainty in position (Heisenberg uncertainty principle visualization)
    vec2 uncertainty = vec2(
        cos(t * 10.0 + phase) * 0.02,
        sin(t * 10.0 + phase * 1.3) * 0.02
    );

    vec2 pos = basePos + uncertainty;
    float dist = length(uv - pos);

    // Probability cloud
    float cloud = exp(-dist * dist * 200.0);

    // Wave function visualization
    float waveFunc = sin(dist * 50.0 - t * 20.0) * 0.5 + 0.5;
    cloud *= waveFunc;

    return cloud;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    float t = time * 0.5;

    // Background quantum field
    float field = quantumWave(uv, t);

    // Color based on wave interference
    vec3 color = vec3(0.0);

    // Interference patterns
    float interference = field * 0.5 + 0.5;

    // Multi-color quantum states
    color.r = sin(interference * 6.28318 + t) * 0.5 + 0.5;
    color.g = sin(interference * 6.28318 + t + 2.094) * 0.5 + 0.5;
    color.b = sin(interference * 6.28318 + t + 4.189) * 0.5 + 0.5;

    color *= 0.3;

    // Add quantum particles
    for (int i = 0; i < 30; i++) {
        float fi = float(i);
        float angle = fi * 0.8 + t * 0.5;
        float radius = 0.3 + sin(fi * 0.5 + t * 0.3) * 0.2;

        vec2 basePos = vec2(cos(angle), sin(angle)) * radius;
        float particle = drawQuantumParticle(uv, basePos, t, fi);

        // Particle color based on quantum state
        vec3 particleColor;
        float state = hash(vec2(fi, 0.0));
        if (state < 0.33) {
            particleColor = vec3(0.5, 0.9, 1.0);  // Excited state (cyan)
        } else if (state < 0.66) {
            particleColor = vec3(1.0, 0.5, 0.9);  // Superposition (magenta)
        } else {
            particleColor = vec3(0.9, 1.0, 0.5);  // Ground state (yellow)
        }

        color += particle * particleColor * 0.8;
    }

    // Quantum entanglement lines
    for (int i = 0; i < 10; i++) {
        float fi = float(i);

        // Particle pair
        float angle1 = fi * 1.5 + t * 0.5;
        float angle2 = angle1 + 3.14159;  // Entangled (opposite)

        float radius = 0.4;
        vec2 pos1 = vec2(cos(angle1), sin(angle1)) * radius;
        vec2 pos2 = vec2(cos(angle2), sin(angle2)) * radius;

        // Draw entanglement connection
        vec2 toLine = uv - pos1;
        vec2 lineDir = normalize(pos2 - pos1);
        float lineLen = length(pos2 - pos1);

        float proj = dot(toLine, lineDir);
        float perp = length(toLine - lineDir * proj);

        if (proj > 0.0 && proj < lineLen) {
            float line = 0.001 / (perp + 0.0005);

            // Pulsing entanglement
            float pulse = sin(t * 5.0 + fi * 2.0) * 0.5 + 0.5;
            color += vec3(1.0, 0.3, 0.8) * line * pulse * 0.3;
        }
    }

    // Add glow to center
    float centerGlow = exp(-length(uv) * 2.0);
    color += vec3(0.1, 0.2, 0.3) * centerGlow * 0.5;

    // Vignette
    float vignette = 1.0 - length(uv * 0.6);
    color *= smoothstep(0.2, 1.0, vignette) * 0.8 + 0.2;

    gl_FragColor = vec4(color, 1.0);
}
