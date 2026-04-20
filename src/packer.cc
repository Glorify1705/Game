#include "packer.h"

#include <SDL3/SDL.h>

#include <atomic>

#include "clock.h"
#include "debug_font.h"
#include "defer.h"
#include "filesystem.h"
#include "image.h"
#include "json_alc.h"
#include "libraries/dr_wav.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_vorbis.h"
#include "libraries/yyjson.h"
#include "physfs.h"
#include "qoa.h"
#include "schema.sql.h"
#include "sqlite_helpers.h"
#include "src/allocators.h"
#include "src/executor.h"
#include "src/stringlib.h"
#include "src/units.h"
#include "xml.h"

// stb_image implementation lives in libraries/stb_image_all.cc with all
// formats enabled (used by both the packer and the asset conversion tools).
#include "libraries/stb_image.h"

namespace G {
namespace {

// Thread-safe bump allocator for shared output memory across worker threads.
// Uses atomic CAS on the position pointer so multiple threads can allocate
// concurrently without a mutex.  Dealloc is a no-op; the entire buffer is
// freed when the parent arena is released.
class BumpAllocator : public Allocator {
 public:
  BumpAllocator(uint8_t* buffer, size_t size) {
    pos_.store(reinterpret_cast<uintptr_t>(buffer), std::memory_order_relaxed);
    end_ = reinterpret_cast<uintptr_t>(buffer) + size;
  }

  void* Alloc(size_t size, size_t /*align*/) override {
    size = Align(size, kMaxAlign);
    uintptr_t pos = pos_.load(std::memory_order_relaxed);
    while (true) {
      uintptr_t next = pos + size;
      if (next > end_) return nullptr;
      if (pos_.compare_exchange_weak(pos, next, std::memory_order_relaxed))
        return reinterpret_cast<void*>(pos);
    }
  }

  void Dealloc(void*, size_t) override {}

  void* Realloc(void* p, size_t old_size, size_t new_size,
                size_t align) override {
    auto* result = Alloc(new_size, align);
    if (result && p) std::memcpy(result, p, old_size);
    return result;
  }

 private:
  std::atomic<uintptr_t> pos_;
  uintptr_t end_;
};

constexpr size_t kScratchArenaSize = Megabytes(64);
constexpr size_t kOutputArenaSize = Megabytes(256);

struct WorkItem {
  enum Type : uint8_t { kPng, kOgg, kWav };
  Type type;
  char filename[256];
  size_t filename_len;
  uint8_t* input;
  size_t input_size;
  uint64_t hash;

  uint8_t* output = nullptr;
  size_t output_size = 0;
  int width = 0, height = 0, channels = 0;
  QoaDesc qoa_desc = {};

