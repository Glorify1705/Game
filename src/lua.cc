#include "lua.h"

#include <cstdio>

#include "lua_scene.h"
#include "sqlite_helpers.h"

namespace G {
namespace {

int GetLuaAbsoluteIndex(lua_State* L, int idx) {
  if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
  return lua_gettop(L) + idx + 1;
}

#define LUA_LOG_VALUE(state, pos, ...)                    \
  do {                                                    \
    FixedStringBuffer<kMaxLogLineLength> _l(kTruncating); \
    _l.Append("", ##__VA_ARGS__);                         \
    Lua::LogValue(state, pos, /*depth=*/0, &_l);          \
    Log(__FILE__, __LINE__, _l.str());                    \
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
  FixedStringBuffer<kMaxLogLineLength> file{kTruncating};
  int line = 0;
  FixedStringBuffer<kMaxLogLineLength> log{kTruncating};
};

void FillLogLine(lua_State* state, LogLine* l) {
  const int num_args = lua_gettop(state);
  lua_getglobal(state, "tostring");
  for (int i = 0; i < num_args; ++i) {
    // Top-level strings are appended raw (no surrounding quotes), so that
    // print("hello") logs `hello` rather than `"hello"`. Nested strings inside
    // tables still get quoted by LogValue itself.
    if (lua_type(state, i + 1) == LUA_TSTRING) {
      l->log.Append(lua_tostring(state, i + 1));
    } else {
      Lua::LogValue(state, /*pos=*/i + 1, /*depth=*/0, &l->log);
    }
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

// Try to parse "filename:line:" at the start of `s`. Returns true and fills
// `file` and `line` on success.
bool TryParseFileAndLine(std::string_view s, std::string_view* file,
                         int* line) {
  // Find first colon (end of filename).
  size_t c1 = s.find(':');
  if (c1 == std::string_view::npos) return false;
  // Find second colon (end of line number).
  size_t c2 = s.find(':', c1 + 1);
  if (c2 == std::string_view::npos || c2 == c1 + 1) return false;
  // Verify the text between colons is all digits.
  for (size_t i = c1 + 1; i < c2; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  *file = s.substr(0, c1);
  // Parse the line number manually (no std::stoi on string_view).
  *line = 0;
  for (size_t i = c1 + 1; i < c2; ++i) {
    *line = 10 * (*line) + (s[i] - '0');
  }
  return true;
}

LuaError ParseLuaError(std::string_view message) {
  LuaError result;
  result.message = message;
  // Try parsing "filename:line: error message" from the first line.
  if (TryParseFileAndLine(message, &result.filename, &result.line)) {
    // Skip past "filename:line: " to get the message portion.
    size_t c2 = message.find(':', result.filename.size() + 1);
    size_t msg_start = c2 + 1;
    if (msg_start < message.size() && message[msg_start] == ' ') msg_start++;
    result.message = message.substr(msg_start);
    return result;
  }
  // Fallback: scan traceback lines for the first "filename:line:" pattern.
  // Lines look like "\ttestcollision3.lua:104: in function ..."
  size_t pos = 0;
  while (pos < message.size()) {
    size_t eol = message.find('\n', pos);
    if (eol == std::string_view::npos) eol = message.size();
    std::string_view line = message.substr(pos, eol - pos);
    // Strip leading whitespace/tabs.
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
      start++;
    std::string_view trimmed = line.substr(start);
    // Skip [C] lines and the "stack traceback:" header.
    if (!trimmed.empty() && trimmed[0] != '[' &&
        TryParseFileAndLine(trimmed, &result.filename, &result.line)) {
      return result;
    }
    pos = eol + 1;
  }
  return result;
}

constexpr struct luaL_Reg kBasicLibs[] = {
    {"docs",
     [](lua_State* state) {
       lua_getglobal(state, "_Docs");
       lua_CFunction func = lua_tocfunction(state, 1);
       lua_pushlightuserdata(state, reinterpret_cast<void*>(func));
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

int ForwardIndexToMetatable(lua_State* state) {
  lua_getmetatable(state, 1);
  lua_pushvalue(state, 2);
  lua_gettable(state, -2);
  if (!lua_iscfunction(state, -1)) {
    lua_getfield(state, -2, "__name");
    LUA_ERROR(state, GetLuaString(state, 2), " is not a valid method for ",
              GetLuaString(state, -1));
    return 0;
  }
  // Remove the metatable, leaving only the found function on top.
  lua_remove(state, -2);
  return 1;
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

int PackageLoaderShim(lua_State* state) {
  return Registry<Lua>::Retrieve(state)->PackageLoader();
}

}  // namespace

void* Lua::Alloc(void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
    return nullptr;
  }
  if (ptr == nullptr) {
    return allocator_->Alloc(nsize, /*align=*/1);
  }
  return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
}

Lua::Lua(Slice<const char*> args, sqlite3* db, DbAssets* assets,
         Allocator* allocator)
    : args_(args),
      allocator_(allocator),
      db_(db),
      assets_(assets),
      scripts_by_name_(allocator),
      scripts_(allocator),
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

void Lua::AddLibrary(const char* name, Slice<const luaL_Reg> funcs) {
  LUA_CHECK_STACK(state_);
  LOG("Adding library ", name);
  lua_getglobal(state_, "G");
  lua_newtable(state_);
  for (size_t i = 0; i < funcs.size(); ++i) {
    CHECK(funcs[i].name != nullptr, "Invalid entry for library ", name, ": ",
          i);
    const auto* func = &funcs[i];
    lua_pushcfunction(state_, func->func);
    lua_setfield(state_, -2, func->name);
  }
  lua_setfield(state_, -2, name);
  lua_pop(state_, 1);
}

void Lua::AddLibraryWithMetadata(const char* name,
                                 Slice<const LuaApiFunction> funcs) {
  LUA_CHECK_STACK(state_);
  LOG("Adding library ", name);
  lua_getglobal(state_, "G");
  lua_newtable(state_);
  for (size_t i = 0; i < funcs.size(); ++i) {
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
  for (size_t i = 0; i < funcs.size(); ++i) {
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
  for (size_t i = 0; i < funcs.size(); ++i) {
    LUA_CHECK_STACK(state_);
    lua_getfield(state_, -1, name);
    lua_pushstring(state_, funcs[i].name);
    lua_gettable(state_, -2);
    lua_getglobal(state_, "G");
    lua_getfield(state_, -1, name);
    lua_pushstring(state_, funcs[i].name);
    lua_gettable(state_, -2);
    auto* ptr = lua_tocfunction(state_, -1);
    lua_pushlightuserdata(state_, reinterpret_cast<void*>(ptr));
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
  if (luaL_loadbuffer(state_, script.data(), script.size(), buf.str()) != 0) {
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
        snprintf(
            ptr, 32, "0x%016llx",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(v)));
        buf->Append(ptr);
      }
    }; break;
    case LUA_TUSERDATA:
      FormatLuaUserdata(state, abs_pos, buf);
      break;
    default:
      buf->Append("?? (", lua_typename(state, lua_type(state, pos)), ")");
      break;
  }
}

bool Lua::EvalString(std::string_view code, StringBuffer* output) {
  LUA_CHECK_STACK(state_);
  int top = lua_gettop(state_);
  // Try loading as an expression first (prepend "return ").
  FixedStringBuffer<kMaxLogLineLength> expr(kTruncating);
  expr.Append("return ", code);
  bool loaded =
      luaL_loadbuffer(state_, expr.str(), strlen(expr.str()), "=eval") == 0;
  if (!loaded) {
    lua_pop(state_, 1);
    // Fall back to loading as a statement.
    loaded = luaL_loadbuffer(state_, code.data(), code.size(), "=eval") == 0;
  }
  if (!loaded) {
    output->Append(luaL_checkstring(state_, -1));
    lua_settop(state_, top);
    return false;
  }
  if (lua_pcall(state_, 0, LUA_MULTRET, 0) != 0) {
    output->Append(luaL_checkstring(state_, -1));
    lua_settop(state_, top);
    return false;
  }
  // Format all return values.
  int nresults = lua_gettop(state_) - top;
  for (int i = 1; i <= nresults; ++i) {
    if (i > 1) output->Append("\t");
    LogValue(state_, top + i, /*depth=*/0, output);
  }
  lua_settop(state_, top);
  return true;
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
  SqlTransaction txn(db_);
  SqlStmt stmt(db_,
               "INSERT OR REPLACE INTO compilation_cache "
               "(source_name, source_hash, compiled) VALUES (?, ?, ?)");
  CHECK(stmt.ok(), "Failed to prepare compilation cache insert");
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
      int top = lua_gettop(state_);
      if (!CompileFennelAsset(script.name, script.contents)) {
        LUA_ERROR(state_, "Failed to compile ", script.name, ": \n",
                  GetLuaString(state_, -1));
        return;
      }
      // CompileFennelAsset leaves package/loaded/fennel tables and the
      // compiled result on the stack. Clean up since we only need the
      // result in the compilation cache, not on the stack.
      lua_settop(state_, top);
      CHECK(compilation_cache_.Lookup(script.name, &cached_script),
            "Did not find ", script.name,
            " in compilation cache. File is corrupted?");
    }
    stmt.BindText(1, script.name);
    stmt.BindInt64(2, checksum);
    stmt.BindText(3, cached_script.contents);
    MUST(stmt.Step());
    stmt.Reset();
  }
  sqlite3_exec(db_, "END TRANSACTION", nullptr, nullptr, nullptr);
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
    CHECK(r.name != nullptr, "Null entry in metatable '", metatable_name,
          "' at index ", i,
          " — remove {nullptr, nullptr} sentinel, LoadMetatable uses array "
          "size directly");
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

void Lua::SetError(std::string_view file, int line, std::string_view error) {
  if (!file.empty()) {
    error_.Set("[", file, ":", line, "] ");
  } else {
    error_.Set("");
  }
  // Remove lines with [C] or "(tail call)" since they are not useful.
  size_t st = 0, en = 0;
  auto flush = [&] {
    if (en <= st) return;
    std::string_view segment = error.substr(st, en - st + 1);
    std::string_view trimmed = Trim(segment);
    if (!HasPrefix(trimmed, "[C]") && !HasPrefix(trimmed, "(tail call)")) {
      error_.Append(segment);
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
  SqlStmt stmt(db_,
               "SELECT c.source_name, c.compiled, c.source_hash "
               "FROM asset_metadata a "
               "INNER JOIN compilation_cache c "
               "WHERE a.name = c.source_name AND c.source_hash = a.hash");
  CHECK(stmt.ok(), "Failed to prepare compilation cache query");
  while (MUST(stmt.Step())) {
    auto name = stmt.ColumnText(0);
    ByteSlice blob = stmt.ColumnBlob(1);
    auto* buffer = static_cast<char*>(allocator_->Alloc(blob.size(), 1));
    std::memcpy(buffer, blob.data(), blob.size());
    CachedScript script;
    script.contents = std::string_view(buffer, blob.size());
    script.checksum = stmt.ColumnInt64(2);
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
    LUA_ERROR(state_, "Expected main.lua to return a table");
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
    LUA_ERROR(state_, "Could not find asset ", modname, ".lua");
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

void Lua::SetPackagePreload(std::string_view modname) {
  // We use a buffer to ensure that filename is null terminated.
  FixedStringBuffer<kMaxPathLength> buf(modname);
  lua_getglobal(state_, "package");
  lua_getfield(state_, -1, "preload");
  lua_pushcfunction(state_, PackageLoaderShim);
  lua_setfield(state_, -2, buf.str());
  lua_pop(state_, 2);
}

// Pushes the active scene table (or _Game if scenes aren't active).
void Lua::PushCallbackTarget() { PushActiveScene(state_); }

void Lua::Init() {
  LUA_CHECK_STACK(state_);

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

  if (!error_.empty()) return;
  READY();
  t_ = t;
  dt_ = dt;
  ProcessPendingTransition(state_);
  PushCallbackTarget();
  lua_getfield(state_, -1, "update");
  lua_insert(state_, -2);
  lua_pushnumber(state_, static_cast<double>(t));
  lua_pushnumber(state_, static_cast<double>(dt));
  if (lua_pcall(state_, 3, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::Draw() {
  LUA_CHECK_STACK(state_);

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
  lua_getfield(state_, -1, "draw");
  lua_insert(state_, -2);
  if (lua_pcall(state_, 1, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleKeypressed(int scancode) {
  LUA_CHECK_STACK(state_);

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
  lua_getfield(state_, -1, "mousemoved");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushnumber(state_, static_cast<double>(pos.x));
  lua_pushnumber(state_, static_cast<double>(pos.y));
  lua_pushnumber(state_, static_cast<double>(delta.x));
  lua_pushnumber(state_, static_cast<double>(delta.y));
  if (lua_pcall(state_, 5, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
  lua_pop(state_, 1);
}

void Lua::StartTestCoroutine() {
  LUA_CHECK_STACK(state_);

  if (!error_.empty()) return;
  READY();
  lua_getglobal(state_, "_Game");
  lua_getfield(state_, -1, "test_inputs");
  if (!lua_isfunction(state_, -1)) {
    lua_pop(state_, 2);
    LOG("No _Game.test_inputs function defined; nothing to run in test mode");
    Stop();
    return;
  }
  // Create a new coroutine and anchor it in the registry so it survives GC,
  // then seed its stack with `test_inputs` and `_Game` (which becomes the
  // implicit `self` argument on the first resume). lua_xmove transfers
  // values between threads owned by the same Lua state.
  //
  // Stack at this point: _Game, test_inputs
  test_co_ = lua_newthread(state_);
  // Stack: _Game, test_inputs, thread
  test_co_ref_ = luaL_ref(state_, LUA_REGISTRYINDEX);
  // Stack: _Game, test_inputs
  lua_xmove(state_, test_co_, 1);
  // Stack: _Game
  lua_pushvalue(state_, -1);
  lua_xmove(state_, test_co_, 1);
  lua_pop(state_, 1);
  // test_co_ stack now: test_inputs, self
}

void Lua::ResumeTestCoroutine() {
  if (test_co_ == nullptr) return;
  // If anything in the engine has errored since the last resume (a script
  // failed to load, hot-reload blew up, etc.), the coroutine can no longer
  // make progress. Tear it down, mark the test as failed, and ask the engine
  // to stop so RunGame returns the failure exit code.
  if (!error_.empty()) {
    test_exit_code_ = 1;
    luaL_unref(state_, LUA_REGISTRYINDEX, test_co_ref_);
    test_co_ref_ = LUA_NOREF;
    test_co_ = nullptr;
    Stop();
    return;
  }
  // On the first resume, we have (test_inputs, self) on the coroutine stack
  // and pass 1 arg. On subsequent resumes the yielded coroutine takes 0 args.
  if (test_co_wait_frames_ > 0) {
    --test_co_wait_frames_;
    return;
  }
  int nargs = test_co_first_resume_ ? 1 : 0;
  test_co_first_resume_ = false;
  int status = lua_resume(test_co_, nargs);
  if (status == LUA_YIELD) {
    // The yielded value (if any) is the number of additional frames to wait
    // before resuming again. The current frame counts as one already-elapsed
    // wait step, so subtract 1.
    int requested = 0;
    if (lua_gettop(test_co_) >= 1 && lua_isnumber(test_co_, -1)) {
      requested = static_cast<int>(lua_tonumber(test_co_, -1));
    }
    lua_settop(test_co_, 0);
    test_co_wait_frames_ = requested > 0 ? requested - 1 : 0;
    return;
  }
  if (status == 0) {
    LOG("Test coroutine finished successfully");
    test_exit_code_ = 0;
  } else {
    const char* msg =
        lua_isstring(test_co_, -1) ? lua_tostring(test_co_, -1) : "unknown";
    LOG("Test coroutine failed: ", msg);
    test_exit_code_ = 1;
  }
  luaL_unref(state_, LUA_REGISTRYINDEX, test_co_ref_);
  test_co_ref_ = LUA_NOREF;
  test_co_ = nullptr;
  Stop();
}

void Lua::HandleQuit() {
  LUA_CHECK_STACK(state_);

  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
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
}

bool Lua::LoadProtoDescriptor(ByteSlice data) {
  LUA_CHECK_STACK(state_);
  lua_getglobal(state_, "pb");
  lua_getfield(state_, -1, "load");
  lua_pushlstring(state_, reinterpret_cast<const char*>(data.data()),
                  data.size());
  if (lua_pcall(state_, 1, 1, 0) != 0) {
    LOG("Failed to load proto descriptor: ", lua_tostring(state_, -1));
    lua_pop(state_, 2);  // error + pb
    return false;
  }
  lua_pop(state_, 2);  // result + pb
  return true;
}

void Lua::HandleNetworkConnect(uint32_t peer_id) {
  LUA_CHECK_STACK(state_);
  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
  lua_getfield(state_, -1, "on_connect");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushinteger(state_, peer_id);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleNetworkDisconnect(uint32_t peer_id) {
  LUA_CHECK_STACK(state_);
  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
  lua_getfield(state_, -1, "on_disconnect");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushinteger(state_, peer_id);
  if (lua_pcall(state_, 2, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

void Lua::HandleNetworkReceive(uint32_t peer_id, ByteSlice data,
                               uint8_t channel) {
  LUA_CHECK_STACK(state_);
  if (!error_.empty()) return;
  READY();
  PushCallbackTarget();
  lua_getfield(state_, -1, "on_receive");
  if (lua_isnil(state_, -1)) {
    lua_pop(state_, 2);
    return;
  }
  lua_insert(state_, -2);
  lua_pushinteger(state_, peer_id);
  lua_pushlstring(state_, reinterpret_cast<const char*>(data.data()),
                  data.size());
  lua_pushinteger(state_, channel);
  if (lua_pcall(state_, 4, 0, traceback_handler_)) {
    lua_error(state_);
    return;
  }
}

#undef LUA_LOG_VALUE

}  // namespace G
