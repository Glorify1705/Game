#include "assets.h"

#include <cstring>

#include "clock.h"

namespace G {
namespace {

int TraceCallback(unsigned int type, void* ctx, void* p, void* x) {
  static_cast<DbAssets*>(ctx)->Trace(type, p, x);
  return 0;
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
  scripts_.Push(script);
  scripts_map_.Insert(filename, &scripts_.back());
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
  fonts_.Push(font);
  fonts_map_.Insert(filename, &fonts_.back());
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
  sounds_.Push(sound);
  sounds_map_.Insert(filename, &sounds_.back());
  sqlite3_finalize(stmt);
}

void DbAssets::LoadShader(std::string_view filename, uint8_t* buffer,
                          std::size_t size) {
  FixedStringBuffer<256> sql(
      "SELECT contents, shader_type FROM shaders WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  auto type_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::string_view type(type_str);
  std::memcpy(buffer, contents, size);
  Shader shader;
  shader.name = filename;
  shader.contents = buffer;
  shader.size = size;
  shader.type = type == "vertex" ? ShaderType::kVertex : ShaderType::kFragment;
  shaders_.Push(shader);
  shaders_map_.Insert(filename, &shaders_.back());
  sqlite3_finalize(stmt);
}

void DbAssets::LoadText(std::string_view filename, uint8_t* buffer,
                        std::size_t size) {
  FixedStringBuffer<256> sql("SELECT contents FROM text_files WHERE name = ?");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
  CHECK(sqlite3_step(stmt) == SQLITE_ROW, "No script ", filename);
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::memcpy(buffer, contents, size);
  TextFile file;
  file.name = filename;
  file.contents = buffer;
  file.size = size;
  text_files_.Push(file);
  text_files_map_.Insert(filename, &text_files_.back());
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
    auto image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::size_t width = sqlite3_column_int(stmt, 1);
    std::size_t height = sqlite3_column_int(stmt, 2);
    Spritesheet sheet;
    sheet.name = filename;
    sheet.width = width;
    sheet.height = height;
    sheet.image = PushName(image);
    spritesheets_.Push(sheet);
    spritesheets_map_.Insert(filename, &spritesheets_.back());
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
      Sprite sprite;
      std::size_t sprite_name_size = sqlite3_column_bytes(stmt, 0);
      auto db_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      std::string_view name =
          PushName(std::string_view(db_name, sprite_name_size));
      sprite.name = name;
      sprite.x = sqlite3_column_int(stmt, 1);
      sprite.y = sqlite3_column_int(stmt, 2);
      sprite.spritesheet = filename;
      sprite.width = sqlite3_column_int(stmt, 3);
      sprite.height = sqlite3_column_int(stmt, 4);
      sprites_.Push(sprite);
      sprites_map_.Insert(sprite.name, &sprites_.back());
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
  images_.Push(image);
  images_map_.Insert(filename, &images_.back());
  sqlite3_finalize(stmt);
}

void DbAssets::Trace(unsigned int type, void* p, void* x) {
  if (type == SQLITE_TRACE_PROFILE) {
    auto sql = static_cast<sqlite3_stmt*>(p);
    auto time = reinterpret_cast<long long*>(x);
    LOG("Executing SQL ", sqlite3_expanded_sql(sql), " took ",
        (*time) / 1'000'000.0, " milliseconds");
  }
}

void DbAssets::ReserveBufferForType(std::string_view type, std::size_t count) {
  if (type == "sound") {
    sounds_.Reserve(count);
  } else if (type == "shader") {
    shaders_.Reserve(count);
  } else if (type == "text_file") {
    text_files_.Reserve(count);
  } else if (type == "font") {
    fonts_.Reserve(count);
  } else if (type == "script") {
    scripts_.Reserve(count);
  } else if (type == "spritesheet") {
    spritesheets_.Reserve(count);
  } else if (type == "shader") {
    shaders_.Reserve(count);
  } else if (type == "font") {
    fonts_.Reserve(count);
  } else if (type == "text_file") {
    text_files_.Reserve(count);
  }
}

void DbAssets::Load() {
  sqlite3_trace_v2(db_, SQLITE_TRACE_STMT | SQLITE_TRACE_PROFILE, TraceCallback,
                   this);
  std::size_t total_size = 0, total_names = 0;
  {
    // Presize all the buffers.
    FixedStringBuffer<256> sql(
        "SELECT type, SUM(size), SUM(LENGTH(name)), COUNT(*) FROM "
        "asset_metadata GROUP BY type");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const size_t size_by_type = sqlite3_column_int(stmt, 1);
      const size_t names_by_type = sqlite3_column_int(stmt, 2);
      const size_t count = sqlite3_column_int(stmt, 3);
      total_size += size_by_type;
      total_names += names_by_type;
      // Count the number of null terminators for all strings.
      total_names += count;
      std::string_view type(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
      ReserveBufferForType(type, count);
    }
    sqlite3_finalize(stmt);
  }
  std::size_t total_sprites = 0;
  {
    // Add the length and count of sprite names of all the spritesheets.
    // Also add the length of image names for buffers.
    FixedStringBuffer<256> sql(
        "SELECT SUM(sprite_name_length), SUM(sprites), SUM(LENGTH(image)), "
        "COUNT(*) FROM spritesheets");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    CHECK(sqlite3_step(stmt) == SQLITE_ROW,
          "Could not read asset metadata: ", sqlite3_errmsg(db_));
    total_names += sqlite3_column_int(stmt, 0);
    const std::size_t sprites = sqlite3_column_int(stmt, 1);
    // Count the number of null terminators for all strings.
    total_names += sprites;
    total_sprites += sprites;
    // Count the length of images. Also add one null terminator for image.
    total_names += sqlite3_column_int(stmt, 2);
    total_names += sqlite3_column_int(stmt, 3);
    sqlite3_finalize(stmt);
  }
  sprites_.Reserve(total_sprites);
  name_size_ = 0;
  name_buffer_ =
      reinterpret_cast<char*>(allocator_->Alloc(total_names, /*align=*/1));
  content_size_ = 0;
  content_buffer_ =
      reinterpret_cast<uint8_t*>(allocator_->Alloc(total_size, /*align=*/1));
  struct Loader {
    std::string_view name;
    void (DbAssets::*load)(std::string_view name, uint8_t* buffer,
                           std::size_t size);
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
      "SELECT name, LENGTH(name), type, size FROM asset_metadata ORDER BY "
      "type");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto name_length = sqlite3_column_int(stmt, 1);
    auto type_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    std::string_view type(type_ptr);
    const std::size_t size = sqlite3_column_int(stmt, 3);
    auto* buffer = &content_buffer_[content_size_];
    content_size_ += size;
    std::string_view namestr(name, name_length);
    std::string_view saved_name = PushName(namestr);
    for (const Loader& loader : kLoaders) {
      if (loader.name.empty()) {
        LOG("No loader for asset ", name, " with type ", type);
        break;
      }
      if (type == loader.name) {
        TIMER("Load DB asset ", name);
        auto method = loader.load;
        if (method == nullptr) {
          LOG("While loading ", name, ": unimplemented asset type ", type);
          break;
        }
        (this->*method)(saved_name, buffer, size);
        break;
      }
    }
  }
  sqlite3_finalize(stmt);
}

}  // namespace G
