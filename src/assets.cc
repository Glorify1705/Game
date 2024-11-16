#include "assets.h"

#include <cstring>

#include "clock.h"
#include "src/sound.h"

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

void DbAssets::LoadScript(std::string_view filename, uint8_t* buffer,
                          std::size_t size) {
  FixedStringBuffer<256> sql("SELECT contents FROM scripts WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Script script;
  script.name = filename;
  script.contents = buffer;
  script.size = size;
  scripts_.Insert(filename, script);
  sqlite3_finalize(stmt);
}

void DbAssets::LoadFont(std::string_view filename, uint8_t* buffer,
                        std::size_t size) {
  FixedStringBuffer<256> sql("SELECT contents FROM fonts WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Font font;
  font.name = filename;
  font.contents = buffer;
  font.size = size;
  fonts_.Insert(filename, font);
  sqlite3_finalize(stmt);
}

void DbAssets::LoadAudio(std::string_view filename, uint8_t* buffer,
                         std::size_t size) {
  FixedStringBuffer<256> sql("SELECT contents FROM audios WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Sound sound;
  sound.name = filename;
  sound.contents = buffer;
  sound.size = size;
  sounds_.Insert(filename, sound);
  sqlite3_finalize(stmt);
}

void DbAssets::LoadSpritesheet(std::string_view filename, uint8_t* buffer,
                               std::size_t size) {
  {
    FixedStringBuffer<256> sql(
        "SELECT image, width, height FROM spritesheets WHERE name = ?");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
    auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::size_t width = sqlite3_column_int(stmt, 0);
    std::size_t height = sqlite3_column_int(stmt, 1);
    std::memcpy(buffer, contents, size);
    Spritesheet sheet;
    sheet.name = filename;
    sheet.width = width;
    sheet.height = height;
    spritesheets_.Insert(filename, sheet);
    sqlite3_finalize(stmt);
  }
  {
    FixedStringBuffer<256> sql(
        "SELECT name, x, y, width, height FROM sprites WHERE spritesheet = ?");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto contents =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      std::memcpy(buffer, contents, size);
      Sprite sprite;
      auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      sprite.name = &name_buffer_[name_size_];
      std::memcpy(&sprite.name, name, std::strlen(name) + 1);
      sprite.x = sqlite3_column_int(stmt, 1);
      sprite.y = sqlite3_column_int(stmt, 2);
      sprite.spritesheet = filename;
      sprite.width = sqlite3_column_int(stmt, 3);
      sprite.height = sqlite3_column_int(stmt, 4);
      sprites_.Insert(sprite.name, sprite);
    }
    sqlite3_finalize(stmt);
  }
}

void DbAssets::LoadImage(std::string_view filename, uint8_t* buffer,
                         std::size_t size) {
  FixedStringBuffer<256> sql(
      "SELECT contents, width, height FROM images WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No image ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  const std::size_t width = sqlite3_column_int(stmt, 1);
  const std::size_t height = sqlite3_column_int(stmt, 2);
  Image image;
  image.name = filename;
  image.width = width;
  image.height = height;
  image.contents = buffer;
  image.size = size;
  images_.Insert(filename, image);
  sqlite3_finalize(stmt);
}

void DbAssets::Load() {
  if (sqlite3_open(db_filename_.str(), &db_) != SQLITE_OK) {
    DIE("Failed to open ", db_filename_, ": ", sqlite3_errmsg(db_));
  }
  std::size_t total_size = 0, total_names = 0;
  {
    FixedStringBuffer<256> sql(
        "SELECT SUM(size), SUM(LENGTH(name)) FROM asset_metadata");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    CHECK(sqlite3_step(stmt) == SQLITE_ROW,
          "Could not read asset metadata: ", sqlite3_errmsg(db_));
    total_size = sqlite3_column_int(stmt, 0);
    total_names = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);
  }
  name_size_ = 0;
  name_buffer_ =
      reinterpret_cast<char*>(allocator_->Alloc(total_names, /*align=*/1));
  content_size_ = 0;
  content_buffer_ =
      reinterpret_cast<uint8_t*>(allocator_->Alloc(total_size, /*align=*/1));
  struct Loader {
    const char* name;
    void (DbAssets::*load)(std::string_view name, uint8_t* buffer,
                           std::size_t size);
  };
  static constexpr Loader kLoaders[] = {
      {.name = "script", .load = &DbAssets::LoadScript},
      {.name = "sheet", .load = nullptr},
      {.name = "image", .load = &DbAssets::LoadImage},
      {.name = "audio", .load = &DbAssets::LoadAudio},
      {.name = "fonts", .load = &DbAssets::LoadFont},
      {.name = nullptr, .load = nullptr},
  };
  FixedStringBuffer<256> sql(
      "SELECT name, LENGTH(name), type, size FROM asset_metadata ORDER BY type");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto name_length = sqlite3_column_int(stmt, 1);
    auto type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const std::size_t size = sqlite3_column_int(stmt, 3);
    auto* buffer = &content_buffer_[content_size_];
    content_size_ += size;
    auto* name_ptr = &name_buffer_[name_size_];
    std::memcpy(name_ptr, name, name_length);
    name_size_ += name_length;
    for (const Loader& loader : kLoaders) {
      if (loader.name == nullptr) {
        LOG("No loader for asset ", name, " with type ", type);
        break;
      }
      if (!std::strcmp(type, loader.name)) {
        TIMER("Load DB asset ", name);
        auto method = loader.load;
        if (method == nullptr) {
          LOG("While loading ", name, ": unimplemented asset type ", type);
          break;
        }
        (this->*method)(std::string_view(name_ptr, name_length), buffer, size);
      }
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
