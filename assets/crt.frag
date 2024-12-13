vec4 effect(sampler2D screen_texture, vec2 tex_coord) {
    return texture(screen_texture, tex_coord);
}
