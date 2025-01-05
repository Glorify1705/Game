#include "lua.h"

#include <algorithm>

#include "SDL.h"
#include "clock.h"
#include "console.h"
#include "defer.h"
#include "filesystem.h"
#include "units.h"

namespace G {
namespace {

int Traceback(lua_State* L) {
  if (!lua_isstring(L, 1)) return 1;
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);
  lua_pushinteger(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

int LoadLuaAsset(lua_State* state, const DbAssets::Script& asset,
                 int traceback_handler = INT_MAX) {
  FixedStringBuffer<kMaxPathLength + 1> buf("@", asset.name);
  if (luaL_loadbuffer(state, reinterpret_cast<const char*>(asset.contents),
                      asset.size, buf.str()) != 0) {
    lua_error(state);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state, 0, 1, traceback_handler)) {
      lua_error(state);
      return 0;
    }
  } else {
    lua_call(state, 0, 1);
  }
  return 1;
}

int LoadFennelAsset(lua_State* state, const DbAssets::Script& asset,
                    int traceback_handler = INT_MAX) {
  std::string_view name = asset.name;
  auto* lua = Registry<Lua>::Retrieve(state);
  if (!lua->LoadFromCache(name, state)) {
    LOG("Explicitly compiling ", name);
    TIMER("Compiling ", name);
    // Load fennel module if not present.
    lua_getfield(state, LUA_GLOBALSINDEX, "package");
    lua_getfield(state, -1, "loaded");
    CHECK(lua_istable(state, -1), "Missing loaded table");
    lua_getfield(state, -1, "fennel");
    if (lua_isnil(state, -1)) {
      LOG("Proactively loading Fennel compiler");
      lua_pop(state, 1);
      DbAssets* assets = Registry<DbAssets>::Retrieve(state);
      auto* fennel = assets->GetScript("fennel.lua");
      if (fennel == nullptr) {
        LUA_ERROR(state, "Fennel compiler is absent, cannot load fennel files");
        return 0;
      }
      // Fennel is not loaded. Load it.
      LoadLuaAsset(state, *fennel);
      CHECK(lua_istable(state, -1), "Invalid fennel compilation result");
      lua_pushvalue(state, -1);
      lua_setfield(state, -3, "fennel");
    }
    LOG("Compiling ", name);
    // Run string on the script contents.
    lua_getfield(state, -1, "compileString");
    CHECK(lua_isfunction(state, -1),
          "Invalid fennel compiler has no function 'eval'");
    lua_pushlstring(state, reinterpret_cast<const char*>(asset.contents),
                    asset.size);
    FixedStringBuffer<kMaxPathLength + 1> buf(name);
    lua_newtable(state);
    lua_pushstring(state, "filename");
    lua_pushstring(state, buf.str());
    lua_settable(state, -3);
    if (traceback_handler != INT_MAX) {
      if (lua_pcall(state, 2, 1, traceback_handler)) {
        lua_error(state);
        return 0;
      }
    } else {
      lua_call(state, 2, 1);
    }
    lua->InsertIntoCache(asset.name, state);
  }
  LOG("Executing script ", name);
  std::string_view script = GetLuaString(state, -1);
  FixedStringBuffer<kMaxPathLength + 1> buf("@", asset.name);
  if (luaL_loadbuffer(state, script.data(), script.size(), buf) != 0) {
    lua_error(state);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state, 0, 1, traceback_handler)) {
      lua_error(state);
      return 0;
    }
  } else {
    lua_call(state, 0, 1);
  }
  return 1;
}

