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

int GetLuaAbsoluteIndex(lua_State* L, int idx) {
  if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
  return lua_gettop(L) + idx + 1;
}

#define LUA_LOG_VALUE(state, pos, ...)           \
  do {                                           \
    FixedStringBuffer<kMaxLogLineLength> _l;     \
    _l.Append("", ##__VA_ARGS__);                \
    Lua::LogValue(state, pos, /*depth=*/0, &_l); \
    Log(__FILE__, __LINE__, _l.str());           \
  } while (0);

int Traceback(lua_State* state) {
  if (!lua_isstring(state, 1)) return 1;
  lua_getglobal(state, "debug");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return 1;
  }
  lua_getfield(state, -1, "traceback");
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 2);
    return 1;
  }
  lua_pushvalue(state, 1);
  lua_pushinteger(state, 2);
  lua_call(state, 2, 1);
  return 1;
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
    Lua::LogValue(state, /*pos=*/i + 1, /*depth=*/0, &l->log);
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
    {"docs",
     [](lua_State* state) {
       lua_getglobal(state, "_Docs");
       lua_CFunction func = lua_tocfunction(state, 1);
       lua_pushlightuserdata(state, (void*)func);
       LUA_LOG_VALUE(state, -1, "Ptr = ");
       lua_gettable(state, -2);
       LUA_LOG_VALUE(state, -1, "Value = ");
       return 1;
     }},
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
      scripts_by_name_(allocator),
      scripts_(1 << 16, allocator),
      compilation_cache_(allocator) {}

void Lua::Crash() {
  std::string_view message = GetLuaString(state_, 1);
  LuaError e = ParseLuaError(message);
  Log(e.filename, e.line, e.message);
  SetError(e.filename, e.line, e.message);
  std::longjmp(on_error_buf_, 1);
}

#define READY()                 \
  if (setjmp(on_error_buf_)) {  \
    hotload_requested_ = false; \
    return;                     \
  }

bool Lua::LoadFromCache(std::string_view script_name, lua_State* state) {
  CachedScript script;
  if (compilation_cache_.Lookup(script_name, &script)) {
    LOG("Found cached compilation for ", script_name);
    const auto checksum = assets_->GetChecksum(script_name);
    if (script.checksum == checksum) {
      lua_pushlstring(state, script.contents.data(), script.contents.size());
      return true;
    }
  }
  LOG("Checksums for ", script_name, " differ or not found");
  return false;
}

void Lua::AddLibrary(const char* name, const luaL_Reg* funcs, size_t N) {
  LUA_CHECK_STACK(state_);
  LOG("Adding library ", name);
  lua_getglobal(state_, "G");
  lua_newtable(state_);
  for (size_t i = 0; i < N; ++i) {
    CHECK(funcs[i].name != nullptr, "Invalid entry for library ", name, ": ",
          i);
    const auto* func = &funcs[i];
    lua_pushcfunction(state_, func->func);
    lua_setfield(state_, -2, func->name);
  }
  lua_setfield(state_, -2, name);
  lua_pop(state_, 1);
}

