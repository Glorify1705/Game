#include "lua_bytebuffer.h"

namespace G {
namespace {

constexpr luaL_Reg kByteBufferMethods[] = {
    {"__index",
     [](lua_State* state) {
       auto* buffer = AsUserdata<ByteBuffer>(state, 1);
       size_t index = luaL_checkinteger(state, 2);
       if (index <= 0 || index > buffer->size) {
         LUA_ERROR(state, "Index out of bounds ", index, " not in range [1, ",
                   buffer->size, "]");
       }
       lua_pushinteger(state, buffer->contents[index]);
       return 1;
     }},
    {"__len",
     [](lua_State* state) {
       auto* buffer = AsUserdata<ByteBuffer>(state, 1);
       lua_pushinteger(state, buffer->size);
       return 1;
     }},
    {"__tostring",
     [](lua_State* state) {
       auto* buffer = AsUserdata<ByteBuffer>(state, 1);
       lua_pushlstring(state, reinterpret_cast<const char*>(buffer->contents),
                       buffer->size);
       return 1;
     }},
    {"__concat", [](lua_State* state) {
       lua_getglobal(state, "tostring");
       lua_pushvalue(state, 1);
       lua_call(state, 1, 1);
       lua_getglobal(state, "tostring");
       lua_pushvalue(state, 2);
       lua_call(state, 1, 1);
       std::string_view a = GetLuaString(state, -2);
       std::string_view b = GetLuaString(state, -1);
       luaL_Buffer buffer;
       luaL_buffinit(state, &buffer);
       luaL_addlstring(&buffer, a.data(), a.size());
       luaL_addlstring(&buffer, b.data(), b.size());
       lua_pop(state, 2);
       luaL_pushresult(&buffer);
       return 1;
     }}};

const struct luaL_Reg kDataLib[] = {
    {"hash", [](lua_State* state) {
       std::string_view contents;
       switch (lua_type(state, 1)) {
         case LUA_TSTRING:
           contents = GetLuaString(state, 1);
           break;
         case LUA_TUSERDATA: {
           auto* buf = AsUserdata<ByteBuffer>(state, 1);
           contents = std::string_view(
               reinterpret_cast<const char*>(buf->contents), buf->size);
         }; break;
         default:
           LUA_ERROR(state, "Argument 1 cannot be hashed");
       }
       lua_pushnumber(state,
                      XXH64(contents.data(), contents.size(), 0xC0D315D474));
       return 1;
     }}};

}  // namespace

uint8_t* PushBufferIntoLua(lua_State* state, size_t size) {
  auto* buf = static_cast<ByteBuffer*>(
      lua_newuserdata(state, sizeof(ByteBuffer) + size));
  buf->size = size;
  luaL_getmetatable(state, "byte_buffer");
  lua_setmetatable(state, -2);
  return buf->contents;
}

void AddByteBufferLibrary(Lua* lua) {
  lua->LoadMetatable("byte_buffer", kByteBufferMethods);
  lua->AddLibrary("data", kDataLib);
}

}  // namespace G
