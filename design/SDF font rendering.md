# SDF Font Rendering

This document describes what Signed Distance Field (SDF) rendering is, how it works, and how to implement it in the engine's font system.

## Background: What is an SDF?

A Signed Distance Field is a 2D grid where each pixel stores the distance from that pixel to the nearest edge of a shape. The "signed" part means inside pixels store positive values and outside pixels store negative values (or vice versa, depending on convention). The edge itself sits at a threshold value (typically 0.5 when normalized to 0-1, or 128 when stored as a uint8).

For font rendering, the "shape" is a glyph outline. A capital "A" would produce an SDF where pixels well inside the strokes are bright white (high distance from edge), pixels well outside are black (high negative distance), and pixels near the stroke boundary transition smoothly from white to black.

### Why SDFs produce sharp text at any scale

Traditional bitmap font atlases store rasterized glyph images. When you scale a 32px bitmap glyph up to 128px, you're interpolating pixel colors, which produces blurry edges. When you scale down aggressively, you lose detail and get aliased artifacts.

An SDF atlas stores *distances*, not colors. The fragment shader reconstructs the glyph edge by thresholding the distance value:

```
if (distance > 0.5) → inside the glyph (draw foreground color)
if (distance < 0.5) → outside the glyph (discard / transparent)
```

When you scale an SDF texture, bilinear interpolation produces *interpolated distances*, which are still geometrically meaningful. The threshold still produces a crisp edge at the correct location. A 48px SDF glyph can be rendered at 8px or 200px and the edges remain sharp because the shader is reconstructing the edge from the distance field, not stretching a pre-rendered image.

The quality limit is determined by how many distance samples exist to capture the shape's features. Fine details (thin serifs, tight curves) need enough SDF resolution to represent them. For most fonts, 32-64px SDF glyphs cover sizes from ~8px to 300px+ without visible artifacts.

### How `stbtt_GetCodepointSDF` works

stb_truetype already includes SDF generation. The function signature:

```cpp
unsigned char *stbtt_GetCodepointSDF(
    const stbtt_fontinfo *info,
    float scale,             // Controls SDF bitmap size (same as regular glyph scale)
    int codepoint,           // Character to generate SDF for
    int padding,             // Extra pixels around glyph filled with distance values
    unsigned char onedge_value,    // Value at the glyph edge (typically 128)
    float pixel_dist_scale,        // Distance units per SDF pixel (typically 128/padding)
    int *width, int *height,       // Output: SDF bitmap dimensions
    int *xoff, int *yoff           // Output: positioning offsets
);
```

The algorithm:
1. Rasterize the glyph outline at the requested scale
2. For each pixel in the output bitmap, compute the minimum distance to any edge of the glyph
3. Encode the distance as a uint8: `onedge_value + distance * pixel_dist_scale` for inside, `onedge_value - distance * pixel_dist_scale` for outside
4. Pixels inside the glyph get values > `onedge_value`, outside get values < `onedge_value`

The `padding` parameter is critical: it determines how many pixels of distance information exist outside the glyph boundary. This padding is what enables outline and glow effects — without padding, you only have distance information inside the glyph and right at its edge. A padding of 5-8 pixels is typical.

The `pixel_dist_scale` parameter controls how quickly the distance "falls off." With `onedge_value=128` and `pixel_dist_scale=128/5.0`, a pixel 5 SDF-pixels away from the edge maps to value 0 (outside) or 255 (inside). Larger values give sharper falloff (less anti-aliasing range but more precise edges); smaller values give more gradual falloff (smoother effects but less edge precision).

### Typical SDF parameters

| Parameter | Value | Rationale |
|---|---|---|
| SDF glyph height | 48px | Good balance of quality vs atlas size. Captures detail in most fonts. |
| `padding` | 6 | Enough room for outlines/glow. 8 for heavy outlines. |
| `onedge_value` | 128 | Center of 0-255 range gives equal resolution inside and outside |
| `pixel_dist_scale` | `128.0 / 6.0` ≈ 21.3 | Maps full padding distance to the available 0-128 range |

## What changes in the rendering pipeline

### Current pipeline (bitmap font atlas)

```
LoadFont:
  stbtt_PackFontRange() → rasterize 95 ASCII glyphs at 100px into 2048x2048 atlas
  Upload atlas as GL_RED texture with R→RGBA swizzle

DrawText:
  For each character:
    stbtt_GetPackedQuad() → atlas UV coords + glyph quad size
    Scale quad by (requested_size / 100.0)
    PushQuad to batch renderer

Fragment shader:
  frag_color = texture(tex, tex_coord) * out_color;
  (samples alpha from atlas, multiplies by vertex color)
```

### Proposed pipeline (SDF font atlas)

```
LoadFont:
  For each ASCII glyph 32-126:
    stbtt_GetCodepointSDF() → individual SDF bitmap + metrics
  Pack SDF bitmaps into atlas using stb_rect_pack (already a dependency)
  Upload atlas as GL_RED texture with R→RGBA swizzle

DrawText:
  SetShaderProgram("sdf")          ← NEW: switch to SDF shader
  For each character:
    Look up glyph atlas UV coords + metrics from stored glyph table
    Scale quad by (requested_size / sdf_pixel_height)
    PushQuad to batch renderer
  SetShaderProgram("pre_pass")     ← NEW: restore default shader

Fragment shader (SDF):
  float distance = texture(tex, tex_coord).r;
  float smoothing = fwidth(distance);
  float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
  frag_color = vec4(out_color.rgb, out_color.a * alpha);
```