void Lua::AddLibraryWithMetadata(const char* name, const LuaApiFunction* funcs,
                                 size_t N) {
  LUA_CHECK_STACK(state_);
  LOG("Adding library ", name);
  lua_getglobal(state_, "G");
  lua_newtable(state_);
  for (size_t i = 0; i < N; ++i) {
    LUA_CHECK_STACK(state_);
    CHECK(funcs[i].name != nullptr, "Invalid entry for library ", name, ": ",
          i);
    const auto* func = &funcs[i];
    lua_pushcfunction(state_, func->func);
    lua_setfield(state_, -2, func->name);
  }
  lua_setfield(state_, -2, name);
  lua_pop(state_, 1);
  // Add the docs.
  lua_getglobal(state_, "_Docs");
  lua_newtable(state_);
  for (size_t i = 0; i < N; ++i) {
    LUA_CHECK_STACK(state_);
    // Create a table with docstring, and args fields.
    lua_newtable(state_);
    lua_pushstring(state_, funcs[i].docstring);
    lua_setfield(state_, -2, "docstring");
    const auto& args = funcs[i].args;
    // Add arguments.
    lua_newtable(state_);
    for (size_t j = 0; j < args.argc; ++j) {
      lua_newtable(state_);
      lua_pushstring(state_, args[j].name);
      lua_setfield(state_, -2, "name");
      lua_pushstring(state_, args[j].docs);
      lua_setfield(state_, -2, "docstring");
      lua_rawseti(state_, -2, j + 1);
    }
    lua_setfield(state_, -2, "args");
    // Add return values.
    lua_newtable(state_);
    const auto& returns = funcs[i].returns;
    for (size_t j = 0; j < returns.argc; ++j) {
      lua_pushstring(state_, returns[j].docs);
      lua_rawseti(state_, -2, j + 1);
    }
    lua_setfield(state_, -2, "returns");
    lua_setfield(state_, -2, funcs[i].name);
  }
  lua_setfield(state_, -2, name);
  // Add the functions in the library with a link to the
  // corresponding entry as a light user data.
  for (size_t i = 0; i < N; ++i) {
    LUA_CHECK_STACK(state_);
    lua_getfield(state_, -1, name);
    lua_pushstring(state_, funcs[i].name);
    lua_gettable(state_, -2);
    lua_getglobal(state_, "G");
    lua_getfield(state_, -1, name);
    lua_pushstring(state_, funcs[i].name);
    lua_gettable(state_, -2);
    auto* ptr = lua_tocfunction(state_, -1);
    lua_pushlightuserdata(state_, (void*)(ptr));
    lua_pushvalue(state_, -5);
    lua_settable(state_, -8);
    lua_pop(state_, 5);
  }
  lua_pop(state_, 1);
}

int Lua::LoadLuaAsset(std::string_view filename,
                      std::string_view script_contents, int traceback_handler) {
  FixedStringBuffer<kMaxPathLength + 1> buf("@", filename);
  LOG("Loading ", filename);
  if (luaL_loadbuffer(state_, script_contents.data(), script_contents.size(),
                      buf.str()) != 0) {
    LOG("Failed to load ", filename, ": ", luaL_checkstring(state_, -1));
    lua_error(state_);
    return 0;
  }
  LOG("Loaded lua asset ", filename, ". Executing.");
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state_, 0, 1, traceback_handler)) {
      LOG("Failed to load ", filename, ": ", luaL_checkstring(state_, -1));
      lua_error(state_);
      return 0;
    }
  } else {
    if (lua_pcall(state_, 0, 1, 0) != 0) {
      LOG("Failed to load ", filename, ": ", luaL_checkstring(state_, -1));
      lua_error(state_);
      return 0;
    }
  }
  LOG("Finished loading ", filename);
  return 1;
}

bool Lua::CompileFennelAsset(std::string_view name,
                             std::string_view script_contents,
                             int traceback_handler) {
  // Load fennel module if not present.
  lua_getfield(state_, LUA_GLOBALSINDEX, "package");
  lua_getfield(state_, -1, "loaded");
  CHECK(lua_istable(state_, -1), "Missing loaded table");
  lua_getfield(state_, -1, "fennel");
  if (lua_isnil(state_, -1)) {
    TIMER("Proactively loading Fennel compiler");
    lua_pop(state_, 1);
    Script* fennel_script = nullptr;
    if (!scripts_by_name_.Lookup("fennel", &fennel_script)) {
      LUA_ERROR(state_, "Fennel compiler is absent, cannot load fennel files");
      return false;
    }
    // Fennel is not loaded. Load it.
    LoadLuaAsset(fennel_script->name, fennel_script->contents);
    CHECK(lua_istable(state_, -1), "Invalid fennel compilation result");
    lua_pushvalue(state_, -1);
    lua_pushvalue(state_, -1);
    lua_setglobal(state_, "_fennel");
    lua_setfield(state_, -3, "fennel");
    // Set debug.traceback to fennel.traceback
    lua_getfield(state_, -1, "traceback");
    lua_getglobal(state_, "debug");
    if (lua_istable(state_, -1)) {
      LOG("Setting debug traceback to fennel's");
      lua_pushvalue(state_, -2);
      lua_setfield(state_, -2, "traceback");
      lua_pop(state_, 2);
    } else {
      LOG("No Lua debug traceback support");
      lua_pop(state_, 2);
    }
  }
  {
    TIMER("Running compiler on ", name);
    // Run string on the script contents.
    lua_getfield(state_, -1, "compileString");
    CHECK(lua_isfunction(state_, -1),
          "Invalid fennel compiler has no function 'eval'");
    lua_pushlstring(state_, script_contents.data(), script_contents.size());
    FixedStringBuffer<kMaxPathLength + 1> buf(name);
    lua_newtable(state_);
    lua_pushstring(state_, "filename");
    lua_pushstring(state_, buf.str());
    lua_settable(state_, -3);
    if (traceback_handler != INT_MAX) {
      if (lua_pcall(state_, 2, 1, traceback_handler)) {
        lua_error(state_);
        return false;
      }
    } else {
      if (lua_pcall(state_, 2, 1, 0) != 0) {
        LOG("Failed to compile ", name, ":\n\n", GetLuaString(state_, -1));
        return false;
      }
    }
    InsertIntoCache(name, state_);
  }
  return true;
}

