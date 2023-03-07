#include "debug_ui.h"

namespace G {

std::ostream& operator<<(std::ostream& os, const stbtt_aligned_quad& q) {
  os << "{ ";
  os << "x0 = " << q.x0 << " y0 = " << q.y0 << " ";
  os << "x1 = " << q.x1 << " y1 = " << q.y1 << " ";
  os << "s0 = " << q.s0 << " t0 = " << q.t0 << " ";
  os << "s1 = " << q.s1 << " t1 = " << q.t1 << " ";
  os << "}";
  return os;
}

DebugConsole::DebugConsole(QuadRenderer* renderer) : renderer_(renderer) {
  const DebugFont font = GetDebugFont();
  CHECK(
      stbtt_BakeFontBitmap(font.data, 0, kFontSize, bitmap_.data(), kBitmapSize,
                           kBitmapSize, 32, 96, char_data_.data()) > 0,
      "Could not fit all characters");
  uint8_t* buffer = new uint8_t[4 * bitmap_.size()];
  for (size_t i = 0, j = 0; j < bitmap_.size(); j++) {
    std::memset(&buffer[i], bitmap_[j], 4);
    i += 4;
  }
  tex_ = renderer->LoadTexture(buffer, kBitmapSize, kBitmapSize);
  delete[] buffer;
  info_.Init(font);
}

void DebugConsole::LogLine(std::string_view text) {
  lines_.push_back(std::string(text));
  if (lines_.size() > kMaxLines) {
    lines_.pop_front();
  }
}

void DebugConsole::PushText(std::string_view text, FVec2 position) {
  text_to_render_.push_back({position, std::string(text)});
}

void DebugConsole::Render() {
  if (!enabled_) return;
  renderer_->SetActiveTexture(tex_);
  {
    FVec2 position(0, 0);
    info_.AdjustPosition(&position);
    for (std::string_view line : lines_) {
      for (char c : line) {
        if (c >= 32) {
          PushChar(c, &position);
        }
      }
      info_.AdjustForNextLine(&position, FVec(0, 0));
    }
  }
  for (const auto& to_render : text_to_render_) {
    const FVec2 origin = to_render.position;
    FVec2 position = origin;
    renderer_->SetActiveTexture(tex_);
    info_.AdjustPosition(&position);
    for (char c : to_render.content) {
      if (c == '\n') {
        info_.AdjustForNextLine(&position, origin);
      } else if (c == '\t') {
        info_.MoveForward(&position, /*chars=*/2);
      } else if (c >= 32) {
        PushChar(c, &position);
      }
    }
  }
}

void DebugConsole::PushChar(char c, FVec2* position) {
  stbtt_aligned_quad q;
  stbtt_GetBakedQuad(char_data_.data(), kBitmapSize, kBitmapSize, c - 32,
                     &position->x, &position->y, &q,
                     /*opengl_fillrule=*/true);
  renderer_->PushQuad(FVec2(q.x0, q.y1), FVec2(q.x1, q.y0), FVec2(q.s0, q.t1),
                      FVec2(q.s1, q.t0), FVec2(0, 0), /*angle=*/0);
}

}  // namespace G