  std::string_view name() const { return {filename, filename_len}; }
};

struct WorkerContext {
  WorkItem* items;
  size_t count;
  std::atomic<size_t> next;
  uint8_t** scratch_bufs;
  BumpAllocator* output_arena;
  std::atomic<size_t> thread_id_counter;
};

void ProcessWorkItems(WorkerContext* ctx) {
  size_t thread_idx =
      ctx->thread_id_counter.fetch_add(1, std::memory_order_relaxed);
  ArenaAllocator scratch(ctx->scratch_bufs[thread_idx], kScratchArenaSize);

  while (true) {
    size_t idx = ctx->next.fetch_add(1, std::memory_order_relaxed);
    if (idx >= ctx->count) break;
    WorkItem& item = ctx->items[idx];
    scratch.Reset();

    switch (item.type) {
      case WorkItem::kPng: {
        auto* pixels = stbi_load_from_memory(
            item.input, static_cast<int>(item.input_size), &item.width,
            &item.height, &item.channels, /*desired_channels=*/0);
        CHECK(pixels != nullptr, "Failed to load ", item.name());
        DEFER([&] { stbi_image_free(pixels); });
        QoiDesc desc;
        desc.width = item.width;
        desc.height = item.height;
        desc.channels = item.channels;
        desc.colorspace = QoiColorspace::kLinear;
        int out_len;
        item.output = static_cast<uint8_t*>(
            QoiEncode(pixels, &desc, &out_len, ctx->output_arena));
        CHECK(item.output != nullptr, "Failed to encode ", item.name());
        item.output_size = static_cast<size_t>(out_len);
        break;
      }
      case WorkItem::kOgg: {
        int error;
        stb_vorbis* v = stb_vorbis_open_memory(
            item.input, static_cast<int>(item.input_size), &error, nullptr);
        CHECK(v != nullptr, "Failed to open OGG ", item.name());
        DEFER([&] { stb_vorbis_close(v); });
        stb_vorbis_info info = stb_vorbis_get_info(v);
        unsigned int total_frames = stb_vorbis_stream_length_in_samples(v);
        CHECK(total_frames > 0, "Empty OGG ", item.name());
        size_t total_samples =
            static_cast<size_t>(total_frames) * info.channels;
        auto* pcm = scratch.NewArray<int16_t>(total_samples);
        stb_vorbis_get_samples_short_interleaved(
            v, info.channels, pcm, static_cast<int>(total_samples));
        item.qoa_desc = {static_cast<uint32_t>(info.channels), info.sample_rate,
                         total_frames};
        Slice<int16_t> samples(pcm, total_samples);
        FixedArray<uint8_t> encoded =
            QoaEncode(samples, &item.qoa_desc, ctx->output_arena);
        DCHECK(!encoded.empty(), "Failed to encode ", item.name());
        item.output = encoded.data();
        item.output_size = encoded.size();
        break;
      }
      case WorkItem::kWav: {
        drwav wav;
        CHECK(drwav_init_memory(&wav, item.input, item.input_size, nullptr),
              "Failed to decode WAV ", item.name());
        DEFER([&] { drwav_uninit(&wav); });
        size_t total_frames = wav.totalPCMFrameCount;
        size_t ch = wav.channels;
        size_t total_samples = total_frames * ch;
        auto* pcm = scratch.NewArray<int16_t>(total_samples);
        drwav_read_pcm_frames_s16(&wav, total_frames, pcm);
        item.qoa_desc = {static_cast<uint32_t>(ch), wav.sampleRate,
                         static_cast<uint32_t>(total_frames)};
        Slice<int16_t> samples(pcm, total_samples);
        FixedArray<uint8_t> encoded =
            QoaEncode(samples, &item.qoa_desc, ctx->output_arena);
        DCHECK(!encoded.empty(), "Failed to encode ", item.name());
        item.output = encoded.data();
        item.output_size = encoded.size();
        break;
      }
    }
  }
}

class DbPacker {
 public:
  struct AssetInfo {
    size_t size;
  };

  DbPacker(sqlite3* db, Allocator* allocator, Executor* executor)
      : db_(db),
        allocator_(allocator),
        executor_(executor),
        scratch_(allocator, Megabytes(64)),
        checksums_(allocator),
        deferred_(allocator) {}

