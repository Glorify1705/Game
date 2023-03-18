#include "fonts.h"
#include "stb_truetype.h"

namespace G {

struct Quadformatter {
  stbtt_aligned_quad q;

  friend void AppendToString(const Quadformatter& qf, std::string& out) {
    const auto& q = qf.q;
    StrAppend(&out, "{ x0 = ", q.x0, ", y0 = ", q.y0, " x1 = ", q.x1,
              " y1 = ", q.y1, " s0 = ", q.s0, " t0 = ", q.t0, " s1 = ", q.s1,
              " t1 = ", q.t1, " }");
  }
};

FontRenderer::FontRenderer(const Assets* assets, BatchRenderer* renderer)
    : assets_(assets), renderer_(renderer) {}

FontRenderer::FontInfo* FontRenderer::LoadFont(std::string_view font_name,
                                               float pixel_height) {
  if (FontInfo * info; font_table_.Lookup(font_name, &info)) return info;
  DCHECK(font_count_ < fonts_.size());
  FontInfo& font = fonts_[font_count_++];
  const FontFile& font_file = *assets_->GetFont(font_name);
  const uint8_t* font_buffer = font_file.contents()->data();
  CHECK(stbtt_InitFont(&font.font_info, font_buffer,
                       stbtt_GetFontOffsetForIndex(font_buffer, 0)),
        "Could not initialize ", font_name);
  font.scale = stbtt_ScaleForPixelHeight(&font.font_info, pixel_height);
  stbtt_GetFontVMetrics(&font.font_info, &font.ascent, &font.descent,
                        &font.line_gap);
  stbtt_PackBegin(&font.context, font.atlas.data(), kAtlasWidth, kAtlasHeight,
                  kAtlasWidth, 1, nullptr);
  stbtt_PackSetOversampling(&font.context, 2, 2);
  CHECK(stbtt_PackFontRange(&font.context, font_buffer, 0, pixel_height, 0, 256,
                            font.chars.data()) == 1,
        "Could not load font");
  stbtt_PackEnd(&font.context);
  uint8_t* buffer = new uint8_t[4 * font.atlas.size()];
  for (size_t i = 0, j = 0; j < font.atlas.size(); j++, i += 4) {
    std::memset(&buffer[i], font.atlas[j], 4);
  }
  font.texture = renderer_->LoadTexture(buffer, kAtlasWidth, kAtlasHeight);
  delete[] buffer;
  font_table_.Insert(font_name, &font);
  return &font;
}

void FontRenderer::DrawText(std::string_view font, float size,
                            std::string_view str, FVec2 position) {
  FontInfo* info = LoadFont(font, size);
  renderer_->SetActiveTexture(info->texture);
  FVec2 p = position;
  p.y += info->scale * (info->ascent - info->descent);
  for (char c : str) {
    if (c == '\n') {
      p.x = position.x;
      p.y += info->scale * (info->ascent - info->descent);
    } else if (c == '\t') {
      p.x += size * 2;
    } else {
      stbtt_aligned_quad q;
      stbtt_GetPackedQuad(info->chars.data(), kAtlasWidth, kAtlasHeight, c,
                          &p.x, &p.y, &q,
                          /*align_to_integer=*/true);
      renderer_->PushQuad(FVec2(q.x0, q.y1), FVec2(q.x1, q.y0),
                          FVec2(q.s0, q.t1), FVec2(q.s1, q.t0), FVec2(0, 0),
                          /*angle=*/0);
    }
  }
}

}  // namespace G
