#include "assets.h"

const assets::Image* Assets::GetImage(const char* name) const {
  for (const auto* image : *assets_->images()) {
    if (!std::strcmp(image->filename()->c_str(), name)) {
      return image;
    }
  }
  return nullptr;
}
const assets::Script* Assets::GetScript(const char* name) const {
  for (const auto* script : *assets_->scripts()) {
    if (!std::strcmp(script->filename()->c_str(), name)) {
      return script;
    }
  }
  return nullptr;
}
const assets::Script* Assets::GetScriptByIndex(size_t idx) const {
  return assets_->scripts()->Get(idx);
}
const assets::Spritesheet* Assets::GetSpritesheet(const char* name) const {
  for (const auto* spritesheet : *assets_->sprite_sheets()) {
    if (!std::strcmp(spritesheet->filename()->c_str(), name)) {
      return spritesheet;
    }
  }
  return nullptr;
}

const assets::Sound* Assets::GetSound(const char* name) const {
  for (const auto* sound : *assets_->sounds()) {
    if (!std::strcmp(sound->filename()->c_str(), name)) {
      return sound;
    }
  }
  return nullptr;
}
