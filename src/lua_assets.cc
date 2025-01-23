#include "lua_assets.h"

#include "renderer.h"

namespace G {
namespace {

const struct LuaApiFunction kAssetsLib[] = {
    {"sprite",
     "Returns a sprite object ptr by name. Returns nil if it cannot find.",
     {{"name", "name of the sprite to fetch"}},
     {{"result", "A userdata ptr to a sprite object"}},
     [](lua_State* state) {
       std::string_view name = GetLuaString(state, 1);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto* sprite = renderer->GetSprite(name);
       if (sprite == nullptr) {
         lua_pushnil(state);
         return 1;
       }
       lua_pushlightuserdata(state, const_cast<DbAssets::Sprite*>(sprite));
       lua_getfield(state, LUA_REGISTRYINDEX, "asset_sprite_ptr");
       lua_setmetatable(state, -2);
       return 1;
     }},
    {"sprite_info",
     "Returns a table with width and height in pixels of a sprite.",
     {{"name", "sprite object ptr or sprite name as string"}},
     {{"result", "A table with two keys, width and height"}},
     [](lua_State* state) {
       const DbAssets::Sprite* ptr = nullptr;
       if (lua_isstring(state, 1)) {
         auto* assets = Registry<Renderer>::Retrieve(state);
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
    {"list_images",
     "Returns a list with all images",
     {},
     {{"result", "A list with name, width, height of all images."}},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       lua_newtable(state);
       for (const auto& image : renderer->GetImages()) {
         lua_pushlstring(state, image.name.data(), image.name.size());
         lua_newtable(state);
         lua_pushnumber(state, image.width);
         lua_setfield(state, -2, "width");
         lua_pushnumber(state, image.height);
         lua_setfield(state, -2, "height");
         lua_settable(state, -2);
       }
       return 1;
     }},
    {"list_sprites",
     "Returns a list with all sprites",
     {},
     {{"result",
       "A list with width, height, x, y position and spritesheet name of all "
       "sprites."}},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       lua_newtable(state);
       for (const auto& sprite : renderer->GetSprites()) {
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
         const auto* spritesheet = renderer->GetSpritesheet(sprite.spritesheet);
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
