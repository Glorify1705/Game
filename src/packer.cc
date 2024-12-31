#include "packer.h"

#include "clock.h"
#include "debug_font.h"
#include "defer.h"
#include "filesystem.h"
#include "libraries/json.h"
#include "libraries/sqlite3.h"
#include "lua.h"
#include "physfs.h"
#include "src/allocators.h"
#include "src/strings.h"
#include "src/units.h"
#include "xxhash.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_FAILURE_USERMSG
#define STBI_LOG(...) LOG(__VA_ARGS__)
#include "libraries/stb_image.h"

namespace G {
namespace {

class DbPacker {
 public:
  struct AssetInfo {
    size_t size;
  };

  explicit DbPacker(sqlite3* db, Allocator* allocator)
      : db_(db), allocator_(allocator), checksums_(allocator) {}

  AssetInfo InsertIntoTable(std::string_view table, std::string_view filename,
                            const uint8_t* buf, size_t size) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql("INSERT OR REPLACE INTO ", table,
                               " (name, contents) VALUES (?, ?);");
    CHECK(sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) == SQLITE_OK,
          "Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, buf, size, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }

    return AssetInfo{.size = size};
  }

  AssetInfo InsertScript(std::string_view filename, const uint8_t* buf,
                         size_t size) {
    return InsertIntoTable("scripts", filename, buf, size);
  }

  AssetInfo InsertFont(std::string_view filename, const uint8_t* buf,
                       size_t size) {
    return InsertIntoTable("fonts", filename, buf, size);
  }

  AssetInfo InsertQoi(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    QoiDesc desc;
    QoiDecode(buf, size, &desc, /*components=*/4, allocator_);
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO images (name, width, height, components, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) == SQLITE_OK,
          "Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, desc.width);
    sqlite3_bind_int(stmt, 3, desc.height);
    sqlite3_bind_int(stmt, 4, desc.channels);
    sqlite3_bind_blob(stmt, 5, buf, size, SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE, "Could not insert data ",
          sqlite3_errmsg(db_));
    return AssetInfo{.size = size};
  }

  AssetInfo InsertPng(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    int x, y, channels;
    auto* contents = stbi_load_from_memory(buf, size, &x, &y, &channels,
                                           /*desired_channels=*/0);
    DCHECK(contents != nullptr, "Could not load ", filename, ": ",
           stbi_failure_reason());
    DEFER([&] { stbi_image_free(contents); });
    QoiDesc desc;
    desc.width = x;
    desc.height = y;
    desc.channels = channels;
    desc.colorspace = 1;
    int out_len;
    auto* qoi_encoded = QoiEncode(contents, &desc, &out_len, allocator_);
    DCHECK(qoi_encoded != nullptr);
    DEFER([&] { allocator_->Dealloc(qoi_encoded, out_len); });
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO images (name, width, height, components, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) == SQLITE_OK,
          "Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, y);
    sqlite3_bind_int(stmt, 4, channels);
    sqlite3_bind_blob(stmt, 5, qoi_encoded, out_len, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
    return AssetInfo{.size = static_cast<size_t>(out_len)};
  }

  AssetInfo InsertAudio(std::string_view filename, const uint8_t* buf,
                        size_t size) {
    return InsertIntoTable("audios", filename, buf, size);
  }

  AssetInfo InsertTextFile(std::string_view filename, const uint8_t* buf,
                           size_t size) {
    return InsertIntoTable("text_files", filename, buf, size);
  }

  void* Alloc(void* ptr, size_t osize, size_t nsize) {
    if (nsize == 0) {
      if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
      return nullptr;
    }
    if (ptr == nullptr) {
      return allocator_->Alloc(nsize, /*align=*/1);
    }
    return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
  }

  static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<DbPacker*>(ud)->Alloc(ptr, osize, nsize);
  }

