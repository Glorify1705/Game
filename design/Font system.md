# Font System

This document describes how the engine loads, rasterizes, and renders text, identifies bugs and limitations in the current implementation, compares the approach against Love2D and other 2D engines, and proposes improvements.

## Current architecture

```
TTF file (SQLite DB)
  → DbAssets::LoadFont()         [assets.cc]
    → Renderer::LoadFont()       [renderer.cc]
      → stbtt_InitFont()
      → stbtt_PackFontRange()    ASCII 32–126, 100px reference height, 2×2 oversampling
      → BatchRenderer::LoadFontTexture()   upload 3000×3000 R8 atlas to GPU
        → font_table_.Insert()   store FontInfo for lookup by name
```

At draw time:
```
Lua: graphics.draw_text("myfont.ttf", 24, "Hello", x, y)
  → Renderer::DrawText()
    → For each character:
        stbtt_GetPackedQuad() → screen quad coords + atlas UVs
        pixel_scale = requested_size / 100.0
        BatchRenderer::PushQuad(scaled position, atlas UVs)
        advance + kerning
```

Text is rendered as textured quads through the same batch renderer pipeline used for sprites and shapes. No special shader; the standard pre-pass fragment shader samples the font atlas texture and multiplies by vertex color.

### Key data structures

```cpp
// renderer.h:283–291
struct FontInfo {
  GLuint texture;              // GPU handle for glyph atlas
  float scale;                 // stbtt scale for reference height
  float pixel_height;          // reference height (always 100)
  int ascent, descent, line_gap;
  stbtt_fontinfo font_info;    // stb_truetype parse state
  stbtt_pack_context context;  // packing context (unused after load)
  stbtt_packedchar chars[256]; // packed glyph data (only 32–126 used)
};
```

Storage: `FixedArray<FontInfo> fonts_` (max 512) with `Dictionary<FontInfo*> font_table_` for O(1) name lookup.

### Atlas details

| Property | Value |
|---|---|
| Dimensions | 3000 × 3000 (9 MB) |
| Format | `GL_RED` uploaded as `GL_RGBA` via swizzle (R→G, R→B, R→A) |
| Filtering | Trilinear (`GL_LINEAR_MIPMAP_LINEAR` min, `GL_LINEAR` mag) |
| Oversampling | 2×2 |
| Character range | ASCII 32–126 (95 glyphs) |
| Reference height | 100 px |

Each font gets its own 9 MB atlas. The atlas is allocated from a scratch `ArenaAllocator` that is freed after GPU upload.

### Font sources

- **Asset fonts**: TTF files stored in the SQLite asset database, loaded by `DbAssets::LoadFont()`.
- **Debug font**: Proggy Clean TTF embedded as a C byte array in `debug_font.h`, inserted into the database by the packer if not already present (`packer.cc:438`). Used by `graphics.print()` at fixed 24px size.

### Lua API

| Function | Signature | Description |
|---|---|---|
| `graphics.draw_text` | `(font, size, text, x, y)` | Render text at position |
| `graphics.text_dimensions` | `(font, size, text) → w, h` | Measure text bounding box |
| `graphics.print` | `(text, x, y)` | Shortcut: debug_font.ttf at 24px |

`draw_text` accepts both Lua strings and `byte_buffer` userdata. `text_dimensions` only accepts strings.

### Special character handling

- `\n` — newline: reset x, advance y by `pixel_scale * scale * (ascent - descent + line_gap)`
- `\t` — tab: render 4 spaces
- `\033` — ANSI escape: parse color code, change draw color until reset

## Bugs and issues

### Bug 1: ANSI escape parser can read out of bounds

`renderer.cc:833–834`:
```cpp
size_t st = i + 1, en = i;
while (str[en] != 'm') en++;
```

If the string contains `\033` but no terminating `m`, this loop reads past the end of the `string_view`. There is no bounds check against `str.size()`.

