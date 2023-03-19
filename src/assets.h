
#pragma once
#ifndef _GAME_ASSETS_H
#define _GAME_ASSETS_H

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "assets_generated.h"
#include "clock.h"
#include "flatbuffers/flatbuffers.h"
#include "logging.h"

namespace G {

inline std::string_view FlatbufferStringview(const flatbuffers::String* s) {
  if (s == nullptr) return std::string_view("", 0);
  return std::string_view(s->data(), s->size());
}

class Assets {
 public:
  Assets(const uint8_t* buffer) : assets_(GetAssetsPack(buffer)) {
    CHECK(assets_ != nullptr, "Failed to build assets from buffer");
  }

  const ImageAsset* GetImage(std::string_view name) const;
  const ScriptAsset* GetScript(std::string_view name) const;
  size_t scripts() const { return assets_->scripts()->size(); }
  const ScriptAsset* GetScriptByIndex(size_t idx) const {
    return assets_->scripts()->Get(idx);
  }
  const SpritesheetAsset* GetSpritesheet(std::string_view name) const;
  const SoundAsset* GetSound(std::string_view name) const;
  size_t spritesheets() const { return assets_->sprite_sheets()->size(); }
  const SpritesheetAsset* GetSpritesheetByIndex(size_t idx) const {
    return assets_->sprite_sheets()->Get(idx);
  }
  const FontAsset* GetFont(std::string_view name) const;
  size_t fonts() const { return assets_->fonts()->size(); }
  const FontAsset* GetFontByIndex(size_t idx) const {
    return assets_->fonts()->Get(idx);
  }

 private:
  const AssetsPack* assets_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H