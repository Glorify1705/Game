#include "assets.h"

#include <cstring>

#include "clock.h"
#include "defer.h"
#include "filesystem.h"
#include "units.h"

namespace G {

void DbAssets::LoadScript(std::string_view filename, uint8_t* buffer,
                          size_t size, ChecksumType checksum) {
  FixedStringBuffer<256> sql("SELECT contents FROM scripts WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Script script;
  script.name = filename;
  script.contents = buffer;
  script.size = size;
  script.checksum = checksum;
  script_loader_.Load(&script);
}

void DbAssets::LoadFont(std::string_view filename, uint8_t* buffer, size_t size,
                        ChecksumType checksum) {
  FixedStringBuffer<256> sql("SELECT contents FROM fonts WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Font font;
  font.name = filename;
  font.contents = buffer;
  font.size = size;
  font.checksum = checksum;
  font_loader_.Load(&font);
}

void DbAssets::LoadAudio(std::string_view filename, uint8_t* buffer,
                         size_t size, ChecksumType checksum) {
  FixedStringBuffer<256> sql("SELECT contents FROM audios WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  Sound sound;
  sound.name = filename;
  sound.contents = buffer;
  sound.size = size;
  sound.checksum = checksum;
  sound_loader_.Load(&sound);
}

void DbAssets::LoadShader(std::string_view filename, uint8_t* buffer,
                          size_t size, ChecksumType checksum) {
  FixedStringBuffer<256> sql(
      "SELECT contents, shader_type FROM shaders WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  auto type_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  std::string_view type(type_str);
  std::memcpy(buffer, contents, size);
  Shader shader;
  shader.name = filename;
  shader.contents = buffer;
  shader.size = size;
  shader.type = type == "vertex" ? ShaderType::kVertex : ShaderType::kFragment;
  shader.checksum = checksum;
  shader_loader_.Load(&shader);
}

void DbAssets::LoadText(std::string_view filename, uint8_t* buffer, size_t size,
                        ChecksumType checksum) {
  FixedStringBuffer<256> sql("SELECT contents FROM text_files WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  TextFile file;
  file.name = filename;
  file.contents = buffer;
  file.size = size;
  file.checksum = checksum;
  text_file_loader_.Load(&file);
}

void DbAssets::LoadSpritesheet(std::string_view filename, uint8_t* buffer,
                               size_t size, ChecksumType checksum) {
  {
    FixedStringBuffer<256> sql(
        "SELECT image, width, height FROM spritesheets WHERE name = ?");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
    auto image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    size_t width = sqlite3_column_int(stmt, 1);
    size_t height = sqlite3_column_int(stmt, 2);
    Spritesheet sheet;
    sheet.name = filename;
    sheet.width = width;
    sheet.height = height;
    sheet.image = InternedString(image);
    sheet.checksum = checksum;
    spritesheet_loader_.Load(&sheet);
  }
  {
    FixedStringBuffer<256> sql(
        "SELECT name, x, y, width, height FROM sprites WHERE spritesheet = ?");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Sprite sprite;
      size_t sprite_name_size = sqlite3_column_bytes(stmt, 0);
      auto db_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      std::string_view name =
          InternedString(std::string_view(db_name, sprite_name_size));
      sprite.name = name;
      sprite.x = sqlite3_column_int(stmt, 1);
      sprite.y = sqlite3_column_int(stmt, 2);
      sprite.spritesheet = filename;
      sprite.width = sqlite3_column_int(stmt, 3);
      sprite.height = sqlite3_column_int(stmt, 4);
      sprite_loader_.Load(&sprite);
    }
  }
}

void DbAssets::LoadImage(std::string_view filename, uint8_t* buffer,
                         size_t size, ChecksumType checksum) {
  FixedStringBuffer<256> sql(
      "SELECT contents, width, height FROM images WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No image ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  const size_t width = sqlite3_column_int(stmt, 1);
  const size_t height = sqlite3_column_int(stmt, 2);
  Image image;
  image.name = filename;
  image.width = width;
  image.height = height;
  image.contents = buffer;
  image.size = size;
  image.checksum = checksum;
  image_loader_.Load(&image);
}

void DbAssets::Trace(unsigned int type, void* p, void* x) {
  if (type == SQLITE_TRACE_PROFILE) {
    auto sql = static_cast<sqlite3_stmt*>(p);
    auto time = reinterpret_cast<long long*>(x);
    LOG("Executing SQL ", sqlite3_expanded_sql(sql), " took ",
        (*time) / 1'000'000.0, " milliseconds");
  }
}

XXH128_hash_t DbAssets::GetChecksum(std::string_view asset) {
  return checksums_map_.LookupOrDie(asset)->checksum;
}

void DbAssets::Load() {
  struct Loader {
    std::string_view name;
    void (DbAssets::*load)(std::string_view, uint8_t*, size_t, ChecksumType);
  };
  static constexpr Loader kLoaders[] = {
      {.name = "script", .load = &DbAssets::LoadScript},
      {.name = "spritesheet", .load = &DbAssets::LoadSpritesheet},
      {.name = "image", .load = &DbAssets::LoadImage},
      {.name = "audio", .load = &DbAssets::LoadAudio},
      {.name = "font", .load = &DbAssets::LoadFont},
      {.name = "shader", .load = &DbAssets::LoadShader},
      {.name = "text", .load = &DbAssets::LoadText},
      {.name = std::string_view(), .load = nullptr},
  };
  FixedStringBuffer<256> sql(
      "SELECT name, type, size, hash_low, hash_high FROM "
      "asset_metadata ORDER BY processing_order, type");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  ArenaAllocator scratch(allocator_, Megabytes(128));
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto type_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string_view type(type_ptr);
    const size_t size = sqlite3_column_int64(stmt, 2);
    std::string_view saved_name = InternedString(name);
    XXH128_hash_t checksum;
    auto* buffer =
        reinterpret_cast<uint8_t*>(scratch.Alloc(size, /*align=*/16));
    checksum.low64 = sqlite3_column_int64(stmt, 3);
    checksum.high64 = sqlite3_column_int64(stmt, 4);
    for (const Loader& loader : kLoaders) {
      if (loader.name.empty()) {
        LOG("No loader for asset ", name, " with type ", type);
        break;
      }
      if (type != loader.name) {
        continue;
      }
      TIMER("Load DB asset ", name);
      auto method = loader.load;
      if (method == nullptr) {
        LOG("While loading ", name, ": unimplemented asset type ", type);
        break;
      }
      (this->*method)(saved_name, buffer, size, checksum);
      Checksum c;
      c.asset = saved_name;
      std::memcpy(&c.checksum, &checksum, sizeof(checksum));
      checksums_.Push(c);
      checksums_map_.Insert(saved_name, &checksums_.back());
      break;
    }
  }
}

}  // namespace G
