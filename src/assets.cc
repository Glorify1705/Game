#include "assets.h"

#include <cstring>
#include <string_view>

#include "blob_store.h"
#include "clock.h"
#include "sqlite_helpers.h"
#include "stringlib.h"
#include "units.h"

namespace G {

namespace {

// Reads the blob into buffer and NUL-terminates it, crashing with the asset
// name on failure. Asset blobs referenced by the metadata table must exist.
void ReadBlobOrDie(std::string_view name, uint64_t blob_hash, uint8_t* buffer,
                   size_t size) {
  auto result = ReadBlob(blob_hash, buffer, size);
  CHECK(!result.is_error(), "Failed to read blob for asset ", name, ": ",
        result.error().message());
  buffer[size] = '\0';
}

}  // namespace

void DbAssets::LoadScript(std::string_view filename, uint8_t* buffer,
                          size_t size, ChecksumType checksum,
                          uint64_t blob_hash) {
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  Script script;
  script.name = filename;
  script.contents = buffer;
  script.size = size;
  script.checksum = checksum;
  script_loader_.Load(&script);
}

void DbAssets::LoadFont(std::string_view filename, uint8_t* buffer, size_t size,
                        ChecksumType checksum, uint64_t blob_hash) {
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  Font font;
  font.name = filename;
  font.contents = buffer;
  font.size = size;
  font.checksum = checksum;
  font_loader_.Load(&font);
}

void DbAssets::LoadAudio(std::string_view filename, uint8_t* buffer,
                         size_t size, ChecksumType checksum,
                         uint64_t blob_hash) {
  SqlStmt stmt(db_,
               "SELECT channels, samplerate, samples "
               "FROM audios WHERE name = ?");
  CHECK(stmt.ok(), "Failed to prepare LoadAudio query");
  stmt.BindText(1, filename);
  CHECK(MUST(stmt.Step()), "No audio ", filename);
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  Sound sound;
  sound.name = filename;
  sound.contents = buffer;
  sound.size = size;
  sound.channels = stmt.ColumnInt(0);
  sound.samplerate = stmt.ColumnInt(1);
  sound.samples = stmt.ColumnInt(2);
  sound.checksum = checksum;
  sound_loader_.Load(&sound);
}

void DbAssets::LoadShader(std::string_view filename, uint8_t* buffer,
                          size_t size, ChecksumType checksum,
                          uint64_t blob_hash) {
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  Shader shader;
  shader.name = filename;
  shader.contents = buffer;
  shader.size = size;
  shader.type = HasSuffix(filename, ".vert") ? ShaderType::kVertex
                                             : ShaderType::kFragment;
  shader.checksum = checksum;
  shader_loader_.Load(&shader);
}

void DbAssets::LoadText(std::string_view filename, uint8_t* buffer, size_t size,
                        ChecksumType checksum, uint64_t blob_hash) {
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  TextFile file;
  file.name = filename;
  file.contents = buffer;
  file.size = size;
  file.checksum = checksum;
  text_files_.Push(file);
  text_files_table_.Insert(file.name, &text_files_.back());
}

void DbAssets::LoadProtoDescriptor(std::string_view filename, uint8_t* buffer,
                                   size_t size, ChecksumType checksum,
                                   uint64_t blob_hash) {
  // Protos that failed to compile are recorded with no blob; skip them.
  if (blob_hash == 0) {
    LOG("Skipping proto descriptor ", filename, " with no compiled blob");
    return;
  }
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  ProtoDescriptor desc;
  desc.name = filename;
  desc.contents = buffer;
  desc.size = size;
  desc.checksum = checksum;
  proto_loader_.Load(&desc);
}

void DbAssets::LoadSpritesheet(std::string_view filename, uint8_t* /*buffer*/,
                               size_t /*size*/, ChecksumType checksum,
                               uint64_t /*blob_hash*/) {
  {
    SqlStmt stmt(db_,
                 "SELECT image, width, height FROM spritesheets "
                 "WHERE name = ?");
    CHECK(stmt.ok(), "Failed to prepare LoadSpritesheet query");
    stmt.BindText(1, filename);
    CHECK(MUST(stmt.Step()), "No spritesheet ", filename);
    Spritesheet sheet;
    sheet.name = filename;
    sheet.width = stmt.ColumnInt(1);
    sheet.height = stmt.ColumnInt(2);
    sheet.image = InternedString(stmt.ColumnText(0));
    sheet.checksum = checksum;
    spritesheet_loader_.Load(&sheet);
  }
  {
    SqlStmt stmt(db_,
                 "SELECT name, x, y, width, height FROM sprites "
                 "WHERE spritesheet = ?");
    CHECK(stmt.ok(), "Failed to prepare sprites query");
    stmt.BindText(1, filename);
    while (MUST(stmt.Step())) {
      Sprite sprite;
      auto name = stmt.ColumnText(0);
      sprite.name = InternedString(name);
      sprite.x = stmt.ColumnInt(1);
      sprite.y = stmt.ColumnInt(2);
      sprite.spritesheet = filename;
      sprite.width = stmt.ColumnInt(3);
      sprite.height = stmt.ColumnInt(4);
      sprite_loader_.Load(&sprite);
    }
  }
}

void DbAssets::LoadImage(std::string_view filename, uint8_t* buffer,
                         size_t size, ChecksumType checksum,
                         uint64_t blob_hash) {
  SqlStmt stmt(db_, "SELECT width, height FROM images WHERE name = ?");
  CHECK(stmt.ok(), "Failed to prepare LoadImage query");
  stmt.BindText(1, filename);
  CHECK(MUST(stmt.Step()), "No image ", filename);
  ReadBlobOrDie(filename, blob_hash, buffer, size);
  Image image;
  image.name = filename;
  image.width = stmt.ColumnInt(0);
  image.height = stmt.ColumnInt(1);
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

DbAssets::ChecksumType DbAssets::GetChecksum(std::string_view asset) {
  return checksums_map_.LookupOrDie(asset)->checksum;
}

void DbAssets::Load() {
  struct Loader {
    std::string_view name;
    void (DbAssets::*load)(std::string_view, uint8_t*, size_t, ChecksumType,
                           uint64_t);
  };
  static constexpr Loader kLoaders[] = {
      {.name = "script", .load = &DbAssets::LoadScript},
      {.name = "spritesheet", .load = &DbAssets::LoadSpritesheet},
      {.name = "image", .load = &DbAssets::LoadImage},
      {.name = "audio", .load = &DbAssets::LoadAudio},
      {.name = "font", .load = &DbAssets::LoadFont},
      {.name = "shader", .load = &DbAssets::LoadShader},
      {.name = "text", .load = &DbAssets::LoadText},
      {.name = "proto", .load = &DbAssets::LoadProtoDescriptor},
      {.name = std::string_view(), .load = nullptr},
  };
  SqlStmt stmt(db_,
               "SELECT name, type, size, hash, blob_hash FROM "
               "asset_metadata ORDER BY processing_order, type");
  CHECK(stmt.ok(), "Failed to prepare asset_metadata query");
  while (MUST(stmt.Step())) {
    auto name = stmt.ColumnText(0);
    const ChecksumType db_checksum = stmt.ColumnInt64(3);
    Checksum* saved_checksum;
    if (checksums_map_.Lookup(name, &saved_checksum) &&
        !std::memcmp(&saved_checksum->checksum, &db_checksum,
                     sizeof(db_checksum))) {
      continue;
    }
    TIMER("Loading asset ", name);
    auto type = stmt.ColumnText(1);
    const size_t size = stmt.ColumnInt64(2);
    const uint64_t blob_hash = static_cast<uint64_t>(stmt.ColumnInt64(4));
    std::string_view saved_name = InternedString(name);
    auto* buf =
        reinterpret_cast<uint8_t*>(allocator_->Alloc(size + 1, /*align=*/16));
    CHECK(buf != nullptr, "Failed to allocate bytes for asset");
    for (const Loader& loader : kLoaders) {
      if (loader.name.empty()) {
        LOG("No loader for asset ", name, " with type ", type);
        break;
      }
      if (type != loader.name) continue;
      auto method = loader.load;
      if (method == nullptr) {
        LOG("While loading ", name, ": unimplemented asset type ", type);
        break;
      }
      (this->*method)(saved_name, buf, size, db_checksum, blob_hash);
      Checksum c;
      c.asset = saved_name;
      std::memcpy(&c.checksum, &db_checksum, sizeof(db_checksum));
      checksums_.Push(c);
      checksums_map_.Insert(saved_name, &checksums_.back());
      break;
    }
  }
}

DbAssets::TextFile* DbAssets::LookupTextFile(std::string_view name) {
  TextFile* result = nullptr;
  text_files_table_.Lookup(name, &result);
  return result;
}

}  // namespace G
