uniform float pixels;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  float dx = 15.0 * (1.0 / pixels);
  float dy = 10.0 * (1.0 / pixels);
  vec2 coord = vec2(dx * floor(tex_coord.x / dx), dy * floor(tex_coord.y / dy));
  return texture(tex, coord);
}
