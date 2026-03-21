// Vignette + color grading post-process effect.
// Darkens edges, applies warm/cool tint, and adjustable intensity.

uniform vec2 iResolution;
uniform float intensity; // 0.0 = no effect, 1.0 = full effect
uniform float warmth;    // -1.0 = cool (blue), 0.0 = neutral, 1.0 = warm (orange)

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
    vec4 pixel = texture(tex, tex_coord) * color;

    // Vignette: darken edges based on distance from center.
    vec2 uv = screen_coord / iResolution;
    float dist = distance(uv, vec2(0.5));
    float vignette = smoothstep(0.8, 0.3, dist * (1.0 + intensity));

    // Color grading: shift warm/cool.
    vec3 graded = pixel.rgb;
    graded.r += warmth * 0.08;
    graded.b -= warmth * 0.08;

    return vec4(graded * vignette, pixel.a);
}
