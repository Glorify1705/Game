// CRT monitor effect: scanlines, chromatic aberration, vignette.
// Per-draw-call shader (not post-process).

vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
  // Chromatic aberration: offset R and B channels slightly.
  float aberration = 0.001;
  vec2 dir = tex_coord - 0.5;
  float r = texture(tex, tex_coord + dir * aberration).r;
  float g = texture(tex, tex_coord).g;
  float b = texture(tex, tex_coord - dir * aberration).b;
  float a = texture(tex, tex_coord).a;

  // Scanlines: subtle darkening every other row.
  float scanline = mod(floor(gl_FragCoord.y), 2.0);
  float darken = scanline * 0.12;

  // Vignette: gentle darkening towards edges.
  vec2 uv = gl_FragCoord.xy / g_ScreenSize;
  float vig = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
  vig = clamp(pow(16.0 * vig, 0.4), 0.0, 1.0);

  // Slight warm phosphor tint.
  vec3 rgb = vec3(r, g, b) * (1.0 - darken) * vig;
  rgb *= vec3(0.97, 1.0, 0.95);

  return vec4(rgb, a) * color;
}