### Summary of changes

| Component | Current | SDF |
|---|---|---|
| Atlas generation | `stbtt_PackFontRange` (bitmap rasterizer) | `stbtt_GetCodepointSDF` per glyph + `stb_rect_pack` for packing |
| Glyph reference height | 100px | 48px (SDF needs less resolution) |
| Atlas size | 2048x2048 (4 MB) | 512x512 or smaller (~256 KB) |
| Fragment shader | Direct texture sample | SDF threshold + smoothstep |
| Shader switching | Not needed | Switch to SDF shader before text, restore after |
| Glyph metrics storage | `stbtt_packedchar[256]` | Custom struct with UV coords + offsets per glyph |

## New libraries

None. Everything needed is already in the codebase:

- **stb_truetype.h** — `stbtt_GetCodepointSDF()` for SDF generation, `stbtt_GetCodepointHMetrics()` for advance/bearing metrics
- **stb_rect_pack.h** — `stbrp_pack_rects()` for packing individual SDF bitmaps into an atlas (already bundled in `libraries/`, currently unused)
- **OpenGL 4.6** — `fwidth()` GLSL function for screen-space anti-aliasing (available in GLSL 4.6 core)

## Detailed implementation

### 1. SDF glyph data structure

Replace the current `stbtt_packedchar chars[256]` with a custom struct that stores SDF-specific data:

```cpp
struct SDFGlyph {
  // Atlas UV coordinates (normalized 0-1)
  float s0, t0, s1, t1;
  // Glyph positioning (in SDF pixel units, scaled at render time)
  float x_offset, y_offset;       // Top-left offset from pen position
  float width, height;            // Glyph quad size
  float advance;                  // Horizontal advance to next character
};

struct FontInfo {
  GLuint texture;
  float scale;                    // stbtt scale for SDF reference height
  float pixel_height;             // SDF reference height (48px)
  int ascent, descent, line_gap;
  stbtt_fontinfo font_info;       // Kept for kerning queries
  SDFGlyph glyphs[128];          // Indexed by codepoint (only 32-126 used)
  int atlas_width, atlas_height;  // Actual atlas dimensions
};
```

### 2. Atlas generation (`LoadFont`)

```cpp
void Renderer::LoadFont(const DbAssets::Font& asset) {
  constexpr float kSDFHeight = 48.0f;
  constexpr int kPadding = 6;
  constexpr unsigned char kOnEdge = 128;
  constexpr float kPixelDistScale = kOnEdge / (float)kPadding;
  constexpr int kFirstChar = 32;
  constexpr int kLastChar = 126;
  constexpr int kNumChars = kLastChar - kFirstChar + 1;

  FontInfo font;
  CHECK(stbtt_InitFont(&font.font_info, asset.contents,
                       stbtt_GetFontOffsetForIndex(asset.contents, 0)),
        "Could not initialize ", asset.name);

  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, kSDFHeight);
  font.pixel_height = kSDFHeight;
  stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                        &font.line_gap);

  // Phase 1: Generate individual SDF bitmaps for each glyph.
  struct GlyphBitmap {
    unsigned char* data;
    int w, h, xoff, yoff;
  };
  GlyphBitmap bitmaps[kNumChars];

  for (int i = 0; i < kNumChars; ++i) {
    int cp = kFirstChar + i;
    bitmaps[i].data = stbtt_GetCodepointSDF(
        &font.font_info, font.scale, cp, kPadding, kOnEdge,
        kPixelDistScale, &bitmaps[i].w, &bitmaps[i].h,
        &bitmaps[i].xoff, &bitmaps[i].yoff);
    // Store advance width.
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&font.font_info, cp, &advance, &bearing);
    font.glyphs[cp].advance = advance * font.scale;
  }

  // Phase 2: Pack glyph rectangles into an atlas using stb_rect_pack.
  stbrp_rect rects[kNumChars];
  for (int i = 0; i < kNumChars; ++i) {
    rects[i].id = i;
    rects[i].w = bitmaps[i].data ? bitmaps[i].w : 0;
    rects[i].h = bitmaps[i].data ? bitmaps[i].h : 0;
  }

  // Start with 256x256 and grow if needed.
  int atlas_dim = 256;
  bool packed = false;
  while (atlas_dim <= 2048 && !packed) {
    stbrp_context pack_ctx;
    stbrp_node nodes[256];
    stbrp_init_target(&pack_ctx, atlas_dim, atlas_dim, nodes, 256);
    packed = stbrp_pack_rects(&pack_ctx, rects, kNumChars);
    if (!packed) atlas_dim *= 2;
  }
  CHECK(packed, "Could not pack SDF atlas for ", asset.name);

  // Phase 3: Blit individual glyph bitmaps into the atlas.
  ArenaAllocator scratch(allocator_, atlas_dim * atlas_dim);
  uint8_t* atlas = scratch.NewArray<uint8_t>(atlas_dim * atlas_dim);
  memset(atlas, 0, atlas_dim * atlas_dim);

  for (int i = 0; i < kNumChars; ++i) {
    if (!bitmaps[i].data) continue;
    int cp = kFirstChar + i;
    const int dx = rects[i].x;
    const int dy = rects[i].y;
    // Copy glyph bitmap into atlas.
    for (int row = 0; row < bitmaps[i].h; ++row) {
      memcpy(&atlas[(dy + row) * atlas_dim + dx],
             &bitmaps[i].data[row * bitmaps[i].w],
             bitmaps[i].w);
    }
    // Store UV coordinates and metrics.
    font.glyphs[cp].s0 = (float)dx / atlas_dim;
    font.glyphs[cp].t0 = (float)dy / atlas_dim;
    font.glyphs[cp].s1 = (float)(dx + bitmaps[i].w) / atlas_dim;
    font.glyphs[cp].t1 = (float)(dy + bitmaps[i].h) / atlas_dim;
    font.glyphs[cp].x_offset = bitmaps[i].xoff;
    font.glyphs[cp].y_offset = bitmaps[i].yoff;
    font.glyphs[cp].width = bitmaps[i].w;
    font.glyphs[cp].height = bitmaps[i].h;
    stbtt_FreeSDF(bitmaps[i].data, 0, 0, nullptr);
  }

  font.atlas_width = atlas_dim;
  font.atlas_height = atlas_dim;
  font.texture = renderer_->LoadFontTexture(atlas, atlas_dim, atlas_dim);
  fonts_.Push(font);
  font_table_.Insert(asset.name, &fonts_.back());
}
```

