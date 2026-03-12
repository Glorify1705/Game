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
end

function Game:draw()
  G.graphics.clear()
  G.graphics.set_color("white")
  G.graphics.print("Hello, world!", 10, 10)
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