  AssetInfo InsertIntoTable(std::string_view table, std::string_view filename,
                            const uint8_t* buf, size_t size) {
    FixedStringBuffer<256> sql("INSERT OR REPLACE INTO ", table,
                               " (name, contents) VALUES (?, ?);");
    SqlStmt stmt(db_, sql.view());
    CHECK(stmt.ok(), "Failed to prepare statement ", sql.view());
    stmt.BindText(1, filename);
    stmt.BindBlob(2, buf, size);
    MUST(stmt.Step());
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
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE INTO images (name, width, height, components, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare image insert statement");
    stmt.BindText(1, filename);
    stmt.BindInt(2, desc.width);
    stmt.BindInt(3, desc.height);
    stmt.BindInt(4, desc.channels);
    stmt.BindBlob(5, buf, size);
    MUST(stmt.Step());
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
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE INTO images (name, width, height, components, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare image insert statement");
    stmt.BindText(1, filename);
    stmt.BindInt(2, x);
    stmt.BindInt(3, y);
    stmt.BindInt(4, channels);
    stmt.BindBlob(5, qoi_encoded, out_len);
    MUST(stmt.Step());
    return AssetInfo{.size = static_cast<size_t>(out_len)};
  }

  AssetInfo InsertImageBlob(std::string_view filename, const uint8_t* buf,
                            size_t size, int width, int height, int channels) {
    SqlStmt stmt(db_, R"(
      INSERT OR REPLACE INTO images (name, width, height, components, contents)
      VALUES (?, ?, ?, ?, ?);
    )");
    CHECK(stmt.ok(), "Failed to prepare image insert statement");
    stmt.BindText(1, filename);
    stmt.BindInt(2, width);
    stmt.BindInt(3, height);
    stmt.BindInt(4, channels);
    stmt.BindBlob(5, buf, size);
    MUST(stmt.Step());
    return AssetInfo{.size = size};
  }

  AssetInfo InsertQoa(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    QoaDesc desc;
    ByteSlice data(buf, size);
    FixedArray<int16_t> decoded = QoaDecode(data, &desc, allocator_);
    if (decoded.empty()) {
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
    DCHECK(!encoded.empty(), "Failed to encode ", filename, " to QOA");

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
    DCHECK(!encoded.empty(), "Failed to encode ", filename, " to QOA");

    return InsertAudioBlob(filename, encoded.cdata(), encoded.size(), desc);
  }

  AssetInfo InsertAudioBlob(std::string_view filename, const uint8_t* buf,
                            size_t size, const QoaDesc& desc) {
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE INTO audios (name, channels, samplerate, samples, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare audio insert statement");
    stmt.BindText(1, filename);
    stmt.BindInt(2, desc.channels);
    stmt.BindInt(3, desc.samplerate);
    stmt.BindInt(4, desc.samples);
    stmt.BindBlob(5, buf, size);
    MUST(stmt.Step());
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
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE
          INTO spritesheets (name, image, width, height, sprites, sprite_name_length)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare spritesheet insert statement");
    stmt.BindText(1, spritesheet);
    stmt.BindText(2, image);
    stmt.BindInt(3, width);
    stmt.BindInt(4, height);
    stmt.BindInt(5, sprite_count);
    stmt.BindInt(6, sprite_name_length);
    MUST(stmt.Step());
  }

  AssetInfo InsertSpritesheetXml(std::string_view filename, const uint8_t* buf,
                                 size_t size) {
    std::string_view xml(reinterpret_cast<const char*>(buf), size);
    ArenaAllocator scratch(allocator_, Kilobytes(64));
    XmlElement* atlas = MUST(ParseXml(xml, &scratch));
    CHECK(atlas->tag == "TextureAtlas", "Expected <TextureAtlas>, got <",
          atlas->tag, "> in ", filename);

    // Insert sprites.
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE INTO sprites (name, spritesheet, x, y, width, height)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare sprite insert statement");

    size_t sprite_count = 0, sprite_name_length = 0;
    atlas->ForEachChild("SubTexture", [&](const XmlElement& sprite) {
      sprite_count++;
      std::string_view name = sprite.Attr("name");
      sprite_name_length += name.size();

      const int x = sprite.AttrInt("x");
      const int y = sprite.AttrInt("y");
      const int w = sprite.AttrInt("width");
      const int h = sprite.AttrInt("height");

      stmt.BindTextTransient(1, name);
      stmt.BindText(2, filename);
      stmt.BindInt(3, x);
      stmt.BindInt(4, y);
      stmt.BindInt(5, w);
      stmt.BindInt(6, h);
      MUST(stmt.Step());
      stmt.Reset();
    });
    // Width and height are not included in texture atlas, you need to get them
    // from the image.
    const int64_t width = atlas->AttrInt("width");
    const int64_t height = atlas->AttrInt("height");
    InsertSpritesheetEntry(filename, width, height, sprite_count,
                           sprite_name_length, atlas->Attr("imagePath"));
    return {};
  }

  AssetInfo InsertSpritesheetJson(std::string_view filename, const uint8_t* buf,
                                  size_t size) {
    ArenaAllocator scratch(allocator_, Megabytes(1));
    yyjson_read_err err{};
    yyjson_doc* doc =
        ReadJson(&scratch,
                 std::string_view(reinterpret_cast<const char*>(buf), size),
                 &err);
    CHECK(doc != nullptr, "Failed to parse spritesheet ", filename, ": ",
          err.msg);
    yyjson_val* root = yyjson_doc_get_root(doc);
    CHECK(yyjson_is_obj(root),
          "invalid spritesheet format, must be a json object");

    // Insert sprites.
    SqlStmt stmt(db_, R"(
          INSERT OR REPLACE INTO sprites (name, spritesheet, x, y, width, height)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    CHECK(stmt.ok(), "Failed to prepare sprite insert statement");

    size_t sprite_count = 0, sprite_name_length = 0;
    yyjson_val* sprites = yyjson_obj_get(root, "sprites");
    CHECK(yyjson_is_arr(sprites), "spritesheet 'sprites' must be an array");
    size_t idx, max;
    yyjson_val* sprite;
    yyjson_arr_foreach(sprites, idx, max, sprite) {
      sprite_count++;

      std::string_view name = YyjsonStrView(yyjson_obj_get(sprite, "name"));
      sprite_name_length += name.size();

      const uint32_t x = yyjson_get_int(yyjson_obj_get(sprite, "x"));
      const uint32_t y = yyjson_get_int(yyjson_obj_get(sprite, "y"));
      const uint32_t w = yyjson_get_int(yyjson_obj_get(sprite, "width"));
      const uint32_t h = yyjson_get_int(yyjson_obj_get(sprite, "height"));

      stmt.BindTextTransient(1, name);
      stmt.BindText(2, filename);
      stmt.BindInt(3, x);
      stmt.BindInt(4, y);
      stmt.BindInt(5, w);
      stmt.BindInt(6, h);
      MUST(stmt.Step());
      stmt.Reset();
    }
    std::string_view atlas_name = YyjsonStrView(yyjson_obj_get(root, "atlas"));
    const int64_t width = yyjson_get_int(yyjson_obj_get(root, "width"));
    const int64_t height = yyjson_get_int(yyjson_obj_get(root, "height"));
    InsertSpritesheetEntry(filename, width, height, sprite_count,
                           sprite_name_length, atlas_name);
    return {};
  }

  AssetInfo InsertShader(std::string_view filename, const uint8_t* buffer,
                         size_t size) {
    SqlStmt stmt(db_,
                 "INSERT OR REPLACE INTO shaders"
                 " (name, contents, shader_type) VALUES (?, ?, ?);");
    CHECK(stmt.ok(), "Failed to prepare shader insert statement");
    stmt.BindText(1, filename);
    stmt.BindBlob(2, buffer, size);
    stmt.BindText(3, HasSuffix(filename, "vert") ? "vertex" : "fragment");
    MUST(stmt.Step());
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
    SqlStmt stmt(db_,
                 "INSERT OR REPLACE INTO asset_metadata (name, size, type, "
                 "hash, processing_order) VALUES (?, ?, ?, ?, ?);");
    CHECK(stmt.ok(), "Failed to prepare asset_metadata insert statement");
    stmt.BindText(1, filename);
    stmt.BindInt(2, size);
    stmt.BindText(3, type);
    stmt.BindInt64(4, hash);
    stmt.BindInt64(5, GetOrderForType(type));
    MUST(stmt.Step());
  }

  bool TryDeferFile(const char* directory, const char* filename) {
    WorkItem::Type type;
    if (HasSuffix(filename, ".png")) {
      type = WorkItem::kPng;
    } else if (HasSuffix(filename, ".ogg")) {
      type = WorkItem::kOgg;
    } else if (HasSuffix(filename, ".wav")) {
      type = WorkItem::kWav;
    } else {
      return false;
    }

    FixedStringBuffer<kMaxPathLength> path(directory, "/", filename);
    auto* handle = PHYSFS_openRead(path.str());
    CHECK(handle != nullptr, "Could not read ", path, ": ",
          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    const size_t bytes = PHYSFS_fileLength(handle);
    scratch_.Reset();
    auto* temp = static_cast<uint8_t*>(scratch_.Alloc(bytes, kMaxAlign));
    CHECK(temp != nullptr);
    const size_t read_bytes = PHYSFS_readBytes(handle, temp, bytes);
    CHECK(read_bytes == bytes);
    CHECK(PHYSFS_close(handle));

    const auto hash = rapidhash(temp, bytes);
    DbAssets::ChecksumType saved;
    if (checksums_.Lookup(filename, &saved) &&
        !std::memcmp(&saved, &hash, sizeof(hash))) {
      return true;
    }

    auto* buffer = allocator_->NewArray<uint8_t>(bytes);
    std::memcpy(buffer, temp, bytes);

    std::string_view fname = Basename(filename);
    WorkItem item;
    item.type = type;
    item.filename_len = std::min(fname.size(), sizeof(item.filename) - 1);
    std::memcpy(item.filename, fname.data(), item.filename_len);
    item.filename[item.filename_len] = '\0';
    item.input = buffer;
    item.input_size = bytes;
    item.hash = hash;
    deferred_.Push(item);
    return true;
  }

  void HandleFile(const char* directory, const char* filename) {
    // Skip directories (e.g. "definitions/") — they are not asset files.
    FixedStringBuffer<kMaxPathLength> full_path(directory, "/", filename);
    PHYSFS_Stat stat;
    if (PHYSFS_stat(full_path.str(), &stat) &&
        stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
      return;
    }

    if (TryDeferFile(directory, filename)) return;

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
      const AssetInfo info = [&] {
        TIMER("Processing file ", fname);
        return (this->*method)(fname, buffer, bytes);
      }();
      InsertIntoAssetMeta(fname, info.size, handler.type, hash);
      result_.written_files++;
      handled = true;
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
    SqlStmt stmt(db_, "SELECT name, hash FROM asset_metadata");
    CHECK(stmt.ok(), "Failed to prepare checksum query");
    while (true) {
      auto row = MUST(stmt.Step());
      if (!row) break;
      auto name = stmt.ColumnText(0);
      const auto hash = stmt.ColumnInt64(1);
      checksums_.Insert(name, hash);
    }
  }

  void ProcessDeferredItemsSequential() {
    for (size_t i = 0; i < deferred_.size(); i++) {
      WorkItem& item = deferred_[i];
      LOG("Processing file ", item.name());
      AssetInfo info;
      switch (item.type) {
        case WorkItem::kPng:
          info = InsertPng(item.name(), item.input, item.input_size);
          InsertIntoAssetMeta(item.name(), info.size, "image", item.hash);
          break;
        case WorkItem::kOgg:
          info = InsertOgg(item.name(), item.input, item.input_size);
          InsertIntoAssetMeta(item.name(), info.size, "audio", item.hash);
          break;
        case WorkItem::kWav:
          info = InsertWav(item.name(), item.input, item.input_size);
          InsertIntoAssetMeta(item.name(), info.size, "audio", item.hash);
          break;
      }
      result_.written_files++;
    }
  }

  void ProcessDeferredItems() {
    if (deferred_.empty()) return;

    int num_threads = SDL_GetNumLogicalCPUCores();
    if (num_threads < 1) num_threads = 1;
    if (static_cast<size_t>(num_threads) > deferred_.size())
      num_threads = static_cast<int>(deferred_.size());

    auto* output_buf = allocator_->NewArray<uint8_t>(kOutputArenaSize);
    if (output_buf == nullptr) {
      LOG("Could not allocate output buffer, falling back to sequential");
      ProcessDeferredItemsSequential();
      return;
    }

    auto** scratch_bufs = allocator_->NewArray<uint8_t*>(num_threads);
    int actual_threads = 0;
    for (int i = 0; i < num_threads; i++) {
      scratch_bufs[i] = allocator_->NewArray<uint8_t>(kScratchArenaSize);
      if (scratch_bufs[i] == nullptr) break;
      actual_threads++;
    }
    if (actual_threads == 0) {
      ProcessDeferredItemsSequential();
      return;
    }
    num_threads = actual_threads;

    BumpAllocator output_arena(output_buf, kOutputArenaSize);

    WorkerContext ctx;
    ctx.items = deferred_.data();
    ctx.count = deferred_.size();
    ctx.next.store(0, std::memory_order_relaxed);
    ctx.scratch_bufs = scratch_bufs;
    ctx.output_arena = &output_arena;
    ctx.thread_id_counter.store(0, std::memory_order_relaxed);

    LOG("Processing ", deferred_.size(), " files with ", num_threads,
        " threads");

    executor_->ParallelFor(
        num_threads, /*min_batch=*/1,
        [](int /*start*/, int /*end*/, void* ud) {
          auto* c = static_cast<WorkerContext*>(ud);
          ProcessWorkItems(c);
        },
        &ctx);

    for (size_t i = 0; i < deferred_.size(); i++) {
      WorkItem& item = deferred_[i];
      if (item.type == WorkItem::kPng) {
        InsertImageBlob(item.name(), item.output, item.output_size, item.width,
                        item.height, item.channels);
      } else {
        InsertAudioBlob(item.name(), item.output, item.output_size,
                        item.qoa_desc);
      }
      InsertIntoAssetMeta(item.name(), item.output_size,
                          item.type == WorkItem::kPng ? "image" : "audio",
                          item.hash);
      result_.written_files++;
    }
  }

  AssetWriteResult HandleFiles() {
    PHYSFS_enumerate("/assets", WriteFileToDb, this);
    ProcessDeferredItems();
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
  Executor* executor_ = nullptr;
  ArenaAllocator scratch_;
  Dictionary<DbAssets::ChecksumType> checksums_;
  DynArray<WorkItem> deferred_;
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
                                          sqlite3* db, Allocator* allocator,
                                          Executor* executor) {
  if (!PHYSFS_mount(source_directory, "/assets", 1)) {
    LOG("Failed to mount directory ", source_directory, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    return Error::Message("failed to mount asset directory");
  }
  SqlTransaction txn(db);
  DbPacker packer(db, allocator, executor);
  packer.LoadChecksums();
  auto result = packer.HandleFiles();
  return result;
}

void InitializeAssetDb(sqlite3* db) {
  LOG("Reloading schema");
  char* err;
  CHECK(sqlite3_exec(db, kSqlSchema, nullptr, nullptr, &err) == SQLITE_OK,
        "Failed to initialize schema: ", err);
}

}  // namespace G
