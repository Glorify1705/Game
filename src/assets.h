
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

class Assets {
 public:
  Assets(const uint8_t* buffer) : assets_(GetAssetsPack(buffer)) {
    CHECK(assets_ != nullptr, "Failed to build assets from buffer");
  }

  const ImageFile* GetImage(const char* name) const;
  const ScriptFile* GetScript(const char* name) const;
  size_t scripts() const { return assets_->scripts()->size(); }
  const ScriptFile* GetScriptByIndex(size_t idx) const {
    return assets_->scripts()->Get(idx);
  }
  const SpritesheetFile* GetSpritesheet(const char* name) const;
  const SoundFile* GetSound(const char* name) const;
  size_t spritesheets() const { return assets_->sprite_sheets()->size(); }
  const SpritesheetFile* GetSpritesheetByIndex(size_t idx) const {
    return assets_->sprite_sheets()->Get(idx);
  }

 private:
  const AssetsPack* assets_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H