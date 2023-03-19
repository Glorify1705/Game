#version 460 core
out vec4 frag_color;

in vec2 tex_coord;
in vec4 out_color;

uniform sampler2D tex;

void main() { frag_color = texture(tex, tex_coord) * out_color; }