int Lua::LoadFennelAsset(std::string_view name,
                         std::string_view script_contents,
                         int traceback_handler) {
  LOG("Loading script ", name);
  auto* lua = Registry<Lua>::Retrieve(state_);
  if (!lua->LoadFromCache(name, state_)) {
    LOG("Could not load script ", name, " from the cache. Compiling again");
    if (!lua->CompileFennelAsset(name, script_contents, traceback_handler)) {
      LOG("Failed to compile asset ", name);
      return 0;
    }
  }
  std::string_view script = GetLuaString(state_, -1);
  LOG("Executing script ", name);
  FixedStringBuffer<kMaxPathLength + 1> buf("@", name);
  if (luaL_loadbuffer(state_, script.data(), script.size(), buf) != 0) {
    lua_error(state_);
    return 0;
  }
  if (traceback_handler != INT_MAX) {
    if (lua_pcall(state_, 0, 1, traceback_handler) != 0) {
      lua_error(state_);
      return 0;
    }
  } else {
    if (lua_pcall(state_, 0, 1, 0) != 0) {
      lua_error(state_);
      return 0;
    }
  }
  return 1;
}

void Lua::LogValue(lua_State* state, int pos, int depth, StringBuffer* buf) {
  LUA_CHECK_STACK(state);
  if (depth > 10) {
    buf->Append("...");
    return;
  }
  int abs_pos = GetLuaAbsoluteIndex(state, pos);
  switch (lua_type(state, abs_pos)) {
    case LUA_TNUMBER:
      buf->Append(luaL_checknumber(state, abs_pos));
      break;
    case LUA_TSTRING:
      buf->Append("\"", luaL_checkstring(state, abs_pos), "\"");
      break;
    case LUA_TBOOLEAN:
      buf->Append(lua_toboolean(state, abs_pos) ? "true" : "false");
      break;
    case LUA_TNIL:
      buf->Append("nil");
      break;
    case LUA_TTABLE: {
      buf->Append("{ ");
      lua_pushnil(state);
      bool first = true;
      while (lua_next(state, abs_pos) != 0) {
        if (!first) buf->Append(", ");
        first = false;
        LogValue(state, -2, depth + 1, buf);
        buf->Append(": ");
        LogValue(state, -1, depth + 1, buf);
        /* removes 'value'; keeps 'key' for next iteration */
        lua_pop(state, 1);
      }
      buf->Append("} ");
    }; break;
    case LUA_TLIGHTUSERDATA: {
      void* v = lua_touserdata(state, pos);
      if (v == nullptr) {
        buf->Append("nil");
      } else {
        char ptr[32];
        snprintf(ptr, 32, "0x%016lx", reinterpret_cast<uintptr_t>(v));
        buf->Append(ptr);
      }
    }; break;
    default:
      buf->Append("?? (", lua_typename(state, lua_type(state, pos)), ")");
      break;
  }
}

void Lua::InsertIntoCache(std::string_view script_name, lua_State* state) {
  TIMER("Inserting script ", script_name, " into cache");
  auto compiled = allocator_->StrDup(GetLuaString(state, -1));
  CachedScript script;
  script.contents = compiled;
  // Mark the script as dirty by not setting the checksum.
  script.checksum = 0;
  compilation_cache_.Insert(script_name, script);
}

