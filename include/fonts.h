#pragma once
#ifndef _GAME_FONT_H
#define _GAME_FONT_H

#include "array.h"
#include "assets.h"
#include "map.h"
#include "renderer.h"
#include "stb_truetype.h"
#include "vec.h"

namespace G {

class FontRenderer {
 public:
  FontRenderer(const Assets* assets, QuadRenderer* renderer);

  void DrawText(std::string_view font, float pixel_size, std::string_view str,
                FVec2 position);

 private:
  inline static constexpr size_t kAtlasWidth = 4096;
  inline static constexpr size_t kAtlasHeight = 4096;

  struct FontInfo {
    GLuint texture;
    float scale = 0;
    int ascent, descent, line_gap;
    stbtt_fontinfo font_info;
    stbtt_pack_context context;
    std::array<stbtt_packedchar, 256> chars;
    std::array<uint8_t, kAtlasWidth * kAtlasHeight> atlas;
  };

  FontInfo* LoadFont(std::string_view font_name, float pixel_height);

  const Assets* const assets_ = nullptr;
  QuadRenderer* renderer_ = nullptr;
  LookupTable<FontInfo*> font_table_;
  std::array<FontInfo, 32> fonts_;
  size_t font_count_ = 0;
};

}  // namespace G

#endif  // _GAME_FONT_H