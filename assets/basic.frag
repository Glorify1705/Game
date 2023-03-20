#version 460 core
out vec4 frag_color;

in vec2 tex_coord;

uniform sampler2D screen_texture;

void main() {
  frag_color = vec4(texture(screen_texture, tex_coord).x, 0, 0, 1);
}
