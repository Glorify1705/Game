#version 460 core
out vec4 frag_color;

in vec2 tex_coord;
in vec4 out_color;

uniform sampler2D tex;

void main() {
    vec4 color = texture(tex, tex_coord) * out_color;
    frag_color = color;
}
