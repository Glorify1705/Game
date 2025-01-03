klajdflk
vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
    return texture(tex, tex_coord);
}