int PackageLoader(lua_State* state) {
  const char* modname = luaL_checkstring(state, 1);
  FixedStringBuffer<kMaxPathLength> buf(modname, ".lua");
  DbAssets* assets = Registry<DbAssets>::Retrieve(state);
  LOG("Attempting to load package ", modname, " from file ", buf);
  auto* asset = assets->GetScript(buf.piece());
  if (asset != nullptr) {
    return LoadLuaAsset(state, *asset);
  }
  LOG("Could not find file ", buf, " trying with Fennel");
  buf.Clear();
  buf.Append(modname, ".fnl");
  LOG("Attempting to load package ", modname, " from file ", buf);
  asset = assets->GetScript(buf.piece());
  if (asset == nullptr) {
    LUA_ERROR(state, "Could not find asset %s.lua", modname);
    return 0;
  }
  return LoadFennelAsset(state, *asset);
}

struct LogLine {
  FixedStringBuffer<kMaxLogLineLength> file;
  int line;
  FixedStringBuffer<kMaxLogLineLength> log;
};

void FillLogLine(lua_State* state, LogLine* l) {
  const int num_args = lua_gettop(state);
  lua_getglobal(state, "tostring");
  for (int i = 0; i < num_args; ++i) {
    // Call tostring to print the value of the argument.
    lua_pushvalue(state, -1);
    lua_pushvalue(state, i + 1);
    lua_call(state, 1, 1);
    size_t length;
    const char* s = lua_tolstring(state, -1, &length);
    if (s == nullptr) {
      LUA_ERROR(state, "'tostring' did not return string");
      return;
    }
    l->log.Append(std::string_view(s, length));
    if (i + 1 < num_args) l->log.Append(" ");
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  lua_Debug ar;
  lua_getstack(state, 1, &ar);
  lua_getinfo(state, "nSl", &ar);
  int line = ar.currentline;
  // Filename starts with @ in the Lua source so that tracebacks
  // work properly.
  const char* file = ar.source + 1;
  lua_pop(state, 1);
  l->file.Append(file);
  l->line = line;
}

int LuaLogPrint(lua_State* state) {
  LogLine l;
  FillLogLine(state, &l);
  Log(l.file.str(), l.line, l.log);
  return 0;
}

struct LuaError {
  std::string_view filename;
  int line = 0;
  std::string_view message;
};

LuaError ParseLuaError(std::string_view message) {
  enum State { FILE, LINE };
  State state = FILE;
  LuaError result;
  for (size_t i = 0; i < message.size(); ++i) {
    const char c = message[i];
    switch (state) {
      case FILE:
        if (c == ':') {
          result.filename = message.substr(0, i);
          state = LINE;
        }
        break;
      case LINE:
        if (c == ':') {
          result.message = message.substr(i + 2);
          return result;
        } else {
          result.line = 10 * result.line + (c - '0');
        }
        break;
    }
  }
  return result;
}

constexpr struct luaL_Reg kBasicLibs[] = {
    {"log", LuaLogPrint},
    {"crash",
     [](lua_State* state) {
       LogLine l;
       FillLogLine(state, &l);
       Crash(l.file.str(), l.line, l.log);
       return 0;
     }},
    {"hotload", [](lua_State* state) {
       Registry<Lua>::Retrieve(state)->RequestHotload();
       return 0;
     }}};

void AddBasicLibs(lua_State* state) {
  static constexpr luaL_Reg lualibs[] = {{"", luaopen_base},
                                         {LUA_LOADLIBNAME, luaopen_package},
                                         {LUA_TABLIBNAME, luaopen_table},
                                         {LUA_STRLIBNAME, luaopen_string},
                                         {LUA_MATHLIBNAME, luaopen_math},
                                         {LUA_DBLIBNAME, luaopen_debug}};
  for (const auto& lib : lualibs) {
    lua_pushcfunction(state, lib.func);
    lua_pushstring(state, lib.name);
    lua_call(state, 1, 0);
  }
}

}  // namespace

void* Lua::Alloc(void* ptr, size_t osize, size_t nsize) {
  allocator_stats_.AddSample(nsize);
  if (nsize == 0) {
    if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
    return nullptr;
  }
  if (ptr == nullptr) {
    return allocator_->Alloc(nsize, /*align=*/1);
  }
  return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
}

