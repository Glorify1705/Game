#pragma once
#ifndef _GAME_TEMPLATES_H
#define _GAME_TEMPLATES_H

namespace G {
namespace templates {

constexpr const char* kConfJson = R"({
  "width": 800,
  "height": 600,
  "title": "%s",
  "org_name": "MyOrg",
  "app_name": "%s",
  "version": "0.1"
})";

constexpr const char* kMainLua = R"(return require("game")
)";

constexpr const char* kGameLua = R"(local Game = {}

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

constexpr const char* kLuarcJson = R"({
  "workspace.library": ["definitions"],
  "runtime.version": "Lua 5.1",
  "diagnostics.globals": ["G"]
})";

}  // namespace templates
}  // namespace G

#endif  // _GAME_TEMPLATES_H
