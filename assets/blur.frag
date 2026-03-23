// Separable Gaussian blur (9-tap).
// Run twice: once with direction=(1,0) for horizontal,
// once with direction=(0,1) for vertical.

uniform vec2 direction;
uniform vec2 texel_size;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
    // 9-tap Gaussian weights (sigma ~= 2.5).
    float w0 = 0.227027;
    float w1 = 0.1945946;
    float w2 = 0.1216216;
    float w3 = 0.054054;
    float w4 = 0.016216;

    vec2 step = direction * texel_size;
    vec4 result = texture(tex, tex_coord) * w0;
    result += texture(tex, tex_coord + step * 1.0) * w1;
    result += texture(tex, tex_coord - step * 1.0) * w1;
    result += texture(tex, tex_coord + step * 2.0) * w2;
    result += texture(tex, tex_coord - step * 2.0) * w2;
    result += texture(tex, tex_coord + step * 3.0) * w3;
    result += texture(tex, tex_coord - step * 3.0) * w3;
    result += texture(tex, tex_coord + step * 4.0) * w4;
    result += texture(tex, tex_coord - step * 4.0) * w4;
    return result * color;
}
