// CRT monitor effect: scanlines, chromatic aberration, vignette, flicker.
// Per-draw-call shader (not post-process).

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  // Chromatic aberration: offset R and B channels slightly.
  float aberration = 0.002;
  vec2 dir = tex_coord - 0.5;
  float r = texture(tex, tex_coord + dir * aberration).r;
  float g = texture(tex, tex_coord).g;
  float b = texture(tex, tex_coord - dir * aberration).b;
  float a = texture(tex, tex_coord).a;

  // Scanlines: darken every other pixel row.
  float scanline = mod(floor(gl_FragCoord.y), 2.0);
  float darken = scanline * 0.35;

  // Thicker scanline bands for more visible effect.
  float band = mod(floor(gl_FragCoord.y / 3.0), 2.0);
  darken += band * 0.08;

  // Vignette: darken towards screen edges.
  vec2 uv = gl_FragCoord.xy / g_ScreenSize;
  float vig = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
  vig = clamp(pow(16.0 * vig, 0.25), 0.0, 1.0);

  // Slight green tint for phosphor look.
  vec3 rgb = vec3(r, g, b) * (1.0 - darken) * vig;
  rgb *= vec3(0.95, 1.0, 0.92);

  return vec4(rgb, a) * color;
}
