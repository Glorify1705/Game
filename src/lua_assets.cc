#include "lua_assets.h"

#include "assets.h"

namespace G {
namespace {

const struct luaL_Reg kAssetsLib[] = {
    {"sprite",
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* assets = Registry<DbAssets>::Retrieve(state);
       auto* sprite = assets->GetSprite(name);
       if (sprite == nullptr) {
         LUA_ERROR(state, "Could not find a sprite called ", name);
       }
       lua_pushlightuserdata(state, const_cast<DbAssets::Sprite*>(sprite));
       lua_getfield(state, LUA_REGISTRYINDEX, "asset_sprite_ptr");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sprite_info",
     [](lua_State* state) {
       const DbAssets::Sprite* ptr = nullptr;
       if (lua_isstring(state, 1)) {
         auto* assets = Registry<DbAssets>::Retrieve(state);
         std::string_view name = GetLuaString(state, 1);
         ptr = assets->GetSprite(name);
       } else {
         ptr = AsUserdata<DbAssets::Sprite>(state, 1);
       }
       if (ptr == nullptr) {
         LUA_ERROR(state, "Could not find sprite");
       }
       lua_newtable(state);
       lua_pushnumber(state, ptr->width);
       lua_setfield(state, -2, "width");
       lua_pushnumber(state, ptr->height);
       lua_setfield(state, -2, "height");
       return 1;
     }},
    {"list_sprites", [](lua_State* state) {
       auto* assets = Registry<DbAssets>::Retrieve(state);
       lua_newtable(state);
       for (const auto& sprite : assets->GetSprites()) {
         lua_pushlstring(state, sprite.name.data(), sprite.name.size());
         lua_newtable(state);
         lua_pushnumber(state, sprite.width);
         lua_setfield(state, -2, "width");
         lua_pushnumber(state, sprite.height);
         lua_setfield(state, -2, "height");
         lua_pushnumber(state, sprite.x);
         lua_setfield(state, -2, "x");
         lua_pushnumber(state, sprite.y);
         lua_setfield(state, -2, "y");
         const auto* spritesheet = assets->GetSpritesheet(sprite.spritesheet);
         CHECK(spritesheet != nullptr, "No spritesheet named ",
               sprite.spritesheet);
         lua_pushlstring(state, spritesheet->name.data(),
                         spritesheet->name.size());
         lua_setfield(state, -2, "spritesheet");
         lua_settable(state, -2);
       }
       return 1;
     }}};

}  // namespace

void AddAssetsLibrary(Lua* lua) { lua->AddLibrary("assets", kAssetsLib); }

}  // namespace G