void Lua::FlushCompilationCache() {
  READY();
  LUA_CHECK_STACK(state_);
  TIMER("Flushing compilation cache");
  // Check if we have anything to flush.
  bool dirty = false;
  for (const auto& script : scripts_) {
    CachedScript cached_script;
    if (script.language == Script::kLuaScript) continue;
    if (!compilation_cache_.Lookup(script.name, &cached_script)) {
      dirty = true;
      break;
    }
    const auto checksum = assets_->GetChecksum(script.name);
    if (checksum != cached_script.checksum) {
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
  DEFER(
      [&] { sqlite3_exec(db_, "END TRANSACTION", nullptr, nullptr, nullptr); });
  FixedStringBuffer<256> sql(R"(
    INSERT OR REPLACE 
    INTO compilation_cache (source_name, source_hash, compiled) 
    VALUES (?, ?, ?);"
  )");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.str(), -1, &stmt, nullptr) != SQLITE_OK) {
    DIE("Failed to prepare statement ", sql.str(), ": ", sqlite3_errmsg(db_));
    return;
  }
  DEFER([&] { sqlite3_finalize(stmt); });
  for (const auto& script : scripts_) {
    CachedScript cached_script;
    if (script.language == Script::kLuaScript) continue;
    const auto checksum = assets_->GetChecksum(script.name);
    if (compilation_cache_.Lookup(script.name, &cached_script)) {
      if (checksum == cached_script.checksum) {
        LOG("Skipping ", script.name, " since it has not changed");
        continue;
      }
    } else {
      if (!CompileFennelAsset(script.name, script.contents)) {
        LUA_ERROR(state_, "Failed to compile ", script.name, ": \n",
                  GetLuaString(state_, -1));
        return;
      }
      CHECK(compilation_cache_.Lookup(script.name, &cached_script),
            "Did not find ", script.name,
            " in compilation cache. File is corrupted?");
    }
    sqlite3_bind_text(stmt, 1, script.name.data(), script.name.size(),
                      SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, checksum);
    sqlite3_bind_text(stmt, 3, cached_script.contents.data(),
                      cached_script.contents.size(), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      DIE("Failed to flush compilation cache when processing  ", script.name,
          ": ", sqlite3_errmsg(db_));
    }
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db_, "END TRANSACTION", nullptr, nullptr, nullptr);
}

int ForwardIndexToMetatable(lua_State* state) {
  LUA_CHECK_STACK(state);
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
  LUA_CHECK_STACK(state_);
  luaL_newmetatable(state_, metatable_name);
  if (registers == nullptr) {
    lua_pop(state_, 1);
    return;
  }
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
  // Create the docs table.
  lua_newtable(state_);
  lua_setglobal(state_, "_Docs");
  // Set the debug traceback to fennel for proper lines.
  lua_pushcfunction(state_, Traceback);
  traceback_handler_ = lua_gettop(state_);
  // Set print as G.console.log for consistency.
  lua_pushcfunction(state_, LuaLogPrint);
  lua_setglobal(state_, "print");
}

void Lua::LoadScript(const DbAssets::Script& script) {
  LUA_CHECK_STACK(state_);
  READY();
  LOG("Loading script ", script.name);
  std::string_view asset_name = script.name;
  Script saved_script;
  if (ConsumeSuffix(&asset_name, ".lua")) {
    saved_script.language = Script::kLuaScript;
  } else if (ConsumeSuffix(&asset_name, ".fnl")) {
    saved_script.language = Script::kFennelScript;
  }
  saved_script.contents =
      allocator_->StrDup(reinterpret_cast<const char*>(script.contents));
  saved_script.name = script.name;
  scripts_.Push(saved_script);
  scripts_by_name_.Insert(asset_name, &scripts_.back());
  // Set the package.
  SetPackagePreload(asset_name);
  // Clear package.loaded so we do not load it from cache.
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "loaded");
  lua_pushlstring(state_, asset_name.data(), asset_name.size());
  lua_pushnil(state_);
  lua_settable(state_, -3);
  lua_pop(state_, 2);
  LOG("Finished loading ", script.name);
}

