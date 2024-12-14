// Stolen from https://www.shadertoy.com/view/WsVSzV

float warp = 0.75; // simulate curvature of CRT monitor
float scan = 0.75; // simulate darkness between scanlines

uniform vec2 iResolution;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  // squared distance from center
  vec2 uv = screen_coord/iResolution.xy;
  vec2 dc = abs(0.5-uv);
  dc *= dc;

  // determine if we are drawing in a scanline
  float apply = abs(sin(screen_coord.y)*0.5*scan);
  // sample the texture
  return vec4(mix(texture(tex,tex_coord).rgb,vec3(0.0),apply),1.0) * color;
}