### 3. SDF fragment shader

Add a new SDF fragment shader alongside the existing pre-pass shaders in `shaders.cc`:

```glsl
#version 460 core
out vec4 frag_color;

in vec2 tex_coord;
in vec4 out_color;

uniform sampler2D tex;

// SDF effect uniforms.
uniform float outline_width;     // 0.0 = no outline, 0.0-0.2 typical
uniform vec4 outline_color;      // RGBA outline color

void main() {
    float distance = texture(tex, tex_coord).r;
    float smoothing = fwidth(distance);

    if (outline_width > 0.0) {
        // Two-threshold rendering: outline edge and fill edge.
        float outline_edge = 0.5 - outline_width;
        float outline_alpha = smoothstep(outline_edge - smoothing,
                                         outline_edge + smoothing, distance);
        float fill_alpha = smoothstep(0.5 - smoothing,
                                      0.5 + smoothing, distance);
        vec4 color = mix(outline_color, vec4(out_color.rgb, 1.0), fill_alpha);
        color.a *= outline_alpha * out_color.a;
        frag_color = color;
    } else {
        // Simple threshold rendering.
        float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
        frag_color = vec4(out_color.rgb, out_color.a * alpha);
    }
}
```

This shader is compiled and linked at startup in `Shaders::Shaders()`:

```cpp
CHECK(Compile(DbAssets::ShaderType::kFragment, "sdf.frag",
              kSDFFragmentShader, kUseCache), LastError());
CHECK(Link("sdf", "pre_pass.vert", "sdf.frag", kUseCache), LastError());
```

The SDF shader reuses the existing pre-pass vertex shader since vertex transformation is identical — only the fragment stage differs.

### 4. Draw-time shader switching

`DrawText` must bracket text rendering with shader switches:

```cpp
void Renderer::DrawText(std::string_view font_name, uint32_t size,
                        std::string_view str, FVec2 position) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) return;

  renderer_->SetShaderProgram("sdf");       // ← Switch to SDF shader
  renderer_->SetActiveTexture(info->texture);

  const float pixel_scale = size / info->pixel_height;
  FVec2 p = position;

  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    // ... handle \n, \t, \033 as before ...

    const SDFGlyph& g = info->glyphs[(int)c];
    float x0 = p.x + g.x_offset * pixel_scale;
    float y0 = p.y + g.y_offset * pixel_scale;
    float x1 = x0 + g.width * pixel_scale;
    float y1 = y0 + g.height * pixel_scale;
    renderer_->PushQuad(FVec(x0, y0), FVec(x1, y1),
                        FVec(g.s0, g.t0), FVec(g.s1, g.t1),
                        FVec(0, 0), 0);
    p.x += g.advance * pixel_scale;

    // Kerning.
    if ((i + 1) < str.size()) {
      p.x += pixel_scale * info->scale *
             stbtt_GetCodepointKernAdvance(&info->font_info,
                                           str[i], str[i + 1]);
    }
  }

  renderer_->SetShaderProgram("pre_pass");  // ← Restore default
}
```

The batch renderer already supports `kSetShader` commands in its command buffer, so shader switches are naturally batched. If multiple `DrawText` calls are made consecutively, the command buffer will contain:

```
kSetShader("sdf")
kSetTexture(font_atlas_1)
kRenderQuad × N  (first string)
kSetTexture(font_atlas_2)  ← only if different font
kRenderQuad × M  (second string)
kSetShader("pre_pass")
```

The batch renderer flushes and re-binds the shader program when it encounters a `kSetShader` command. This means all text quads between two shader switches are drawn in a single batch.

### 5. `TextDimensions` update

