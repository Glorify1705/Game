#include "assets.h"

namespace G {
namespace {

bool Match(const flatbuffers::String& a, std::string_view b) {
  return a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size());
}

template <typename T>
const T* Search(const flatbuffers::Vector<flatbuffers::Offset<T>>& v,
                std::string_view name) {
  for (const auto* entry : v) {
    if (Match(*entry->name(), name)) return entry;
  }
  return nullptr;
}

}  // namespace

const ImageAsset* Assets::GetImage(std::string_view name) const {
  return Search(*assets_->images(), name);
}

const SpriteAsset* Assets::GetSprite(std::string_view name) const {
  return Search(*assets_->sprites(), name);
}

const ScriptAsset* Assets::GetScript(std::string_view name) const {
  return Search(*assets_->scripts(), name);
}
const SpritesheetAsset* Assets::GetSpritesheet(std::string_view name) const {
  return Search(*assets_->spritesheets(), name);
}

const SoundAsset* Assets::GetSound(std::string_view name) const {
  return Search(*assets_->sounds(), name);
}

const FontAsset* Assets::GetFont(std::string_view name) const {
  return Search(*assets_->fonts(), name);
}

const TextFileAsset* Assets::GetText(std::string_view name) const {
  return Search(*assets_->texts(), name);
}

const ShaderAsset* Assets::GetShader(std::string_view name) const {
  return Search(*assets_->shaders(), name);
}

}  // namespace G
