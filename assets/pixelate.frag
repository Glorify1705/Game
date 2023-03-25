#version 460 core
out vec4 frag_color;

in vec2 tex_coord;

uniform sampler2D screen_texture;
uniform float pixels;

void main() {
  float dx = 15.0 * (1.0 / pixels);
  float dy = 10.0 * (1.0 / pixels);
  vec2 coord = vec2(dx * floor(tex_coord.x / dx), dy * floor(tex_coord.y / dy));
  frag_color = texture(screen_texture, coord);
}