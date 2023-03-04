#include "assets.h"

namespace G {

const ImageFile* Assets::GetImage(const char* name) const {
  for (const auto* image : *assets_->images()) {
    if (!std::strcmp(image->filename()->c_str(), name)) {
      return image;
    }
  }
  return nullptr;
}
const ScriptFile* Assets::GetScript(const char* name) const {
  for (const auto* script : *assets_->scripts()) {
    if (!std::strcmp(script->filename()->c_str(), name)) {
      return script;
    }
  }
  return nullptr;
}
const SpritesheetFile* Assets::GetSpritesheet(const char* name) const {
  for (const auto* spritesheet : *assets_->sprite_sheets()) {
    if (!std::strcmp(spritesheet->filename()->c_str(), name)) {
      return spritesheet;
    }
  }
  return nullptr;
}

const SoundFile* Assets::GetSound(const char* name) const {
  for (const auto* sound : *assets_->sounds()) {
    if (!std::strcmp(sound->filename()->c_str(), name)) {
      return sound;
    }
  }
  return nullptr;
}

}  // namespace G