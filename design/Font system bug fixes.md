# Font System Bug Fixes

This document explains 8 bugs/issues in the font rendering code, why each is wrong, and how to fix it. No code changes yet — this is a reference for when we implement the fixes.

## Background

### Font atlases

A **font atlas** (or glyph atlas) is a single texture containing pre-rasterized images of every glyph the font needs to display. Instead of rasterizing characters on the fly each frame, we rasterize them once at load time into a big bitmap, upload that bitmap to the GPU as a texture, and then render each character by drawing a textured quad that samples the right rectangle from the atlas.

`stbtt_PackFontRange()` rasterizes glyphs into the atlas bitmap. For each glyph it records a `stbtt_packedchar` struct containing the glyph's position in the atlas (UV coordinates) and its screen-space offset/advance metrics. Later, `stbtt_GetPackedQuad()` reads that struct to produce the quad coordinates and UVs for a given character.

### Reference height and runtime scaling

Our engine rasterizes all glyphs at a **reference height** of 100 pixels (`pixel_height = 100` in `LoadFont`). At draw time, if the user requests size 24, we compute `pixel_scale = 24 / 100.0 = 0.24` and multiply all quad positions by that factor. This means one atlas serves all sizes — small sizes downsample (fine), large sizes upsample (blurry above ~100px).

The `info->scale` field stores `stbtt_ScaleForPixelHeight(&font_info, 100)`, which converts from the font's internal coordinate system (design units, typically ~2048 units per em) to 100px. To convert from design units to the *requested* size, you need `pixel_scale * info->scale`.

### Kerning

**Kerning** is the per-pair adjustment of horizontal spacing between two adjacent characters. For example, "AV" typically has negative kerning — the V tucks under the A's overhang. `stbtt_GetCodepointKernAdvance()` returns the kerning value in **design units** (the font's internal coordinate space). You must multiply by a scale factor to convert to pixels at your target size.

### Oversampling

**Oversampling** rasterizes each glyph at a higher resolution than the atlas cell, then downsamples. With 2x2 oversampling, a glyph occupying a 50x100 region in the atlas is internally rasterized at 100x200 and averaged down. This acts as anti-aliasing, smoothing edges without requiring a larger atlas. The tradeoff is that packing takes longer and glyphs occupy slightly more atlas space due to padding.

### stbtt_GetPackedQuad vs stbtt_GetCodepointHMetrics

These are two different ways to get glyph metrics:

- **`stbtt_GetPackedQuad(chars, atlas_w, atlas_h, char_index, &x, &y, &quad, align)`**: Reads from the pre-computed `stbtt_packedchar` array. Returns screen-space quad corners (`quad.x0/y0/x1/y1`) and atlas UV coordinates (`quad.s0/t0/s1/t1`) for rendering. The `x` output parameter is the advance width — how far to move the cursor for the next character. All values are in the atlas's reference-height pixel space (100px in our case). These values already account for oversampling adjustments.