  void InsertSpritesheetEntry(std::string_view spritesheet, int width,
                              int height, size_t sprite_count,
                              size_t sprite_name_length,
                              std::string_view image) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE 
          INTO spritesheets (name, image, width, height, sprites, sprite_name_length)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, spritesheet.data(), spritesheet.size(),
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, image.data(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, width);
    sqlite3_bind_int(stmt, 4, height);
    sqlite3_bind_int(stmt, 5, sprite_count);
    sqlite3_bind_int(stmt, 6, sprite_name_length);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
  }

  AssetInfo InsertSpritesheetJson(std::string_view filename, const uint8_t* buf,
                                  size_t size) {
    std::string_view buffer(reinterpret_cast<const char*>(buf), size);

    auto [status, json] = jt::Json::parse(
        std::string_view(reinterpret_cast<const char*>(buf), size));
    CHECK(status == jt::Json::success, "failed to parse ", filename, ": ",
          jt::Json::StatusToString(status));
    CHECK(json.isObject(),
          "invalid spritesheet format, must return a json object");
    // Insert sprites.
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO sprites (name, spritesheet, x, y, width, height)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    CHECK(sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) == SQLITE_OK,
          "Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    DEFER([&] { sqlite3_finalize(stmt); });

    size_t sprite_count = 0, sprite_name_length = 0;
    for (auto& sprite : json["sprites"].getArray()) {
      sprite_count++;

      std::string name = sprite["name"];
      sprite_name_length += name.length();

      const uint32_t x = sprite["x"].getLong();
      const uint32_t y = sprite["y"].getLong();
      const uint32_t w = sprite["width"].getLong();
      const uint32_t h = sprite["height"].getLong();

      sqlite3_bind_text(stmt, 1, name.data(), name.length(), SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, filename.data(), filename.size(),
                        SQLITE_STATIC);
      sqlite3_bind_int(stmt, 3, x);
      sqlite3_bind_int(stmt, 4, y);
      sqlite3_bind_int(stmt, 5, w);
      sqlite3_bind_int(stmt, 6, h);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        DIE("Could not insert data for ", name, " in ", filename, ": ",
            sqlite3_errmsg(db_));
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
    std::string atlas = json["atlas"].getString();
    const int64_t width = json["width"].getLong();
    const int64_t height = json["height"].getLong();
    InsertSpritesheetEntry(filename, width, height, sprite_count,
                           sprite_name_length, atlas.c_str());
    return {};
  }

  AssetInfo InsertShader(std::string_view filename, const uint8_t* buffer,
                         size_t size) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(
        "INSERT OR REPLACE INTO shaders"
        " (name, contents, shader_type) VALUES (?, ?, ?);");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, buffer, size, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3,
                      HasSuffix(filename, "vert") ? "vertex" : "fragment", -1,
                      SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE, "Could not insert data for ",
          filename, ": ", sqlite3_errmsg(db_));
    return AssetInfo{.size = size};
  }

  void InsertIntoAssetMeta(std::string_view filename, size_t size,
                           std::string_view type, XXH128_hash_t hash) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(
        "INSERT OR REPLACE INTO asset_metadata (name, size, type, hash_low, "
        "hash_high) VALUES (?, ?, ?, ?, ?);");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, size);
    sqlite3_bind_text(stmt, 3, type.data(), type.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, hash.low64);
    sqlite3_bind_int(stmt, 5, hash.high64);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
  }

  void HandleFile(const char* directory, const char* filename) {
    struct DbHandler {
      std::string_view extension;
      AssetInfo (DbPacker::*handler)(std::string_view filename,
                                     const uint8_t* buf, size_t size);
      std::string_view type;
    };
    FixedStringBuffer<kMaxPathLength> path(directory, "/", filename);

    static constexpr DbHandler kHandlers[] = {
        {".lua", &DbPacker::InsertScript, "script"},
        {".fnl", &DbPacker::InsertScript, "script"},
        {".qoi", &DbPacker::InsertQoi, "image"},
        {".png", &DbPacker::InsertPng, "image"},
        {".sprites.json", &DbPacker::InsertSpritesheetJson, "spritesheet"},
        {".ogg", &DbPacker::InsertAudio, "audio"},
        {".ttf", &DbPacker::InsertFont, "font"},
        {".wav", &DbPacker::InsertAudio, "audio"},
        {".vert", &DbPacker::InsertShader, "shader"},
        {".frag", &DbPacker::InsertShader, "shader"},
        {".json", &DbPacker::InsertTextFile, "text"},
        {".txt", &DbPacker::InsertTextFile, "text"}};

    std::string_view fname = Basename(filename);
    bool handled = false;
    ArenaAllocator scratch(allocator_, Megabytes(128));

    for (const DbHandler& handler : kHandlers) {
      if (!HasSuffix(filename, handler.extension)) {
        continue;
      }
      scratch.Reset();
      TIMER("Handling database file ", path);
      auto* handle = PHYSFS_openRead(path.str());
      CHECK(handle != nullptr, "Could not read ", path, ": ",
            PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      const size_t bytes = PHYSFS_fileLength(handle);
      auto* buffer = static_cast<uint8_t*>(scratch.Alloc(bytes, /*align=*/1));
      const size_t read_bytes = PHYSFS_readBytes(handle, buffer, bytes);
      CHECK(read_bytes == bytes, " failed to read ", path,
            " error = ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      CHECK(PHYSFS_close(handle), "failed to finish reading ", filename, ": ",
            PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      auto method = handler.handler;
      const XXH128_hash_t hash = XXH3_128bits(buffer, bytes);
      XXH128_hash_t saved;
      if (checksums_.Lookup(filename, &saved) &&
          !std::memcmp(&saved, &hash, sizeof(hash))) {
        LOG("Skipping loading ", filename, " because file is the same");
        handled = true;
        break;
      }
      const AssetInfo info = (this->*method)(fname, buffer, bytes);
      InsertIntoAssetMeta(fname, info.size, handler.type, hash);
      scratch.Dealloc(buffer, bytes);
      handled = true;
      break;
    }
    if (!handled) {
      LOG("No handler for file ", filename);
    }
  }

  static PHYSFS_EnumerateCallbackResult WriteFileToDb(void* ud,
                                                      const char* dirname,
                                                      const char* filename) {
    auto* db_handler = static_cast<DbPacker*>(ud);
    db_handler->HandleFile(dirname, filename);
    return PHYSFS_ENUM_OK;
  }

  void LoadChecksums() {
    // Load all checksums to avoid reprocessing files that have not changed.
    FixedStringBuffer<256> sql(
        "SELECT name, hash_low, hash_high FROM asset_metadata");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      int64_t hash_low = sqlite3_column_int64(stmt, 1);
      int64_t hash_high = sqlite3_column_int64(stmt, 1);
      XXH128_hash_t hash;
      hash.low64 = hash_low;
      hash.high64 = hash_high;
      checksums_.Insert(name, hash);
    }
  }

  void HandleFiles() {
    PHYSFS_enumerate("/assets", WriteFileToDb, this);
    // Ensure we always have the debug font available.
    InsertFont("debug_font.ttf", kProggyCleanFont, kProggyCleanFontLength);
    const XXH128_hash_t hash =
        XXH3_128bits(kProggyCleanFont, kProggyCleanFontLength);
    InsertIntoAssetMeta("debug_font.ttf", kProggyCleanFontLength, "font", hash);
  }

 private:
  sqlite3* db_ = nullptr;
  Allocator* allocator_ = nullptr;
  Dictionary<XXH128_hash_t> checksums_;
};

}  // namespace

DbAssets* ReadAssetsFromDb(sqlite3* db, Allocator* allocator) {
  auto* result = New<DbAssets>(allocator, db, allocator);
  result->Load();
  return result;
}

void WriteAssetsToDb(const char* source_directory, sqlite3* db,
                     Allocator* allocator) {
  PHYSFS_CHECK(PHYSFS_mount(source_directory, "/assets", 1),
               " while trying to mount directory ", source_directory);
  sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
  DbPacker packer(db, allocator);
  packer.LoadChecksums();
  packer.HandleFiles();
  sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

}  // namespace G
