
#pragma once
#ifndef _GAME_ASSETS_H
#define _GAME_ASSETS_H

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "assets_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "logging.h"

class Assets {
 public:
  Assets(const uint8_t* buffer) : assets_(assets::GetAssets(buffer)) {
    CHECK(assets_ != nullptr, "Failed to build assets from buffer");
  }

  const assets::Image* GetImage(const char* name) const;
  const assets::Script* GetScript(const char* name) const;
  size_t scripts() const { return assets_->scripts()->size(); }
  const assets::Script* GetScriptByIndex(size_t idx) const;
  const assets::Spritesheet* GetSpritesheet(const char* name) const;
  const assets::Sound* GetSound(const char* name) const;

 private:
  const assets::Assets* assets_;
};

#endif  // _GAME_ASSETS_H