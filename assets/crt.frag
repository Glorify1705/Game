// CRT monitor effect (scanlines + vignette).
// Adapted from https://www.shadertoy.com/view/WsVSzV
//
// This is a per-draw-call shader (not post-process), so barrel distortion
// is not possible — tex is the sprite/font atlas, not a fullscreen buffer.

uniform vec2 iResolution;

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  vec4 texel = texture(tex, tex_coord);

  // Scanlines: darken every other pixel row.
  // Uses gl_FragCoord for actual pixel position (screen_coord is model space).
  float scanline = mod(floor(gl_FragCoord.y), 2.0);
  float darken = scanline * 0.4;

  // Vignette: darken towards screen edges.
  vec2 uv = gl_FragCoord.xy / iResolution;
  float vig = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
  vig = clamp(pow(16.0 * vig, 0.3), 0.0, 1.0);

  vec3 rgb = texel.rgb * (1.0 - darken) * vig;
  return vec4(rgb, texel.a) * color;
}
