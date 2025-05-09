#include "lua_graphics.h"

#include <algorithm>

#include "SDL.h"
#include "allocators.h"
#include "clock.h"
#include "image.h"
#include "lua_bytebuffer.h"
#include "lua_filesystem.h"
#include "renderer.h"
#include "units.h"

namespace G {
namespace {

static const LuaApiFunction kGraphicsLib[] = {
    {"clear",
     "Clear the screen to black",
     {},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->ClearForFrame();
       return 0;
     }},
    {"take_screenshot",
     "Saves a screenshot from the contents of the current framebuffer",
     {{"file?",
       "If provided, a filename where we should write the screenshot."}},
     {{"result",
       "If a file was provided, nil if the write suceeded or an error message "
       "otherwise. If no file was provided, a byte buffer with the image "
       "contents"}},
     [](lua_State* state) {
       TIMER("Screenshot");

       const bool write_to_file = lua_gettop(state) == 1;

       auto* renderer = Registry<BatchRenderer>::Retrieve(state);
       auto* allocator = Registry<Lua>::Retrieve(state)->allocator();

       ArenaAllocator scratch(allocator, Megabytes(32));
       auto screenshot = renderer->TakeScreenshot(&scratch);

       QoiDesc desc;
       desc.width = screenshot.width;
       desc.height = screenshot.height;
       desc.channels = 4;
       desc.colorspace = 0;

       void* bytebuf = PushBufferIntoLua(state, MemoryNeededToEncode(&desc));

       int size;
       bool error;
       QoiEncode(screenshot.buffer, &desc, &size, bytebuf, &error);

       if (error) {
         LUA_ERROR(state, "Failed to encode screenshot");
         return 0;
       }

       if (write_to_file) {
         // Write the image to the screenshot file.
         const int result = LuaWriteToFile(state, -1, GetLuaString(state, 1));
         // Pop the byte buffer since we do not need it anymore.
         lua_pop(state, 1);
         return result;
       }

       return 1;
     }},
    {"draw_sprite",
     "Draws a sprite by name to the screen",
     {{"sprite", "the name of the sprite in any sprite sheet"},
      {"x",
       "the x position (left-right) in screen coordinates where to draw the "
       "sprite"},
      {"y",
       "the y position (top-bottom) in screen coordinates where to draw the "
       "sprite"},
      {"angle?", "if provided, the angle to rotate the sprite"}},
     {},
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       std::string_view sprite_name = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawSprite(sprite_name, FVec(x, y), angle);
       return 0;
     }},
    {"draw_image",
     "Draws an image by name to the screen",
     {{"sprite", "the name of the sprite in any sprite sheet"},
      {"x",
       "the x position (left-right) in screen coordinates where to draw the "
       "sprite"},
      {"y",
       "the y position (top-bottom) in screen coordinates where to draw the "
       "sprite"},
      {"angle?", "if provided, the angle to rotate the sprite"}},
     {},
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       std::string_view image_name = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawImage(image_name, FVec(x, y), angle);
       return 0;
     }},
    {"draw_rect",
     "Draws a solid rectangle to the screen, with the color provided by the "
     "global context",
     {{"x1", "the x coordinate for the top left of the rectangle"},
      {"y1", "the y position for the top left of the rectangle"},
      {"x2", "the x position for the bottom right of the rectangle"},
      {"y2", "the y position for the bottom right of the rectangle"},
      {"angle?", "if provided, the angle to rotate the rectangle"}},
     {},
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       const float x1 = luaL_checknumber(state, 1);
       const float y1 = luaL_checknumber(state, 2);
       const float x2 = luaL_checknumber(state, 3);
       const float y2 = luaL_checknumber(state, 4);
       float angle = 0;
       if (parameters == 5) angle = luaL_checknumber(state, 5);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawRect(FVec(x1, y1), FVec(x2, y2), angle);
       return 0;
     }},
    {"set_color",
     "Set the global context color for all subsequent operations",
     {{"1:color", "a string representing a color name"},
      {"2:r", "r component of the RGBA for the color"},
      {"2:g", "g component of the RGBA for the color"},
      {"2:b", "b component of the RGBA for the color"},
      {"2:a", "a component of the RGBA for the color"}},
     {},
     [](lua_State* state) {
       Color color = Color::Zero();
       if (lua_gettop(state) == 1) {
         std::string_view s = GetLuaString(state, 1);
         if (!ColorFromTable(s, &color)) {
           LUA_ERROR(state, "Unknown color ", s);
           return 0;
         }
       } else {
         auto clamp = [](float f) -> uint8_t {
           return std::clamp(f, 0.0f, 255.0f);
         };
         color.r = clamp(luaL_checknumber(state, 1));
         color.g = clamp(luaL_checknumber(state, 2));
         color.b = clamp(luaL_checknumber(state, 3));
         color.a = clamp(luaL_checknumber(state, 4));
       }
       auto* renderer = Registry<Renderer>::Retrieve(state);
       const Color previous = renderer->SetColor(color);
       lua_newtable(state);
       lua_pushnumber(state, previous.r);
       lua_setfield(state, -2, "r");
       lua_pushnumber(state, previous.g);
       lua_setfield(state, -2, "g");
       lua_pushnumber(state, previous.b);
       lua_setfield(state, -2, "b");
       lua_pushnumber(state, previous.a);
       lua_setfield(state, -2, "a");
       return 0;
     }},
    {"draw_circle",
     "Draws a circle with the global context color to the screen",
     {{"x",
       "the x position (left-right) in screen coordinates of the center of the "
       "circle"},
      {"y",
       "the y position (top-bottom) in screen coordinates of the center of the "
       "circle"},
      {"r", "the radius in pixels of the center of the circle"}},
     {},
     [](lua_State* state) {
       const float x = luaL_checknumber(state, 1);
       const float y = luaL_checknumber(state, 2);
       const float radius = luaL_checknumber(state, 3);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawCircle(FVec(x, y), radius);
       return 0;
     }},
    {"draw_triangle",
     "Draws a triangle with the global context color to the screen",
     {{"p1x",
       "The x coordinate in screen coordinates of the first point of the "
       "triangle"},
      {"p1y",
       "The y coordinate in screen coordinates of the first point of the "
       "triangle"},
      {"p2x",
       "The x coordinate in screen coordinates of the second point of the "
       "triangle"},
      {"p2y",
       "The y coordinate in screen coordinates of the second point of the "
       "triangle"},
      {"p3x",
       "The x coordinate in screen coordinates of the third point of the "
       "triangle"},
      {"p3y",
       "The y coordinate in screen coordinates of the third point of the "
       "triangle"}},
     {},
     [](lua_State* state) {
       const auto p1 =
           FVec(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       const auto p2 =
           FVec(luaL_checknumber(state, 3), luaL_checknumber(state, 4));
       const auto p3 =
           FVec(luaL_checknumber(state, 5), luaL_checknumber(state, 6));
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawTriangle(p1, p2, p3);
       return 0;
     }},
    {"draw_line",
     "Draws a line with the global context color to the screen",
     {{"p1x",
       "The x coordinate in screen coordinates of the first point of the line"},
      {"p1y",
       "The y coordinate in screen coordinates of the first point of the line"},
      {"p2x",
       "The x coordinate in screen coordinates of the second point of the "
       "line"},
      {"p2y",
       "The y coordinate in screen coordinates of the second point of the "
       "line"}},
     {},
     [](lua_State* state) {
       const auto p1 =
           FVec(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       const auto p2 =
           FVec(luaL_checknumber(state, 3), luaL_checknumber(state, 4));
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawLine(p1, p2);
       return 0;
     }},
    {"draw_lines",
     "Draws a list of lines with the global context color to the screen",
     {{"ps", "A list of points, two consecutive points i and i+1 for a line."},
      {"p1y",
       "The y coordinate in screen coordinates of the first point of the line"},
      {"p2x",
       "The x coordinate in screen coordinates of the second point of the "
       "line"},
      {"p2y",
       "The y coordinate in screen coordinates of the second point of the "
       "line"}},
     {},
     [](lua_State* state) {
       if (!lua_istable(state, 1)) {
         LUA_ERROR(state, "not a table");
         return 0;
       }
       const size_t n = lua_objlen(state, 1);
       auto* allocator = Registry<Lua>::Retrieve(state)->allocator();
       ArenaAllocator scratch(allocator, (n + 1) * sizeof(FVec2));
       FixedArray<FVec2> temp(n, &scratch);
       for (size_t i = 1; i <= n; ++i) {
         lua_rawgeti(state, 1, i);
         lua_rawgeti(state, -1, 1);
         float x = luaL_checknumber(state, -1);
         lua_pop(state, 1);
         lua_rawgeti(state, -1, 2);
         float y = luaL_checknumber(state, -1);
         lua_pop(state, 2);
         temp.Push(FVec(x, y));
       }
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->DrawLines(temp.data(), temp.size());
       return 0;
     }},
    {"print",
     "Writes text to the screen with debug font and fixed size. For quick "
     "debug printing.",
     {{"text",
       "A string or byte buffer with the contents to render to the screen"},
      {"x",
       "Horizontal position in screen space pixels left-to-right where to "
       "render the text"},
      {"y",
       "Vertical position in screen space pixels top-to-bottom where to render "
       "the text"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view font = "debug_font.ttf";
       const uint32_t font_size = 24;
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       if (lua_type(state, 1) == LUA_TSTRING) {
         std::string_view text = GetLuaString(state, 1);
         renderer->DrawText(font, font_size, text, FVec(x, y));
       } else if (lua_type(state, 1) == LUA_TUSERDATA) {
         auto* buf = AsUserdata<ByteBuffer>(state, 1);
         std::string_view text(reinterpret_cast<const char*>(buf->contents),
                               buf->size);
         renderer->DrawText(font, font_size, text, FVec(x, y));
       }
       return 0;
     }},
    {"draw_text",
     "Writes text to the screen.",
     {{"font", "Font name to use for writing text"},
      {"text",
       "A string or byte buffer with the contents to render to the screen"},
      {"size", "Size in pixels to use for rendering the text"},
      {"x",
       "Horizontal position in screen space pixels left-to-right where to "
       "render the text"},
      {"y",
       "Vertical position in screen space pixels top-to-bottom where to render "
       "the text"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view font = GetLuaString(state, 1);
       const uint32_t font_size = luaL_checkinteger(state, 2);
       const float x = luaL_checknumber(state, 4);
       const float y = luaL_checknumber(state, 5);
       if (lua_type(state, 3) == LUA_TSTRING) {
         std::string_view text = GetLuaString(state, 3);
         renderer->DrawText(font, font_size, text, FVec(x, y));
       } else if (lua_type(state, 3) == LUA_TUSERDATA) {
         auto* buf = AsUserdata<ByteBuffer>(state, 3);
         std::string_view text(reinterpret_cast<const char*>(buf->contents),
                               buf->size);
         renderer->DrawText(font, font_size, text, FVec(x, y));
       }
       return 0;
     }},
    {"text_dimensions",
     "Returns the dimensions for a text rendered with a given font and size",
     {{"font", "Font name to use for writing text"},
      {"size", "Size in pixels that the text would be rendered to the screen"},
      {"text",
       "A string or byte buffer with the contents that would be rendered to "
       "the screen"}},
     {{"width", "Width in pixels the text would occupy in the screen"},
      {"height", "Height in pixels the text would occupy in the screen"}},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       std::string_view font = GetLuaString(state, 1);
       const uint32_t font_size = luaL_checkinteger(state, 2);
       std::string_view text = GetLuaString(state, 3);
       const IVec2 result = renderer->TextDimensions(font, font_size, text);
       lua_pushinteger(state, result.x);
       lua_pushinteger(state, result.y);
       return 2;
     }},
    {"push",
     "Push a transform to the screen into the transform stack.",
     {{"transform", "A 4x4 matrix with the transform to push"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Push();
       return 0;
     }},
    {"pop",
     "Pop the transform at the top of the stack. It will not apply anymore.",
     {},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Pop();
       return 0;
     }},
    {"rotate",
     "Push a transform to the screen that rotates all objects by a given angle",
     {{"angle",
       "All objects will be rotated by this angle in radians clockwise"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Rotate(luaL_checknumber(state, 1));
       return 0;
     }},
    {"scale",
     "Push a transform to the screen that scales all objects by a given angle",
     {{"xf", "Scalar factor to scale up the x coordinate"},
      {"yf", "Scalar factor to scale up the y coordinate"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Scale(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"translate",
     "Translate all objects in the screen by moving the coordinate system "
     "center",
     {{"x", "New x position of the coordinate system center"},
      {"y", "New y position of the coordinate system center"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Translate(luaL_checknumber(state, 1),
                           luaL_checknumber(state, 2));
       return 0;
     }},
    {"new_shader",
     "Creates a new shader with a given name and source code, compiling it in "
     "the GPU",
     {{"shader?",
       "Shader to attach, if nothing is passed then pre_pass.frag will be "
       "passed"}},
     {},
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       std::string_view code = GetLuaString(state, 2);
       const bool compiles = shaders->Compile(
           HasSuffix(name, ".vert") ? DbAssets::ShaderType::kVertex
                                    : DbAssets::ShaderType::kFragment,
           name, code, Shaders::kUseCache);
       if (!compiles) {
         LUA_ERROR(state, "Could not compile shader ", name, ": ",
                   shaders->LastError());
       }
       return 0;
     }},
    {"attach_shader",
     "Attach a shader by name, if no shader is passed resets to the default "
     "shader",
     {{"shader?",
       "Shader to attach, if nothing is passed then pre_pass.frag will be "
       "passed"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<BatchRenderer>::Retrieve(state);
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view fragment_shader;
       if (lua_gettop(state) == 0) {
         fragment_shader = "pre_pass.frag";
       } else {
         fragment_shader = GetLuaString(state, 1);
       }
       std::string_view program_name = fragment_shader;
       if (!ConsumeSuffix(&program_name, ".frag")) {
         LUA_ERROR(state, "Could not switch shader ", program_name,
                   ": not a fragment shader (i.e. "
                   "name does not end in .frag)",
                   program_name);
         return 0;
       }
       if (!shaders->Link(program_name, "pre_pass.vert", fragment_shader,
                          Shaders::kUseCache)) {
         LUA_ERROR(state, "Could not switch shader ", program_name, ": ",
                   shaders->LastError());
         return 0;
       }
       renderer->SetShaderProgram(program_name);
       shaders->UseProgram(program_name);
       return 0;
     }},
    {"send_uniform",
     "Sends a uniform with the given name to the current shader",
     {{"name", "Name of the uniform to send"},
      {"value",
       "Value to send. Supported values are G.math.v2,v3,v4, G.math.m2x2, "
       "G.math.m3x3, G.math.m4x4, and floats"}},
     {},
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (lua_isnumber(state, 2)) {
         if (!shaders->SetUniformF(name, luaL_checknumber(state, 2))) {
           LUA_ERROR(state, "Could not set uniform ", name, ": ",
                     shaders->LastError());
         }
       } else {
         if (!lua_getmetatable(state, 2)) {
           LUA_ERROR(state, "Invalid parameter");
         }
         lua_getfield(state, -1, "send_as_uniform");
         if (!lua_isfunction(state, -1)) {
           LUA_ERROR(state, "Passed parameter has no `send_as_uniform` method");
         }
         lua_pushvalue(state, 2);
         lua_pushvalue(state, 1);
         lua_call(state, 2, LUA_MULTRET);
       }
       return 0;
     }},
    {"new_canvas",
     "Unimplemented.",
     {},
     {},
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"set_canvas",
     "Unimplemented.",
     {},
     {},
     [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }},
    {"draw_canvas", "Unimplemented.", {}, {}, [](lua_State* state) {
       LUA_ERROR(state, "Unimplemented");
       return 0;
     }}};

static const luaL_Reg kWindowLib[] = {
    {"dimensions",
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       IVec2 viewport = renderer->viewport();
       lua_pushnumber(state, viewport.x);
       lua_pushnumber(state, viewport.y);
       return 2;
     }},
    {"set_dimensions",
     [](lua_State* state) {
       auto* renderer = Registry<BatchRenderer>::Retrieve(state);
       size_t x = luaL_checkinteger(state, 1);
       size_t y = luaL_checkinteger(state, 2);
       renderer->SetViewport(IVec(x, y));
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowSize(window, x, y);
       return 0;
     }},
    {"set_fullscreen",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
       return 0;
     }},
    {"set_borderless",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
       return 0;
     }},
    {"set_windowed",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       // 0 means we use windowed mode.
       SDL_SetWindowFullscreen(window, 0);
       return 0;
     }},
    {"set_title",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowTitle(window, luaL_checkstring(state, 1));
       return 0;
     }},
    {"get_title",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushstring(state, SDL_GetWindowTitle(window));
       return 1;
     }},
    {"has_input_focus",
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }},
    {"has_mouse_focus", [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }}};

}  // namespace

void AddGraphicsLibrary(Lua* lua) {
  lua->AddLibrary("graphics", kGraphicsLib);
  lua->AddLibrary("window", kWindowLib);
}

}  // namespace G
