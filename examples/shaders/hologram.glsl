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

// Draw a hexagon
float hexagon(vec2 p, float r) {
    vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

// Draw a glowing line
float drawLine(vec2 uv, vec2 start, vec2 end, float thickness) {
    vec2 dir = end - start;
    float len = length(dir);
    dir = normalize(dir);

    vec2 toPoint = uv - start;
    float proj = dot(toPoint, dir);
    float perp = length(toPoint - dir * proj);

    if (proj > 0.0 && proj < len) {
        return thickness / (perp + thickness * 0.5);
    }
    return 0.0;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;
    vec2 screenUv = gl_FragCoord.xy / resolution.xy;

    float t = time * 0.5;

    // Dark background
    vec3 color = vec3(0.0, 0.05, 0.1);

    // Holographic grid
    vec3 holoColor = vec3(0.0);
    vec3 cyan = vec3(0.0, 1.0, 1.0);

    // Hexagonal grid pattern
    vec2 hexUv = uv * 8.0;
    hexUv.y += t * 0.5;

    vec2 hexId = floor(hexUv);
    vec2 hexLocal = fract(hexUv) - 0.5;

    float hex = hexagon(hexLocal * 2.0, 0.9);
    float hexGrid = smoothstep(0.05, 0.0, abs(hex - 0.1));

    // Flickering hexagons
    float hexFlicker = hash(hexId);
    float hexPulse = sin(t * 3.0 + hexFlicker * 10.0) * 0.5 + 0.5;
    hexPulse = step(0.95, hexPulse);

    holoColor += cyan * hexGrid * 0.3;
    holoColor += cyan * hexPulse * 0.5 * smoothstep(0.2, 0.0, abs(hex));

    // Vertical scan lines
    float scanLine = sin(screenUv.y * 500.0) * 0.5 + 0.5;
    scanLine = pow(scanLine, 10.0);
    holoColor += cyan * scanLine * 0.2;

    // Horizontal sweeping scan
    float sweep = mod(screenUv.y + t * 0.2, 1.0);
    float sweepLine = exp(-abs(screenUv.y - sweep) * 40.0);
    holoColor += cyan * sweepLine * 0.8;

    // Add glitch effect
    float glitchTime = floor(t * 2.0);
    float glitch = hash(vec2(glitchTime, floor(screenUv.y * 20.0)));
    if (glitch > 0.95) {
        holoColor.x += 0.3;
        uv.x += (glitch - 0.95) * 0.1;
    }

    // Circular HUD elements
    float radius = 0.3;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float angle = fi * 2.094 + t * 0.3;
        vec2 center = vec2(cos(angle), sin(angle)) * 0.5;

        float dist = length(uv - center);

        // Rotating circle
        float circle = abs(dist - radius * (0.5 + fi * 0.2));
        circle = 0.01 / (circle + 0.005);

        // Rotation indicator
        float markerAngle = atan(uv.y - center.y, uv.x - center.x);
        float marker = sin(markerAngle * 8.0 - t * 2.0) * 0.5 + 0.5;
        marker = step(0.9, marker);

        float circleRange = smoothstep(radius * 1.2, radius * 0.8, dist);
        circleRange *= smoothstep(radius * 0.5, radius * 0.7, dist);

        holoColor += cyan * circle * 0.3;
        holoColor += cyan * marker * circleRange * 0.4;
    }

    // Data stream lines
    for (int i = 0; i < 8; i++) {
        float fi = float(i);
        float yPos = -0.6 + fi * 0.17;

        float lineY = abs(uv.y - yPos);
        float line = 0.002 / (lineY + 0.001);

        // Moving segments
        float segmentPos = mod(uv.x + t * (0.3 + fi * 0.1), 1.5) - 0.75;
        float segment = smoothstep(0.3, 0.0, abs(segmentPos));

        holoColor += cyan * line * segment * 0.4;

        // Data points
        float dataPoint = sin(segmentPos * 20.0 - t * 5.0) * 0.5 + 0.5;
        dataPoint = step(0.8, dataPoint);
        holoColor += cyan * dataPoint * segment * line * 2.0;
    }

    // Crosshair in center
    float crosshairH = 0.005 / (abs(uv.y) + 0.003);
    float crosshairV = 0.005 / (abs(uv.x) + 0.003);
    crosshairH *= smoothstep(0.15, 0.05, abs(uv.x));
    crosshairV *= smoothstep(0.15, 0.05, abs(uv.y));

    holoColor += cyan * (crosshairH + crosshairV) * 0.5;

    // Corner brackets
    float cornerSize = 0.6;
    float cornerThickness = 0.15;

    vec2 cornerPos = abs(uv);
    float corner = 0.0;

    if (cornerPos.x > cornerSize && cornerPos.y > cornerSize - cornerThickness) {
        corner = 1.0;
    }
    if (cornerPos.y > cornerSize && cornerPos.x > cornerSize - cornerThickness) {
        corner = 1.0;
    }

    corner *= smoothstep(0.01, 0.0, abs(cornerPos.x - cornerSize));
    corner += smoothstep(0.01, 0.0, abs(cornerPos.y - cornerSize));

    holoColor += cyan * corner * 0.6;

    // Particle effects
    for (int i = 0; i < 20; i++) {
        float fi = float(i);

        vec2 particleStart = vec2(
            hash(vec2(fi, 0.0)) * 2.0 - 1.0,
            hash(vec2(fi, 1.0)) * 2.0 - 1.0
        );

        float particleTime = mod(t * 0.5 + fi * 0.1, 1.0);
        vec2 particlePos = particleStart + vec2(
            sin(particleTime * 6.28318 + fi) * 0.2,
            cos(particleTime * 6.28318 + fi * 1.3) * 0.2
        );

        float particleDist = length(uv - particlePos);
        float particle = 0.005 / (particleDist + 0.002);

        float particleFade = 1.0 - particleTime;
        holoColor += cyan * particle * particleFade * 0.3;
    }

    // Add noise/interference
    float interference = noise(uv * 50.0 + t * 2.0) * 0.1;
    holoColor += interference * cyan;

    // Flicker effect
    float flicker = sin(t * 50.0) * 0.02 + 0.98;
    holoColor *= flicker;

    // Combine with background
    color += holoColor;

    // Edge glow
    float edgeDist = min(min(screenUv.x, 1.0 - screenUv.x), min(screenUv.y, 1.0 - screenUv.y));
    float edgeGlow = exp(-edgeDist * 10.0);
    color += cyan * edgeGlow * 0.1;

    // Vignette
    float vignette = 1.0 - length(uv * 0.5);
    vignette = smoothstep(0.2, 1.0, vignette);
    color *= vignette * 0.7 + 0.3;

    // Boost brightness
    color *= 1.2;

    gl_FragColor = vec4(color, 1.0);
}
