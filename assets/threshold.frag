// Brightness threshold extraction for bloom.
// Outputs only pixels above a luminance threshold; everything else is black.

uniform float threshold;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
    vec4 pixel = texture(tex, tex_coord) * color;
    float luminance = dot(pixel.rgb, vec3(0.2126, 0.7152, 0.0722));
    // Soft knee: smoothly fade near the threshold to avoid hard cutoffs.
    float contribution = smoothstep(threshold - 0.1, threshold + 0.1, luminance);
    return vec4(pixel.rgb * contribution, pixel.a * contribution);
}
