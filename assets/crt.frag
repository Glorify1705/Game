// Adapted from https://www.shadertoy.com/view/WsVSzV

float warp = 0.75; // simulate curvature of CRT monitor
float scan = 0.75; // simulate darkness between scanlines

uniform vec2 iResolution;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  // squared distance from center
  vec2 uv = screen_coord / iResolution.xy;
  vec2 dc = abs(0.5 - uv);
  dc *= dc;

  // warp the texture coordinates to simulate CRT curvature
  uv.x -= 0.5; uv.x *= 1.0 + (dc.y * (0.3 * warp)); uv.x += 0.5;
  uv.y -= 0.5; uv.y *= 1.0 + (dc.x * (0.4 * warp)); uv.y += 0.5;

  // black outside the warped screen area
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    return vec4(0.0, 0.0, 0.0, 1.0);

  // determine if we are drawing in a scanline
  float apply = abs(sin(screen_coord.y) * 0.5 * scan);
  // sample the texture using warped coordinates
  return vec4(mix(texture(tex, uv).rgb, vec3(0.0), apply), 1.0) * color;
}
