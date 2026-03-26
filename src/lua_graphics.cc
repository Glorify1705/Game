#include "lua_graphics.h"

#include <SDL3/SDL.h>

#include <algorithm>

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
     "Clear the current render target. With no arguments clears to transparent "
     "black. With arguments clears to the given RGBA color.",
     {{"r?", "red component (0-1)", "number"},
      {"g?", "green component (0-1)", "number"},
      {"b?", "blue component (0-1)", "number"},
      {"a?", "alpha component (0-1)", "number"}},
     {},
     [](lua_State* state) {
       auto* batch = Registry<BatchRenderer>::Retrieve(state);
       const int params = lua_gettop(state);
       if (params >= 4) {
         float r = luaL_checknumber(state, 1);
         float g = luaL_checknumber(state, 2);
         float b = luaL_checknumber(state, 3);
         float a = luaL_checknumber(state, 4);
         batch->ClearWithColor(r, g, b, a);
       } else {
         batch->ClearWithColor(0, 0, 0, 0);
       }
       return 0;
     }},
    {"take_screenshot",
     "Saves a screenshot from the contents of the current framebuffer",
     {{"file?", "If provided, a filename where we should write the screenshot.",
       "string"}},
     {{"result",
       "If a file was provided, nil if the write suceeded or an error message "
       "otherwise. If no file was provided, a byte buffer with the image "
       "contents",
       "byte_buffer"}},
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
       desc.colorspace = QoiColorspace::kSrgb;

       void* bytebuf = PushBufferIntoLua(state, MemoryNeededToEncode(&desc));

       int size;
       auto encode_result = QoiEncode(screenshot.buffer, &desc, &size, bytebuf);

       if (encode_result.is_error()) {
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
     {{"sprite", "the name of the sprite in any sprite sheet", "string"},
      {"x",
       "the x position (left-right) in screen coordinates where to draw the "
       "sprite",
       "number"},
      {"y",
       "the y position (top-bottom) in screen coordinates where to draw the "
       "sprite",
       "number"},
      {"angle?", "if provided, the angle to rotate the sprite", "number"}},
     {},
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       std::string_view sprite_name = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto result = renderer->DrawSprite(sprite_name, FVec(x, y), angle);
       if (result.is_error()) {
         LUA_ERROR(state, result.error().message());
       }
       return 0;
     }},
    {"draw_image",
     "Draws an image by name to the screen",
     {{"sprite", "the name of the sprite in any sprite sheet", "string"},
      {"x",
       "the x position (left-right) in screen coordinates where to draw the "
       "sprite",
       "number"},
      {"y",
       "the y position (top-bottom) in screen coordinates where to draw the "
       "sprite",
       "number"},
      {"angle?", "if provided, the angle to rotate the sprite", "number"}},
     {},
     [](lua_State* state) {
       const int parameters = lua_gettop(state);
       std::string_view image_name = GetLuaString(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (parameters == 4) angle = luaL_checknumber(state, 4);
       auto* renderer = Registry<Renderer>::Retrieve(state);
       auto result = renderer->DrawImage(image_name, FVec(x, y), angle);
       if (result.is_error()) {
         LUA_ERROR(state, result.error().message());
       }
       return 0;
     }},
    {"draw_rect",
     "Draws a solid rectangle to the screen, with the color provided by the "
     "global context",
     {{"x1", "the x coordinate for the top left of the rectangle", "number"},
      {"y1", "the y position for the top left of the rectangle", "number"},
      {"x2", "the x position for the bottom right of the rectangle", "number"},
      {"y2", "the y position for the bottom right of the rectangle", "number"},
      {"angle?", "if provided, the angle to rotate the rectangle", "number"}},
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
     {{"1:color", "a string representing a color name", "string"},
      {"2:r", "r component of the RGBA for the color", "number"},
      {"2:g", "g component of the RGBA for the color", "number"},
      {"2:b", "b component of the RGBA for the color", "number"},
      {"2:a", "a component of the RGBA for the color", "number"}},
     {},
     [](lua_State* state) {
       Color color = Color::Zero();
       if (lua_gettop(state) == 1) {
         std::string_view s = GetLuaString(state, 1);
         auto color_result = ColorFromTable(s);
         if (color_result.is_error()) {
           LUA_ERROR(state, "Unknown color ", s);
           return 0;
         }
         color = color_result.release_value();
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
       "circle",
       "number"},
      {"y",
       "the y position (top-bottom) in screen coordinates of the center of the "
       "circle",
       "number"},
      {"r", "the radius in pixels of the center of the circle", "number"}},
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
       "triangle",
       "number"},
      {"p1y",
       "The y coordinate in screen coordinates of the first point of the "
       "triangle",
       "number"},
      {"p2x",
       "The x coordinate in screen coordinates of the second point of the "
       "triangle",
       "number"},
      {"p2y",
       "The y coordinate in screen coordinates of the second point of the "
       "triangle",
       "number"},
      {"p3x",
       "The x coordinate in screen coordinates of the third point of the "
       "triangle",
       "number"},
      {"p3y",
       "The y coordinate in screen coordinates of the third point of the "
       "triangle",
       "number"}},
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
       "The x coordinate in screen coordinates of the first point of the line",
       "number"},
      {"p1y",
       "The y coordinate in screen coordinates of the first point of the line",
       "number"},
      {"p2x",
       "The x coordinate in screen coordinates of the second point of the "
       "line",
       "number"},
      {"p2y",
       "The y coordinate in screen coordinates of the second point of the "
       "line",
       "number"}},
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
     {{"ps", "A list of points, two consecutive points i and i+1 for a line.",
       "table"}},
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
       "A string or byte buffer with the contents to render to the screen",
       "string"},
      {"x",
       "Horizontal position in screen space pixels left-to-right where to "
       "render the text",
       "number"},
      {"y",
       "Vertical position in screen space pixels top-to-bottom where to render "
       "the text",
       "number"}},
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
     {{"font", "Font name to use for writing text", "string"},
      {"text",
       "A string or byte buffer with the contents to render to the screen",
       "string"},
      {"size", "Size in pixels to use for rendering the text", "integer"},
      {"x",
       "Horizontal position in screen space pixels left-to-right where to "
       "render the text",
       "number"},
      {"y",
       "Vertical position in screen space pixels top-to-bottom where to render "
       "the text",
       "number"}},
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
     {{"font", "Font name to use for writing text", "string"},
      {"size", "Size in pixels that the text would be rendered to the screen",
       "integer"},
      {"text",
       "A string or byte buffer with the contents that would be rendered to "
       "the screen",
       "string"}},
     {{"width", "Width in pixels the text would occupy in the screen",
       "integer"},
      {"height", "Height in pixels the text would occupy in the screen",
       "integer"}},
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
     {{"transform", "A 4x4 matrix with the transform to push", "mat4x4"}},
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
       "All objects will be rotated by this angle in radians clockwise",
       "number"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Rotate(luaL_checknumber(state, 1));
       return 0;
     }},
    {"scale",
     "Push a transform to the screen that scales all objects by a given angle",
     {{"xf", "Scalar factor to scale up the x coordinate", "number"},
      {"yf", "Scalar factor to scale up the y coordinate", "number"}},
     {},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       renderer->Scale(luaL_checknumber(state, 1), luaL_checknumber(state, 2));
       return 0;
     }},
    {"translate",
     "Translate all objects in the screen by moving the coordinate system "
     "center",
     {{"x", "New x position of the coordinate system center", "number"},
      {"y", "New y position of the coordinate system center", "number"}},
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
       "passed",
       "string"}},
     {},
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       std::string_view name = GetLuaString(state, 1);
       std::string_view code = GetLuaString(state, 2);
       auto result = shaders->Compile(HasSuffix(name, ".vert")
                                          ? DbAssets::ShaderType::kVertex
                                          : DbAssets::ShaderType::kFragment,
                                      name, code, Shaders::kUseCache);
       if (result.is_error()) {
         LUA_ERROR(state, "Could not compile shader ", name, ": ",
                   result.error().message());
       }
       return 0;
     }},
    {"attach_shader",
     "Attach a shader by name, if no shader is passed resets to the default "
     "shader",
     {{"shader?",
       "Shader to attach, if nothing is passed then pre_pass.frag will be "
       "passed",
       "string"}},
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
       auto link_result = shaders->Link(program_name, "pre_pass.vert",
                                        fragment_shader, Shaders::kUseCache);
       if (link_result.is_error()) {
         LUA_ERROR(state, "Could not switch shader ", program_name, ": ",
                   link_result.error().message());
         return 0;
       }
       renderer->SetShaderProgram(program_name);
       shaders->UseProgram(program_name);
       return 0;
     }},
    {"send_uniform",
     "Sends a uniform with the given name to the current shader",
     {{"name", "Name of the uniform to send", "string"},
      {"value",
       "Value to send. Supported values are G.math.v2,v3,v4, G.math.m2x2, "
       "G.math.m3x3, G.math.m4x4, and floats",
       "number"}},
     {},
     [](lua_State* state) {
       auto* shaders = Registry<Shaders>::Retrieve(state);
       const char* name = luaL_checkstring(state, 1);
       if (lua_isnumber(state, 2)) {
         auto uniform_result =
             shaders->SetUniformF(name, luaL_checknumber(state, 2));
         if (uniform_result.is_error()) {
           LUA_ERROR(state, "Could not set uniform ", name, ": ",
                     uniform_result.error().message());
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
    {"set_blend_mode",
     "Set the blend mode for subsequent drawing operations",
     {{"mode",
       "Blend mode: 'alpha' (default), 'add' (additive), 'multiply', or "
       "'replace'",
       "string"}},
     {},
     [](lua_State* state) {
       auto* batch = Registry<BatchRenderer>::Retrieve(state);
       std::string_view mode = GetLuaString(state, 1);
       if (mode == "alpha") {
         batch->SetActiveBlendMode(BLEND_ALPHA);
       } else if (mode == "add") {
         batch->SetActiveBlendMode(BLEND_ADD);
       } else if (mode == "multiply") {
         batch->SetActiveBlendMode(BLEND_MULTIPLY);
       } else if (mode == "replace") {
         batch->SetActiveBlendMode(BLEND_REPLACE);
       } else if (mode == "premultiplied") {
         batch->SetActiveBlendMode(BLEND_PREMULTIPLIED);
       } else {
         LUA_ERROR(state, "Unknown blend mode '", mode,
                   "'. Expected 'alpha', 'add', 'multiply', 'replace', "
                   "or 'premultiplied'");
       }
       return 0;
     }},
    {"new_canvas",
     "Create an off-screen canvas for rendering. Returns a canvas object.",
     {{"width", "Canvas width in pixels", "integer"},
      {"height", "Canvas height in pixels", "integer"},
      {"options?",
       "Optional table with 'filter' key: 'nearest' for pixel art or "
       "'linear' (default)",
       "table"}},
     {{"canvas", "A canvas object", "canvas"}},
     [](lua_State* state) {
       auto* batch = Registry<BatchRenderer>::Retrieve(state);
       int width = luaL_checkinteger(state, 1);
       int height = luaL_checkinteger(state, 2);
       bool nearest = false;
       if (lua_gettop(state) >= 3 && lua_istable(state, 3)) {
         lua_getfield(state, 3, "filter");
         if (lua_isstring(state, -1)) {
           std::string_view filter = GetLuaString(state, -1);
           nearest = (filter == "nearest");
         }
         lua_pop(state, 1);
       }
       Canvas c = batch->CreateCanvas(width, height, nearest);
       NewUserdata<Canvas>(state, c);
       return 1;
     }},
    {"set_canvas",
     "Redirect all subsequent drawing to a canvas. Call with no arguments to "
     "reset to the screen.",
     {{"canvas?", "Canvas to draw to, or nil/nothing to reset to screen",
       "canvas"}},
     {},
     [](lua_State* state) {
       auto* batch = Registry<BatchRenderer>::Retrieve(state);
       if (lua_gettop(state) == 0 || lua_isnil(state, 1)) {
         batch->ResetCanvas();
       } else {
         auto* c = AsUserdata<Canvas>(state, 1);
         batch->SetActiveCanvas(c->fbo, c->width, c->height);
       }
       return 0;
     }},
    {"draw_canvas",
     "Draw a canvas as a textured quad. Handles Y-flip automatically. "
     "Uses the current blend mode (set via set_blend_mode). For canvases "
     "with semi-transparent content, use set_blend_mode('premultiplied') "
     "before drawing to avoid alpha darkening artifacts.",
     {{"canvas", "The canvas to draw", "canvas"},
      {"x", "X position to draw at", "number"},
      {"y", "Y position to draw at", "number"},
      {"angle?", "Rotation angle in radians", "number"},
      {"w?", "Width to draw at (default: canvas width)", "number"},
      {"h?", "Height to draw at (default: canvas height)", "number"}},
     {},
     [](lua_State* state) {
       const int params = lua_gettop(state);
       auto* c = AsUserdata<Canvas>(state, 1);
       const float x = luaL_checknumber(state, 2);
       const float y = luaL_checknumber(state, 3);
       float angle = 0;
       if (params >= 4) angle = luaL_checknumber(state, 4);
       float w = static_cast<float>(c->width);
       float h = static_cast<float>(c->height);
       if (params >= 6) {
         w = luaL_checknumber(state, 5);
         h = luaL_checknumber(state, 6);
       }
       auto* batch = Registry<BatchRenderer>::Retrieve(state);
       batch->SetActiveTexture(c->texture_unit);
       // Y-flip the UVs: canvas textures have bottom-up origin.
       FVec2 p0 = FVec(x, y);
       FVec2 p1 = FVec(x + w, y + h);
       FVec2 q0 = FVec(0.f, 1.f);  // top-left UV (flipped)
       FVec2 q1 = FVec(1.f, 0.f);  // bottom-right UV (flipped)
       FVec2 origin = FVec(x + w / 2.f, y + h / 2.f);
       batch->PushQuad(p0, p1, q0, q1, origin, angle);
       return 0;
     }}};

static const LuaApiFunction kWindowLib[] = {
    {"dimensions",
     "Returns the window dimensions in pixels",
     {},
     {{"width", "the window width", "number"},
      {"height", "the window height", "number"}},
     [](lua_State* state) {
       auto* renderer = Registry<Renderer>::Retrieve(state);
       IVec2 viewport = renderer->viewport();
       lua_pushnumber(state, viewport.x);
       lua_pushnumber(state, viewport.y);
       return 2;
     }},
    {"set_dimensions",
     "Sets the window dimensions",
     {{"width", "the window width", "integer"},
      {"height", "the window height", "integer"}},
     {},
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
     "Sets the window to exclusive fullscreen mode",
     {},
     {},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, true);
       return 0;
     }},
    {"set_borderless",
     "Sets the window to borderless fullscreen mode",
     {},
     {},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, true);
       return 0;
     }},
    {"set_windowed",
     "Sets the window to windowed mode",
     {},
     {},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowFullscreen(window, false);
       return 0;
     }},
    {"set_title",
     "Sets the window title",
     {{"title", "the new window title", "string"}},
     {},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       SDL_SetWindowTitle(window, luaL_checkstring(state, 1));
       return 0;
     }},
    {"get_title",
     "Returns the current window title",
     {},
     {{"title", "the window title", "string"}},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushstring(state, SDL_GetWindowTitle(window));
       return 1;
     }},
    {"has_input_focus",
     "Returns true if the window has keyboard input focus",
     {},
     {{"focused", "whether the window has input focus", "boolean"}},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }},
    {"has_mouse_focus",
     "Returns true if the window has mouse focus",
     {},
     {{"focused", "whether the window has mouse focus", "boolean"}},
     [](lua_State* state) {
       auto* window = Registry<SDL_Window>::Retrieve(state);
       lua_pushboolean(state,
                       SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
       return 1;
     }}};

constexpr luaL_Reg kCanvasMethods[] = {
    {"dimensions",
     [](lua_State* state) {
       auto* c = AsUserdata<Canvas>(state, 1);
       lua_pushinteger(state, c->width);
       lua_pushinteger(state, c->height);
       return 2;
     }},
    {"width",
     [](lua_State* state) {
       auto* c = AsUserdata<Canvas>(state, 1);
       lua_pushinteger(state, c->width);
       return 1;
     }},
    {"height",
     [](lua_State* state) {
       auto* c = AsUserdata<Canvas>(state, 1);
       lua_pushinteger(state, c->height);
       return 1;
     }},
    {"__tostring", [](lua_State* state) {
       auto* c = AsUserdata<Canvas>(state, 1);
       lua_pushfstring(state, "canvas(%dx%d)", c->width, c->height);
       return 1;
     }}};

}  // namespace

void AddGraphicsLibrary(Lua* lua) {
  LOAD_METATABLE(lua, "canvas", kCanvasMethods);
  lua->AddLibrary("graphics", kGraphicsLib);
  lua->AddLibrary("window", kWindowLib);
}

}  // namespace G
