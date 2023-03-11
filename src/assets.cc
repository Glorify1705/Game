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
    if (Match(*entry->filename(), name)) return entry;
  }
  return nullptr;
}

}  // namespace

const ImageFile* Assets::GetImage(std::string_view name) const {
  return Search(*assets_->images(), name);
}

const ScriptFile* Assets::GetScript(std::string_view name) const {
  return Search(*assets_->scripts(), name);
}
const SpritesheetFile* Assets::GetSpritesheet(std::string_view name) const {
  return Search(*assets_->sprite_sheets(), name);
}

const SoundFile* Assets::GetSound(std::string_view name) const {
  return Search(*assets_->sounds(), name);
}

const FontFile* Assets::GetFont(std::string_view name) const {
  return Search(*assets_->fonts(), name);
}

}  // namespace G