### Bug 2: Kerning not scaled by pixel_scale in DrawText

`renderer.cc:824–827`:
```cpp
p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                     str[i], str[i + 1]);
```

The kerning advance is scaled by `info->scale` (the scale for the 100px reference height) but not by `pixel_scale` (the ratio of requested size to 100px). This means kerning is applied at the 100px reference magnitude regardless of the actual rendered size. For a 24px request, kerning would be ~4× too large. For a 200px request, kerning would be ~0.5× too small.

Compare with `TextDimensions` (line 880) which uses `stbtt_ScaleForPixelHeight(&info->font_info, size)` — a completely different scale — making the kerning inconsistent between measurement and rendering.

### Bug 3: DrawText and TextDimensions use different scaling strategies

`DrawText` computes text advance using `stbtt_GetPackedQuad` (which returns positions relative to the atlas reference height) and then scales by `pixel_scale = size / 100.0`.

`TextDimensions` computes advance using `stbtt_GetCodepointHMetrics` with a fresh `stbtt_ScaleForPixelHeight(font_info, size)`. These can produce different widths for the same string because:
1. `GetPackedQuad` returns positions that include oversampling adjustments.
2. The scale factors are computed differently.

This means `text_dimensions()` may not match the actual rendered width.

### Bug 4: Tab rendering passes `str.size()` as index to handle_char

`renderer.cc:841`:
```cpp
for (int j = 0; j < 4; ++j) handle_char(str.size(), ' ');
```

`handle_char` uses the index parameter for kerning lookahead: `if ((i + 1) < str.size())`. Passing `str.size()` makes the comparison `str.size() + 1 < str.size()` which is always false, so tabs never kern with the next character. This is probably fine in practice but is semantically wrong — the correct index would be `i`.

### Bug 5: stbtt_pack_context stored after PackEnd

`FontInfo` stores `stbtt_pack_context context` which is only used during `LoadFont`. After `stbtt_PackEnd()`, the context references freed scratch memory. Keeping it in the struct wastes space and is a dangling-pointer hazard if anyone accidentally uses it later.

### Issue: text_dimensions does not handle tab or ANSI escapes

`TextDimensions` has no handling for `\t` or `\033`. A string with tabs will measure incorrectly (tabs measured as glyph width of character 9 rather than 4 spaces). ANSI escapes will be measured as visible characters.

### Issue: 9 MB atlas per font is wasteful for ASCII-only

Only 95 glyphs are packed into a 3000×3000 atlas. At 100px reference height with 2×2 oversampling, each glyph is roughly 50×200 effective pixels. 95 glyphs need ~950,000 pixels of actual glyph data in a 9,000,000 pixel atlas — about 10% utilization.

### Issue: GL_REPEAT wrap mode on font atlas

`renderer.cc:284`: The font atlas uses `GL_REPEAT` wrap mode. `GL_CLAMP_TO_EDGE` would be more correct for an atlas to avoid bleeding artifacts at texture coordinate boundaries.

## Comparison with other engines

### Love2D

Love2D uses FreeType for font rasterization and creates texture atlases on demand. Key differences:

| Aspect          | This Engine                                | Love2D                                                 |
| --------------- | ------------------------------------------ | ------------------------------------------------------ |
| Font library    | stb_truetype                               | FreeType 2                                             |
| Atlas sizing    | Fixed 3000×3000                            | Dynamic, grows as needed                               |
| Glyph loading   | All ASCII at load                          | On-demand per glyph                                    |
| Hinting         | None (stb_truetype has basic auto-hinting) | FreeType hinting (auto, light, mono, none)             |
| SDF support     | No                                         | No (but can be added via shaders)                      |
| Character range | ASCII 32–126 only                          | Full Unicode                                           |
| Font objects    | Looked up by filename string               | First-class `Font` objects with methods                |
| Text alignment  | Manual                                     | Built-in `printf` with align/wrap                      |
| Colored text    | ANSI escape codes                          | Inline color table `{color, "text", color, "text"}`    |
| Rich text       | No                                         | `Text` objects with formatted sections                 |
| DPI scaling     | No                                         | Automatic DPI scaling with `love.window.getDPIScale()` |

