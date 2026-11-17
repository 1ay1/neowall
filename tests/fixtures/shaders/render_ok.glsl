// A small two-pass shader for `neowall render` tests: feedback buffer +
// image, reads the reactive time-of-day so --at has something to change.
// Buffer A
void mainImage(out vec4 c, in vec2 f) {
    vec2 uv = f / iResolution.xy;
    vec3 prev = texture(iChannel0, uv).rgb;
    vec3 now  = 0.5 + 0.5 * cos(iTime + uv.xyx * 3.0 + vec3(0.0, 2.0, 4.0));
    c = vec4(mix(prev, now, 0.25), 1.0);
}
// Image
void mainImage(out vec4 c, in vec2 f) {
    vec2 uv = f / iResolution.xy;
    vec3 col = texture(iChannel0, uv).rgb * (0.4 + 0.6 * iTimeOfDay);
    c = vec4(col + 0.2 * iAudioBeat, 1.0);
}