- **`stbtt_GetCodepointHMetrics(font_info, codepoint, &advanceWidth, &leftSideBearing)`**: Reads from the raw font tables. Returns the advance width and left side bearing in **design units** (the font's internal coordinate system). You must multiply by a scale factor (from `stbtt_ScaleForPixelHeight`) to convert to pixels. These values do NOT account for oversampling.

The key difference: `GetPackedQuad` gives positions ready for rendering at the reference height, while `GetCodepointHMetrics` gives raw font metrics that need manual scaling. Using both in the same codebase for the same purpose leads to inconsistencies.

### GL texture wrapping modes

When a texture coordinate falls outside the [0, 1] range, the **wrap mode** controls what the GPU samples:

- **`GL_REPEAT`**: Wraps around (coordinate 1.1 samples the same as 0.1). Useful for tiling textures.
- **`GL_CLAMP_TO_EDGE`**: Clamps to the nearest edge texel. Coordinate 1.1 samples the same as 1.0.

For a font atlas, `GL_REPEAT` is wrong because glyphs near the atlas edges could bleed into glyphs on the opposite edge. `GL_CLAMP_TO_EDGE` prevents this. In practice the bug may not be visible if no glyph quads have UVs near 0 or 1, but it's incorrect and can cause subtle artifacts with mipmapping or floating-point imprecision.

---

## Bug 1: ANSI escape parser reads out of bounds

### What

`renderer.cc:833–834` — The ANSI escape sequence parser in `DrawText` has no bounds check:

```cpp
size_t st = i + 1, en = i;
while (str[en] != 'm') en++;
```

### Why it's wrong

If a string contains `\033` (ESC) but no terminating `m` character, the loop increments `en` past `str.size()` and reads garbage memory. Since `str` is a `string_view`, there's no null terminator guarantee — this is undefined behavior.

**Example**: The string `"\033[31"` (ESC + color code, but missing the `m`) will read past the end looking for `m`, accessing arbitrary memory until it either finds an `m` byte or crashes.

### Current code

```cpp
if (c == '\033') {
  // Skip ANSI escape sequence.
  size_t st = i + 1, en = i;
  while (str[en] != 'm') en++;
  renderer_->SetActiveColor(ParseColor(str.substr(st, en - st)));
  i = en + 1;
  continue;
}
```

### Fix

Add a bounds check to the while loop:

```diff
 if (c == '\033') {
   // Skip ANSI escape sequence.
-  size_t st = i + 1, en = i;
-  while (str[en] != 'm') en++;
+  size_t st = i + 1, en = i;
+  while (en < str.size() && str[en] != 'm') en++;
+  if (en >= str.size()) break;
   renderer_->SetActiveColor(ParseColor(str.substr(st, en - st)));
   i = en + 1;
   continue;
 }
```

### Tradeoffs

None. Pure safety fix. A malformed escape sequence is silently dropped, which matches the behavior of most terminal emulators.

---

## Bug 2: Kerning not scaled by pixel_scale

### What

`renderer.cc:825` — The kerning adjustment in `DrawText` uses `info->scale` but not `pixel_scale`:

```cpp
p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                     str[i], str[i + 1]);
```

### Why it's wrong

`stbtt_GetCodepointKernAdvance` returns kerning in **design units**. To convert to screen pixels at the requested size, you need to multiply by both:
- `info->scale` — converts design units → 100px reference pixels
- `pixel_scale` — converts 100px reference → requested size

Currently only `info->scale` is applied, so the kerning is always at 100px magnitude regardless of the requested size.

**Example**: Suppose the font has a kerning value of -128 design units for "AV", and `info->scale = 0.049` (which maps 2048 design units → 100px).

- **Current** (requesting size 24): `p.x += 0.049 * (-128) = -6.3px`. The kern is 6.3px even though we're rendering at 24px.
- **Correct** (requesting size 24, `pixel_scale = 0.24`): `p.x += 0.24 * 0.049 * (-128) = -1.5px`. The kern is correctly proportional to the rendered size.

At 24px, kerning is ~4x too large. At 200px, kerning would be ~0.5x too small.

### Current code

```cpp
if ((i + 1) < str.size()) {
  p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                       str[i], str[i + 1]);
}
```

### Fix

Multiply by `pixel_scale`:

```diff
 if ((i + 1) < str.size()) {
-  p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
-                                                       str[i], str[i + 1]);
+  p.x += pixel_scale * info->scale *
+         stbtt_GetCodepointKernAdvance(&info->font_info, str[i], str[i + 1]);
 }
```

### Tradeoffs

None. This is a straightforward correctness fix. Many fonts have zero kerning data, so the bug may not be visible with all fonts, but it's wrong when kerning is present.

---

## Bug 3: TextDimensions doesn't handle \t or \033

### What

`renderer.cc:869–883` — `TextDimensions` only handles `\n` and regular characters. It has no special handling for tabs (`\t`) or ANSI escapes (`\033`).

### Why it's wrong

- **Tabs**: A `\t` character (codepoint 9) falls through to the `else` branch where `stbtt_GetCodepointHMetrics` is called with codepoint 9. Most fonts either have no glyph for codepoint 9 (returning 0 width) or have a placeholder glyph. Meanwhile, `DrawText` renders tabs as 4 spaces. So `TextDimensions` gives the wrong width for any string containing tabs.

- **ANSI escapes**: The entire escape sequence (e.g., `\033[31m`) is measured character-by-character as if each byte were a visible glyph. `DrawText` skips these sequences entirely. So `TextDimensions` overcounts the width by including the escape sequence widths.

**Example**: For the string `"\033[31mHi"`:
- `DrawText` renders just "Hi" (in red).
- `TextDimensions` measures `ESC`, `[`, `3`, `1`, `m`, `H`, `i` — 7 characters instead of 2.

### Current code

```cpp
for (size_t i = 0; i < str.size(); ++i) {
  const char c = str[i];
  if (c == '\n') {
    x = std::max(x, p.x);
    p.x = 0;
    p.y += scale * (info->ascent - info->descent + info->line_gap);
  } else {
    int width, bearing;
    stbtt_GetCodepointHMetrics(&info->font_info, c, &width, &bearing);
    p.x += scale * width;
    if ((i + 1) < str.size()) {
      p.x += scale * stbtt_GetCodepointKernAdvance(&info->font_info, str[i],
                                                     str[i + 1]);
    }
  }
}
```

### Fix

Add tab and ANSI-skip logic matching `DrawText`:

```diff
 for (size_t i = 0; i < str.size(); ++i) {
   const char c = str[i];
-  if (c == '\n') {
+  if (c == '\033') {
+    // Skip ANSI escape sequence, matching DrawText behavior.
+    while (i < str.size() && str[i] != 'm') i++;
+    continue;
+  }
+  if (c == '\t') {
+    // 4 spaces, matching DrawText behavior.
+    int width, bearing;
+    stbtt_GetCodepointHMetrics(&info->font_info, ' ', &width, &bearing);
+    p.x += scale * width * 4;
+    continue;
+  }
+  if (c == '\n') {
     x = std::max(x, p.x);
     p.x = 0;
     p.y += scale * (info->ascent - info->descent + info->line_gap);
```

### Tradeoffs

None. The tab width is hardcoded to 4 spaces in both places, matching `DrawText`.

---

## Bug 4: Tab passes str.size() as index to handle_char

### What

`renderer.cc:841` — When rendering tab characters, `DrawText` calls `handle_char(str.size(), ' ')`, passing `str.size()` as the character index:

```cpp
for (int j = 0; j < 4; ++j) handle_char(str.size(), ' ');
```

### Why it's wrong

Inside `handle_char`, the index is used for kerning lookahead:

```cpp
auto handle_char = [&](size_t i, char c) {
  // ... quad rendering ...
  p.x += x * pixel_scale;
  if ((i + 1) < str.size()) {
    p.x += info->scale * stbtt_GetCodepointKernAdvance(&info->font_info,
                                                         str[i], str[i + 1]);
  }
};
```

When `i = str.size()`, the condition `(str.size() + 1) < str.size()` is always false, so kerning between the last tab-space and the next real character is skipped. More importantly, passing `str.size()` as an index is semantically confusing — it looks like a potential OOB access even though the bounds check prevents it.

The correct index would be `i` (the tab character's position), so the last of the 4 spaces can kern with whatever character follows the tab.

### Current code

```cpp
if (c == '\t') {
  // Add 4 spaces.
  for (int j = 0; j < 4; ++j) handle_char(str.size(), ' ');
  i++;
  continue;
}
```

### Fix

Pass `i` as the index. Only the last space in the tab should kern with the next character, so for the first 3 spaces we can pass an index that suppresses kerning (e.g., `str.size()`), and only the 4th space gets the real index:

```diff
 if (c == '\t') {
   // Add 4 spaces.
-  for (int j = 0; j < 4; ++j) handle_char(str.size(), ' ');
+  for (int j = 0; j < 3; ++j) handle_char(str.size(), ' ');
+  handle_char(i, ' ');
   i++;
   continue;
 }
```

### Tradeoffs

Minimal impact. Kerning between a space and the next character is usually zero or near-zero. But this is the correct behavior.

---

## Bug 5: Dangling stbtt_pack_context in FontInfo

### What

`renderer.h:289` — The `FontInfo` struct contains a `stbtt_pack_context context` field:

```cpp
struct FontInfo {
  GLuint texture;
  float scale = 0;
  float pixel_height = 0;
  int ascent, descent, line_gap;
  stbtt_fontinfo font_info;
  stbtt_pack_context context;   // ← this one
  stbtt_packedchar chars[256];
};
```

### Why it's wrong

The pack context is only used during `LoadFont` (`renderer.cc:766–772`):

```cpp
stbtt_PackBegin(&font.context, atlas, kAtlasWidth, kAtlasHeight,
                kAtlasWidth, 1, /*alloc_context=*/&scratch);
stbtt_PackSetOversampling(&font.context, 2, 2);
CHECK(stbtt_PackFontRange(&font.context, asset.contents, 0, pixel_height,
                          32, 126 - 32 + 1, font.chars) == 1,
      "Could not load font ", asset.name, ", atlas is too small");
stbtt_PackEnd(&font.context);
```

After `stbtt_PackEnd()`, the context internally references the scratch allocator's memory, which is freed when `LoadFont` returns (`ArenaAllocator scratch` is stack-allocated). The `context` field now contains dangling pointers. Nobody reads it after `PackEnd`, but it wastes `sizeof(stbtt_pack_context)` bytes per FontInfo and is a trap for anyone who might try to use it later.

### Fix

Use a local variable instead of the struct field:

In `renderer.h`, remove the field:
```diff
 struct FontInfo {
   GLuint texture;
   float scale = 0;
   float pixel_height = 0;
   int ascent, descent, line_gap;
   stbtt_fontinfo font_info;
-  stbtt_pack_context context;
   stbtt_packedchar chars[256];
 };
```

In `renderer.cc:LoadFont`, use a local variable:
```diff
+  stbtt_pack_context context;
   {
     TIMER("Building font atlas for ", asset.name);
     stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                           &font.line_gap);
-    stbtt_PackBegin(&font.context, atlas, kAtlasWidth, kAtlasHeight,
+    stbtt_PackBegin(&context, atlas, kAtlasWidth, kAtlasHeight,
                     kAtlasWidth, 1, /*alloc_context=*/&scratch);
-    stbtt_PackSetOversampling(&font.context, 2, 2);
-    CHECK(stbtt_PackFontRange(&font.context, asset.contents, 0, pixel_height,
+    stbtt_PackSetOversampling(&context, 2, 2);
+    CHECK(stbtt_PackFontRange(&context, asset.contents, 0, pixel_height,
                               32, 126 - 32 + 1, font.chars) == 1,
           "Could not load font ", asset.name, ", atlas is too small");
-    stbtt_PackEnd(&font.context);
+    stbtt_PackEnd(&context);
   }
```

### Tradeoffs

None. The field is unused after load. This is a cleanup that reduces `FontInfo` size and eliminates the dangling pointer.

---

## Bug 6: Atlas 3000x3000 for 95 glyphs

### What

`renderer.h:279–281` — The font atlas is 3000x3000 pixels (9 MB) for only 95 ASCII glyphs:

```cpp
inline static constexpr size_t kAtlasWidth = 3000;
inline static constexpr size_t kAtlasHeight = 3000;
inline static constexpr size_t kAtlasSize = kAtlasWidth * kAtlasHeight;
```

### Why it's wrong

At 100px reference height with 2x2 oversampling, each glyph is rasterized at ~200px tall. A typical glyph is roughly 50-100px wide (100-200px with oversampling). So each glyph occupies roughly 200 x 200 = 40,000 pixels. For 95 glyphs: ~3.8M pixels of actual glyph data.

A 3000x3000 atlas = 9,000,000 pixels. Utilization is about 42% in the best case (wider glyphs pack less efficiently). A 1024x1024 atlas = 1,048,576 pixels, which is tight but should fit 95 glyphs at this size — `stbtt_PackFontRange` is reasonably efficient at packing.

The 9 MB atlas also means 9 MB of scratch memory during loading (`ArenaAllocator scratch(allocator_, kAtlasSize * 5)` = 45 MB scratch!) and 9 MB of GPU texture memory per font. Each additional font loaded adds another 9 MB.

**Compounding factor — we're not using stb_rect_pack**: `stb_truetype` has two rectangle packing modes. If `stb_rect_pack.h` is included before `stb_truetype.h`, it uses the **Skyline Bottom-Left** algorithm from `stb_rect_pack` (a proper bin-packing algorithm). If not, it falls back to a **naive row-based packer** that just walks left-to-right, bottom-to-top with no attempt to fill gaps.

We have `stb_rect_pack.h` in the repo (`libraries/stb_rect_pack.h`) and it's compiled (`libraries/stb_rect_pack.cc`), but `stb_truetype.cc` doesn't include it before the implementation:

```cpp
// stb_truetype.cc (current)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
```

`stb_truetype.h` checks `#ifndef STB_RECT_PACK_VERSION` (line 4227) — since `stb_rect_pack.h` was never included, that symbol is undefined, and stb_truetype defines its own minimal `stbrp_*` types with the naive packer. The good packer is sitting right there unused.

This is likely why the atlas needed to be 3000x3000 in the first place — the naive packer wastes so much space that a smaller atlas wouldn't fit 95 glyphs.

### Fix

Two changes:

**1. Enable stb_rect_pack** in `stb_truetype.cc`:

```diff
+#include "stb_rect_pack.h"
 #define STB_TRUETYPE_IMPLEMENTATION
 #include "stb_truetype.h"
```

This makes `stbtt_PackFontRange` use the Skyline Bottom-Left algorithm instead of the naive row packer, dramatically improving atlas utilization.

**2. Reduce atlas to 1024x1024** in `renderer.h`:

```diff
-inline static constexpr size_t kAtlasWidth = 3000;
-inline static constexpr size_t kAtlasHeight = 3000;
+inline static constexpr size_t kAtlasWidth = 1024;
+inline static constexpr size_t kAtlasHeight = 1024;
 inline static constexpr size_t kAtlasSize = kAtlasWidth * kAtlasHeight;
```

### Tradeoffs

- With the Skyline packer enabled, 1024x1024 should comfortably fit 95 ASCII glyphs at 100px with 2x2 oversampling. If packing fails for a font with unusually wide glyphs, the existing `CHECK` at `renderer.cc:770` catches it:
  ```cpp
  CHECK(stbtt_PackFontRange(&font.context, asset.contents, 0, pixel_height,
                            32, 126 - 32 + 1, font.chars) == 1,
        "Could not load font ", asset.name, ", atlas is too small");
  ```
  If this fires, we can either bump to 2048x2048 (4 MB, still much better than 9 MB) or reduce oversampling to 1x1 for that font.

- Memory savings: 9 MB → 1 MB per font on the GPU, 45 MB → 5 MB scratch during loading.

- The `stb_rect_pack.h` include is a no-risk change — it's the recommended configuration per stb_truetype's own docs (line 111: `#include "stb_rect_pack.h" -- optional, but you really want it`).

---

## Bug 7: GL_REPEAT on font atlas

### What

`renderer.cc:284–285` — The font atlas texture uses `GL_REPEAT` wrap mode:

```cpp
OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
```

### Why it's wrong

`GL_REPEAT` causes texture coordinates outside [0, 1] to wrap around. For a font atlas, this means:
- A glyph near the right edge of the atlas could bleed into glyphs on the left edge when mipmapping or when floating-point imprecision pushes a UV slightly past 1.0.
- With trilinear filtering (`GL_LINEAR_MIPMAP_LINEAR` on line 286–287), the mipmap generation takes neighboring texels into account. At the atlas boundaries, `GL_REPEAT` tells the GPU the texture tiles, so mipmap levels will average pixels from opposite edges of the atlas.

`GL_CLAMP_TO_EDGE` tells the GPU to use the nearest edge texel for out-of-range coordinates, which prevents cross-edge bleeding.

Note: `LoadTexture` (line 264–265) also uses `GL_REPEAT`, which is correct for sprite/tilemap textures that may intentionally tile. Only the font atlas path (`LoadFontTexture`) needs changing.

### Current code

```cpp
size_t BatchRenderer::LoadFontTexture(const void* data, size_t width,
                                      size_t height) {
  GLuint tex;
  const size_t index = tex_.size();
  OPENGL_CALL(glGenTextures(1, &tex));
  OPENGL_CALL(glActiveTexture(GL_TEXTURE0 + index));
  OPENGL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
  OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
```

### Fix

```diff
-OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
-OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
+OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
+OPENGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
```

### Tradeoffs

None. `GL_CLAMP_TO_EDGE` is universally correct for atlas textures. No visual difference in most cases, but eliminates a class of subtle edge artifacts.

---

## Bug 8: TextDimensions uses different line height calculation than DrawText

### What

`DrawText` (`renderer.cc:847–848`) computes newline advance as:
```cpp
p.y += pixel_scale * info->scale * (info->ascent - info->descent + info->line_gap);
```

`TextDimensions` (`renderer.cc:867, 874`) computes it as:
```cpp
const float scale = stbtt_ScaleForPixelHeight(&info->font_info, size);
p.y = scale * (info->ascent - info->descent + info->line_gap);
```

### Why it's wrong

These are **mathematically equivalent** — `pixel_scale * info->scale` equals `stbtt_ScaleForPixelHeight(&info->font_info, size)` because:
- `info->scale = stbtt_ScaleForPixelHeight(&info->font_info, 100)`
- `pixel_scale = size / 100.0`
- `pixel_scale * info->scale = (size / 100.0) * stbtt_ScaleForPixelHeight(&info->font_info, 100)`
- `stbtt_ScaleForPixelHeight` is `pixel_height / (ascent - descent)`, so this simplifies to `size / (ascent - descent)`, which is the same as `stbtt_ScaleForPixelHeight(&info->font_info, size)`.

So the line heights match. However, using two different code paths to compute the same value is a maintainability problem:
1. It's not obvious they're equivalent without doing the algebra.
2. If one path changes (e.g., rounding, or a different reference height), the other might not be updated.
3. The overall scaling strategy in `TextDimensions` uses `stbtt_GetCodepointHMetrics` (raw font metrics) while `DrawText` uses `stbtt_GetPackedQuad` (atlas-based metrics). This means the *character widths* are computed differently even if line heights match. That width inconsistency is the real bug (see Bug 3).

### Fix

Make `TextDimensions` use the same approach as `DrawText` — compute `pixel_scale` and use `info->scale`:

```diff
 IVec2 Renderer::TextDimensions(std::string_view font_name, uint32_t size,
                                std::string_view str) {
   FontInfo* info = nullptr;
   if (!font_table_.Lookup(font_name, &info)) {
     LOG("Could not find ", font_name, " in fonts");
     return IVec2();
   }
   auto p = FVec2::Zero();
-  const float scale = stbtt_ScaleForPixelHeight(&info->font_info, size);
-  p.y = scale * (info->ascent - info->descent + info->line_gap);
+  const float pixel_scale = size / info->pixel_height;
+  const float scale = pixel_scale * info->scale;
+  p.y = scale * (info->ascent - info->descent + info->line_gap);
   float x = 0;
```

The rest of `TextDimensions` continues using `scale` as before — since `pixel_scale * info->scale` equals the old `stbtt_ScaleForPixelHeight(&info->font_info, size)`, the numeric results are identical. But now both functions visibly use the same scaling strategy, and any future change to `pixel_height` or `info->scale` will automatically propagate.

### Tradeoffs

This is a readability/maintainability change with no behavioral difference. The deeper issue (Bug 3) is that `TextDimensions` uses `stbtt_GetCodepointHMetrics` for character widths while `DrawText` uses `stbtt_GetPackedQuad`. Ideally `TextDimensions` would use `stbtt_GetPackedQuad` too for perfect consistency, but that's a larger refactor beyond the scope of this fix.
