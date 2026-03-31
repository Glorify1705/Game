// CRT monitor effect: scanlines and chromatic aberration.
// Per-draw-call shader (not post-process).

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  // Chromatic aberration: offset R and B channels.
  float aberration = 0.003;
  vec2 dir = tex_coord - 0.5;
  float r = texture(tex, tex_coord + dir * aberration).r;
  float g = texture(tex, tex_coord).g;
  float b = texture(tex, tex_coord - dir * aberration).b;
  float a = texture(tex, tex_coord).a;

  // Scanlines: darken every other row.
  float scanline = mod(floor(gl_FragCoord.y), 2.0);
  float darken = scanline * 0.12;

  // Slight green tint for CRT phosphor look.
  vec3 rgb = vec3(r, g, b) * (1.0 - darken);
  rgb.g *= 1.03;

  return vec4(rgb, a) * color;
}
