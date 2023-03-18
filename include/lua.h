#pragma once
#ifndef _GAME_LUA_H
#define _GAME_LUA_H

#include <string_view>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "assets.h"
#include "clock.h"

namespace G {

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
      luaL_error(state, "Could not find a module for %s", typeid(T).name());
      return nullptr;
    }
    return result;
  }

 private:
  inline static char kKey = 'k';
};

class Lua {
 public:
  Lua(const char* scriptname, Assets* assets);
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

 private:
  void LoadMain(const ScriptFile& asset);
  void SetPackagePreload(std::string_view filename);

  lua_State* state_ = nullptr;
  bool stopped_ = false;
  int traceback_handler_;
};

}  // namespace G

#endif  // _GAME_LUA_H