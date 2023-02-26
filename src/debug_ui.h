#pragma once
#ifndef _GAME_DEBUG_UI_H
#define _GAME_DEBUG_UI_H

#include <array>
#include <cstdint>
#include <deque>
#include <memory>

#include "assets.h"
#include "debug_font.h"
#include "logging.h"
#include "renderer.h"
#include "stb_truetype.h"
#include "strings.h"

class DebugConsole {
 public:
  DebugConsole(QuadRenderer* renderer);

  void LogLine(std::string_view text);

  template <typename... T>
  void Log(T... ts) {
    LogLine(StrCat(ts...));
  }

  void Render();

  void Clear() { text_to_render_.clear(); }
  void PushText(std::string_view text, FVec2 position);

  void Toggle() { enabled_ = !enabled_; }

 private:
  inline static constexpr size_t kFontSize = 16;
  inline static constexpr size_t kBitmapSize = 1024;
  inline static constexpr size_t kMaxLines = 20;

  class FontInfo {
   public:
    void Init(const DebugFont& font) {
      stbtt_InitFont(&info_, font.data, 0);
      scale_ = stbtt_ScaleForPixelHeight(&info_, kFontSize);
      stbtt_GetFontVMetrics(&info_, &ascent_, &descent_, &line_gap_);
    }

    void AdjustPosition(FVec2* pos) {
      pos->y = pos->y + scale_ * (ascent_ - descent_);
    }

    void AdjustForNextLine(FVec2* pos, FVec2 origin) {
      pos->x = origin.x;
      pos->y += scale_ * (ascent_ - descent_);
    }

    void MoveForward(FVec2* pos, int chars) {
      pos->x += (kFontSize / 2) * chars;
    }

   private:
    stbtt_fontinfo info_;
    int ascent_, descent_, line_gap_;
    float scale_;
  };

  void PushChar(char c, FVec2* position);

  struct TextToRender {
    FVec2 position;
    std::string content;
  };

  std::deque<std::string> lines_;
  std::vector<TextToRender> text_to_render_;
  bool enabled_ = false;
  QuadRenderer* renderer_ = nullptr;
  std::array<uint8_t, kBitmapSize * kBitmapSize> bitmap_;
  std::array<stbtt_bakedchar, 128> char_data_;
  FontInfo info_;
  GLuint tex_;
};

#endif  // _GAME_DEBUG_UI_H