Love2D's `Font:getWidth()`, `Font:getHeight()`, `Font:getWrap()` provide robust text measurement. The engine's `text_dimensions` is the only measurement function and has the bugs noted above.

Love2D also supports `Text` objects — pre-rendered text that can be drawn repeatedly without re-computing layout. This is useful for static UI text.

### Raylib

Raylib also uses stb_truetype but with a different approach:

| Aspect | This Engine | Raylib |
|---|---|---|
| Default font | Proggy Clean TTF | Built-in bitmap font (no TTF needed) |
| Atlas sizing | Fixed 3000×3000 | Tight-fit around loaded glyphs |
| API | `draw_text(font, size, text, x, y)` | `DrawTextEx(font, text, pos, fontSize, spacing, color)` |
| Spacing control | None (uses kerning only) | Explicit character spacing parameter |
| Multi-size | Re-scales from 100px reference | Separate atlas per size |

Raylib generates a separate atlas for each font size, which gives pixel-perfect rendering at each size but uses more GPU memory when many sizes are needed.

### Godot

| Aspect | This Engine | Godot |
|---|---|---|
| Font library | stb_truetype | FreeType + MSDF generator |
| SDF support | No | Multi-channel SDF (MSDF) by default |
| Text rendering | Character-by-character quads | TextServer abstraction with BiDi, shaping |
| Anti-aliasing | Oversampling + mipmaps | MSDF (resolution-independent) |
| Outline/shadow | No | Built-in outline, shadow, glow |

Godot's MSDF approach renders text that looks sharp at any scale without quality loss. This is the state of the art for 2D game text rendering.

### SDL_ttf

SDL_ttf wraps FreeType and renders text to SDL surfaces. It is simple but not GPU-accelerated — each `TTF_RenderText` call produces a CPU bitmap that must be uploaded to a texture. Not suitable for real-time text that changes every frame.

## Proposed improvements

### 1. Signed Distance Field (SDF) font rendering

**Problem**: The current approach rasterizes glyphs at 100px and scales the texture at runtime. Downscaling is acceptable but upscaling above ~100px produces blurry text. Even at the reference size, the oversampled bitmaps are softer than they need to be.

**Proposal**: Generate SDF (or multi-channel SDF) atlas at load time using stb_truetype's `stbtt_GetCodepointSDF()` function, which is already available in the library. Render with a dedicated SDF fragment shader:

```glsl
// SDF fragment shader
float distance = texture(tex, tex_coord).r;
float smoothing = fwidth(distance);
float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
frag_color = vec4(out_color.rgb, out_color.a * alpha);
```

Benefits:
- Sharp text at any scale with a single atlas
- Much smaller atlas (SDF glyphs can be 32–64px and still look perfect at 200px+)
- Enables outline, glow, and drop shadow effects cheaply in the shader
- Reduces atlas memory from 9 MB to under 500 KB

This requires adding a `kSetShader` command before text draw calls to switch to the SDF shader, and switching back after. The batch renderer already supports `kSetShader`.

### 2. Right-size the atlas

**Problem**: 3000×3000 = 9 MB per font for 95 ASCII glyphs at 100px is extremely wasteful. Most of the atlas is empty.

**Proposal**: Calculate the required atlas size based on the number and size of glyphs being packed. A 512×512 atlas is more than sufficient for 95 ASCII glyphs at 100px with 2×2 oversampling. If SDF is adopted, a 256×256 atlas would suffice.

Alternatively, use `stb_rect_pack` (already a dependency) to determine the minimum atlas size with a binary search.

### 3. Text alignment and wrapping

**Problem**: There is no built-in way to center, right-align, or wrap text. Game developers must manually call `text_dimensions` and compute positions.

