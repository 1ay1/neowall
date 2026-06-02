// circadian_sky.glsl — a sky that tracks your real clock: dawn, noon, dusk,
// night, all driven by iTimeOfDay / iSun. Sun arcs across by day; at night the
// stars come out and a moon rises. Clouds drift faster with the wind of network
// activity. The most "ambient" of the set — it just quietly matches the room.

vec3 skyGradient(float y, float sun) {
    // Night → dawn/dusk → day palette, blended by sun elevation.
    vec3 night = mix(vec3(0.02, 0.03, 0.08), vec3(0.0, 0.0, 0.02), y);
    vec3 dawn  = mix(vec3(0.9, 0.4, 0.25), vec3(0.25, 0.2, 0.45), y);
    vec3 day   = mix(vec3(0.55, 0.75, 1.0), vec3(0.15, 0.4, 0.85), y);
    vec3 lowSky = mix(night, dawn, smoothstep(0.0, 0.35, sun));
    return mix(lowSky, day, smoothstep(0.25, 0.8, sun));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p  = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float sun = iSun;                 // 0 night, 1 noon
    float tod = iTimeOfDay;           // 0..1 across the day

    vec3 col = skyGradient(uv.y, sun);

    // Sun / moon position: arc from left horizon at dawn to right at dusk.
    float arc = tod * 6.2831 - 1.5708;          // angle across the day
    vec2 sunPos = vec2(cos(arc) * 0.9, sin(arc) * 0.7);
    float toSun = distance(p, sunPos);

    // Daytime sun: bright disc + glow.
    float dayAmt = smoothstep(0.05, 0.3, sun);
    col += dayAmt * (smoothstep(0.08, 0.0, toSun) * vec3(1.0, 0.95, 0.8) * 3.0);
    col += dayAmt * (0.15 / (toSun + 0.1)) * vec3(1.0, 0.8, 0.5);

    // Nighttime moon: opposite arc, cool, with a soft halo.
    float nightAmt = smoothstep(0.25, 0.0, sun);
    vec2 moonPos = -sunPos;
    float toMoon = distance(p, moonPos);
    col += nightAmt * smoothstep(0.06, 0.0, toMoon) * vec3(0.9, 0.92, 1.0) * 2.0;
    col += nightAmt * (0.04 / (toMoon + 0.08)) * vec3(0.4, 0.5, 0.7);

    // Stars at night, twinkling, denser higher in the sky.
    float starN = nwHash21(floor(fragCoord * 0.7));
    float star = step(0.9975, starN);
    float tw = 0.5 + 0.5 * sin(iTime * 3.0 + starN * 50.0);
    col += star * tw * nightAmt * uv.y * 1.2;

    // Drifting clouds — speed scales with network "wind".
    float wind = 0.02 + pulse(iNetDown) * 0.2;
    vec2 cuv = uv * vec2(2.5, 1.5) + vec2(iTime * wind, 0.0);
    float clouds = nwFbm(cuv, 6);
    clouds = smoothstep(0.45, 0.9, clouds) * smoothstep(0.0, 0.5, uv.y);
    vec3 cloudCol = mix(vec3(0.5, 0.45, 0.5), vec3(1.0, 0.98, 0.95), sun);
    // Clouds catch warm light near the sun at dawn/dusk.
    cloudCol = mix(cloudCol, vec3(1.0, 0.6, 0.4), smoothstep(0.4, 0.0, toSun) * (1.0 - dayAmt));
    col = mix(col, cloudCol, clouds * 0.7);

    // Horizon haze.
    col += exp(-uv.y * 8.0) * mix(vec3(0.1, 0.05, 0.1), vec3(0.9, 0.6, 0.4), sun) * 0.4;

    fragColor = vec4(nwGamma(nwTonemap(col)), 1.0);
}
