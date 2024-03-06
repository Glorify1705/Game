local Game = {}

function Game:init()
end

function Game:update(t, dt)
end

function Game:draw()
    G.graphics.set_color("white")
    G.graphics.draw_rect(100, 300, 1000, 600)
    G.graphics.set_color("red")
    G.graphics.draw_text("ponderosa.ttf", 24, "The quick brown fox jumps over the lazy dog", 100, 300)
end

return Game
