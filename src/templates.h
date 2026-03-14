#pragma once
#ifndef _GAME_TEMPLATES_H
#define _GAME_TEMPLATES_H

#include <string_view>

namespace G {
namespace templates {

constexpr std::string_view kConfJson = R"({
  "width": 800,
  "height": 600,
  "title": "%s",
  "org_name": "MyOrg",
  "app_name": "%s",
  "version": "0.1"
})";

constexpr std::string_view kMainLua = R"(return require("game")
)";

constexpr std::string_view kGameLua = R"(local Game = {}

function Game:init()
end

function Game:update(t, dt)
  if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
    G.system.quit()
  end
end

function Game:draw()
  G.graphics.clear()
  G.graphics.set_color("white")
  local text = "Hello, world!"
  local size = 48
  local tw, th = G.graphics.text_dimensions("debug_font.ttf", size, text)
  local w, h = G.window.dimensions()
  local x = (w - tw) / 2
  local y = (h + th) / 2 + 60
  G.graphics.draw_text("debug_font.ttf", size, text, x, y)
end

return Game
)";

constexpr std::string_view kLuarcJson = R"({
  "workspace.library": ["definitions"],
  "runtime.version": "Lua 5.1",
  "diagnostics.globals": ["G"]
})";

}  // namespace templates
}  // namespace G

#endif  // _GAME_TEMPLATES_H
