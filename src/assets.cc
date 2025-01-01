#include "assets.h"

#include <cstring>

#include "clock.h"
#include "defer.h"
#include "filesystem.h"
#include "units.h"

namespace G {

#if 0
// Allocates memory in pages, multiple of 2. It can "resize" allocations
// inside a page if it has the same size. If it cannot find a page of the  
// appropriate size, it will create a new one with the next size.
class PageAllocator {
  public:
    explicit PageAllocator(Allocator* allocator) : allocator_(allocator) {
      Page* current = nullptr;
      for (size_t i = 0; i < kPageSizes.size(); i++) {
        auto* new_page = New<Page>(allocator_);
        new_page->size = kPageSizes[i];
        new_page->SetFree();
        new_page->start = allocator->Alloc(kPageSizes[i], sizeof(std::max_align_t));
        if (current == nullptr) {
          current = new_page;
        } else {
          current->next = new_page;
        }
      }
    }

    void* Allocate(size_t s) {
      const size_t actual_size = NextPow2(s);
      // Find the page size to use.
      size_t page_size = 0;
      for (size_t i = 0; i <= kPageSizes.size(); i++) {
        if (kPageSizes[i] >= actual_size) {
          page_size = kPageSizes[i];
          break;
        }
      }
      auto* page = FindPage(page_size);
      DCHECK(page != nullptr);
      page->SetUsed();
      return page->start;
    }

    void* Resize(void* buf, size_t new_size) {
      auto pos = reinterpret_cast<ptrdiff_t>(buf);
      auto page = reinterpret_cast<Page*>(pos - offsetof(Page, start));
      if (new_size <= page->size) return buf;
      // Mark the page as unused.
      page->SetUsed();
      auto new_page = FindPage(new_size);
      return new_page->start;
    }

  private:

    inline static constexpr std::array<size_t, 19> kPageSizes = {
      1 << 5,
      1 << 6,
      1 << 7,
      1 << 8,
      1 << 9,
      1 << 10,
      1 << 11,
      1 << 12,
      1 << 14,
      1 << 15,
      1 << 18,
      1 << 19,
      1 << 20,
      1 << 21,
      1 << 22,
      1 << 24,
      1 << 25,
      1 << 27,
      1 << 28
    };

    struct Page {
      // The top bit of size is whether the page is used or not.
      size_t size;
      Page* next = nullptr;

      bool IsFree() const { return size & (1UL << 63); }
      void SetUsed() { size |= (1UL << 63); }
      void SetFree() { size &= ~(1UL << 63); }

      void* start;
    };

    Page* FindPage(size_t size) {
      for (Page* p = page_; p != nullptr; p = p->next) {
        if (p->IsFree()) return p;
      }
      // We did not find a free page. Get a page of the next size and split it.
      auto* page = FindPage(2 * size);
      if (page == nullptr) {
        page = reinterpret_cast<Page*>(allocator_->Alloc(sizeof(Page) + 2 * size, sizeof(std::max_align_t)));
      }
      // Split the page and add it to the linked list.
      auto* new_page = New<Page>(allocator_);
      new_page->size = size / 2;
      page->size = size / 2;
      page->next = new_page;
      new_page->next = page_;
      page_ = new_page;
    }

    Allocator* allocator_;
    Page* page_;
};
#endif

void DbAssets::LoadScript(std::string_view filename, uint8_t* buffer,
                          size_t size) {
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
  scripts_.Push(script);
  scripts_map_.Insert(filename, &scripts_.back());
}

void DbAssets::LoadFont(std::string_view filename, uint8_t* buffer,
                        size_t size) {
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
  fonts_.Push(font);
  fonts_map_.Insert(filename, &fonts_.back());
}

void DbAssets::LoadAudio(std::string_view filename, uint8_t* buffer,
                         size_t size) {
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
  sounds_.Push(sound);
  sounds_map_.Insert(filename, &sounds_.back());
}

void DbAssets::LoadShader(std::string_view filename, uint8_t* buffer,
                          size_t size) {
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
  shaders_.Push(shader);
  shaders_map_.Insert(filename, &shaders_.back());
}

void DbAssets::LoadText(std::string_view filename, uint8_t* buffer,
                        size_t size) {
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
  text_files_.Push(file);
  text_files_map_.Insert(filename, &text_files_.back());
}

void DbAssets::LoadSpritesheet(std::string_view filename, uint8_t* buffer,
                               size_t size) {
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
    sheet.image = PushName(image);
    spritesheets_.Push(sheet);
    spritesheets_map_.Insert(filename, &spritesheets_.back());
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
  }
}

