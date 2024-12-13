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

  void LoadScripts();

  void LoadMain();

  void Init();

  void Update(float t, float dt);

  void Draw();

  bool LoadFromCache(std::string_view script_name, lua_State* state);

  void InsertIntoCache(std::string_view script_name, lua_State* state);

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

  void Crash();

  double time() const { return t_; }
  double dt() const { return dt_; }

  size_t argc() const { return argc_; }
  std::string_view argv(size_t i) const { return argv_[i]; }

 private:
  void BuildCompilationCache();

  void FlushCompilationCache();

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

  void SetError(std::string_view file, int line, std::string_view error);

  const size_t argc_;
  const char** const argv_;

  lua_State* state_ = nullptr;
  bool stopped_ = false;
  int traceback_handler_;

  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* assets_;

  FixedStringBuffer<1024> error_;
  std::jmp_buf on_error_buf_;

  Stats allocator_stats_;

  Dictionary<std::string_view> compilation_cache_;

  double t_ = 0;
  double dt_ = 0;
};

}  // namespace G

#endif  // _GAME_LUA_H
