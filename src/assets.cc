#include "assets.h"
#include <cstring>
#include "src/allocators.h"

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

void DbAssets::Load() {
  if (sqlite3_open(db_filename_.str(), &db_) != SQLITE_OK) {
    DIE("Failed to open ", db_filename_, ": ", sqlite3_errmsg(db_));
  }
  size_t total_size = 0;
  size_t total_names = 0;
  {
    FixedStringBuffer<256> sql("SELECT SUM(size), SUM(LENGTH(name)) FROM asset_metadata");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    CHECK(sqlite3_step(stmt) == SQLITE_ROW, "Could not read asset metadata: ", sqlite3_errmsg(db_));
    total_size = sqlite3_column_int(stmt, 0);
    total_names = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);
  }
  name_size_ = 0;
  name_buffer_ = reinterpret_cast<char*>(allocator_->Alloc(total_names, /*align=*/1));
  content_size_ = 0;
  content_buffer_ = reinterpret_cast<uint8_t*>(allocator_->Alloc(total_size, /*align=*/1));
  FixedStringBuffer<256> sql("SELECT name, LENGTH(name), type, size FROM asset_metadata");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto name_length = sqlite3_column_int(stmt, 1);
    auto type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const size_t size = sqlite3_column_int(stmt, 3);
    std::memcpy(&name_buffer_[name_size_], name, name_length);
    if (!strcmp(type, "script")) {
      Script script;
      script.name = std::string_view(name, name_length);
      script.contents = 
    } else if (!strcmp(type, "sheet") {
    }
  } 
  sqlite3_finalize(stmt);
}

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
