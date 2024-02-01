#pragma once
#ifndef _GAME_LUA_H
#define _GAME_LUA_H

#include <string_view>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "SDL.h"
#include "assets.h"
#include "clock.h"
#include "stats.h"

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
  Lua(std::string_view scriptname, Assets* assets, Allocator* allocator);
  ~Lua() { lua_close(state_); }

  template <typename T>
  void Register(T* t) {
    Registry<T>::Register(state_, t);
  }

  void Init();

  void Update(float t, float dt);

  void Draw();

  void Stop() { stopped_ = true; }
  bool Stopped() const { return stopped_; }

  Stats AllocatorStats() { return allocator_stats_; };

  Allocator* allocator() const { return allocator_; }

 private:
  void LoadAssets();

  void Load(std::string_view script_name);

  void LoadMain(const ScriptAsset& asset);
  void SetPackagePreload(std::string_view filename);

  void* Alloc(void* ptr, size_t osize, size_t nsize);

  static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<Lua*>(ud)->Alloc(ptr, osize, nsize);
  }

  lua_State* state_ = nullptr;
  bool stopped_ = false;
  int traceback_handler_;

  Allocator* allocator_;
  Assets* assets_;

  Stats allocator_stats_;
};

}  // namespace G

#endif  // _GAME_LUA_H