Lua::Lua(size_t argc, const char** argv, sqlite3* db, DbAssets* assets,
         Allocator* allocator)
    : argc_(argc),
      argv_(argv),
      allocator_(allocator),
      db_(db),
      assets_(assets),
      compilation_cache_(allocator) {}

void Lua::Crash() {
  std::string_view message = GetLuaString(state_, 1);
  LuaError e = ParseLuaError(message);
  Log(e.filename, e.line, e.message);
  SetError(e.filename, e.line, e.message);
  std::longjmp(on_error_buf_, 1);
}

#define READY()                \
  if (setjmp(on_error_buf_)) { \
    return;                    \
  }

bool Lua::LoadFromCache(std::string_view script_name, lua_State* state) {
  CachedScript script;
  if (compilation_cache_.Lookup(script_name, &script)) {
    LOG("Found cached compilation for ", script_name);
    lua_pushlstring(state, script.contents.data(), script.contents.size());
    return true;
  }
  return false;
}

void Lua::AddLibrary(const char* name, const luaL_Reg* funcs, size_t N) {
  LOG("Adding library ", name);
  lua_getglobal(state_, "G");
  lua_newtable(state_);
  for (size_t i = 0; i < N; ++i) {
    CHECK(funcs[i].name != nullptr, "Invalid entry for library ", name, ": ",
          i);
    const auto* func = &funcs[i];
    lua_pushstring(state_, func->name);
    lua_pushcfunction(state_, func->func);
    lua_settable(state_, -3);
  }
  lua_setfield(state_, -2, name);
  lua_pop(state_, 1);
}

void Lua::InsertIntoCache(std::string_view script_name, lua_State* state) {
  std::string_view compiled = GetLuaString(state, -1);
  auto* buffer = allocator_->Alloc(compiled.size(), 1);
  std::memcpy(buffer, compiled.data(), compiled.size());
  CachedScript script;
  script.contents = compiled;
  // Mark the script as dirty by not setting the checksum.
  script.checksum_low = script.checksum_high = 0;
  compilation_cache_.Insert(script_name, script);
}

void Lua::FlushCompilationCache() {
  TIMER("Flushing compilation cache");
  // Check if we have anything to flush.
  bool dirty = false;
  for (const auto& script : assets_->GetScripts()) {
    CachedScript cached_script;
    if (!compilation_cache_.Lookup(script.name, &cached_script)) {
      continue;
    }
    XXH128_hash_t checksum = assets_->GetChecksum(script.name);
    if (checksum.low64 != cached_script.checksum_low ||
        checksum.high64 != cached_script.checksum_high) {
      LOG(script.name, " is dirty");
      dirty = true;
      break;
    }
  }
  if (!dirty) {
    LOG("Nothing to flush in the compilation cache");
    return;
  }
  sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  FixedStringBuffer<256> sql(R"(
    INSERT OR REPLACE 
    INTO compilation_cache (source_name, source_hash_low, source_hash_high, compiled) 
    VALUES (?, ?, ?, ?);"
  )");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    return;
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  for (const auto& script : assets_->GetScripts()) {
    CachedScript cached_script;
    if (!compilation_cache_.Lookup(script.name, &cached_script)) {
      continue;
    }
    XXH128_hash_t checksum = assets_->GetChecksum(script.name);
    if (checksum.low64 == cached_script.checksum_low &&
        checksum.high64 == cached_script.checksum_high) {
      continue;
    }
    sqlite3_bind_text(stmt, 1, script.name.data(), script.name.size(),
                      SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, checksum.low64);
    sqlite3_bind_int64(stmt, 3, checksum.high64);
    sqlite3_bind_text(stmt, 4, cached_script.contents.data(),
                      cached_script.contents.size(), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Failed to flush compilation cache: ", sqlite3_errmsg(db_));
    }
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db_, "END TRANSACTION", nullptr, nullptr, nullptr);
}

