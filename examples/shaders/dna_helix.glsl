#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function
float hash(float p) {
    return fract(sin(p * 127.1) * 43758.5453123);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    // Background with depth
    vec3 color = vec3(0.01, 0.02, 0.05);
    float bg = length(uv) * 0.3;
    color = mix(vec3(0.05, 0.02, 0.1), color, bg);

    float t = time * 0.6;

    // DNA parameters
    float helixRadius = 0.3;
    float helixHeight = 3.0;

    vec3 glowColor = vec3(0.0);

    // Draw double helix
    for (int i = 0; i < 40; i++) {
        float fi = float(i);
        float z = -1.5 + mod(fi * 0.15 + t * 0.5, helixHeight);
        float angle = fi * 0.5 + t;

        // First strand (cyan)
        vec3 pos1 = vec3(
            cos(angle) * helixRadius,
            sin(angle) * helixRadius,
            z
        );

        // Second strand (magenta) - 180 degrees offset
        vec3 pos2 = vec3(
            cos(angle + 3.14159) * helixRadius,
            sin(angle + 3.14159) * helixRadius,
            z
        );

        // Project to screen space
        vec2 proj1 = pos1.xy / (pos1.z + 3.0);
        vec2 proj2 = pos2.xy / (pos2.z + 3.0);

        float depth1 = 1.0 / (pos1.z + 3.0);
        float depth2 = 1.0 / (pos2.z + 3.0);

        // Draw backbone spheres
        float dist1 = length(uv - proj1);
        float dist2 = length(uv - proj2);

        float glow1 = 0.015 / (dist1 + 0.005) * depth1;
        float glow2 = 0.015 / (dist2 + 0.005) * depth2;

        // Pulsing effect
        float pulse = sin(t * 2.0 + fi * 0.3) * 0.3 + 0.7;

        // Strand colors
        vec3 color1 = vec3(0.0, 0.8, 1.0) * glow1 * pulse;  // Cyan
        vec3 color2 = vec3(1.0, 0.0, 0.8) * glow2 * pulse;  // Magenta

        glowColor += color1 + color2;

        // Draw base pairs (connecting bars) every few segments
        if (mod(fi, 4.0) < 1.0) {
            vec2 toBar = uv - (proj1 + proj2) * 0.5;
            vec2 barDir = normalize(proj2 - proj1);
            float barLen = length(proj2 - proj1);

            float alongBar = dot(toBar, barDir);
            float perpBar = length(toBar - barDir * alongBar);

            if (abs(alongBar) < barLen * 0.5) {
                float barGlow = 0.002 / (perpBar + 0.001) * depth1;

                // Color based on base pair type (simulate ATCG)
                float pairType = hash(fi);
                vec3 pairColor;
                if (pairType < 0.25) {
                    pairColor = vec3(1.0, 0.3, 0.3);  // A-T (red)
                } else if (pairType < 0.5) {
                    pairColor = vec3(0.3, 1.0, 0.3);  // T-A (green)
                } else if (pairType < 0.75) {
                    pairColor = vec3(0.3, 0.3, 1.0);  // C-G (blue)
                } else {
                    pairColor = vec3(1.0, 1.0, 0.3);  // G-C (yellow)
                }

                glowColor += pairColor * barGlow * pulse * 0.5;
            }
        }
    }

    color += glowColor;

    // Add particles floating around
    for (int i = 0; i < 20; i++) {
        float fi = float(i);
        float angle = fi * 2.5 + t * 0.5;
        float radius = 0.6 + sin(fi) * 0.2;

        vec2 particlePos = vec2(cos(angle), sin(angle)) * radius;
        float dist = length(uv - particlePos);
        float particle = 0.003 / (dist + 0.001);

        color += vec3(0.5, 0.7, 1.0) * particle * 0.3;
    }

    // Vignette
    float vignette = 1.0 - length(uv * 0.4);
    color *= smoothstep(0.0, 1.0, vignette) * 0.8 + 0.2;

    gl_FragColor = vec4(color, 1.0);
}
