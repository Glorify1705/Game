uniform float pixels;

vec4 effect(sampler2D screen_texture, vec2 tex_coord) {
  float dx = 15.0 * (1.0 / pixels);
  float dy = 10.0 * (1.0 / pixels);
  vec2 coord = vec2(dx * floor(tex_coord.x / dx), dy * floor(tex_coord.y / dy));
  return texture(screen_texture, coord);
}
