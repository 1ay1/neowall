#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function for pseudo-random numbers
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

// Draw a node
float drawNode(vec2 uv, vec2 pos, float activation, float pulseTime) {
    float dist = length(uv - pos);
    float glow = 0.015 / (dist + 0.001);

    // Pulsing effect
    float pulse = sin(pulseTime * 6.28318) * 0.5 + 0.5;
    glow *= activation * (0.5 + pulse * 0.5);

    return glow;
}

// Draw a connection with traveling pulse
float drawConnection(vec2 uv, vec2 start, vec2 end, float progress, float active) {
    vec2 dir = end - start;
    float len = length(dir);
    dir = normalize(dir);

    vec2 toPoint = uv - start;
    float proj = dot(toPoint, dir);
    float perp = length(toPoint - dir * proj);

    // Line thickness
    float line = 0.0;
    if (proj > 0.0 && proj < len) {
        line = 0.003 / (perp + 0.0005);
        line *= active;
    }

    // Traveling pulse
    float pulsePos = mod(progress * len, len);
    float distToPulse = abs(proj - pulsePos);
    float pulse = 0.01 / (distToPulse + 0.005);
    pulse *= active * smoothstep(len + 0.1, 0.0, distToPulse);

    return line * 0.3 + pulse;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    // Background
    float bg = noise(uv * 2.0 + time * 0.05) * 0.02;
    vec3 color = vec3(0.02, 0.02, 0.05) + bg;

    // Network layers
    float layers[4];
    layers[0] = 3.0;
    layers[1] = 5.0;
    layers[2] = 4.0;
    layers[3] = 2.0;

    vec3 nodeColor = vec3(0.0);
    vec3 connColor = vec3(0.0);

    float t = time * 0.8;

    // Draw all layers
    for (int layer = 0; layer < 4; layer++) {
        float layerX = -0.7 + float(layer) * 0.45;
        int nodeCount = int(layers[layer]);

        // Draw nodes in this layer
        for (int i = 0; i < 8; i++) {
            if (i >= nodeCount) break;

            float nodeY = -0.4 + float(i) * (0.8 / float(nodeCount - 1));
            if (nodeCount == 1) nodeY = 0.0;

            vec2 nodePos = vec2(layerX, nodeY);

            // Node activation based on time and position
            float activation = sin(t + float(layer) * 1.5 + float(i) * 0.8) * 0.5 + 0.5;
            activation = 0.3 + activation * 0.7;

            float pulseTime = t * 2.0 + float(layer) * 0.5 + float(i) * 0.3;
            float node = drawNode(uv, nodePos, activation, pulseTime);

            // Color based on activation
            vec3 nColor = mix(
                vec3(0.1, 0.5, 1.0),  // Blue for low activation
                vec3(1.0, 0.3, 0.8),  // Pink for high activation
                activation
            );
            nodeColor += node * nColor;

            // Draw connections to next layer
            if (layer < 3) {
                int nextCount = int(layers[layer + 1]);
                float nextLayerX = -0.7 + float(layer + 1) * 0.45;

                for (int j = 0; j < 8; j++) {
                    if (j >= nextCount) break;

                    float nextNodeY = -0.4 + float(j) * (0.8 / float(nextCount - 1));
                    if (nextCount == 1) nextNodeY = 0.0;

                    vec2 nextPos = vec2(nextLayerX, nextNodeY);

                    // Connection strength
                    float connStrength = hash(vec2(float(layer * 10 + i), float(j))) * 0.5 + 0.5;
                    float connActive = sin(t * 1.5 + float(layer) * 2.0 + float(i) * 0.5 + float(j) * 0.3) * 0.5 + 0.5;

                    float conn = drawConnection(uv, nodePos, nextPos, t * 0.5, connStrength * connActive);

                    vec3 cColor = mix(
                        vec3(0.0, 0.3, 0.6),
                        vec3(0.0, 0.8, 1.0),
                        connActive
                    );
                    connColor += conn * cColor * 0.5;
                }
            }
        }
    }

    // Combine
    color += nodeColor;
    color += connColor;

    // Subtle vignette
    float vignette = 1.0 - length(uv * 0.5);
    vignette = smoothstep(0.3, 1.0, vignette);
    color *= vignette * 0.7 + 0.3;

    gl_FragColor = vec4(color, 1.0);
}
