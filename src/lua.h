#pragma once
#ifndef _GAME_LUA_H
#define _GAME_LUA_H

#include <csetjmp>
#include <string_view>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "SDL.h"
#include "assets.h"
#include "clock.h"
#include "libraries/sqlite3.h"
#include "mat.h"
#include "stats.h"
#include "vec.h"
#include "xxhash.h"

namespace G {

template <typename T>
constexpr const char* Typename() {
  return typeid(T).name();
}

template <>
constexpr const char* Typename<SDL_Window>() {
  return "SDL_Window";
}

template <typename T>
class Registry {
 public:
  static void Register(lua_State* state, T* ptr) {
    lua_pushlightuserdata(state, static_cast<void*>(&kKey));
    lua_pushlightuserdata(state, static_cast<void*>(ptr));
    lua_settable(state, LUA_REGISTRYINDEX);
  }

  static T* Retrieve(lua_State* state) {
    lua_pushlightuserdata(state, (void*)&kKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    auto* result = static_cast<T*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (result == nullptr) {
      luaL_error(state, "Could not find a module for %s", Typename<T>());
      return nullptr;
    }
    return result;
  }

 private:
  inline static char kKey = 'k';
};

// Forward declare for the userdata name.
struct ByteBuffer;

template <typename T>
struct UserdataName;

#define USERDATA_ENTRY(type, name)                    \
  template <>                                         \
  struct UserdataName<type> {                         \
    inline static constexpr const char* kName = name; \
  }

USERDATA_ENTRY(FVec2, "fvec2");
USERDATA_ENTRY(FVec3, "fvec3");
USERDATA_ENTRY(FVec4, "fvec4");
USERDATA_ENTRY(FMat2x2, "fmat2x2");
USERDATA_ENTRY(FMat3x3, "fmat3x3");
USERDATA_ENTRY(FMat4x4, "fmat4x4");
USERDATA_ENTRY(DbAssets::Sprite, "asset_sprite_ptr");
USERDATA_ENTRY(ByteBuffer, "byte_buffer");

#undef USERDATA_ENTRY

template <typename T, typename... Args>
T* NewUserdata(lua_State* state, Args... args) {
  auto* userdata = static_cast<T*>(lua_newuserdata(state, sizeof(T)));
  new (userdata) T(args...);
  luaL_getmetatable(state, UserdataName<T>::kName);
  lua_setmetatable(state, -2);
  return userdata;
}

template <typename T>
T* AsUserdata(lua_State* state, int index) {
  return reinterpret_cast<T*>(
      luaL_checkudata(state, index, UserdataName<T>::kName));
}

#define LUA_ERROR(state, ...)                                                \
  do {                                                                       \
    FixedStringBuffer<kMaxLogLineLength> _luaerror_buffer;                   \
    _luaerror_buffer.Append(Basename(__FILE__), ":", __LINE__,               \
                            "]: ", ##__VA_ARGS__);                           \
    lua_pushlstring(state, _luaerror_buffer.str(), _luaerror_buffer.size()); \
    lua_error(state);                                                        \
    __builtin_unreachable();                                                 \
  } while (0);

inline std::string_view GetLuaString(lua_State* state, int index) {
  size_t len;
  const char* data = luaL_checklstring(state, index, &len);
  return std::string_view(data, len);
}

struct LuaApiFunctionArg {
  const char* name = {0};
  const char* docs = {0};
};

struct LuaApiFunctionArgList {
  size_t argc;
  LuaApiFunctionArg args[64];

  LuaApiFunctionArgList(std::initializer_list<LuaApiFunctionArg> a)
      : argc(a.size()) {
    size_t i = 0;
    for (const auto& arg : a) {
      if (i >= 64) break;
      args[i++] = arg;
    }
  }

  const LuaApiFunctionArg& operator[](size_t i) const { return args[i]; }
};

struct LuaApiFunction {
  const char* name = {0};
  const char* docstring = {0};
  LuaApiFunctionArgList args;
  LuaApiFunctionArgList returns;
  lua_CFunction func = nullptr;
};

class Lua {
 public:
  Lua(size_t argc, const char** argv, sqlite3* db, DbAssets* assets,
      Allocator* allocator);
  ~Lua() { lua_close(state_); }

  template <typename T>
  void Register(T* t) {
    Registry<T>::Register(state_, t);
  }

  void LoadLibraries();

  void LoadScript(const DbAssets::Script& script);

  void LoadMain();

  void Init();

  void Update(float t, float dt);

  void Draw();

  void BuildCompilationCache();

  void FlushCompilationCache();

  bool LoadFromCache(std::string_view script_name, lua_State* state);

  void InsertIntoCache(std::string_view script_name, lua_State* state);

  void LogValue(int pos, int depth, FixedStringBuffer<kMaxLogLineLength>* buf);

  // Handles events if callbacks are present
  void HandleKeypressed(int scancode);
  void HandleKeyreleased(int scancode);
  void HandleMousePressed(int button);
  void HandleMouseReleased(int button);
  void HandleMouseMoved(FVec2 pos, FVec2 delta);
  void HandleTextInput(std::string_view input);
  void HandleQuit();

  void Stop() { stopped_ = true; }
  bool Stopped() const { return stopped_; }

  Stats AllocatorStats() { return allocator_stats_; };

  Allocator* allocator() const { return allocator_; }

  // Checks whether there is a permanent error and returns the message length.
  size_t Error(char* buf, size_t max_size);
  bool HasError() { return !error_.empty(); }

  void ClearError() { error_.Clear(); }

  void SetError(std::string_view file, int line, std::string_view error);

  void Crash();

  double time() const { return t_; }
  double dt() const { return dt_; }

  size_t argc() const { return argc_; }
  std::string_view argv(size_t i) const { return argv_[i]; }

  void RequestHotload() { hotload_requested_ = true; }

  void RunGc() {
    TIMER("GC");
    lua_gc(state_, LUA_GCCOLLECT, /*data=*/0);
  }

  bool HotloadRequested() {
    const bool result = hotload_requested_;
    hotload_requested_ = false;
    return result;
  }

  size_t MemoryUsage() {
    size_t mem_kb = lua_gc(state_, LUA_GCCOUNT, 0);
    size_t mem_b = lua_gc(state_, LUA_GCCOUNTB, 0);
    return Kilobytes(mem_kb) + mem_b;
  }

  int PackageLoader();

  friend void AddGraphicsLibrary(Lua* lua);
  friend void AddMathLibrary(Lua* lua);
  friend void AddRandomLibrary(Lua* lua);
  friend void AddSystemLibrary(Lua* lua);
  friend void AddInputLibrary(Lua* lua);
  friend void AddPhysicsLibrary(Lua* lua);
  friend void AddSoundLibrary(Lua* lua);
  friend void AddBufferLibrary(Lua* lua);
  friend void AddFilesystemLibrary(Lua* lua);
  friend void AddByteBufferLibrary(Lua* lua);
  friend void AddAssetsLibrary(Lua* lua);

 private:
  int LoadLuaAsset(std::string_view filename, std::string_view script_contents,
                   int traceback_handler = INT_MAX);

  int LoadFennelAsset(std::string_view filename,
                      std::string_view script_contents,
                      int traceback_handler = INT_MAX);

  // Compiles the fennel asset and leaves the result in the top of the Lua
  // stack.
  bool CompileFennelAsset(std::string_view filename,
                          std::string_view script_contents,
                          int traceback_handler = INT_MAX);

  void LoadAssets();
  void LoadMetatable(const char* metatable_name, const luaL_Reg* registers,
                     size_t register_count);

  template <size_t N>
  void LoadMetatable(const char* metatable_name,
                     const luaL_Reg (&registers)[N]) {
    LoadMetatable(metatable_name, registers, N);
  }

  void SetPackagePreload(std::string_view filename);

  void* Alloc(void* ptr, size_t osize, size_t nsize);

  static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<Lua*>(ud)->Alloc(ptr, osize, nsize);
  }

  void AddLibrary(const char* name, const luaL_Reg* funcs, size_t N);
  void AddLibraryWithMetadata(const char* name, const LuaApiFunction* funcs,
                              size_t N);

  template <size_t N>
  void AddLibrary(const char* name, const luaL_Reg (&funcs)[N]) {
    AddLibrary(name, funcs, N);
  }

  template <size_t N>
  void AddLibrary(const char* name, const LuaApiFunction (&funcs)[N]) {
    AddLibraryWithMetadata(name, funcs, N);
  }

  const size_t argc_;
  const char** const argv_;

  lua_State* state_ = nullptr;
  bool stopped_ = false;
  bool single_evaluation_ = false;
  int traceback_handler_;

  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* assets_;

  FixedStringBuffer<1024> error_;
  std::jmp_buf on_error_buf_;

  Stats allocator_stats_;

  struct CachedScript {
    DbAssets::ChecksumType checksum;
    std::string_view contents;
  };

  struct Script {
    enum Language { kLuaScript, kFennelScript };

    Language language;
    std::string_view name;
    std::string_view contents;
  };

  Dictionary<Script*> scripts_by_name_;
  FixedArray<Script> scripts_;
  Dictionary<CachedScript> compilation_cache_;

  double t_ = 0;
  double dt_ = 0;

  bool hotload_requested_ = false;
};

}  // namespace G

#endif  // _GAME_LUA_H