int ForwardIndexToMetatable(lua_State* state) {
  lua_getmetatable(state, 1);
  lua_pushvalue(state, 2);
  lua_gettable(state, -2);
  if (!lua_iscfunction(state, -1)) {
    lua_getmetatable(state, 1);
    lua_getfield(state, -1, "__name");
    LUA_ERROR(state, GetLuaString(state, 2), " is not a valid method for ",
              GetLuaString(state, -1));
    return 0;
  }
  return 1;
}

void Lua::LoadMetatable(const char* metatable_name, const luaL_Reg* registers,
                        size_t register_count) {
  luaL_newmetatable(state_, metatable_name);
  if (registers == nullptr) return;
  lua_pushstring(state_, metatable_name);
  lua_setfield(state_, -2, "__name");
  for (size_t i = 0; i < register_count; ++i) {
    const luaL_Reg& r = registers[i];
    lua_pushstring(state_, r.name);
    lua_pushcfunction(state_, r.func);
    lua_settable(state_, -3);
  }
  // Forward __index to the metatable.
  lua_pushcfunction(state_, ForwardIndexToMetatable);
  lua_setfield(state_, -2, "__index");
  lua_pop(state_, 1);  // Pop the metatable from the stack.
}

void Lua::LoadLibraries() {
  if (state_ != nullptr) lua_close(state_);
  state_ = lua_newstate(&Lua::LuaAlloc, this);
  lua_atpanic(state_, [](lua_State* state) {
    auto* lua = Registry<Lua>::Retrieve(state);
    lua->Crash();
    DIE("What are you doing here? Get out of here, it's gonna blow!");
    return 0;
  });
  READY();
  Register(this);
  // Create basic initial state.
  AddBasicLibs(state_);
  // Create the global namespace table (G) so functions live under it (e.g.
  // G.graphics.draw_rect).
  lua_newtable(state_);
  // Set the basic libs directly into the global namespace.
  for (const auto& fn : kBasicLibs) {
    lua_pushstring(state_, fn.name);
    lua_pushcfunction(state_, fn.func);
    lua_settable(state_, -3);
  }
  lua_setglobal(state_, "G");
  lua_pushcfunction(state_, Traceback);
  traceback_handler_ = lua_gettop(state_);
  // Set print as G.console.log for consistency.
  lua_pushcfunction(state_, LuaLogPrint);
  lua_setglobal(state_, "print");
}

void Lua::LoadScripts() {
  READY();
  BuildCompilationCache();
  for (const auto& script : assets_->GetScripts()) {
    std::string_view asset_name = script.name;
    if (asset_name == "main.lua") continue;
    ConsumeSuffix(&asset_name, ".lua");
    ConsumeSuffix(&asset_name, ".fnl");
    SetPackagePreload(asset_name);
  }
}

size_t Lua::Error(char* buf, size_t max_size) {
  if (error_.empty()) return 0;
  const size_t size = std::min(error_.size(), max_size);
  std::memcpy(buf, error_.str(), size);
  buf[size] = '\0';
  return size;
}

std::string_view Trim(std::string_view s) {
  size_t i = 0, j = s.size() - 1;
  auto is_whitespace = [&](size_t p) {
    return s[p] == ' ' || s[p] == '\t' || s[p] == '\n';
  };
  while (i < s.size() && is_whitespace(i)) i++;
  while (j > 0 && is_whitespace(j)) j--;
  return s.substr(i, j - i);
}

void Lua::SetError(std::string_view file, int line, std::string_view error) {
  error_.Set("[", file, ":", line, "] ");
  // Remove lines with [C] or "(tail call)" since they are not useful.
  size_t st = 0, en = 0;
  auto flush = [&] {
    if (en <= st) return;
    std::string_view line = error.substr(st, en - st + 1);
    std::string_view trimmed = Trim(line);
    if (!HasPrefix(trimmed, "[C]") && !HasPrefix(trimmed, "(tail call)")) {
      error_.Append(line);
      error_.Append("\n");
    }
  };
  for (size_t i = 0; i < error.size(); ++i) {
    if (error[i] == '\n') {
      flush();
      st = en = i + 1;
    } else {
      en = i;
    }
  }
  flush();
}

