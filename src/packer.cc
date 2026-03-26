#include "packer.h"

#include "debug_font.h"
#include "defer.h"
#include "filesystem.h"
#include "image.h"
#include "libraries/dr_wav.h"
#include "libraries/json.h"
#include "libraries/pugixml.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_vorbis.h"
#include "physfs.h"
#include "qoa.h"
#include "schema.sql.h"
#include "src/allocators.h"
#include "src/stringlib.h"
#include "src/units.h"

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
      : db_(db),
        allocator_(allocator),
        scratch_(allocator, Megabytes(64)),
        checksums_(allocator) {}

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
    QoiDecode(buf, size, &desc, /*channels=*/4, allocator_);
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
    desc.colorspace = QoiColorspace::kLinear;
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

  AssetInfo InsertQoa(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    QoaDesc desc;
    ByteSlice data(buf, size);
    FixedArray<int16_t> decoded = QoaDecode(data, &desc, allocator_);
    if (decoded.size() == 0) {
      DIE("Failed to decode QOA file ", filename);
    }
    return InsertAudioBlob(filename, buf, size, desc);
  }

  // TODO: Use Slice instead of buf + size in the packer.
  AssetInfo InsertWav(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    drwav wav;
    if (!drwav_init_memory(&wav, buf, size, /*pAllocationCallbacks=*/nullptr)) {
      DIE("Failed to decode WAV file ", filename);
    }
    DEFER([&] { drwav_uninit(&wav); });

    size_t total_frames = wav.totalPCMFrameCount;
    size_t channels = wav.channels;
    size_t total_samples = total_frames * channels;

    auto* pcm = static_cast<int16_t*>(
        scratch_.Alloc(total_samples * sizeof(int16_t), alignof(int16_t)));
    drwav_read_pcm_frames_s16(&wav, total_frames, pcm);

    QoaDesc desc;
    desc.channels = channels;
    desc.samplerate = wav.sampleRate;
    desc.samples = total_frames;

    Slice<int16_t> samples(pcm, total_samples);
    FixedArray<uint8_t> encoded = QoaEncode(samples, &desc, allocator_);
    DCHECK(encoded.size() > 0, "Failed to encode ", filename, " to QOA");

    return InsertAudioBlob(filename, encoded.cdata(), encoded.size(), desc);
  }

  AssetInfo InsertOgg(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    int error;
    stb_vorbis* v = stb_vorbis_open_memory(buf, size, &error, nullptr);
    if (v == nullptr) {
      DIE("Failed to open OGG file ", filename, " (error ", error, ")");
    }
    DEFER([&] { stb_vorbis_close(v); });

    stb_vorbis_info info = stb_vorbis_get_info(v);
    unsigned int total_frames = stb_vorbis_stream_length_in_samples(v);
    CHECK(total_frames > 0, "Failed to get OGG stream length for ", filename);

    size_t total_samples = static_cast<size_t>(total_frames) * info.channels;
    auto* pcm = scratch_.NewArray<int16_t>(total_samples);
    CHECK(pcm != nullptr, "Failed to allocate PCM buffer for ", filename);

    stb_vorbis_get_samples_short_interleaved(v, info.channels, pcm,
                                             static_cast<int>(total_samples));

    QoaDesc desc;
    desc.channels = info.channels;
    desc.samplerate = info.sample_rate;
    desc.samples = total_frames;

    Slice<int16_t> samples(pcm, total_samples);
    FixedArray<uint8_t> encoded = QoaEncode(samples, &desc, allocator_);
    DCHECK(encoded.size() > 0, "Failed to encode ", filename, " to QOA");

    return InsertAudioBlob(filename, encoded.cdata(), encoded.size(), desc);
  }

  AssetInfo InsertAudioBlob(std::string_view filename, const uint8_t* buf,
                            size_t size, const QoaDesc& desc) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO audios (name, channels, samplerate, samples, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) == SQLITE_OK,
          "Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, desc.channels);
    sqlite3_bind_int(stmt, 3, desc.samplerate);
    sqlite3_bind_int(stmt, 4, desc.samples);
    sqlite3_bind_blob(stmt, 5, buf, size, SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE, "Could not insert data ",
          sqlite3_errmsg(db_));
    return AssetInfo{.size = size};
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
    sqlite3_bind_text(stmt, 2, image.data(), image.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, width);
    sqlite3_bind_int(stmt, 4, height);
    sqlite3_bind_int(stmt, 5, sprite_count);
    sqlite3_bind_int(stmt, 6, sprite_name_length);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
  }

  AssetInfo InsertSpritesheetXml(std::string_view filename, const uint8_t* buf,
                                 size_t size) {
    pugi::xml_document doc;
    // TODO: change signatures so we pass uint8_t*.
    doc.load_buffer_inplace(const_cast<uint8_t*>(buf), size);

    pugi::xml_node atlas = doc.child("TextureAtlas");

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
    for (auto& sprite : atlas.children("SubTexture")) {
      sprite_count++;

      std::string name = sprite.attribute("name").as_string();
      sprite_name_length += name.length();

      const uint32_t x = sprite.attribute("x").as_int();
      const uint32_t y = sprite.attribute("y").as_int();
      const uint32_t w = sprite.attribute("width").as_int();
      const uint32_t h = sprite.attribute("height").as_int();

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
    std::string atlas_image = atlas.attribute("imagePath").as_string();
    // Width and height are not included in texture atlas, you need to get them
    // from the image.
    const int64_t width = atlas.attribute("width").as_int();
    const int64_t height = atlas.attribute("height").as_int();
    InsertSpritesheetEntry(filename, width, height, sprite_count,
                           sprite_name_length, atlas_image.c_str());
    return {};
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

      std::string name = sprite["name"].getString();
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

  int GetOrderForType(std::string_view type) {
    // We need to process all images, then all spritesheets, then all scripts,
    // then the rest.
    if (type == "image") {
      return 0;
    }
    if (type == "spritesheets") {
      return 1;
    }
    // We sort by type as well in the asset loader, so here we can return 2.
    return 2;
  }

  void InsertIntoAssetMeta(std::string_view filename, size_t size,
                           std::string_view type, DbAssets::ChecksumType hash) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(
        "INSERT OR REPLACE INTO asset_metadata (name, size, type, hash, "
        "processing_order) "
        "VALUES (?, ?, ?, ?, ?);");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, size);
    sqlite3_bind_text(stmt, 3, type.data(), type.size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, hash);
    sqlite3_bind_int64(stmt, 5, GetOrderForType(type));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
  }

  void HandleFile(const char* directory, const char* filename) {
    // Skip directories (e.g. "definitions/") — they are not asset files.
    FixedStringBuffer<kMaxPathLength> full_path(directory, "/", filename);
    PHYSFS_Stat stat;
    if (PHYSFS_stat(full_path.str(), &stat) &&
        stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
      return;
    }

    struct DbHandler {
      std::string_view extension;
      AssetInfo (DbPacker::*handler)(std::string_view filename,
                                     const uint8_t* buf, size_t size);
      std::string_view type;
    };

    static constexpr DbHandler kHandlers[] = {
        {".lua", &DbPacker::InsertScript, "script"},
        {".fnl", &DbPacker::InsertScript, "script"},
        {".qoi", &DbPacker::InsertQoi, "image"},
        {".png", &DbPacker::InsertPng, "image"},
        {".sprites.json", &DbPacker::InsertSpritesheetJson, "spritesheet"},
        {".sprites.xml", &DbPacker::InsertSpritesheetXml, "spritesheet"},
        {".qoa", &DbPacker::InsertQoa, "audio"},
        {".ogg", &DbPacker::InsertOgg, "audio"},
        {".ttf", &DbPacker::InsertFont, "font"},
        {".wav", &DbPacker::InsertWav, "audio"},
        {".vert", &DbPacker::InsertShader, "shader"},
        {".frag", &DbPacker::InsertShader, "shader"},
        {".json", &DbPacker::InsertTextFile, "text"},
        {".txt", &DbPacker::InsertTextFile, "text"}};

    FixedStringBuffer<kMaxPathLength> path(directory, "/", filename);

    std::string_view fname = Basename(filename);
    bool handled = false;

    for (const DbHandler& handler : kHandlers) {
      if (!HasSuffix(filename, handler.extension)) {
        continue;
      }
      scratch_.Reset();
      auto* handle = PHYSFS_openRead(path.str());
      CHECK(handle != nullptr, "Could not read ", path, ": ",
            PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      const size_t bytes = PHYSFS_fileLength(handle);
      auto* buffer = static_cast<uint8_t*>(scratch_.Alloc(bytes, kMaxAlign));
      CHECK(buffer != nullptr);
      const size_t read_bytes = PHYSFS_readBytes(handle, buffer, bytes);
      CHECK(read_bytes == bytes, " failed to read ", path,
            " error = ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      CHECK(PHYSFS_close(handle), "failed to finish reading ", filename, ": ",
            PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
      auto method = handler.handler;
      const auto hash = rapidhash(buffer, bytes);
      DbAssets::ChecksumType saved;
      if (checksums_.Lookup(filename, &saved) &&
          !std::memcmp(&saved, &hash, sizeof(hash))) {
        handled = true;
        break;
      }
      LOG("Processing file ", fname);
      const AssetInfo info = (this->*method)(fname, buffer, bytes);
      InsertIntoAssetMeta(fname, info.size, handler.type, hash);
      result_.written_files++;
      handled = true;
      LOG("Finished processing file ", fname);
      break;
    }
    if (!handled) {
      LOG("No handler for file ", filename, ". ignoring");
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
    FixedStringBuffer<256> sql("SELECT name, hash FROM asset_metadata");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      const auto hash = sqlite3_column_int64(stmt, 1);
      checksums_.Insert(name, hash);
    }
  }

  AssetWriteResult HandleFiles() {
    PHYSFS_enumerate("/assets", WriteFileToDb, this);
    // Ensure we always have the debug font available.
    if (!checksums_.Contains("debug_font.ttf")) {
      InsertFont("debug_font.ttf", kProggyCleanFont, kProggyCleanFontLength);
      const auto hash = rapidhash(kProggyCleanFont, kProggyCleanFontLength);
      InsertIntoAssetMeta("debug_font.ttf", kProggyCleanFontLength, "font",
                          hash);
    }
    // Handle missing dimensions from TextureAtlas.
    {
      sqlite3_exec(db_, R"(
        UPDATE spritesheets
        SET width = i.w, height = i.h
        FROM (SELECT s.id, i.width as w, i.height as h 
          FROM spritesheets s INNER JOIN images i ON s.image = i.name) AS i
        WHERE spritesheets.id = i.id AND (spritesheets.width = 0 OR spritesheets.height = 0);
      )",
                   nullptr, nullptr, nullptr);
    }
    return result_;
  }

 private:
  sqlite3* db_ = nullptr;
  Allocator* allocator_ = nullptr;
  ArenaAllocator scratch_;
  Dictionary<DbAssets::ChecksumType> checksums_;
  AssetWriteResult result_;
};

}  // namespace

ErrorOr<DbAssets*> ReadAssetsFromDb(sqlite3* db, Allocator* allocator,
                                    Allocator* asset_allocator) {
  auto* result = allocator->New<DbAssets>(db, asset_allocator);
  result->Load();
  return result;
}

ErrorOr<AssetWriteResult> WriteAssetsToDb(const char* source_directory,
                                          sqlite3* db, Allocator* allocator) {
  if (!PHYSFS_mount(source_directory, "/assets", 1)) {
    LOG("Failed to mount directory ", source_directory, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to mount asset directory");
  }
  sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
  DbPacker packer(db, allocator);
  packer.LoadChecksums();
  auto result = packer.HandleFiles();
  sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
  return result;
}

void InitializeAssetDb(sqlite3* db) {
  LOG("Reloading schema");
  char* err;
  CHECK(sqlite3_exec(db, kSqlSchema, nullptr, nullptr, &err) == SQLITE_OK,
        "Failed to initialize schema: ", err);
}

}  // namespace G