`TextDimensions` does not need shader changes (it doesn't render anything), but should use the same metric source as `DrawText` for consistency:

```cpp
IVec2 Renderer::TextDimensions(std::string_view font_name, uint32_t size,
                               std::string_view str) {
  FontInfo* info = nullptr;
  if (!font_table_.Lookup(font_name, &info)) return IVec2();

  const float pixel_scale = size / info->pixel_height;
  const float scale = pixel_scale * info->scale;
  float x = 0, max_x = 0;
  float y = scale * (info->ascent - info->descent + info->line_gap);

  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    // ... handle \n, \t, \033 ...
    x += info->glyphs[(int)c].advance * pixel_scale;
    if ((i + 1) < str.size()) {
      x += pixel_scale * info->scale *
           stbtt_GetCodepointKernAdvance(&info->font_info, str[i], str[i + 1]);
    }
  }
  max_x = std::max(max_x, x);
  return IVec2((int)max_x, (int)y);
}
```

This fixes Bug 3 (DrawText/TextDimensions inconsistency) as a side effect, because both now use the same `SDFGlyph::advance` field.

### 6. `LoadFontTexture` changes

The current `LoadFontTexture` sets `GL_REPEAT` wrapping. SDF atlases need `GL_CLAMP_TO_EDGE` to prevent distance values from wrapping at atlas boundaries, which would cause artifacts at glyph edges near the atlas border.

Also, mipmaps are counterproductive for SDF textures. SDF relies on the raw distance values being interpolated accurately; mipmapping averages distances across multiple texels, which blurs the distance field and defeats the purpose. Use `GL_LINEAR` for both min and mag filters:

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
// Do NOT call glGenerateMipmap for SDF textures.
```

This could either be done by adding a separate `LoadSDFFontTexture` method, or adding a flag parameter to the existing `LoadFontTexture`.

### 7. SDF atlas caching in the asset database

SDF generation is the most expensive step in font loading (10-50ms per font vs 2-5ms for bitmap rasterization). The engine already has a caching pattern for expensive transformations: the Fennel-to-Lua compilation cache. SDF atlas generation should use the same approach — compute once, persist to SQLite, skip regeneration on subsequent launches if the source font hasn't changed.

#### How the Fennel cache works

The Fennel compilation cache follows this flow:

```
Load .fnl file
  → rapidhash(source bytes) → checksum
  → SELECT from compilation_cache WHERE source_name = name AND source_hash = checksum
    → HIT: return cached compiled Lua, skip fennel.compileString()
    → MISS: compile, INSERT OR REPLACE into compilation_cache
```

The `compilation_cache` table in `schema.sql`:

```sql
CREATE TABLE IF NOT EXISTS
compilation_cache(id INTEGER PRIMARY KEY AUTOINCREMENT,
                  source_name VARCHAR(255) UNIQUE NOT NULL,
                  source_hash INTEGER NOT NULL,
                  compiled BLOB NOT NULL);
```

Staleness is detected by comparing `rapidhash()` of the current source bytes against the stored `source_hash`. If the font file changes (different TTF, updated version), the hash won't match and the SDF atlas is regenerated.

#### SDF cache design

Add a new table for SDF atlas data:

```sql
CREATE TABLE IF NOT EXISTS
sdf_cache(id INTEGER PRIMARY KEY AUTOINCREMENT,
          font_name VARCHAR(255) UNIQUE NOT NULL,
          font_hash INTEGER NOT NULL,
          atlas_width INTEGER NOT NULL,
          atlas_height INTEGER NOT NULL,
          glyph_metrics BLOB NOT NULL,
          atlas_bitmap BLOB NOT NULL);
```

| Column | Contents |
|---|---|
| `font_name` | Asset name of the TTF file (e.g. `"ponderosa.ttf"`) |
| `font_hash` | `rapidhash()` of the TTF file bytes |
| `atlas_width`, `atlas_height` | Atlas dimensions (needed for UV reconstruction) |
| `glyph_metrics` | Serialized array of `SDFGlyph` structs (95 glyphs × ~36 bytes = ~3.4 KB) |
| `atlas_bitmap` | Raw atlas pixel data (256×256 = 64 KB or 512×512 = 256 KB) |

The `atlas_bitmap` is the fully packed atlas ready for direct GPU upload — no SDF computation or rect packing needed on cache hit.

#### Modified `LoadFont` flow

```cpp
void Renderer::LoadFont(const DbAssets::Font& asset) {
  FontInfo font;
  CHECK(stbtt_InitFont(&font.font_info, asset.contents, ...));
  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, kSDFHeight);
  font.pixel_height = kSDFHeight;
  stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                        &font.line_gap);

  const auto font_hash = rapidhash(asset.contents, asset.size);

  // Try loading from cache.
  if (LoadSDFFromCache(asset.name, font_hash, &font)) {
    // Cache hit: atlas_bitmap and glyph_metrics loaded directly.
    // Just upload the atlas to the GPU.
    font.texture = renderer_->LoadFontTexture(
        cached_atlas, font.atlas_width, font.atlas_height);
  } else {
    // Cache miss: generate SDF glyphs, pack atlas, upload to GPU.
    // ... (full SDF generation as described in section 2) ...

    // Persist to cache for next launch.
    SaveSDFToCache(asset.name, font_hash, font);
  }

  fonts_.Push(font);
  font_table_.Insert(asset.name, &fonts_.back());
}
```

#### Cache read (`LoadSDFFromCache`)

```cpp
bool Renderer::LoadSDFFromCache(std::string_view font_name,
                                uint64_t font_hash,
                                FontInfo* font) {
  // SELECT atlas_width, atlas_height, glyph_metrics, atlas_bitmap
  // FROM sdf_cache
  // WHERE font_name = ? AND font_hash = ?
  //
  // If found:
  //   memcpy glyph_metrics → font->glyphs
  //   Upload atlas_bitmap → GPU texture
  //   return true
  // Else:
  //   return false
}
```

#### Cache write (`SaveSDFToCache`)

```cpp
void Renderer::SaveSDFToCache(std::string_view font_name,
                              uint64_t font_hash,
                              const FontInfo& font,
                              const uint8_t* atlas_bitmap) {
  // INSERT OR REPLACE INTO sdf_cache
  //   (font_name, font_hash, atlas_width, atlas_height,
  //    glyph_metrics, atlas_bitmap)
  // VALUES (?, ?, ?, ?, ?, ?)
  //
  // glyph_metrics = raw bytes of font.glyphs[32..126]
  // atlas_bitmap = raw bytes of packed atlas
}
```

#### Cache invalidation

The cache is invalidated automatically when `font_hash` doesn't match — the `WHERE font_hash = ?` clause in the SELECT ensures stale entries are never used. If the SDF parameters change (glyph height, padding, pixel_dist_scale), the cache should also be invalidated. This can be handled by including a version constant in the hash:

```cpp
// Combine font bytes hash with SDF parameter version.
constexpr uint64_t kSDFCacheVersion = 1;  // Bump when SDF params change.
const auto font_hash = rapidhash(asset.contents, asset.size) ^ kSDFCacheVersion;
```

Bumping `kSDFCacheVersion` invalidates all cached SDF atlases, forcing regeneration with the new parameters.

#### Performance impact

On cache hit, `LoadFont` skips the two most expensive operations (SDF generation and rect packing) and reduces to a SQLite read + GPU texture upload. For a 256×256 atlas:

| Operation | Cache miss | Cache hit |
|---|---|---|
| SDF generation | 10-50ms | 0ms |
| Rect packing | ~1ms | 0ms |
| SQLite read | 0ms | ~1ms |
| GPU upload | ~0.5ms | ~0.5ms |
| **Total** | **~12-52ms** | **~1.5ms** |

The cached atlas blob is small (64-256 KB) and adds negligible size to the asset database.

## Performance considerations

### Load time: SDF generation cost

`stbtt_GetCodepointSDF` is more expensive than `stbtt_PackFontRange` because it computes per-pixel distances rather than doing simple rasterization. For 95 ASCII glyphs at 48px + 6px padding:

- Each glyph SDF bitmap is roughly 60x60 pixels = 3,600 pixels
- Each pixel requires finding the minimum distance to all glyph edges
- stb_truetype uses a brute-force O(pixels × edges) approach

Estimated per-font: 10-50ms on modern hardware (vs ~2-5ms for bitmap rasterization). This is a one-time startup cost and is unlikely to be noticeable. The SDF atlas caching strategy described below eliminates this cost on subsequent launches.

### Runtime: draw call overhead

The shader switch (`kSetShader`) before and after text rendering causes the batch renderer to flush its current batch and rebind the shader program. This adds 2 draw calls per "text block" (consecutive text draws).

In practice this is negligible because:
- The engine already uses a command buffer that batches all text quads between shader switches into a single draw call
- Shader rebinding is one `glUseProgram` call (~1-2 microseconds on modern GPUs)
- The real bottleneck in text rendering is vertex submission, not shader state changes

If text and sprites are heavily interleaved (draw sprite, draw text, draw sprite, draw text...), each interleaving adds a shader switch pair. This can be mitigated by sorting draw calls by shader, which the batch renderer could do automatically. However, this is an optimization for later — interleaved text/sprite rendering is uncommon in practice because UI text is usually drawn in a separate pass from game objects.

### Runtime: SDF fragment shader cost

The SDF fragment shader is slightly more expensive than the bitmap shader:

| Operation | Bitmap shader | SDF shader |
|---|---|---|
| Texture sample | 1 | 1 |
| `fwidth` | 0 | 1 |
| `smoothstep` | 0 | 1 (or 2 with outlines) |
| Multiply | 1 | 1-2 |

The extra `fwidth` + `smoothstep` adds ~1-2 ALU operations per fragment. On any GPU from the last decade this is completely negligible. Text quads are small relative to full-screen effects, so the total fragment shader work for all text is a tiny fraction of frame time.

### Memory: atlas size reduction

| | Current (bitmap) | SDF |
|---|---|---|
| Glyph reference size | 100px | 48px |
| Atlas dimensions | 2048x2048 | 256x256 or 512x512 |
| Atlas memory | 4 MB | 64-256 KB |
| Mipmaps | Yes (adds ~33%) | No |
| Total per font | ~5.3 MB | 64-256 KB |

SDF reduces GPU memory per font by **20-80x**. This makes it practical to load many fonts simultaneously.

### Quality vs atlas resolution tradeoff

SDF quality degrades when the rendered size is so large that individual SDF texels become visible as rounded corners on what should be sharp features. With a 48px SDF, this starts happening above ~400-500px rendered size. For game UI text this is irrelevant — text above 200px is rare.

If enormous text is needed (title screens, splash screens), the SDF reference size can be increased to 64 or 96px per glyph. Even at 96px, the atlas fits in 512x512.

For very small text (<10px), SDF can produce slightly softer results than a pixel-snapped bitmap font because the smoothstep anti-aliasing spreads across a larger fraction of the glyph. This is usually acceptable and often looks better than aliased bitmap text. If pixel-perfect small text is critical, a separate bitmap font for the debug/small size can coexist with SDF fonts.

## Features SDF enables

### 1. Resolution-independent text

The primary feature: text looks sharp at any size from a single atlas. No need for multi-size atlases or oversampling. Zooming the camera, scaling the UI, or animating text size all produce crisp edges.

### 2. Outlines

Outlines are rendered by adding a second threshold in the SDF shader. The distance between the outline edge and the fill edge controls outline thickness:

```glsl
float outline_edge = 0.5 - outline_width;
float outline_alpha = smoothstep(outline_edge - smoothing, outline_edge + smoothing, distance);
float fill_alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
vec4 color = mix(outline_color, vec4(fill_color.rgb, 1.0), fill_alpha);
color.a *= outline_alpha;
```

This adds zero extra draw calls — the outline is computed entirely in the fragment shader from the same distance field.

Lua API:

```lua
G.graphics.set_text_outline(width, {r, g, b, a})
G.graphics.draw_text("font.ttf", 32, "Outlined!", x, y)
G.graphics.set_text_outline(0)  -- disable
```

### 3. Drop shadows

A drop shadow is text drawn at a slight offset in a shadow color with softer edges. With SDF, this can be done in a single pass by sampling the SDF at a shifted UV coordinate:

```glsl
// In the SDF shader:
vec2 shadow_offset = vec2(0.002, 0.002);  // in UV space
float shadow_dist = texture(tex, tex_coord - shadow_offset).r;
float shadow_alpha = smoothstep(0.3 - smoothing, 0.3 + smoothing, shadow_dist);
// Composite shadow behind fill
```

Alternatively, for simplicity, draw the text twice — once offset in the shadow color, once at the normal position. This costs 2x draw calls but avoids shader complexity.

### 4. Glow / soft shadow

A glow effect uses a lower threshold in the SDF shader to extend the visible region beyond the glyph edge:

```glsl
float glow_edge = 0.2;  // how far the glow extends (lower = wider glow)
float glow_alpha = smoothstep(glow_edge - smoothing, glow_edge + smoothing, distance);
vec4 glow = vec4(glow_color.rgb, glow_alpha * glow_strength);
```

This creates a soft halo around text, useful for making text readable over busy backgrounds. Again, zero extra draw calls.

### 5. Bold / weight adjustment at runtime

Shifting the SDF threshold changes the apparent weight of the text:

```glsl
float threshold = 0.5 - bold_amount;  // positive bold_amount = thicker
float alpha = smoothstep(threshold - smoothing, threshold + smoothing, distance);
```

`bold_amount = 0.05` gives a subtle bold effect; `0.1` gives heavy bold. This is not true typographic bold (which changes stroke geometry), but it's a good approximation that requires no additional atlas data.

### 6. Animated text effects

Because the edge is reconstructed in the shader, it can be animated per-frame:

- **Pulsing glow**: Animate `glow_strength` with a sine wave
- **Dissolve**: Animate the threshold from 0.5 toward 0.0
- **Typewriter with soft edges**: Animate per-character alpha using distance from a threshold

All of these are uniform changes, requiring no vertex or atlas modifications.

## What about MSDF?

Multi-channel SDF (MSDF) uses 3 or 4 channels (RGB or RGBA) to encode distance information. Each channel stores the distance to a different edge segment of the glyph. The fragment shader reconstructs sharp corners by taking the median of the three channels:

```glsl
float distance = median(texture(tex, tex_coord).rgb);
```

MSDF solves the main limitation of single-channel SDF: rounded corners. Standard SDF cannot represent sharp corners because the distance field is smooth everywhere — two edges meeting at a point produce a rounded intersection in the distance field. MSDF fixes this by encoding directional distance information.

### Should we use MSDF?

**Not initially.** MSDF adds complexity:

- Requires an external library (msdfgen or msdf-atlas-gen) for atlas generation — stb_truetype only supports single-channel SDF
- Atlas uses 3-4x more memory per texel (RGB/RGBA instead of single channel)
- Fragment shader is more complex (median of 3 channels)
- Most game fonts at typical UI sizes (12-48px) don't show visible corner rounding with standard SDF

Single-channel SDF is the right starting point. If corner quality becomes a visible problem (usually only with large geometric/display fonts at very large sizes), MSDF can be added later as an upgrade path. The atlas generation and shader are different, but the rest of the pipeline (glyph metrics, batch rendering, shader switching) is identical.

## Reasons not to implement SDF

### The current system might already be good enough

The existing bitmap atlas rasterizes at 100px with 2x2 oversampling. For a game that only renders text at sizes between ~16px and ~80px, this produces perfectly acceptable results — downscaling a 100px reference is clean, and the oversampling smooths edges. The text is not blurry at these sizes. SDF's sharpness advantage only becomes visible when rendering above the reference height or when zooming the camera into text.

If the game's text needs are fixed-size UI labels and debug output (which is what the engine is doing today), the current pipeline is simpler and already works. The strongest argument against SDF is: **don't add complexity to solve a problem that doesn't exist yet.**

### Pixel-perfect small text

SDF fundamentally cannot produce pixel-snapped, hinted text. FreeType with hinting enabled aligns glyph stems and baselines to exact pixel boundaries, which makes small text (8-14px) noticeably crisper on low-DPI screens. stb_truetype's bitmap rasterizer with oversampling also does a reasonable job here.

SDF reconstructs edges via smoothstep, which produces sub-pixel anti-aliasing that can look "soft" or "swimmy" at small sizes — the edge doesn't land on a clean pixel boundary. For a debug font at 24px or a terminal-style monospace font, this softness can be distracting compared to a well-hinted bitmap.

This matters most for:
- The debug font (`graphics.print` at 24px) where crispness aids readability
- Monospace fonts (Terminus) used for code or console output
- Any UI font rendered at a fixed, known size that never changes

If most text in the game is rendered at one or two known sizes, a bitmap atlas at those exact sizes will look sharper than SDF.

### Shader switching cost in mixed rendering

The current pipeline uses a single fragment shader (`pre_pass.frag`) for everything — sprites, shapes, and text all flow through the same batch without state changes. SDF requires bracketing every text draw with `kSetShader("sdf")` / `kSetShader("pre_pass")`, which forces a batch flush and `glUseProgram` call.

For a frame with 3-4 blocks of text drawn together, this adds ~4-8 shader switches (negligible). But if text and sprites are heavily interleaved — drawing a health bar with an icon, then a number, then another icon, then another number — each transition between text and non-text flushes the batch. In the worst case, this fragments what would be a single draw call into many small ones.

The current pipeline has zero shader switches for text. This is a genuine architectural advantage of treating text as "just another textured quad."

### Added complexity in the rendering pipeline

SDF introduces a second shader program, a second texture upload path (no mipmaps, different filtering), a new glyph data structure, a new SQLite table, a cache invalidation scheme, and new Lua API surface for outline/glow uniforms. Each of these is individually simple, but collectively they roughly double the font system's code surface area.

The current font system is ~200 lines of straightforward code: pack glyphs, upload atlas, sample texture. It is easy to understand, debug, and modify. SDF makes the font system the most complex subsystem in the renderer, which is disproportionate for something that displays ASCII text.

### stb_truetype's SDF quality limitations

`stbtt_GetCodepointSDF` uses a brute-force algorithm that computes exact distances. This is correct but slow — it's O(pixels x edges) per glyph. More importantly, stb_truetype's SDF does not handle overlapping contours well. Some fonts (especially decorative or hand-drawn fonts) have self-intersecting outlines that produce artifacts in the distance field: interior regions get negative distance values where contours overlap, causing holes or thin spots in the rendered glyph.

FreeType's SDF renderer (used by Love2D, Godot) handles overlapping contours correctly. If this becomes a problem, the fix is switching to FreeType for SDF generation, which adds a significant new dependency (FreeType is ~500KB of code vs stb_truetype's single header).

### Corner rounding

Single-channel SDF cannot represent sharp corners. Two edges meeting at a 90-degree angle produce a smooth, rounded intersection in the distance field. This is visible at large sizes on geometric fonts (e.g., a capital "L" or "E" will have slightly rounded inner corners). At typical UI sizes (12-48px) the rounding is sub-pixel and invisible, but at display sizes (100px+) it becomes noticeable.

MSDF solves this but requires an external library (msdfgen) and a more complex shader. Staying with bitmap rendering avoids the problem entirely since rasterized glyphs reproduce the font's designed corners exactly.

## Alternatives to SDF

### Alternative 1: Multi-size bitmap atlases

Pack glyphs at 2-3 reference sizes (e.g., 16px, 48px, 128px) into the same atlas using `stbtt_PackFontRanges`. At render time, select the closest reference size and scale from there. This limits the scaling ratio to ~3x in either direction, which keeps bitmap quality high.

Advantages over SDF:
- No shader changes — same `texture * color` fragment shader
- No batch-breaking shader switches
- Pixel-snapped at reference sizes
- Simpler implementation (stb_truetype already supports `PackFontRanges`)

Disadvantages:
- 3x atlas memory compared to single-size (still less than the current 2048x2048)
- No outline/glow effects
- Quality still degrades at extreme scales
- Need to select the right reference size at render time

This is a good middle ground if the goal is just "better quality at more sizes" without the full SDF pipeline.

### Alternative 2: Keep the current (already fixed) system

All 8 documented bugs have already been fixed (`920bba1`..`3fbfe3f`):
- Kerning is correctly scaled by pixel_scale
- TextDimensions uses the same scaling approach as DrawText
- Atlas uses stb_rect_pack and is sized to 1024x1024
- `GL_CLAMP_TO_EDGE` is set on font atlas textures
- ANSI parser is bounds-checked, tabs measure correctly, dangling pack context removed

The remaining limitation is blurry text above the 100px reference height. If the game doesn't render text that large, the current system is already in good shape and no further work is needed.

### Alternative 3: Higher reference size with mipmaps

Increase the bitmap reference height from 100px to 200px or 256px. This pushes the "blurry upscaling" threshold much higher, and downscaling with trilinear filtering (which the engine already uses) produces clean results at small sizes. A 256px reference for 95 ASCII glyphs fits in a 2048x2048 atlas, same as today.

Advantages:
- Zero code changes to the rendering pipeline
- Sharp text up to ~200-256px
- Mipmaps handle downscaling well

Disadvantages:
- Larger atlas (but same as the current oversized one)
- No outline/glow
- Still blurry above reference size
- No zoom-independence

### Recommendation

If the game needs outlines, glow, or camera-zoom-independent text — implement SDF. These effects are genuinely hard to do without distance fields, and SDF is the standard solution.

If the game only needs clean text at known UI sizes — fix the bugs, right-size the atlas, and move on. The current bitmap pipeline is simpler and the text quality is already fine for fixed-size rendering. SDF can be added later if requirements change.

## Files to modify

| File | Changes |
|---|---|
| `src/renderer.h` | Replace `stbtt_packedchar chars[256]` with `SDFGlyph glyphs[128]` in `FontInfo`. Add `atlas_width`, `atlas_height` fields. |
| `src/renderer.cc` | Rewrite `LoadFont` to use `stbtt_GetCodepointSDF` + `stb_rect_pack` with SQLite cache read/write. Update `DrawText` to use `SDFGlyph` metrics and bracket with shader switches. Update `TextDimensions` to use `SDFGlyph::advance`. |
| `src/shaders.cc` | Add `kSDFFragmentShader` string. Compile and link the `"sdf"` program at startup. |
| `src/shaders.h` | No changes needed (shader system is generic). |
| `src/schema.sql` | Add `sdf_cache` table. |
| `src/lua_graphics.cc` | Add Lua bindings for outline parameters (`set_text_outline`). |

## Implementation order

1. **Add SDF shader** — compile `sdf.frag` in `Shaders::Shaders()`, verify it links
2. **Add `SDFGlyph` struct** — define in `renderer.h`
3. **Add `sdf_cache` table** — add schema to `schema.sql`
4. **Rewrite `LoadFont`** — SDF generation + rect packing + atlas upload, with cache read/write
5. **Update `LoadFontTexture`** — `GL_CLAMP_TO_EDGE`, `GL_LINEAR`, no mipmaps
6. **Update `DrawText`** — use `SDFGlyph` metrics, add shader switches
7. **Update `TextDimensions`** — use `SDFGlyph::advance`
8. **Test at multiple sizes** — verify quality from 8px to 200px+
9. **Add outline support** — uniforms + Lua API

## Resources

### Foundational paper

- **"Improved Alpha-Tested Magnification for Vector Textures and Special Effects"** — Chris Green, Valve (SIGGRAPH 2007). The paper that introduced SDF rendering for real-time graphics. Describes the core technique of encoding distance fields in textures and reconstructing edges with alpha testing. Demonstrates the approach on Team Fortress 2's in-game text and decals. https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf

### Technique explanations and tutorials

- **"Signed Distance Fields"** — Lode Vandevenne. Thorough walkthrough of how distance fields are computed, how the smoothstep threshold works, and how to implement outline/glow/shadow effects. Good diagrams showing the relationship between distance values and rendered edges. https://lodev.org/sdf/
- **"Drawing Text with Signed Distance Fields in Mapbox GL"** — Mapbox engineering blog. Practical write-up of shipping SDF text rendering in production, covering atlas generation, shader implementation, halo/outline effects, and the tradeoffs they encountered at scale. https://blog.mapbox.com/drawing-text-with-signed-distance-fields-in-mapbox-gl-b0933af6f817
- **"GPU Gems 3, Chapter 25: Rendering Vector Art on the GPU"** — Charles Loop, Jim Blinn. Covers distance field rendering of vector shapes more broadly, including the math behind why bilinear interpolation of distance values preserves edge quality. https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-25-rendering-vector-art-gpu

### Multi-channel SDF (MSDF)

- **"Shape Decomposition for Multi-channel Distance Fields"** — Viktor Chlumsky (Master's thesis, 2015). The original MSDF paper. Explains why single-channel SDF rounds corners, how decomposing the shape into three edge-color channels solves this, and the median-of-three reconstruction in the fragment shader. https://github.com/Chlumsky/msdfgen/files/3050967/thesis.pdf
- **msdfgen** — Reference implementation of MSDF atlas generation. C++ library and CLI tool. https://github.com/Chlumsky/msdfgen
- **msdf-atlas-gen** — Builds on msdfgen to generate complete font atlases with JSON/CSV metadata. Integrates with FreeType for glyph loading. https://github.com/Chlumsky/msdf-atlas-gen

### stb_truetype SDF documentation

- **stb_truetype.h header comments** (lines 1058-1086 in `libraries/stb_truetype.h`). Documents `stbtt_GetCodepointSDF` / `stbtt_GetGlyphSDF` parameters: `scale`, `padding`, `onedge_value`, `pixel_dist_scale`. The implementation starts at line 4935.
- **stb libraries repository** — Sean Barrett. https://github.com/nothings/stb

### SDF in game engines

- **Godot Engine SDF font documentation** — Describes Godot's MSDF font rendering pipeline, including how they generate MSDF atlases at import time and their shader implementation. Useful as a reference for what a mature SDF font system looks like. https://docs.godotengine.org/en/stable/tutorials/ui/gui_using_fonts.html
- **Bevy Engine text rendering** — Bevy uses `cosmic-text` for shaping and SDF for rendering. Their approach to batching SDF text draws alongside sprite draws is relevant to the shader-switching strategy described in this doc. https://bevyengine.org/
