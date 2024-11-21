#include "packer.h"

#include "clock.h"
#include "debug_font.h"
#include "filesystem.h"
#include "libraries/sqlite3.h"
#include "lua.h"
#include "physfs.h"
#include "src/allocators.h"
#include "src/strings.h"
#include "src/units.h"
#include "xxhash.h"

namespace G {
namespace {

class DbPacker {
 public:
  explicit DbPacker(sqlite3* db, Allocator* allocator)
      : db_(db), allocator_(allocator) {}

  void InsertIntoTable(std::string_view table, std::string_view filename,
                       const uint8_t* buf, size_t size) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql("INSERT OR REPLACE INTO ", table,
                               " (name, contents) VALUES (?, ?);");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, buf, size, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  void InsertScript(std::string_view filename, const uint8_t* buf,
                    size_t size) {
    InsertIntoTable("scripts", filename, buf, size);
  }

  void InsertFont(std::string_view filename, const uint8_t* buf, size_t size) {
    InsertIntoTable("fonts", filename, buf, size);
  }

  void InsertImage(std::string_view filename, const uint8_t* buf, size_t size) {
    QoiDesc desc;
    QoiDecode(buf, size, &desc, /*components=*/4, allocator_);
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO images (name, width, height, components, contents)
          VALUES (?, ?, ?, ?, ?);
      )");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, desc.width);
    sqlite3_bind_int(stmt, 3, desc.height);
    sqlite3_bind_int(stmt, 4, desc.channels);
    sqlite3_bind_blob(stmt, 5, buf, size, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  void InsertAudio(std::string_view filename, const uint8_t* buf, size_t size) {
    InsertIntoTable("audios", filename, buf, size);
  }

  void InsertTextFile(std::string_view filename, const uint8_t* buf,
                      size_t size) {
    InsertIntoTable("text_files", filename, buf, size);
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
                              size_t sprite_name_length, const char* image) {
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
    sqlite3_bind_text(stmt, 1, spritesheet.data(), spritesheet.size(),
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, image, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, width);
    sqlite3_bind_int(stmt, 4, height);
    sqlite3_bind_int(stmt, 5, sprite_count);
    sqlite3_bind_int(stmt, 6, sprite_name_length);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  void InsertSpritesheet(std::string_view filename, const uint8_t* buf,
                         size_t size) {
    auto* state = lua_newstate(&DbPacker::LuaAlloc, this);
    CHECK(luaL_loadbuffer(state, reinterpret_cast<const char*>(buf), size,
                          filename.data()) == 0,
          "Failed to load ", filename, ": ", luaL_checkstring(state, -1));
    CHECK(lua_pcall(state, 0, LUA_MULTRET, 0) == 0,
          "Failed to load script: ", filename, ": ",
          luaL_checkstring(state, -1));
    lua_pushstring(state, "atlas");
    lua_gettable(state, -2);
    const char* atlas = lua_tostring(state, -1);
    lua_pop(state, 1);
    lua_pushstring(state, "width");
    lua_gettable(state, -2);
    const int width = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_pushstring(state, "height");
    lua_gettable(state, -2);
    const int height = lua_tonumber(state, -1);
    lua_pop(state, 1);
    lua_pushstring(state, "sprites");
    lua_gettable(state, -2);
    sqlite3_stmt* sprite_stmt;
    FixedStringBuffer<256> sql(R"(
          INSERT OR REPLACE INTO sprites (name, spritesheet, x, y, width, height)
          VALUES (?, ?, ?, ?, ?, ?);
      )");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &sprite_stmt, nullptr) !=
        SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    size_t sprite_count = 0, sprite_name_length = 0;
    for (lua_pushnil(state); lua_next(state, -2); lua_pop(state, 1)) {
      sprite_count++;

      lua_pushstring(state, "name");
      lua_gettable(state, -2);
      size_t namelen = 0;
      const char* namestr = luaL_checklstring(state, -1, &namelen);
      lua_pop(state, 1);

      sprite_name_length += namelen;

      auto get_number = [&](const char* name) {
        lua_pushstring(state, name);
        lua_gettable(state, -2);
        uint32_t result = luaL_checknumber(state, -1);
        lua_pop(state, 1);
        return result;
      };

      const uint32_t x = get_number("x");
      const uint32_t y = get_number("y");
      const uint32_t w = get_number("width");
      const uint32_t h = get_number("height");

      sqlite3_bind_text(sprite_stmt, 1, namestr, namelen, SQLITE_TRANSIENT);
      sqlite3_bind_text(sprite_stmt, 2, filename.data(), filename.size(),
                        SQLITE_STATIC);
      sqlite3_bind_int(sprite_stmt, 3, x);
      sqlite3_bind_int(sprite_stmt, 4, y);
      sqlite3_bind_int(sprite_stmt, 5, w);
      sqlite3_bind_int(sprite_stmt, 6, h);
      if (sqlite3_step(sprite_stmt) != SQLITE_DONE) {
        DIE("Could not insert data for ", namestr, " in ", filename, ": ",
            sqlite3_errmsg(db_));
      }
      sqlite3_reset(sprite_stmt);
      sqlite3_clear_bindings(sprite_stmt);
    }
    sqlite3_finalize(sprite_stmt);
    InsertSpritesheetEntry(filename, width, height, sprite_count,
                           sprite_name_length, atlas);
    lua_pop(state, 1);
    lua_close(state);
  }

  void InsertShader(std::string_view filename, const uint8_t* buffer,
                    size_t size) {
    sqlite3_stmt* stmt;
    FixedStringBuffer<256> sql(
        "INSERT OR REPLACE INTO shaders"
        " (name, contents, shader_type) VALUES (?, ?, ?);");
    if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
      DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, buffer, size, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3,
                      HasSuffix(filename, "vert") ? "vertex" : "fragment", -1,
                      SQLITE_STATIC);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE, "Could not insert data for ",
          filename, ": ", sqlite3_errmsg(db_));
    sqlite3_finalize(stmt);
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
    sqlite3_bind_text(stmt, 1, filename.data(), filename.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, size);
    sqlite3_bind_text(stmt, 3, type.data(), type.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, hash.low64);
    sqlite3_bind_int(stmt, 5, hash.high64);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Could not insert data ", sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  void HandleFile(const char* directory, const char* filename) {
    struct DbHandler {
      std::string_view extension;
      void (DbPacker::*handler)(std::string_view filename, const uint8_t* buf,
                                size_t size);
      std::string_view type;
    };
    FixedStringBuffer<kMaxPathLength> path(directory, "/", filename);

    static constexpr DbHandler kHandlers[] = {
        {".lua", &DbPacker::InsertScript, "script"},
        {".fnl", &DbPacker::InsertScript, "script"},
        {".qoi", &DbPacker::InsertImage, "image"},
        {".sprites", &DbPacker::InsertSpritesheet, "spritesheet"},
        {".ogg", &DbPacker::InsertAudio, "audio"},
        {".ttf", &DbPacker::InsertFont, "font"},
        {".wav", &DbPacker::InsertAudio, "audio"},
        {".vert", &DbPacker::InsertShader, "shader"},
        {".frag", &DbPacker::InsertShader, "shader"},
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
      InsertIntoAssetMeta(fname, bytes, handler.type, hash);
      (this->*method)(fname, buffer, bytes);
      scratch.Dealloc(buffer, bytes);
      handled = true;
      break;
    }
    CHECK(handled, "No handler for file ", filename);
  }

  static PHYSFS_EnumerateCallbackResult WriteFileToDb(void* ud,
                                                      const char* dirname,
                                                      const char* filename) {
    auto* db_handler = static_cast<DbPacker*>(ud);
    db_handler->HandleFile(dirname, filename);
    return PHYSFS_ENUM_OK;
  }

  void HandleFiles() {
    PHYSFS_enumerate("/assets", WriteFileToDb, this);
    // Ensure we always have the debug font available.
    InsertFont("debug_font.ttf", kProggyCleanFont, kProggyCleanFontLength);
  }

 private:
  sqlite3* db_ = nullptr;
  Allocator* allocator_ = nullptr;
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
  packer.HandleFiles();
  sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

}  // namespace G
