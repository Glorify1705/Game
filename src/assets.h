
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
  Assets(const AssetsPack* assets, size_t size) : assets_(assets), size_(size) {
    CHECK(assets_ != nullptr, "Failed to build assets from buffer");
  }

  const ImageAsset* GetImage(std::string_view name) const;
  const ScriptAsset* GetScript(std::string_view name) const;
  size_t scripts() const { return assets_->scripts()->size(); }
  const ScriptAsset* GetScriptByIndex(size_t idx) const {
    return assets_->scripts()->Get(idx);
  }
  const SpritesheetAsset* GetSpritesheet(std::string_view name) const;
  size_t spritesheets() const { return assets_->spritesheets()->size(); }
  const SpritesheetAsset* GetSpritesheetByIndex(size_t idx) const {
    return assets_->spritesheets()->Get(idx);
  }
  const SpriteAsset* GetSprite(std::string_view name) const;
  size_t sprites() const { return assets_->sprites()->size(); }
  const SpriteAsset* GetSpriteByIndex(size_t idx) const {
    return assets_->sprites()->Get(idx);
  }
  const FontAsset* GetFont(std::string_view name) const;
  size_t fonts() const { return assets_->fonts()->size(); }
  const FontAsset* GetFontByIndex(size_t idx) const {
    return assets_->fonts()->Get(idx);
  }
  const TextFileAsset* GetText(std::string_view name) const;
  size_t shaders() const { return assets_->shaders()->size(); }
  const ShaderAsset* GetShaderByIndex(size_t idx) const {
    return assets_->shaders()->Get(idx);
  }
  const SoundAsset* GetSound(std::string_view name) const;
  const ShaderAsset* GetShader(std::string_view) const;

  const AssetsPack* PackedAssets() const { return assets_; }
  size_t PackerAssetSize() const { return size_; }

 private:
  const AssetsPack* assets_;
  const size_t size_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H