bool Lua::Error(StringBuffer* buffer) {
  if (error_.empty()) return false;
  buffer->Set(error_);
  return true;
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
      SELECT c.source_name, c.compiled, c.source_hash
      FROM asset_metadata a 
      INNER JOIN compilation_cache c 
      WHERE a.name = c.source_name AND c.source_hash = a.hash
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
    script.checksum = sqlite3_column_int64(stmt, 2);
    LOG("Loading ", name, " into compilation cache");
    compilation_cache_.Insert(name, script);
  }
}

void Lua::LoadMain() {
  LUA_CHECK_STACK(state_);
  READY();
  Script* main = nullptr;
  // Reset the _Game var.
  lua_pushnil(state_);
  lua_setglobal(state_, "_Game");
  CHECK(scripts_by_name_.Lookup("main", &main), "Unknown script main.lua");
  if (main->language == Script::kLuaScript) {
    LoadLuaAsset(main->name, main->contents, traceback_handler_);
  } else {
    LoadFennelAsset(main->name, main->contents, traceback_handler_);
  }
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

int Lua::PackageLoader() {
  const std::string_view modname = GetLuaString(state_, 1);
  Script* script = nullptr;
  if (!scripts_by_name_.Lookup(modname, &script)) {
    LUA_ERROR(state_, "Could not find asset %s.lua", modname);
    return 0;
  }
  int result = 0;
  if (script->language == Script::kLuaScript) {
    result = LoadLuaAsset(script->name, script->contents);
  } else {
    result = LoadFennelAsset(script->name, script->contents);
  }
  if (result == 0) {
    LOG("Failed to load ", modname);
    return result;
  }
  LOG("Loaded ", modname, " successfully. Setting package.loaded");
  if (lua_isnil(state_, -1)) {
    LOG("No result from script");
    lua_pop(state_, 1);
    lua_pushboolean(state_, true);
  }
  // Set the value of package.loaded to the module.
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "loaded");
  lua_pushlstring(state_, modname.data(), modname.size());
  lua_pushvalue(state_, -3);
  if (lua_isnil(state_, -1)) {
    LOG("No result from script");
    lua_pop(state_, 1);
    lua_pushboolean(state_, true);
  }
  lua_settable(state_, -3);
  // Pop until the top is the compilation result.
  lua_pop(state_, 2);
  return result;
}

int PackageLoaderShim(lua_State* state) {
  return Registry<Lua>::Retrieve(state)->PackageLoader();
}

void Lua::SetPackagePreload(std::string_view modname) {
  // We use a buffer to ensure that filename is null terminated.
  FixedStringBuffer<kMaxPathLength> buf(modname);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, PackageLoaderShim);
  lua_setfield(state_, -2, buf);
  lua_pop(state_, 2);
}

void Lua::Init() {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "init");
  if (lua_isnil(state_, -1)) {
    LUA_ERROR(state_, "No main function");
    return;
  }
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Update(float t, float dt) {
  LUA_CHECK_STACK(state_);
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
  LUA_CHECK_STACK(state_);
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
  LUA_CHECK_STACK(state_);
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
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
  lua_pop(state_, 1);
}

void Lua::HandleKeyreleased(int scancode) {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "keyreleased");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, scancode);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMousePressed(int button) {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousepressed");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleTextInput(std::string_view input) {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "textinput");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushlstring(state_, input.data(), input.size());
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleMouseReleased(int button) {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousereleased");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, button);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
  lua_pop(state_, 1);
}

void Lua::HandleMouseMoved(FVec2 pos, FVec2 delta) {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "mousemoved");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, pos.x);
  lua_pushnumber(state_, pos.y);
  lua_pushnumber(state_, delta.x);
  lua_pushnumber(state_, delta.y);
  if (lua_pcall(state_, 5, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
  lua_pop(state_, 1);
}

void Lua::HandleQuit() {
  LUA_CHECK_STACK(state_);
  if (single_evaluation_) return;
  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "quit");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
  lua_pop(state_, 1);
}

#undef LUA_LOG_VALUE

}  // namespace G