**Proposal**: Add alignment and max-width parameters to `draw_text`:

```lua
graphics.draw_text("font.ttf", 24, "Hello world", x, y, {
  align = "center",  -- "left" | "center" | "right"
  max_width = 400,   -- wrap at this width (nil = no wrap)
})
```

Implementation: perform a layout pass (word-wrap + alignment) before emitting quads. The layout pass reuses the same metrics code as `TextDimensions`.

### 4. Text outlines and shadows

**Problem**: No way to render outlined or shadowed text, which is essential for readable text over varying backgrounds.

**Proposal** (with SDF): Modify the SDF shader to support outline and shadow:

```glsl
uniform float outline_width;
uniform vec4 outline_color;

float distance = texture(tex, tex_coord).r;
float smoothing = fwidth(distance);

// Outline
float outline_edge = 0.5 - outline_width;
float outline_alpha = smoothstep(outline_edge - smoothing, outline_edge + smoothing, distance);
float fill_alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);

vec4 color = mix(outline_color, vec4(out_color.rgb, 1.0), fill_alpha);
color.a *= outline_alpha * out_color.a;
frag_color = color;
```

**Proposal** (without SDF): Render the text string multiple times at slight offsets in the outline/shadow color before rendering the main text. Simple but multiplies draw calls.

### 5. Pre-computed text objects

**Problem**: Static UI text (score displays, menu labels, etc.) is re-laid-out every frame even though the content rarely changes.

**Proposal**: Add a `Text` object that caches the vertex data:

```lua
local label = graphics.new_text("font.ttf", 24, "Score: 0")
-- Later, in draw:
graphics.draw_cached_text(label, x, y)
-- Update when needed:
label:set("Score: " .. score)
```

The `Text` object stores pre-computed quad vertices. Only re-computes when the string changes. Drawing a `Text` object is a single batch of quads with no per-character layout work.

### 6. Formatted / colored text without ANSI escapes

**Problem**: The current ANSI escape code approach is fragile (see Bug 1), hard to use from Lua, and non-standard for a game engine.

**Proposal**: Accept a table of `{color, text}` pairs:

```lua
graphics.draw_text("font.ttf", 24, {
  {1, 0, 0, 1}, "Red text ",
  {0, 1, 0, 1}, "Green text"
}, x, y)
```

This is the same approach Love2D uses (`love.graphics.print({color, text, ...})`) and is more natural for Lua code. The ANSI escape parsing can be removed.

### 7. Font size caching

**Problem**: If a game uses the same font at many different sizes, there is no issue with the current approach (one atlas scales to all sizes). But with SDF, the atlas is even more scale-independent. Without SDF, very small sizes (8–12px) look poor because the 100px reference glyphs are downscaled 8–12×.

**Proposal**: For non-SDF rendering, allow packing multiple size ranges into the same atlas (stb_truetype supports `stbtt_PackFontRanges` with multiple ranges). Pack a small size (e.g. 16px) and a large size (e.g. 64px) and select the closer reference at render time.

With SDF this is unnecessary — SDF handles all sizes well from a single atlas.

### 8. Character spacing parameter

**Problem**: No way to adjust letter spacing beyond the font's built-in kerning. Games often want tighter or looser tracking for stylistic reasons.

**Proposal**: Add an optional spacing parameter:

```lua
graphics.draw_text("font.ttf", 24, "TITLE", x, y, { spacing = 5 })
```

Implementation: add `spacing` to the advance after each character in `DrawText`.

## Summary

The font system is functional and well-integrated with the batch renderer, but has several correctness bugs (ANSI OOB, kerning scaling, DrawText/TextDimensions inconsistency) and is missing features expected of a modern 2D engine (SDF, alignment, outlines, pre-computed text). The most impactful improvement would be adopting SDF rendering, which solves the quality-at-all-sizes problem while also enabling outline and glow effects with minimal additional code.