void Lua::BuildCompilationCache() {
  FixedStringBuffer<512> sql(R"(
      SELECT c.source_name, c.compiled, c.source_hash_low, c.source_hash_high 
      FROM asset_metadata a 
      INNER JOIN compilation_cache c 
      WHERE a.name = c.source_name AND c.source_hash_low = a.hash_low 
      AND c.source_hash_high = a.hash_high;
    )");
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql, ": ", sqlite3_errmsg(db_));
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto* contents =
        reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 1));
    size_t content_size = sqlite3_column_bytes(stmt, 1);
    auto* buffer = static_cast<char*>(allocator_->Alloc(content_size, 1));
    std::memcpy(buffer, contents, content_size);
    CachedScript script;
    script.contents = std::string_view(buffer, content_size);
    script.checksum_low = sqlite3_column_int64(stmt, 2);
    script.checksum_high = sqlite3_column_int64(stmt, 3);
    compilation_cache_.Insert(name, script);
  }
}

void Lua::LoadMain() {
  READY();
  LOG("Loading main file main.lua");
  auto* main = assets_->GetScript("main.lua");
  CHECK(main != nullptr, "Unknown script main.lua");
  LoadLuaAsset(state_, *main, traceback_handler_);
  FlushCompilationCache();
  if (!lua_istable(state_, -1)) {
    if (lua_isboolean(state_, -1)) {
      LOG("Single evaluation mode. Finished");
      single_evaluation_ = true;
    } else {
      LUA_ERROR(state_, "Expected a table");
    }
    return;
  }
  // Check all important functions are defined.
  for (const char* fn : {"init", "update", "draw"}) {
    lua_getfield(state_, -1, fn);
    if (lua_isnil(state_, -1)) {
      DIE("Cannot run main code: ", fn, " is not defined in main.lua");
    }
    lua_pop(state_, 1);
  }
  lua_setglobal(state_, "_Game");
  LOG("Loaded main successfully");
}

void Lua::SetPackagePreload(std::string_view filename) {
  // We use a buffer to ensure that filename is null terminated.
  FixedStringBuffer<kMaxPathLength> buf(filename);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, &PackageLoader);
  lua_setfield(state_, -2, buf);
  lua_pop(state_, 2);
}

void Lua::Init() {
  if (single_evaluation_) return;
  LOG("Initializing game");
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "init");
  if (lua_isnil(state_, -1)) {
    LUA_ERROR(state_, "No main function");
    return;
  }
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, LUA_MULTRET, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Update(float t, float dt) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  t_ = t;
  dt_ = dt;
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "update");
  lua_insert(state_, -2);
  lua_pushnumber(state_, t);
  lua_pushnumber(state_, dt);
  if (lua_pcall(state_, 3, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Draw() {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "draw");
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleKeypressed(int scancode) {
  if (single_evaluation_) {
    if (scancode == SDL_SCANCODE_Q || scancode == SDL_SCANCODE_ESCAPE) {
      Stop();
    }
    return;
  }
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "keypressed");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleKeyreleased(int scancode) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "keyreleased");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMousePressed(int button) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousepressed");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleTextInput(std::string_view input) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "textinput");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushlstring(state_, input.data(), input.size());
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMouseReleased(int button) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousereleased");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMouseMoved(FVec2 pos, FVec2 delta) {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousemoved");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  lua_pushnumber(state_, pos.x);
  lua_pushnumber(state_, pos.y);
  lua_pushnumber(state_, delta.x);
  lua_pushnumber(state_, delta.y);
  if (lua_pcall(state_, 5, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleQuit() {
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "quit");
  if (lua_isnil(state_, -1)) return;
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

}  // namespace G
