local Game = {}

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