void DbAssets::LoadImage(std::string_view filename, uint8_t* buffer,
                         size_t size) {
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
  images_.Push(image);
  images_map_.Insert(filename, &images_.back());
}

void DbAssets::Trace(unsigned int type, void* p, void* x) {
  if (type == SQLITE_TRACE_PROFILE) {
    auto sql = static_cast<sqlite3_stmt*>(p);
    auto time = reinterpret_cast<long long*>(x);
    LOG("Executing SQL ", sqlite3_expanded_sql(sql), " took ",
        (*time) / 1'000'000.0, " milliseconds");
  }
}

void DbAssets::ReserveBufferForType(std::string_view type, size_t count) {
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

XXH128_hash_t DbAssets::GetChecksum(std::string_view asset) {
  return checksums_map_.LookupOrDie(asset)->checksum;
}

void DbAssets::Load() {
  size_t total_size = 0;
  {
    // Presize all the buffers.
    FixedStringBuffer<256> sql(
        "SELECT type, SUM(size), COUNT(*) FROM asset_metadata GROUP BY type");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const size_t size_by_type = sqlite3_column_int(stmt, 1);
      const size_t count = sqlite3_column_int(stmt, 2);
      total_size += size_by_type;
      std::string_view type(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
      ReserveBufferForType(type, count);
    }
  }
  size_t total_sprites = 0;
  {
    // Add the length and count of sprite names of all the spritesheets.
    // Also add the length of image names for buffers.
    FixedStringBuffer<256> sql("SELECT SUM(sprites) FROM spritesheets");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
    }
    DEFER([&] { sqlite3_finalize(stmt); });
    CHECK(sqlite3_step(stmt) == SQLITE_ROW,
          "Could not read asset metadata: ", sqlite3_errmsg(db_));
    const size_t sprites = sqlite3_column_int(stmt, 0);
    total_sprites += sprites;
  }
  sprites_.Reserve(total_sprites);
  content_size_ = 0;
  content_buffer_ =
      reinterpret_cast<uint8_t*>(allocator_->Alloc(total_size, /*align=*/1));
  struct Loader {
    std::string_view name;
    void (DbAssets::*load)(std::string_view name, uint8_t* buffer, size_t size);
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
      "asset_metadata ORDER BY "
      "type");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto type_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string_view type(type_ptr);
    const size_t size = sqlite3_column_int64(stmt, 2);
    auto* buffer = &content_buffer_[content_size_];
    content_size_ += size;
    std::string_view saved_name = PushName(name);
    XXH128_hash_t checksum;
    checksum.low64 = sqlite3_column_int64(stmt, 3);
    checksum.high64 = sqlite3_column_int64(stmt, 4);
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
        Checksum c;
        c.asset = saved_name;
        std::memcpy(&c.checksum, &checksum, sizeof(checksum));
        checksums_.Push(c);
        checksums_map_.Insert(saved_name, &checksums_.back());
        break;
      }
    }
  }
}

void DbAssets::CheckForChangedFiles(const char* source_directory,
                                    Allocator* allocator) {
  PHYSFS_CHECK(PHYSFS_mount(source_directory, "/assets", 1),
               " while trying to mount directory ", source_directory);
  constexpr size_t kBufferSize = Megabytes(16);
  ArenaAllocator scratch(allocator, kBufferSize + 16);
  auto* buffer = scratch.Alloc(kBufferSize, /*align=*/16);
  CHECK(buffer != nullptr);
  for (const auto& checksum : checksums_) {
    if (checksum.asset == "debug_font.ttf") continue;
    FixedStringBuffer<kMaxPathLength> path("/assets/", checksum.asset);
    if (!PHYSFS_exists(path.str())) {
      LOG("File ", path, " is gone");
      continue;
    }
    auto* handle = PHYSFS_openRead(path.str());
    CHECK(handle != nullptr, "Could not read ", path, ": ",
          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    DEFER([&handle] { PHYSFS_close(handle); });
    auto* hash_state = XXH3_createState();
    XXH3_128bits_reset(hash_state);
    DEFER([&hash_state] { XXH3_freeState(hash_state); });
    while (!PHYSFS_eof(handle)) {
      const int64_t read_bytes = PHYSFS_readBytes(handle, buffer, kBufferSize);
      PHYSFS_CHECK(read_bytes >= 0, "Failed to read ", path);
      XXH3_128bits_update(hash_state, buffer, read_bytes);
    }
    const XXH128_hash_t hash = XXH3_128bits_digest(hash_state);
    if (!std::memcmp(&hash, &checksum.checksum, sizeof(hash))) {
      continue;
    }
  }
}

}  // namespace G
